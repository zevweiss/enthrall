/*
 * Generic interface for event-loop stuff (monitoring file descriptors for
 * read/write readiness, timers, etc.).
 */

#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

/* Opaque context for a monitored file descriptor */
struct fdmon_ctx;

typedef void (*fdmon_callback_t)(struct fdmon_ctx* ctx, void* arg);

struct fdmon_ctx* fdmon_register_fd(int fd, fdmon_callback_t readcb,
                                    fdmon_callback_t writecb, void* arg);

void fdmon_unregister(struct fdmon_ctx* ctx);

#define FM_READ  (1 << 0)
#define FM_WRITE (1 << 1)

void fdmon_monitor(struct fdmon_ctx* ctx, uint32_t flags);
void fdmon_unmonitor(struct fdmon_ctx* ctx, uint32_t flags);

typedef void* timer_ctx_t;

timer_ctx_t schedule_call(void (*fn)(void* arg), void* arg, void (*arg_dtor)(void*), uint64_t delay);
int cancel_call(timer_ctx_t timer);

void run_event_loop(void);

/*
 * gettimeofday() is sufficiently portable, but sadly non-monotonic.
 * clock_gettime() is monotonic (or at least can be), but sadly does not exist
 * on OSX, despite being in POSIX.1-2001.  So instead we have this, a
 * platform-specific microsecond-resolution monotonic time-since-an-epoch
 * function.
 */
uint64_t get_microtime(void);

#endif /* EVENTS_H */
