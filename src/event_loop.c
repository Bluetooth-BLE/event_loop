/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#include <string.h>
#include <stdint.h>
#include <rtthread.h>

#define DBG_TAG  "evt_loop"
#define DBG_LVL  DBG_WARNING
#include <rtdbg.h>

#include "event_loop.h"

/**********************
 *      DEFINES
 **********************/
#ifndef CONFIG_EVENT_LOOP_MAX_EVENT_CNT
#define CONFIG_EVENT_LOOP_MAX_EVENT_CNT 32
#endif
#ifndef CONFIG_EVENT_LOOP_MSGQ_DEPTH
#define CONFIG_EVENT_LOOP_MSGQ_DEPTH 15
#endif
#ifndef CONFIG_EVENT_LOOP_THREAD_STACK_SIZE
#define CONFIG_EVENT_LOOP_THREAD_STACK_SIZE 3072
#endif
#ifndef CONFIG_EVENT_LOOP_THREAD_PRIORITY
#define CONFIG_EVENT_LOOP_THREAD_PRIORITY 12
#endif

#define EVT_LOOP_MAX_EVENT_CNT      CONFIG_EVENT_LOOP_MAX_EVENT_CNT
#define EVT_LOOP_MSGQ_DEPTH         CONFIG_EVENT_LOOP_MSGQ_DEPTH
#define EVT_LOOP_THREAD_STACK_SIZE  CONFIG_EVENT_LOOP_THREAD_STACK_SIZE
#define EVT_LOOP_THREAD_PRIORITY    CONFIG_EVENT_LOOP_THREAD_PRIORITY

/*
 * Scheduling priority: smaller number == higher CPU priority (RT-Thread).
 * Soft-timer work runs in the timer daemon thread: RT_TIMER_THREAD_PRIO
 * evt_loop (MQ + callbacks) must be weaker than the soft-timer thread.
 */
#if defined(RT_USING_TIMER_SOFT) && defined(RT_TIMER_THREAD_PRIO)
#if (EVT_LOOP_THREAD_PRIORITY) <= (RT_TIMER_THREAD_PRIO)
#error "event_loop: CONFIG_EVENT_LOOP_THREAD_PRIORITY must be > RT_TIMER_THREAD_PRIO (weaker than soft-timer thread)"
#endif
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    uint32_t event[EVT_LOOP_MAX_EVENT_CNT];
    uint32_t args[EVT_LOOP_MAX_EVENT_CNT];
    uint32_t delay[EVT_LOOP_MAX_EVENT_CNT];
} evt_loop_table_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static evt_loop_table_t  s_evt_loop_tab;
static struct rt_thread  s_evt_loop_thread;
static rt_uint8_t        s_evt_loop_stack[EVT_LOOP_THREAD_STACK_SIZE];

static volatile uint32_t s_evt_loop_start_ms;

static struct rt_messagequeue s_evt_loop_mq;
#define EVT_LOOP_MSG_SIZE (sizeof(evt_loop_handle_t))
static uint8_t s_evt_loop_mq_pool[RT_MQ_BUF_SIZE(EVT_LOOP_MSG_SIZE, EVT_LOOP_MSGQ_DEPTH)];

static struct rt_mutex s_evt_loop_mutex;
static struct rt_timer s_evt_loop_timer;

/**********************
 *  STATIC PROTOTYPES
 *  Order: callees before callers (bottom-up).
 **********************/
static uint32_t _evt_loop_ticks_to_ms(rt_tick_t ticks);
static void     _evt_loop_callback_implementation_push(void *pfunc, void *pargs);
static int      _evt_loop_push_next_delay(void *param);
static void     _evt_loop_check_elapsed(uint32_t elapsed_ms);
static void     _evt_loop_soft_timer_callback(void *param);
static void     _evt_loop_task(void *parameter);
static int      _evt_loop_init(void);

/**********************
 *  STATIC FUNCTIONS
 **********************/
