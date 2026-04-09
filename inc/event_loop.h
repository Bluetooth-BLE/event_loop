/*
 * Copyright (c) 2026, John.liu <450547566@qq.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author     Notes
 * 2026-04-07     John       first version
 */

#ifndef __EVENT_LOOP_H__
#define __EVENT_LOOP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *      DEFINES
 **********************/
#define EVT_LOOP_PUSH(pfunc, pargs, delay) evt_loop_push_delayed((void *)(pfunc), (pargs), (delay))
#define EVT_LOOP_REMOVE(pfunc)             evt_loop_remove_delayed((void *)(pfunc), NULL)
#define EVT_LOOP_REMOVE_WITH_ARGS(pfunc, pargs) evt_loop_remove_delayed((void *)(pfunc), (pargs))

/**********************
 *      TYPEDEFS
 **********************/
typedef void (*EVT_LOOP_FUNC_T)(void *arg);

typedef struct {
    void           *pargs;
    EVT_LOOP_FUNC_T pfunc;
} evt_loop_handle_t;

void    evt_loop_push_delayed(void *pfunc, void *pargs, uint32_t delay_ms);
uint8_t evt_loop_remove_delayed(void *pfunc, void *pargs);

#ifdef __cplusplus
}
#endif

#endif /* __EVENT_LOOP_H__ */
