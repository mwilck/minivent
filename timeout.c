/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <syslog.h>
#include "common.h"
#include "ts-util.h"
#include "log.h"
#include "timeout.h"
#include "event.h"

struct timeout_handler {
        int source;
        size_t len;
        struct timespec **timeouts;
	struct timespec expiry;
	struct event ev;
};

int timeout_get_clocksource(const struct event *evt)
{
	return container_of_const(evt, struct timeout_handler, ev)->source;
}

static void free_timeout_handler(struct timeout_handler *th)
{
        if (th->ev.fd != -1)
                close(th->ev.fd);

        if (th->timeouts)
                free(th->timeouts);

        free(th);
}

void free_timeout_event(struct event *ev)
{
	return free_timeout_handler(container_of(ev, struct timeout_handler, ev));
}

struct event *new_timeout_event(int source)
{
        struct timeout_handler *th = calloc(1, sizeof(*th));

        if (!th)
                return NULL;
        th->ev.fd = timerfd_create(source, TFD_NONBLOCK|TFD_CLOEXEC);
        if (th->ev.fd == -1) {
                msg(LOG_ERR, "timerfd_create: %m\n");
                free(th);
                return NULL;
        }
        th->source = source;
	th->ev.ep.events = EPOLLIN;
	th->ev.ep.data.ptr = &th->ev;
	th->ev.callback = timeout_event;

	msg(LOG_DEBUG, "done\n");
        return &th->ev;
}

static long _timeout_rearm(struct timeout_handler *th, long pos)
{
        struct itimerspec it = { .it_interval = { 0, 0 }, };
        int rc;

        if (pos < (long)th->len)
                it.it_value = *th->timeouts[pos];

	if (ts_compare(&it.it_value, &th->expiry) == 0)
		return pos;

        msg(LOG_DEBUG, "current: %ld/%zd, expire: %ld.%06ld\n",
            pos, th->len, (long)it.it_value.tv_sec, it.it_value.tv_nsec / 1000L);

        rc = timerfd_settime(th->ev.fd, TFD_TIMER_ABSTIME, &it, NULL);
        if (rc == -1) {
                msg(LOG_ERR, "timerfd_settime: %m\n");
                return -errno;
        } else {
		th->expiry = it.it_value;
                return pos;
	}
}

static const struct timespec null_ts;

static long timeout_resize(struct timeout_handler *th, size_t size)
{
	struct timespec **tmp;

	if (size > LONG_MAX)
		return -EOVERFLOW;

	if (size == 0) {
		free(th->timeouts);
		th->timeouts = NULL;
		th->len = 0;
		return 0;
	}

	msg(LOG_DEBUG, "size old %zu new %zu\n", th->len, size);
	tmp = realloc(th->timeouts, size * sizeof(*th->timeouts));
	if (tmp == NULL)
		return -errno;

	th->timeouts = tmp;
	return size;
}

int timeout_reset(struct event  *tmo_event)
{
	struct timeout_handler *th =
		container_of(tmo_event, struct timeout_handler, ev);

	timeout_resize(th, 0);
	return _timeout_rearm(th, 0);
}

static int absolute_timespec(int source, struct timespec *ts)
{
	struct timespec now;

	if (clock_gettime(source, &now) == -1)
		return -errno;
	ts->tv_sec += now.tv_sec;
	ts->tv_nsec += now.tv_nsec;
	return 0;
}

static int timeout_add_ev(struct timeout_handler *th, struct event *event)
{
        long pos;
	int rc;

        if (!th || !event)
                return -EINVAL;

	if (ts_compare(&event->tmo, &null_ts) == 0)
		return 0;

	for (pos = 0; pos < (long)th->len; pos++)
		if (th->timeouts[pos] == &event->tmo) {
			msg(LOG_DEBUG, "event %p exists already at pos %ld/%zu\n",
			    event, pos, th->len);
			return -EEXIST;
		};

	if ((rc = timeout_resize(th, th->len + 1)) < 0) {
		msg(LOG_ERR, "failed to increase array size: %m\n");
		return rc;
	}

        if (~event->flags & TMO_ABS &&
	    absolute_timespec(th->source, &event->tmo) == -1)
			return -errno;

        pos = ts_insert(th->timeouts, &th->len, th->len + 1, &event->tmo);
        if (pos < 0) {
                msg(LOG_ERR, "ts_insert failed: %m\n");
                return errno ? -errno : -EIO;
        }

        msg(LOG_DEBUG, "new timeout at pos %ld/%zd: %ld.%06ld\n",
            pos, th->len, (long)event->tmo.tv_sec, event->tmo.tv_nsec / 1000L);

        if (pos == 0)
                _timeout_rearm(th, pos);

        return 0;
}

int timeout_add(struct event *tmo_event, struct event *ev)
{
	return timeout_add_ev(container_of(tmo_event, struct timeout_handler, ev), ev);
}

static int timeout_cancel_ev(struct timeout_handler *th, struct event *evt)
{
        struct timespec *ts = &evt->tmo;
        long pos;

	if (ts_compare(&evt->tmo, &null_ts) == 0)
		return 0;

        for (pos = 0; pos < (long)th->len && ts != th->timeouts[pos]; pos++);

        if (pos == (long)th->len) {
                msg(LOG_DEBUG, "%p: not found\n", evt);
		/*
		 * This is normal if called from a timeout handler.
		 * Mark the event as having no timeout.
		 */
		*ts = null_ts;
                return -ENOENT;
        }

	msg(LOG_DEBUG, "timeout %ld cancelled, %ld.%06ld\n",
            pos, (long)ts->tv_sec, ts->tv_nsec / 1000L);

	*ts = null_ts;
        memmove(&th->timeouts[pos], &th->timeouts[pos + 1],
                (th->len - pos - 1) * sizeof(*th->timeouts));

        th->len--;
        if (pos == 0)
                _timeout_rearm(th, 0);
        return 0;
}

