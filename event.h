/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _EVENT_H
#define _EVENT_H
#include <stddef.h>
#include <sys/epoll.h>

struct event;
struct dispatcher;

/**
 * reason codes for event callback function
 * @REASON_EVENT_OCCURED: the event has occured
 * @REASON_TIMEOUT: the timeout has expired
 * @REASON_CLEANUP: dispatcher is about to exit
 */
enum {
	REASON_EVENT_OCCURED,
	REASON_TIMEOUT,
	__MAX_CALLBACK_REASON,
};

/*
 * reason_str: string representation for the reason a callback is called.
 */
extern const char * const reason_str[__MAX_CALLBACK_REASON];

/**
 * Return codes for callback function
 * @EVENTCB_CONTINUE:  continue processing
 * @EVENTCB_REMOVE:    remove this event
 * @EVENTCB_CLEANUP:   call the cleanup callback (implies EVENTCB_REMOVE)
 */
enum {
	EVENTCB_CONTINUE = 0,
	EVENTCB_REMOVE =   1,
	EVENTCB_CLEANUP =  2,
};

/*
 * Flags for struct event
 */
enum {
	/*
	 * timeout is absolute time, not relative.
	 * Used in event_add() and event_modify_timeout()
	 */
	TMO_ABS = 1,
};

/**
 * Prototype for event callback.
 *
 * @evt: the event object which registered the callback
 * @events: bit mask of epoll events (see epoll_ctl(2))
 *
 * In the callback, check event->reason to obtain the reason the
 * callback was called for.
 *
 * NOTE: race conditions between timeout and event completion can't
 * be fully avoided. Even if called with @REASON_TIMEOUT, the callback
 * should check whether an event might have arrived in the meantime,
 * and in this case, handle the event as if it had arrived before
 * the timeout.
 *
 * CAUTION: don not free() @evt in this callback.
 *
 * Return: an EVENTCB_xxx value (see above).
 */
typedef int (*cb_fn)(struct event *evt, uint32_t events);

/**
 * Prototype for cleanup callback.
 * @evt: the event object which registered the callback
 *
 * Called for this event if the event callback returned EVENTCB_CLEANUP, and
 * from cleanup_dispatcher() / free_dispatcher(), for all registered events.
 *
 * If this callback is called, the event has already been
 * removed from the dispatcher's internal lists. Use this callback
 * to free the event (if necessary), close file descriptors, and
 * release other resources as appropriate.
 *
 * @evt: the event object which registered the callback
 */
typedef void (*cleanup_fn)(struct event *evt);


/**
 * cleanup_event_on_stack() - convenience cleanup callback
 * @evt: the event object which registered the callback
 *
 * This cleanup function simply closes @evt->fd.
 */
void cleanup_event_on_stack(struct event *evt);

/**
 * cleanup_event_on_heap() - convenience cleanup callback
 * @evt: the event object which registered the callback
 *
 * This cleanup function closes @evt->fd and frees @eft.
 */
void cleanup_event_on_heap(struct event *evt);

/**
 * struct event - data structure for a generic event with timeout
 *
 * For best results, embed this data structure in the data you need.
 *
 * @ep: struct epoll_event. Fill in the @ep.events field with the epoll event
 *      types you're interested in (see epoll_ctl(2)). If @ep.events is
 *      0, the event is "disabled"; the timeout will still be active if set.
 *      CAUTION: don't touch ep.data, it's used by the dispatcher internally.
 * @fd: the file desciptor to monitor. Use -1 (and fill in the tmo field)
 *      to create a timer.
 *      Note: don't change the fd field after creating the event. In particular,
 *      setting a positve fd after calling event_add with fd == -1 is not allowed.
 * @callback: the callback function to be called if the event occurs or
 *      times out. This field *must* be set.
 * @dispatcher: the dispatcher object to which this event belongs
 * @tmo: The timeout for the event.
 *      Setting @tmo.tv_sec = @tmo.tv_nsec = 0 on calls to event_add()
 *      creates an event with no (=infinite) timeout.
 *      CAUTION: USED INTERNALLY. Do not change this any more after calling
 *      event_add(), after event_finish(), it may be set again. The field
 *      may be modified by the dispatcher code. To change the timeout,
 *      call event_mod_timeout().
 * @flags: See above. Currently only @TMO_ABS is supported. This field may
 *      be used internally by the dispatcher, be sure to set or clear only
 *      public bits.
 */

