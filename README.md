# Event Loop

中文说明：[README_zh.md](./README_zh.md)

## 1. Introduction

**Event Loop** is an RT-Thread software package that schedules **delayed callbacks** in user space. Pending work is stored in a **fixed-size delay table**; a **one-shot soft timer** drives time progression; **due** jobs are sent through a **message queue** and executed on a dedicated **`evt_loop` thread** (so callbacks do not run in the timer daemon context).

Typical use: defer UI/state-machine work, staggered retries, or periodic-style chains built from `EVT_LOOP_PUSH` in the callback.

The package is enabled with **`PKG_USING_EVENT_LOOP`**. Metadata is in `package.json`; build integration uses `Kconfig` and `SConscript`.

## 2. Features

- **Delayed dispatch** — `evt_loop_push_delayed()` / macro **`EVT_LOOP_PUSH(func, args, delay_ms)`** (`delay_ms <= 1` is treated as immediate via the message queue).
- **Cancel** — **`EVT_LOOP_REMOVE(func)`** removes **all** pending delayed entries for that function pointer; **`EVT_LOOP_REMOVE_WITH_ARGS(func, args)`** removes entries matching both (see header comments).
- **Thread + MQ** — Callbacks run on the **`evt_loop`** thread; the queue depth is configurable (`EVENT_LOOP_MSGQ_DEPTH`).
- **Single soft one-shot timer** — One `RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER` instance; next expiry is the minimum remaining delay in the table.
- **Concurrency** — Delay table updates are protected by a **mutex**; re-arm logic handles RT-Thread’s **`RT_TIMER_FLAG_PROCESSING`** restriction inside the soft-timer callback (`rt_timer_stop` before reprogramming, deferred `rt_timer_start` via the same message queue when required).
- **Optional sample** — `EVENT_LOOP_USING_SAMPLES` builds `event_loop_test.c` and exports MSH command **`evt_loop_test`** (requires FinSH/MSH).

## 3. Architecture (brief)

```
[ Any thread ] --push_delayed/remove--> [ delay table + mutex ]
                                              |
                    soft timer (one-shot) ----+--> apply elapsed --> rt_mq_send (due)
                                              |
                    evt_loop thread <----------+---- rt_mq_recv --> user callback (func, args)
```

The following **sequence diagram** sketches Push / Remove / timeout delivery under **one-shot soft-timer** mode (aligned with `event_loop.c`: delay table + soft timer + message queue). “Lost time” in the figure matches **`delay_diff` / elapsed** calibration in the implementation.

```mermaid
sequenceDiagram
    actor User as Caller / timer<br/>wake-up
    participant Push as Push path<br/>evt_loop_push_delayed
    participant Remove as Remove path<br/>evt_loop_remove_delayed
    participant TimerCore as Timer core<br/>soft timer + apply elapsed
    participant EventArray as Delay table<br/>event array
    participant Task as Work thread<br/>evt_loop task

    rect rgb(240,248,255)
    Note over User,Task: One-shot timer mode — end-to-end flow
    end

    %% Path 1: push
    User->>Push: push delayed event
    Push->>EventArray: 1. find free slot & write entry
    Note over Push: lost time = now - last StartTime
    Push->>TimerCore: 2. stop timer & apply calibration
    TimerCore->>EventArray: 3. walk table, subtract elapsed
    TimerCore->>EventArray: 4. min delay & re-arm timer
    Note over TimerCore: set StartTime, start one-shot

    %% Path 2: remove
    alt cancel / remove
        User->>Remove: remove delayed event
        Remove->>EventArray: 1. find & clear matching slot(s)
        Note over Remove: lost time = now - last StartTime
        Remove->>TimerCore: 2. stop timer & apply calibration
        TimerCore->>EventArray: 3. walk table, subtract elapsed
        alt table not empty
            TimerCore->>TimerCore: 4. next minimum timeout
            TimerCore->>TimerCore: 5. restart timer
        else
            TimerCore->>TimerCore: 5. stop timer — no pending work
        end
    end

    %% Timeout → task
    TimerCore-->>Task: timer fires (due callbacks)
    Note over TimerCore,Task: enqueue due jobs; run user callbacks on evt_loop
    Task-->>User: application-visible result
```

## 4. API

Include **`event_loop.h`**.

