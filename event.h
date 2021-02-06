#ifndef _EVENT_H
#define _EVENT_H
#include <sys/epoll.h>

/**
 * reason codes for event callback function
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

struct event;
struct dispatcher;

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
 * @reason: REASON_TIMEOUT or REASON_EVENT_OCCURED
 * @events: bit mask of epoll events (see epoll_ctl(2))
 */
typedef void (*cb_fn)(struct event *evt, int reason, uint32_t events);

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
 * @callback: the callback function to be called if the event occurs or
 *      times out. This field *must* be set.
 * @dispatcher: the dispatcher object to which this event belongs
 * @tmo: The timeout for the event.
 *      Setting @tmo.tv_sec = @tmo.tv_nsec = 0 on calls to event_add()
 *      creates an event with no (=infinite) timeout.
 *      CAUTION: USED INTERNALLY. Do not change this any more after calling
 *      event_add(), after event_finish(), it may be set again. The field
 *      may be modified by the dispatcher code.
 * @flags: See above. Currently only @TMO_ABS is supported. This field may
 *      be used internally by the dispatcher, be sure to set or clear only
 *      public bits.
 */

struct event {
	struct epoll_event ep;
	int fd;
	cb_fn callback;
	const struct dispatcher *dsp;
	struct timespec tmo;
	unsigned int flags;
};

/**
 * event_add() - add an event to be monitored.
 *
 * @dispatcher: a dispatcher object
 * @event: an event structure. See the description above for the
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_add(const struct dispatcher *dsp, struct event *event);

/**
 * event_remove() - remove the event from epoll.
 *
 * @event: a previously added event structure
 *
 * NOTE: this function is intended for being called from the callback
 * if it is called with REASON_TIMEOUT. In this case, the timeout is
 * inactive already (because it has expired). In all other situations,
 * call event_finished() instead to deactivate both the event itself
 * and the associated timeout. Calling event_remove() in those other
 * situations would leave an existing timeout active.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_remove(struct event *event);

/**
 * event_finished() - remove an event and timeout
 *
 * @event: a previously added event structure
 *
 * NOTE: this function is intended for being called from the callback
 * if it is called with REASON_EVENT_OCCURED, and if it's not a recurring
 * event. Both the event's fd will be removed from epoll and the event
 * timeout will be canceled.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 */
int event_finished(struct event *event);

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
int event_mod_timeout(struct event *event, struct timespec *tmo);

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
 * Return: a positive number (number of events seen) on success, a
 * negative error code (-errno) on failure.
 * In particular, it returns -EINTR if interrupted by a caught signal.
 */
int event_wait(const struct dispatcher *dsp, const sigset_t *sigmask);

/**
 * event_loop(): wait for some or timeouts, repeatedly
 *
 * @dispatcher: a dispatcher object
 * @sigmask: set of signals to be blocked while waiting
 *
 * This function calls event_wait() in a loop, and returns if event_wait()
 * returns an error code.
 *
 * Return: 0 on success, negative error code (-errno) on failure.
 * In particular, it returns -EINTR if interrupted by caught signal.
 */
int event_loop(const struct dispatcher *dsp, const sigset_t *sigmask);

/**
 * free_dispatcher() - free a dispatcher object.
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

#endif