static int _evt_loop_init(void)
{
    rt_err_t err;

    memset(&s_evt_loop_tab, 0, sizeof(s_evt_loop_tab));
    s_evt_loop_start_ms = (uint32_t)rt_tick_get_millisecond();

    err = rt_mq_init(&s_evt_loop_mq, "el_mq", s_evt_loop_mq_pool, EVT_LOOP_MSG_SIZE,
                     sizeof(s_evt_loop_mq_pool), RT_IPC_FLAG_FIFO);
    if (err != RT_EOK) {
        LOG_E("evt_loop mq init fail: %d", err);
        return (int)err;
    }

    err = rt_mutex_init(&s_evt_loop_mutex, "el_mtx", RT_IPC_FLAG_FIFO);
    if (err != RT_EOK) {
        LOG_E("evt_loop mutex init fail: %d", err);
        return (int)err;
    }

    rt_timer_init(&s_evt_loop_timer, "el_tmr", _evt_loop_soft_timer_callback, RT_NULL,
                  (rt_tick_t)0xFFFFFF, RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);

    err = rt_thread_init(&s_evt_loop_thread, "evt_loop", _evt_loop_task, RT_NULL, s_evt_loop_stack,
                         sizeof(s_evt_loop_stack), EVT_LOOP_THREAD_PRIORITY, 10);
    if (err != RT_EOK) {
        LOG_E("evt_loop thread init fail: %d", err);
        return (int)err;
    }

    err = rt_thread_startup(&s_evt_loop_thread);
    if (err != RT_EOK) {
        LOG_E("evt_loop thread startup fail: %d", err);
        return (int)err;
    }

    return 0;
}

static uint32_t _evt_loop_ticks_to_ms(rt_tick_t ticks)
{
    if (RT_TICK_PER_SECOND == 0U) {
        return 0U;
    }
    return (uint32_t)(((rt_uint64_t)ticks * 1000ULL) / (rt_uint64_t)RT_TICK_PER_SECOND);
}

static void _evt_loop_callback_implementation_push(void *pfunc, void *pargs)
{
    evt_loop_handle_t ev;

    ev.pfunc = (EVT_LOOP_FUNC_T)pfunc;
    ev.pargs = pargs;

    if (rt_mq_send_wait(&s_evt_loop_mq, &ev, EVT_LOOP_MSG_SIZE, RT_WAITING_FOREVER) != RT_EOK) {
        LOG_E("event loop mq send_wait fail");
    }
}

static int _evt_loop_push_next_delay(void *param)
{
    uint32_t  next_delay = (uint32_t)(uintptr_t)param;
    rt_tick_t t          = rt_tick_from_millisecond((rt_int32_t)next_delay);
    rt_err_t  err;

    if (t == 0) {
        t = 1;
    }
    (void)rt_timer_stop(&s_evt_loop_timer);
    rt_timer_control(&s_evt_loop_timer, RT_TIMER_CTRL_SET_TIME, &t);
    err = rt_timer_start(&s_evt_loop_timer);
    if (err == RT_EOK) {
        s_evt_loop_start_ms = (uint32_t)rt_tick_get_millisecond();
    }

    return err;
}

static void _evt_loop_check_elapsed(uint32_t elapsed_ms)
{
    int      i;
    uint32_t event;
    uint32_t next_delay = UINT32_MAX;

    if (rt_mutex_take(&s_evt_loop_mutex, rt_tick_from_millisecond(1000)) != RT_EOK) {
        LOG_E("event loop mutex take fail");
        return;
    }

    for (i = 0; i < EVT_LOOP_MAX_EVENT_CNT; i++) {
        if (s_evt_loop_tab.event[i]) {
            if (s_evt_loop_tab.delay[i] > elapsed_ms) {
                s_evt_loop_tab.delay[i] -= elapsed_ms;
            } else {
                s_evt_loop_tab.delay[i] = 0;
            }

            if (s_evt_loop_tab.delay[i] <= 0U) {
                event = s_evt_loop_tab.event[i];
                s_evt_loop_tab.event[i] = 0x00U;
                _evt_loop_callback_implementation_push((void *)(uintptr_t)event, (void *)(uintptr_t)s_evt_loop_tab.args[i]);
            }
        }
    }

    for (i = 0; i < EVT_LOOP_MAX_EVENT_CNT; i++) {
        if (s_evt_loop_tab.event[i]) {
            if (s_evt_loop_tab.delay[i] < next_delay) {
                next_delay = s_evt_loop_tab.delay[i];
            }
        }
    }

    rt_mutex_release(&s_evt_loop_mutex);

    /* Same as fw second loop; use UINT32_MAX so “no pending” does not arm timer. */
    if ((next_delay != UINT32_MAX) && (next_delay > 0U)) {
        if (_evt_loop_push_next_delay((void *)(uintptr_t)next_delay) != RT_EOK) {
            _evt_loop_callback_implementation_push(_evt_loop_push_next_delay, (void *)(uintptr_t)next_delay);
        }
    }
}