| Symbol | Role |
|--------|------|
| `EVT_LOOP_PUSH(pfunc, pargs, delay_ms)` | Queue a delayed call; `pfunc` is `void (*)(void *)`. |
| `EVT_LOOP_REMOVE(pfunc)` | Remove **all** table slots whose function pointer equals `pfunc`. |
| `EVT_LOOP_REMOVE_WITH_ARGS(pfunc, pargs)` | Remove slots matching `pfunc` and `pargs` (or all `pfunc` if `pargs` matches the macro’s rules — see implementation). |
| `evt_loop_push_delayed()` / `evt_loop_remove_delayed()` | Underlying C API. |

Initialization is **`INIT_APP_EXPORT(_evt_loop_init)`**; no extra call is required after boot.

## 5. Directory layout

```
event_loop/
├── README.md              # This file
├── README_zh.md           # Chinese readme
├── inc/
│   └── event_loop.h       # Public API
├── src/
│   └── event_loop.c       # Implementation
├── samples/
│   └── event_loop_test.c  # Optional MSH demo
├── Kconfig                # PKG_USING_EVENT_LOOP and options
└── SConscript             # DefineGroup, CPPPATH
```

## 6. Dependencies (Kconfig)

Enabling **`PKG_USING_EVENT_LOOP`** selects:

- `RT_USING_MESSAGEQUEUE`
- `RT_USING_MUTEX`
- `RT_USING_TIMER_SOFT`

Configurable options:

| Option | Meaning |
|--------|---------|
| `EVENT_LOOP_MAX_EVENT_CNT` | Maximum concurrent delayed slots (default 32). |
| `EVENT_LOOP_MSGQ_DEPTH` | Message queue depth (default 15). |
| `EVENT_LOOP_THREAD_STACK_SIZE` | `evt_loop` thread stack (default 3072). |
| `EVENT_LOOP_THREAD_PRIORITY` | Priority (default 12). **Must be strictly greater than `RT_TIMER_THREAD_PRIO`** (numerically larger = lower CPU priority than the soft-timer thread). |
| `EVENT_LOOP_USING_SAMPLES` | Build the sample (default off in Kconfig). |

## 7. Get started

### 7.1 menuconfig

```
RT-Thread online packages
    system packages --->
        [*] Event loop (delayed dispatch: mq + soft one-shottimer) --->
            (32) Maximum delayed slots in table
            (15) Message queue depth (immediate + due callbacks)
            (3072) Event loop thread stack size (bytes)
            (12) Event loop thread priority (smaller = higher5
            [*] Build event_loop sample (event_loop_test.c)
```

1. Open **menuconfig** from your BSP.
2. Enable **`PKG_USING_EVENT_LOOP`** (*Event loop (delayed dispatch: mq + soft one-shot timer)*).
3. Adjust stack, priority, table size, and MQ depth as needed.
4. Optionally enable **`EVENT_LOOP_USING_SAMPLES`** for `evt_loop_test`.
5. Save and confirm `rtconfig.h` defines `PKG_USING_EVENT_LOOP`.

### 7.2 Build

From the BSP root, run `scons` (or your usual RT-Thread build).

### 7.3 Application code

```c
#include "event_loop.h"

static void my_job(void *arg)
{
    /* ... */
    EVT_LOOP_REMOVE(my_job);
    /* optional: EVT_LOOP_PUSH(my_job, arg, delay_ms); */
}

/* from any thread after boot */
EVT_LOOP_PUSH(my_job, (void *)ctx, 100);

/* from any thread after boot: cancel all pending delayed calls for my_job */
EVT_LOOP_REMOVE(my_job);

/* from any thread after boot: cancel delayed call(s) matching this function pointer and args */
EVT_LOOP_REMOVE_WITH_ARGS(my_job, (void *)1);

```

### Sample animation (`evt_loop_test`)

With **`EVENT_LOOP_USING_SAMPLES`** enabled, this is a short capture of running **`evt_loop_test`** from MSH:

![evt_loop_test sample animation](./doc/test.gif)

## 8. Notes

- **Priority** — If compile fails with the static assert / `#error` on priority, raise `EVENT_LOOP_THREAD_PRIORITY` above `RT_TIMER_THREAD_PRIO` in `rtconfig.h`.
- **Table full** — If the delay table is exhausted, a warning is logged and the push is dropped; size with `EVENT_LOOP_MAX_EVENT_CNT`.
- **Same function, multiple slots** — `EVT_LOOP_REMOVE(func)` clears every matching slot; design callbacks so this matches your intent.

## 9. License

Apache License 2.0 (SPDX in sources and `package.json`).

## 10. Maintainer / repository

See **`package.json`** for author, repository URL, and version/site entries.