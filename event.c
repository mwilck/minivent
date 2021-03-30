/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <syslog.h>
#include <string.h>
#include <limits.h>
#include "log.h"
#include "common.h"
#include "cleanup.h"
#include "event.h"
#include "timeout.h"

/* size of events array in call to epoll_pwait() */
#define MAX_EVENTS 8
#define LEN_CHUNK 8

struct dispatcher {
	int epoll_fd;
	bool exiting;
	struct event *timeout_event;
	unsigned int len, n, free;
	struct event **events;
};

const char * const reason_str[__MAX_CALLBACK_REASON] = {
	[REASON_EVENT_OCCURED] = "event occured",
	[REASON_TIMEOUT] = "timeout",
};

static int _dispatcher_increase(struct dispatcher *dsp)
{
	struct event **new;

	if (dsp->len >= UINT_MAX - LEN_CHUNK)
		return -EOVERFLOW;
	new = realloc(dsp->events, (dsp->len + LEN_CHUNK) * sizeof(*new));
	if (!new)
		return -ENOMEM;
	dsp->len += LEN_CHUNK;
	dsp->events = new;
	msg(LOG_DEBUG, "new size: %u\n", dsp->len);
	return 0;
}

static unsigned int _dispatcher_find(const struct dispatcher *dsp,
				     const struct event *evt)
{
	unsigned int i;

	for (i = 0; i < dsp->n; i++)
		if (dsp->events[i] == evt)
			return i;
	return UINT_MAX;
}

static int _dispatcher_add(struct dispatcher *dsp, struct event *evt)
{
	unsigned int i;
	int rc;

	if (_dispatcher_find(dsp, evt) != UINT_MAX)
		return -EEXIST;

	if (dsp->free > 0) {
		for (i = 0; i < dsp->n; i++) {
			if (dsp->events[i] == NULL)
				break;
		}
		if (i == dsp->n) {
			msg(LOG_WARNING, "free=%u, but no empty slot found\n",
			    dsp->free);
			dsp->free = 0;
		} else {
			dsp->events[i] = evt;
			dsp->free--;
			msg(LOG_DEBUG, "new event @%u, %u/%u/%u free\n",
			    i, dsp->free, dsp->n, dsp->len);
			return 0;
		}
	}

	if (dsp->len == dsp->n)
		if ((rc = _dispatcher_increase(dsp)) < 0)
			return rc;

	dsp->events[dsp->n] = evt;
	dsp->n++;
	msg(LOG_DEBUG, "new event @%u, %u/%u/%u free\n",
	    dsp->n - 1, dsp->free, dsp->n, dsp->len);
	return 0;
}

static int _dispatcher_gc(struct dispatcher *dsp) {
	unsigned int i, n;
        struct event **new;

	if (dsp->free <= dsp->len / 4)
		return 0;

	n = dsp->n;
	for (i = n; i > 0; i--) {
		unsigned int j;

		if (dsp->events[i - 1] != NULL)
			continue;

		for (j = i - 1; j > 0; j--)
			if (dsp->events[j - 1] != NULL)
				break;

		memmove(&dsp->events[j], &dsp->events[i],
			(dsp->n - i) * sizeof(*dsp->events));

		n -= (i - j);
		if (j == 0)
			break;
		else
			i = j;
	}

	if (dsp->n - n  != dsp->free)
		msg(LOG_ERR, "error: %u != %u\n", dsp->free, dsp->n - n);
	else {
		msg(LOG_DEBUG, "collected %u slots\n", dsp->free);
		dsp->n = n;
		dsp->free = 0;
	}

	for (i = 0; i < dsp->n; i++) {
		if (dsp->events[i] == NULL)
			msg(LOG_ERR, "error at %u\n", i);
	}

	if (dsp->len <= 2 * LEN_CHUNK || dsp->n >= dsp->len / 2)
		return 0;

	new = realloc(dsp->events, (dsp->len / 2) * sizeof(*new));
	if (!new)
		return -ENOMEM;
	dsp->events = new;
	dsp->len = dsp->len / 2;

	msg(LOG_NOTICE, "new size: %u/%u\n", dsp->n, dsp->len);
	return 0;
}

static int _dispatcher_remove(struct dispatcher *dsp, struct event *ev,
			      bool do_gc)
{
	unsigned int i;

	if ((i = _dispatcher_find(dsp, ev)) == UINT_MAX) {
		msg(LOG_NOTICE, "event not found\n");
		return -ENOENT;
	}

	dsp->events[i] = NULL;
	if (i == dsp->n - 1)
		dsp->n--;
	else
		dsp->free++;

	msg(LOG_DEBUG, "removed event @%u, %u/%u/%u free\n",
	    i, dsp->free, dsp->n, dsp->len);

	return do_gc ? _dispatcher_gc(dsp) : 0;
}

