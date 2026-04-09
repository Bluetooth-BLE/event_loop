/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <rtthread.h>

#define DBG_TAG  "evt_test"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#include "event_loop.h"

#define EVT_TEST_ARGS(run, branch) ((void *)(uintptr_t)(((uintptr_t)(run) << 1) | ((uintptr_t)(branch) & 1u)))

static uint32_t count1 = 10;
static uint32_t count2 = 10;
static uint32_t s_evt_test_run;

static uint32_t s_last_ms1;
static uint32_t s_last_ms2;

static void event_loop_test1(void *args);
static void event_loop_test2(void *args);

static void event_loop_test1(void *args)
{
    uintptr_t        a         = (uintptr_t)args;
    uint32_t         msg_run   = (uint32_t)(a >> 1);
    uint32_t         branch    = (uint32_t)(a & 1u);
    uint32_t         current_ms = (uint32_t)rt_tick_get_millisecond();

    if (msg_run != s_evt_test_run) {
        LOG_E("event_loop_test1: stale run id %u (expected %u)", msg_run, s_evt_test_run);
        return;
    }

    LOG_D("event_loop_test1 delta_ms = %u, count1 = %u", (unsigned)(current_ms - s_last_ms1), count1);
    s_last_ms1 = current_ms;

    EVT_LOOP_REMOVE(event_loop_test1);
    if (count1 > 0U) {
        if (branch == 1U) {
            EVT_LOOP_PUSH(event_loop_test1, EVT_TEST_ARGS(s_evt_test_run, 0), 3341);
        } else {
            EVT_LOOP_PUSH(event_loop_test1, EVT_TEST_ARGS(s_evt_test_run, 1), 1759);
        }
        count1--;
    }
}

static void event_loop_test2(void *args)
{
    uintptr_t        a          = (uintptr_t)args;
    uint32_t         msg_run    = (uint32_t)(a >> 1);
    uint32_t         branch     = (uint32_t)(a & 1u);
    uint32_t         current_ms = (uint32_t)rt_tick_get_millisecond();

    if (msg_run != s_evt_test_run) {
        LOG_E("event_loop_test2: stale run id %u (expected %u)", msg_run, s_evt_test_run);
        return;
    }

    LOG_D("event_loop_test2 delta_ms = %u, count2 = %u", (unsigned)(current_ms - s_last_ms2), count2);
    s_last_ms2 = current_ms;

    EVT_LOOP_REMOVE(event_loop_test2);
    if (count2 > 0U) {
        if (branch == 1U) {
            EVT_LOOP_PUSH(event_loop_test2, EVT_TEST_ARGS(s_evt_test_run, 0), 1234);
        } else {
            EVT_LOOP_PUSH(event_loop_test2, EVT_TEST_ARGS(s_evt_test_run, 1), 667);
        }
        count2--;
    }
}

static int event_loop_test(int argc, char **argv)
{
    char          *end;
    unsigned long  cnt1;
    unsigned long  cnt2;

    if (argc != 3) {
        LOG_W("Usage: evt_loop_test <cnt1> <cnt2>");
        LOG_W("  cnts: decimal (e.g. evt_loop_test 10 10)");
        return -RT_EINVAL;
    }

    cnt1 = strtoul(argv[1], &end, 0);
    if (end == argv[1] || *end != '\0') {
        LOG_E("bad cnt1: %s", argv[1]);
        return -RT_EINVAL;
    }
    cnt2 = strtoul(argv[2], &end, 0);
    if (end == argv[2] || *end != '\0') {
        LOG_E("bad cnt2: %s", argv[2]);
        return -RT_EINVAL;
    }

    /* Drop incomplete previous run: delayed table + ignore stale MQ deliveries via run id. */
    EVT_LOOP_REMOVE(event_loop_test1);
    EVT_LOOP_REMOVE(event_loop_test2);

    s_evt_test_run ++;
    count1 = (uint32_t)cnt1;
    count2 = (uint32_t)cnt2;

    {
        uint32_t now = (uint32_t)rt_tick_get_millisecond();
        s_last_ms1   = now;
        s_last_ms2   = now;
    }

    EVT_LOOP_PUSH(event_loop_test1, EVT_TEST_ARGS(s_evt_test_run, 1), 3000);
    EVT_LOOP_PUSH(event_loop_test2, EVT_TEST_ARGS(s_evt_test_run, 0), 2370);
    LOG_I("evt_loop_test ok cnt1=%lu cnt2=%lu", cnt1, cnt2);
    return RT_EOK;
}

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
MSH_CMD_EXPORT_ALIAS(event_loop_test, evt_loop_test, run event_loop delayed demo);
#endif