struct event {
	struct epoll_event ep;
	int fd;
	unsigned short reason;
	unsigned short flags;
	struct dispatcher *dsp;
	struct timespec tmo;
	cb_fn callback;
	cleanup_fn cleanup;
};

/**
 * event_add() - add an event to be monitored.
 *
 * @dispatcher: a dispatcher object
 * @event: an event structure. See the description above for the
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_add(struct dispatcher *dsp, struct event *event);

/**
 * event_remove() - remove the event from epoll.
 *
 * @event: a previously added event structure
 *
 * Removes the event from the dispatcher, and cancels the associated
 * timeout (if any).
 *
 * CAUTION: don't call this from callbacks. Use EVENTCB_xxx return codes
 * instead.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_remove(struct event *event);

/**
 * event_modify() - modify epoll events to wait for
 *
 * @event: a previously added event structure
 *
 * Call this function to change the epoll events (event->ep.events).
 * By setting @ep.events = 0, the event is temporarily disabled and
 * can be re-enabled later. NOTE: this function doesn't disable an
 * active timeout; use event_mod_timeout() for that.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_modify(struct event *event);

/**
 * event_mod_timeout() - modify or re-arm timeout for an event
 *
 * @event: a previously added event structure
 * @tmo: the new timeout value
 *
 * Call this function to modify or re-enable a timeout for an event.
 * It can (and must!) be used from the callback to change the timeout
 * if the event occured, to wait longer if it has timed out. 
 * If @tmo->tv_sec and @tmo->tv_nsec are both 0, an existing timeout is
 * cleared (an inifinite timeout is used for this event), as if the tmo field
 * had been set to { 0, 0 } in the call to event_add().  Set or clear
 * @event->flags to indicate whether @tmo is an absolute or relative
 * timeout. Note that the flags fields is "remembered", so if you want to use
 * a relative timeout after having used an absolute timeout before, you must
 * clear the @TMO_ABS field in event->flags before calling this function.
 *
 * NOTE: if the callback is called with reason REASON_TIMEOUT, the timeout
 * has expired and *must* be rearmed if the event is monitored further.
 * Otherwise, the timeout will implicitly be changed to "infinite", because
 * there is no timeout for this event any more.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_mod_timeout(struct event *event, const struct timespec *tmo);

/**
 * int _event_invoke_callback - handle callback invocation
 * @reason: one of the reason codes above
 *
 * Internal use only.
 */
void _event_invoke_callback(struct event *, unsigned short, unsigned int, bool);

/**
 * event_wait(): wait for events or timeouts, once
 *
 * @dispatcher: a dispatcher object
 * @sigmask: set of signals to be blocked while waiting
 *
 * This function waits for events or timeouts to occur, and calls
 * callbacks as appropriate. A single epoll_wait() call is made.
 * Depending on how the code was compiled, 0, 1, or more events may
 * occur in a single call. While waiting, the signal mask will be
 * set to @sigmask atomically. It is recommended to block all signals
 * except those that the application wants to receive (e.g. SIGTERM),
 * and install a signal handler for these signals to avoid the default
 * action (usually program termination, see signal(7)).
 *
 * NOTE: if no events have been added to the dispatcher before calling this
 * function, it will block waiting until a signal is caught.
 *
 * Return: 0 on success, a negative error code (-errno) on failure,
 * which might be -EINTR.
 */
int event_wait(const struct dispatcher *dsp, const sigset_t *sigmask);