int _event_remove_epoll(struct event *evt)
{
	if (evt->fd != -1) {
		int rc = epoll_ctl(evt->dsp->epoll_fd, EPOLL_CTL_DEL, evt->fd, NULL);

		if (rc == -1)
			msg(LOG_ERR, "EPOLL_CTL_DEL: %m");
		return rc;
	} else
		return 0;
}

static void _run_cleanup_handlers(struct dispatcher *dsp, bool do_epoll)
{
	unsigned int i;

	for (i = 0; i < dsp->n; i++) {
		struct event *evt = dsp->events[i];

		if (!evt)
			continue;

		if (do_epoll)
			_event_remove_epoll(evt);
		if (evt->cleanup)
			evt->cleanup(evt);
	}
}

int cleanup_dispatcher(struct dispatcher *dsp)
{

	if (!dsp)
		return -EINVAL;
	if (dsp->exiting)
		return 0;

	dsp->exiting = true;

	_run_cleanup_handlers(dsp, true);
	timeout_reset(dsp->timeout_event);

	dsp->len = dsp->n = dsp->free = 0;
	free(dsp->events);
	dsp->events = NULL;
	dsp->exiting = false;
	return 0;
}

void free_dispatcher(struct dispatcher *dsp)
{
	if (!dsp)
		return;

	/*
	 * If this function is called e.g. after fork(), we must not
	 * call epoll_ctl() or reset the timerfd (thus not call timeout_reset()).
	 * Just close the dup'd timerfd and epoll_fd, and free memory.
	 */
	_run_cleanup_handlers(dsp, false);
	if (dsp->timeout_event)
		free_timeout_event(dsp->timeout_event);
	if (dsp->epoll_fd != -1)
		close(dsp->epoll_fd);
	free(dsp->events);
	free(dsp);
}

static DEFINE_CLEANUP_FUNC(free_dsp_p, struct dispatcher *, free_dispatcher);
static int _event_add(struct dispatcher *dsp, struct event *evt);

struct dispatcher *new_dispatcher(int clocksrc)
{
	struct dispatcher *dsp __cleanup__(free_dsp_p) = NULL;

	dsp = calloc(1, sizeof(*dsp));
	if (!dsp)
		return NULL;