static void _evt_loop_soft_timer_callback(void *param)
{
    RT_UNUSED(param);
    rt_tick_t elapsed_ticks = 0;

    rt_timer_control(&s_evt_loop_timer, RT_TIMER_CTRL_GET_TIME, &elapsed_ticks);
    _evt_loop_check_elapsed(_evt_loop_ticks_to_ms(elapsed_ticks));
}

static void _evt_loop_task(void *parameter)
{
    RT_UNUSED(parameter);

    evt_loop_handle_t evt;

    LOG_I("event loop thread started");

    for (;;) {
        rt_ssize_t n = rt_mq_recv(&s_evt_loop_mq, &evt, EVT_LOOP_MSG_SIZE, RT_WAITING_FOREVER);
        if (n > 0 && evt.pfunc != RT_NULL) {
            evt.pfunc(evt.pargs);
        }
    }
}

/**********************
 *  EXPORT FUNCTIONS
 **********************/
void evt_loop_push_delayed(void *pfunc, void *pargs, uint32_t delay_ms)
{
    int      i = 0;
    uint32_t delay_diff = 0U;

    if (delay_ms <= 1U) {
        _evt_loop_callback_implementation_push(pfunc, pargs);
        return;
    }

    if (rt_mutex_take(&s_evt_loop_mutex, rt_tick_from_millisecond(1000)) != RT_EOK) {
        LOG_E("event loop mutex take fail (delayed)");
        return;
    }

    for (i = 0; i < EVT_LOOP_MAX_EVENT_CNT; i++) {
        if (s_evt_loop_tab.event[i] == 0U) {
            break;
        }
    }

    if (i < EVT_LOOP_MAX_EVENT_CNT) {
        s_evt_loop_tab.event[i] = (uint32_t)(uintptr_t)pfunc;
        s_evt_loop_tab.args[i]  = (uint32_t)(uintptr_t)pargs;
        delay_diff              = (uint32_t)(rt_tick_get_millisecond() - s_evt_loop_start_ms);
        s_evt_loop_tab.delay[i] = delay_ms + delay_diff;
    }

    rt_mutex_release(&s_evt_loop_mutex);

    if (i < EVT_LOOP_MAX_EVENT_CNT) {
        rt_tick_t delay_diff_tick = rt_tick_from_millisecond((rt_int32_t)delay_diff);

        if (delay_diff_tick == 0) {
            delay_diff_tick = 1;
        }

        rt_timer_stop(&s_evt_loop_timer);
        _evt_loop_check_elapsed(_evt_loop_ticks_to_ms(delay_diff_tick));
    } else {
        LOG_W("evt_loop: delayed table full (max=%d)", EVT_LOOP_MAX_EVENT_CNT);
    }
}

uint8_t evt_loop_remove_delayed(void *pfunc, void *pargs)
{
    uint8_t removed = 0U;
    int     j;

    if (rt_mutex_take(&s_evt_loop_mutex, rt_tick_from_millisecond(1000)) != RT_EOK) {
        LOG_E("event loop mutex take fail (remove)");
        return 0U;
    }

    for (j = 0; j < EVT_LOOP_MAX_EVENT_CNT; j++) {
        if ((uint32_t)(uintptr_t)pfunc == s_evt_loop_tab.event[j]) {
            if (pargs == RT_NULL || (uint32_t)(uintptr_t)pargs == s_evt_loop_tab.args[j]) {
                s_evt_loop_tab.event[j] = 0x00U;
                removed                   = 1U;
            }
        }
    }

    rt_mutex_release(&s_evt_loop_mutex);

    if (removed != 0U) {
        uint32_t ticks_diff = (uint32_t)(rt_tick_get_millisecond() - s_evt_loop_start_ms);
        rt_tick_t ticks_diff_tick = rt_tick_from_millisecond((rt_int32_t)ticks_diff);

        if (ticks_diff_tick == 0) {
            ticks_diff_tick = 1;
        }
        rt_timer_stop(&s_evt_loop_timer);
        _evt_loop_check_elapsed(_evt_loop_ticks_to_ms(ticks_diff_tick));

    }

    return removed;
}

INIT_APP_EXPORT(_evt_loop_init);