/**
 * Return codes for err_handler in event_loop()
 */
enum {
	ELOOP_CONTINUE = 0,
	ELOOP_QUIT,
};

/**
 * event_loop(): wait for some or timeouts, repeatedly
 *
 * @dispatcher: a dispatcher object
 * @sigmask: set of signals to be blocked while waiting
 * @err_handler: callback for event_wait
 *
 * This function calls event_wait() in a loop, and calls err_handler() if
 * event_wait() returns an error code, passing it the negative error code
 * (e.g. -EINTR) in the @err parameter. err_handler() should return ELOOP_QUIT
 * or a negative error code to make event_loop() return, and ELOOP_CONTINUE
 * if event_loop() should continue execution.
 * @err_handler may be NULL, in which case event_loop() will simply return
 * the error code from event_wait().
 *
 * Return: 0 on success, or negative error code (-errno) on failure.
 * In particular, it returns -EINTR if interrupted by caught signal.
 */
int event_loop(const struct dispatcher *dsp, const sigset_t *sigmask,
	       int (*err_handler)(int err));

/**
 * cleanup_dispatcher() - clean out all events and timeouts
 * @dsp: a pointer returned by new_dispatcher().
 *
 * Remove all events and timeouts, and call every event's @cleanup
 * callback. The dispatcher object itself remains intact, and can
 * be re-used by adding new events.
 *
 * NOTE: unlike free_dispatcher(), this function disables the timer
 * event (as it cancels all timeouts), and removes all fds from the
 * dispatcher's epoll instance. Thus calling this e.g. after fork()
 * affects the parent process's operation.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int cleanup_dispatcher(struct dispatcher *dsp);

/**
 * free_dispatcher() - free a dispatcher object.
 * @dsp: a pointer returned by new_dispatcher().
 *
 * Calls the @cleanup callback of every registered event, and frees
 * the dispatcher's data structures.
 *
 * NOTE: Unlike cleanup_dispatcher(), this function doesn't touch the
 * kernel-owned epoll and itimerfd data structures. It's safe to call after
 * fork() without disturbing the parent.
 */
void free_dispatcher(struct dispatcher *dsp);

/**
 * new_dispatcher() - allocate and return a new dispatcher object.
 *
 * @clocksrc: one of the supported clock sources of the system,
 *            see clock_gettime(2). It will be used for timeout handling.
 *
 * Return: NULL on failure, a valid pointer otherwise.
 */
struct dispatcher *new_dispatcher(int clocksrc);

/**
 * dispatcher_get_efd() - obtain the epoll file descriptor
 *
 * @dispatcher: a dispatcher object
 *
 * Use this function if you want to implement a custom wait loop, to
 * obtain the file descriptor to be passed to epoll_wait().
 */
int dispatcher_get_efd(const struct dispatcher *dsp);

/**
 * dispatcher_get_clocksource() - obtain the clock source used for timeouts
 *
 * @dispatcher: a dispatcher object
 * *
 * Return: the clocksrc passed to new_dispatcher when the object was
 * created.
 */
int dispatcher_get_clocksource(const struct dispatcher *dsp);

/**
 * Convenenience macros for event initialization
 *
 * IMPORTANT: The cleanup functionality of the ON_HEAP variants requires
 * that "struct event" is embedded in the application's data structures
 * at offset 0.
 */

/**
 * __EVENT_INIT() - generic timer initializer
 */
#define __EVENT_INIT(cb, cln, f, ev, s, ns)		\
	((struct event){				\
		.fd = (f),				\
		.ep.events = (ev),			\
		.callback = (cb),			\
		.cleanup = (cln),			\
		.tmo.tv_sec  = (s),			\
		.tmo.tv_nsec = (ns),			\
	})

/**
 * EVENT_W_TMO_ON_STACK() - initializer for struct event
 * @cb: callback of type @cb_fn
 * @f:  file descriptor
 * @ev: epoll event mask
 * @us: timeout in microseconds, must be non-negative
 */