int timeout_cancel(struct event *tmo_event, struct event *ev)
{
	return timeout_cancel_ev(container_of(tmo_event, struct timeout_handler, ev), ev);
}

int timeout_modify(struct event *tmo_event, struct event *evt, struct timespec *new)
{
	struct timeout_handler *th =
		container_of(tmo_event, struct timeout_handler, ev);
        struct timespec *ts = &evt->tmo;
        long pos, pnew, pmin;

	if (ts_compare(&evt->tmo, &null_ts) == 0 || th->len == 0) {
		evt->tmo = *new;
		return timeout_add_ev(th, evt);
	}

	if (ts_compare(new, &null_ts) == 0)
		return timeout_cancel_ev(th, evt);

	if (ts_compare(new, &evt->tmo) == 0)
		/* Nothing changed */
		return 0;

	/* There could be several timeouts with the same expiry, find the right one */
	pmin = ts_search(th->timeouts, th->len, ts);
        for (pos = pmin;
             pos < (long)th->len &&
                     ts_compare(th->timeouts[pos], ts) == 0;
             pos++) {
                if (ts == th->timeouts[pos])
                        break;
        }

        if (pos == (long)th->len || ts != th->timeouts[pos]) {
		/* This is normal if timeout_modify called from timeout handler */
                msg(LOG_DEBUG, "%p: not found\n", evt);
                evt->tmo = *new;
		return timeout_add_ev(th, evt);
        }

        if (~evt->flags & TMO_ABS && absolute_timespec(th->source, new) == -1)
		return -errno;

	ts_normalize(new);
	pnew = ts_search(th->timeouts, th->len, new);
	if (pnew < 0)
		return pnew;

	if (pnew > pos + 1) {
		/*
		 * ts_search returns the position (pnew) at which the new tmo would be
		 * inserted. All members at pnew or higher are >= new.
		 * So if pnew = pos + 1, nothing needs to be done.
		 * Subtract 1, because pnew is after pos but pos will be moved away.
		 */
		pnew--;
		memmove(&th->timeouts[pos], &th->timeouts[pos + 1],
			(pnew - pos)  * sizeof(*th->timeouts));
		th->timeouts[pnew] = &evt->tmo;
	} else if (pnew < pos) {
		memmove(&th->timeouts[pnew + 1], &th->timeouts[pnew],
			(pos - pnew)  * sizeof(*th->timeouts));
		th->timeouts[pnew] = &evt->tmo;
	}
	msg(LOG_DEBUG, "timeout %ld now at pos %ld, %ld.%06ld -> %ld.%06ld\n",
            pos, pnew, (long)ts->tv_sec, ts->tv_nsec / 1000L,
            (long)new->tv_sec, new->tv_nsec / 1000L);
	evt->tmo = *new;


        if (pnew == 0)
                _timeout_rearm(th, 0);
        return 0;
}

static void _timeout_run_callbacks(struct timespec **tss, long n)
{
        long i;

        for (i = 0; i < n; i++) {
                struct event *evt;

                evt = container_of(tss[i], struct event, tmo);

                msg(LOG_DEBUG, "calling callback %ld (%ld.%06ld)\n", i,
                    (long)tss[i]->tv_sec, tss[i]->tv_nsec / 1000);

		_event_invoke_callback(evt, REASON_TIMEOUT, 0, true);
        }

}

int timeout_event(struct event *tmo_ev, uint32_t events)
{
	struct timeout_handler *th = container_of(tmo_ev, struct timeout_handler, ev);
        struct timespec now;
        struct timespec **expired;
        long pos = th->len;
	uint64_t val;

	if (tmo_ev->reason != REASON_EVENT_OCCURED || events & ~EPOLLIN) {
		msg(LOG_WARNING, "unexpected reason %s, events 0x%08x\n",
		    reason_str[tmo_ev->reason], events);
		return EVENTCB_CONTINUE;
	}

	if (read(tmo_ev->fd, &val, sizeof(val)) == -1)
		/*
		 * EAGAIN happens if the most recent timer was cancelled
		 * and the timer rearmed before we get here.
		 */
		msg(errno == EAGAIN ? LOG_DEBUG : LOG_ERR,
		    "failed to read timerfd: %m\n");

	clock_gettime(th->source, &now);

        /*
         * callbacks may add new timers, therefore we must iterate here.
	 * Also, we can't simply run _timeout_run_callbacks(th->timeouts),
	 * because the array might be changed under us. Therefore allocate
	 * a new array for the expired timers and iterate over it.
	 * Note: If the callback forks, this array might never be freed and
	 * valgrind may report some bytes "still reachable".
         */
        while (th->len > 0) {

		/* Expired timeouts are at the beginning, don't ts_search() here */
		for (pos = 0;
		     pos < (long)th->len && ts_compare(th->timeouts[pos], &now) <= 0;
		     pos++);

                if (pos == (long)th->len) {
                        expired = th->timeouts;
                        th->len = 0;
                        th->timeouts = NULL;
                        _timeout_run_callbacks(expired, pos);
                        free(expired);
                } else if (pos > 0) {
                        expired = malloc(pos * sizeof(*expired));
                        if (expired)
                                memcpy(expired, th->timeouts, pos * sizeof(*expired));
                        th->len -= pos;
                        memmove(th->timeouts, &th->timeouts[pos],
                                th->len * sizeof(*th->timeouts));
                        if (expired) {
                                _timeout_run_callbacks(expired, pos);
                                free(expired);
                        }
                } else
                        break;
        }

        _timeout_rearm(th, 0);
	return EVENTCB_CONTINUE;
}