	if ((dsp->epoll_fd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
		msg(LOG_ERR, "epoll_create1: %m\n");
		return NULL;
	}

	if (!(dsp->timeout_event = new_timeout_event(clocksrc))) {
		msg(LOG_ERR, "failed to create timeout event: %m\n");
		return NULL;
	}

	/* Don't use event_add() here, timeout is tracked separately */
	if (_event_add(dsp, dsp->timeout_event) != 0) {
		msg(LOG_ERR, "failed to dispatch timeout event: %m\n");
		return NULL;
	} else
		return STEAL_PTR(dsp);
}

int dispatcher_get_efd(const struct dispatcher *dsp)
{
	if (!dsp)
		return -EINVAL;
	return dsp->epoll_fd;
}

static int _event_add(struct dispatcher *dsp, struct event *evt)
{
	evt->ep.data.ptr = evt;
	if (evt->fd != -1 &&
	    epoll_ctl(dsp->epoll_fd, EPOLL_CTL_ADD, evt->fd, &evt->ep) == -1) {
		msg(LOG_ERR, "failed to add event: %m\n");
		_dispatcher_remove(evt->dsp, evt, true);
		return -errno;
	}
	evt->dsp = dsp;
	evt->reason = 0;
	return timeout_add(dsp->timeout_event, evt);
}

int event_add(struct dispatcher *dsp, struct event *evt)
{
	int rc;

	if (!dsp || !evt || !evt->callback)
		return -EINVAL;
	if (dsp->exiting)
		return -EBUSY;
	if ((rc = _dispatcher_add(dsp, evt)) < 0)
		return rc;
	return _event_add(dsp, evt);
}

static int _event_remove(struct event *evt, bool do_gc)
{
	int rc;

	if (!evt || !evt->dsp)
		return -EINVAL;

	rc = _event_remove_epoll(evt);
	if (rc == -1)
		rc = -errno;

	_dispatcher_remove(evt->dsp, evt, do_gc);
	timeout_cancel(evt->dsp->timeout_event, evt);
	evt->dsp = NULL;

	return rc;
}

int event_remove(struct event *evt)
{
	return _event_remove(evt, true);
}

int event_mod_timeout(struct event *evt, const struct timespec *tmo)
{
	unsigned int i;
	struct timespec ts;

	if (!evt || !evt->dsp || !tmo)
		return -EINVAL;
	if (evt->dsp->exiting)
		return -EBUSY;
	if ((i = _dispatcher_find(evt->dsp, evt)) == UINT_MAX) {
		msg(LOG_WARNING, "attempt to modify non-existing event\n");
		return -EEXIST;
	}

	ts = *tmo;
	return timeout_modify(evt->dsp->timeout_event, evt, &ts);
}

int event_modify(struct event *evt)
{
	int rc;
	unsigned int i;

	if (!evt || !evt->dsp)
		return -EINVAL;
	if (evt->dsp->exiting)
		return -EBUSY;
	if ((i = _dispatcher_find(evt->dsp, evt)) == UINT_MAX) {
		msg(LOG_WARNING, "attempt to modify non-existing event\n");
		return -EEXIST;
	}
	rc= epoll_ctl(evt->dsp->epoll_fd, EPOLL_CTL_MOD,
			   evt->fd, &evt->ep);
	return rc == -1 ? -errno : 0;
}

void _event_invoke_callback(struct event *ev, unsigned short reason,
			   unsigned int events, bool reset_reason)
{
	int rc;

	if (ev->reason) {
		msg(LOG_DEBUG, "skipping callback for %s because of %s\n",
		    reason_str[reason], reason_str[ev->reason]);
		return;
	}
	if (ev->flags & (__EV_CLEANUP|__EV_REMOVE)) {
		msg(LOG_DEBUG, "skipping callback for %s, event scheduled for removal\n",
		    reason_str[reason]);
		return;
	}

	ev->reason = reason;
	rc = ev->callback(ev, events);

	if (rc == EVENTCB_CLEANUP)
		ev->flags |= __EV_CLEANUP;
	else if (rc == EVENTCB_REMOVE)
		ev->flags |= __EV_REMOVE;
	if (reset_reason)
		ev->reason = 0;
}


int event_wait(struct dispatcher *dsp, const sigset_t *sigmask)
{
	int ep_fd = dispatcher_get_efd(dsp);
	int rc, i;
	unsigned int j;
	bool removed = false;
	struct epoll_event events[MAX_EVENTS];
	struct epoll_event *tmo_event = NULL;

	if (!dsp)
		return -EINVAL;
	if (dsp->exiting)
		return -EBUSY;
	if (ep_fd < 0)
		return -EINVAL;

	rc = epoll_pwait(ep_fd, events, MAX_EVENTS, -1, sigmask);
	if (rc == -1) {
		msg(errno == EINTR ? LOG_DEBUG : LOG_WARNING,
		    "epoll_pwait: %m\n");
		return -errno;
	}

	msg(LOG_DEBUG, "received %d events\n", rc);
	for (i = 0; i < rc; i++) {
		struct event *ev = events[i].data.ptr;

		if (ev == dsp->timeout_event)
			tmo_event = &events[i];
		else
			_event_invoke_callback(ev, REASON_EVENT_OCCURED,
					       events[i].events, false);
	}

	if (tmo_event) {
		struct event *ev = tmo_event->data.ptr;

		_event_invoke_callback(ev, REASON_EVENT_OCCURED,
					    tmo_event->events, false);
	}

	for (i = 0; i < rc; i++) {
		struct event *ev = events[i].data.ptr;

		ev->reason = 0;
	}

	for (j = 0; j < dsp->n; j++) {
		struct event *ev = dsp->events[j];

		if (ev && (ev->flags & (__EV_REMOVE | __EV_CLEANUP))) {
			msg(LOG_DEBUG, "cleaning out event %u\n", j);
			_event_remove(ev, false);
			if (ev->flags & __EV_CLEANUP && ev->cleanup)
				ev->cleanup(ev);
			removed = true;
		}
	}
	if (removed)
		_dispatcher_gc(dsp);

	return ELOOP_CONTINUE;
}

int event_loop(struct dispatcher *dsp, const sigset_t *sigmask,
	       int (*err_handler)(int err))
{
	int rc;

	do {
		rc = event_wait(dsp, sigmask);
		if (rc < 0 && err_handler)
			rc = err_handler(-errno);
	} while (rc == ELOOP_CONTINUE);

	return rc;
}

void cleanup_event_on_stack(struct event *evt)
{
	if (!evt)
		return;
	if (evt->fd != -1)
		close(evt->fd);
}

void cleanup_event_on_heap(struct event *evt)
{
	if (!evt)
		return;
	cleanup_event_on_stack(evt);
	free(evt);
}

int _call_timer_cb(struct event *evt, uint32_t events __attribute__((unused)))
{
	struct timer_event *tim = container_of(evt, struct timer_event, e);

	if (!evt)
		return -EINVAL;

	tim->timer_fn(tim->timer_arg);
	return EVENTCB_CLEANUP;
}

int dispatcher_get_clocksource(const struct dispatcher *dsp)
{
	if (!dsp)
		return -EINVAL;
	return timeout_get_clocksource(dsp->timeout_event);
}