#define EVENT_W_TMO_ON_STACK(cb, f, ev, us)			\
	__EVENT_INIT(cb, cleanup_event_on_stack, f, ev,		\
		     (us) / 1000000L, (us) % 1000000L * 1000)

/**
 * EVENT_ON_STACK() - initializer for struct event
 * @cb: callback of type @cb_fn
 * @f:  file descriptor
 * @ev: epoll event mask
 *
 * The initialized event has no timeout.
 */
#define EVENT_ON_STACK(cb, f, ev) \
	EVENT_W_TMO_ON_STACK(cb, f, ev, 0)

/**
 * TIMER_EVENT_ON_STACK() - initializer for struct event
 * @cb: callback of type @cb_fn
 * @us: timeout in microseconds, must be non-negative
 * NOTE: it's pointless to set a timeout of 0 us (timer inactive),
 *       thus the code sets it to 1ns at least.
 * Thus, by passing us = 0, an event is created that will fire
 * immediately after calling event_wait() or event_loop().
 */
#define TIMER_EVENT_ON_STACK(cb, us)				\
	__EVENT_INIT(cb, cleanup_event_on_stack, -1, 0,		\
		     (us) / 1000000L, (us) % 1000000L * 1000 + 1)

/**
 * EVENT_W_TMO_ON_HEAP() - initializer for struct event
 * Like EVENT_W_TMO_ON_STACK(), but the cleanup callback
 * will free the struct event.
 */
#define EVENT_W_TMO_ON_HEAP(cb, f, ev, us)			\
	__EVENT_INIT(cb, cleanup_event_on_heap, f, ev,		\
		     (us) / 1000000L, (us) % 1000000L * 1000)

/**
 * EVENT_ON_HEAP() - initializer for struct event
 * Like EVENT_ON_STACK(), but the cleanup callback
 * will free the struct event.
 */
#define EVENT_ON_HEAP(cb, f, ev)			\
	EVENT_W_TMO_ON_HEAP(cb, f, ev, 0)

/**
 * TIMER_EVENT_ON_HEAP() - initializer for struct event
 * Like TIMER_EVENT_ON_STACK(), but the cleanup callback
 * will free the struct event.
 */
#define TIMER_EVENT_ON_HEAP(cb, us)				\
	__EVENT_INIT(cb, cleanup_event_on_heap, -1, 0,		\
		     (us) / 1000000L, (us) % 1000000L * 1000 + 1)

/**
 * timer_cb - prototype for a generic single-shot timer callback
 * Use the TIMER macros below.
 */
typedef void (*timer_cb)(void *arg);

/**
 * _call_timer_cb() - helper for invoking timer callbacks
 *
 * Internal use.
 */
int _call_timer_cb(struct event *, uint32_t events);

/**
 * struct timer_event - helper struct for invoking timer callbacks
 */
struct timer_event {
	struct event e;
	timer_cb timer_fn;
	void *timer_arg;
};

/**
 * TIMER_ON_STACK() - initializer for a single-shot timer
 * @fn: callback of type @timer_cb
 * @arg: argument to pass to @fn
 * @us: timeout in microseconds
 */
#define TIMER_ON_STACK(fn, arg, us)					\
	((struct timer_event){						\
		.e = TIMER_EVENT_ON_STACK(_call_timer_cb, us),		\
		.timer_fn = fn,						\
		.timer_arg = arg,					\
	})

/**
 * TIMER_ON_HEAP() - initializer for a single-shot timer
 * Like TIMER_ON_STACK(), but the cleanup callback
 * will free the struct event.
 */
#define TIMER_ON_HEAP(fn, arg, us)					\
	((struct timer_event){						\
		.e = TIMER_EVENT_ON_HEAP(_call_timer_cb, us),		\
		.timer_fn = fn,						\
		.timer_arg = arg,					\
	})

#endif
