/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <syslog.h>

#include <avahi-common/llist.h>
#include <avahi-common/malloc.h>
#include <avahi-common/timeval.h>
#include <avahi-common/watch.h>

#include "log.h"
#include "../event.h"
#include "avahi.h"
#include "common.h"

#ifndef HOST_NAME_MAX
# include <limits.h>
# define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

static const struct timespec null_ts = { .tv_sec = 0, };

typedef struct AvahiWatch {
	struct event ev;
	AvahiMiniPoll *eventpoll;

	AvahiWatchCallback cb;
	void *userdata;

	AVAHI_LLIST_FIELDS(AvahiWatch, watches);
} AvahiWatch;

typedef struct AvahiTimeout {
	struct event ev;
	AvahiMiniPoll *eventpoll;

	AvahiTimeoutCallback cb;
	void *userdata;

	AVAHI_LLIST_FIELDS(AvahiTimeout, timeouts);
} AvahiTimeout;

struct AvahiMiniPoll {
	AvahiPoll api;
	struct dispatcher *base;

	AVAHI_LLIST_HEAD(AvahiWatch, watches);
	AVAHI_LLIST_HEAD(AvahiTimeout, timeouts);
};

static int watch_cb(struct event *evt, uint32_t what)
{
	AvahiWatch *w = container_of(evt, AvahiWatch, ev);
	AvahiWatchEvent events = 0;

	assert(evt->reason == REASON_EVENT_OCCURED);
	if (what & EPOLLIN)
		events |= AVAHI_WATCH_IN;
	if (what & EPOLLOUT)
		events |= AVAHI_WATCH_OUT;

	w->cb(w, evt->fd, events, w->userdata);
	return EVENTCB_CONTINUE;
}

static int
watch_add(AvahiWatch *w, int fd, AvahiWatchEvent events)
{
	AvahiMiniPoll *ep = w->eventpoll;
	short ev_events = 0;

	if (events & AVAHI_WATCH_IN)
		ev_events |= EPOLLIN;
	if (events & AVAHI_WATCH_OUT)
		ev_events |= EPOLLOUT;

	w->ev.fd = fd;
	w->ev.ep.events = events;
	w->ev.callback = watch_cb;
	w->ev.tmo = null_ts;

	return event_add(ep->base, &w->ev);
}

static AvahiWatch *
watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent events,
	  AvahiWatchCallback cb, void *userdata)
{
	AvahiMiniPoll *ep;
	AvahiWatch *w;
	int ret;

	assert(api);
	assert(fd >= 0);
	assert(cb);

	ep = api->userdata;
	assert(ep);

	w = avahi_new(AvahiWatch, 1);
	if (!w)
		return NULL;

	w->eventpoll = ep;
	w->cb = cb;
	w->userdata = userdata;

	ret = watch_add(w, fd, events);
	if (ret != 0) {
		free(w);
		return NULL;
	}

	AVAHI_LLIST_PREPEND(AvahiWatch, watches, ep->watches, w);

	return w;
}

static void
watch_update(AvahiWatch *w, AvahiWatchEvent events)
{
	msg(LOG_DEBUG, "called: %x\n", events);
	w->ev.ep.events &= ~(EPOLLIN | EPOLLOUT);
	if (events & AVAHI_WATCH_IN)
		w->ev.ep.events |= EPOLLIN;
	if (events & AVAHI_WATCH_OUT)
		w->ev.ep.events |= EPOLLOUT;

	event_modify(&w->ev);
}

static AvahiWatchEvent
watch_get_events(AvahiWatch *w)
{
	AvahiWatchEvent events = 0;

	msg(LOG_DEBUG, "called\n");
	if (w->ev.ep.events & EPOLLIN)
		events |= AVAHI_WATCH_IN;
	if (w->ev.ep.events & EPOLLOUT)
		events |= AVAHI_WATCH_OUT;

	return events;
}

static void
watch_free(AvahiWatch *w)
{
	AvahiMiniPoll *ep = w->eventpoll;

	msg(LOG_DEBUG, "called\n");
	event_remove(&w->ev);

	AVAHI_LLIST_REMOVE(AvahiWatch, watches, ep->watches, w);

	free(w);
}

static int
timeout_cb(struct event *evt, AVAHI_GCC_UNUSED uint32_t what)
{
	AvahiTimeout *t = container_of(evt, AvahiTimeout, ev);

	assert(evt->reason == REASON_TIMEOUT);
	msg(LOG_DEBUG, "called\n");
	t->cb(t, t->userdata);
	return EVENTCB_CONTINUE;
}

static int
timeout_add(AvahiTimeout *t, const struct timeval *tv)
{
	AvahiMiniPoll *ep = t->eventpoll;

	msg(LOG_DEBUG, "called\n");
	t->ev.callback = timeout_cb;
	t->ev.fd = -1;
	t->ev.tmo.tv_sec = tv->tv_sec;
	t->ev.tmo.tv_nsec = 1000 * tv->tv_usec;
	t->ev.flags = TMO_ABS;

	return event_add(ep->base, &t->ev);
}

static AvahiTimeout *
timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback cb, void *userdata)
{
	AvahiMiniPoll *ep;
	AvahiTimeout *t;
	int ret;

	assert(api);
	assert(cb);

	ep = api->userdata;

	assert(ep);

	t = avahi_new(AvahiTimeout, 1);
	if (!t)
		return NULL;

	t->eventpoll = ep;
	t->cb = cb;
	t->userdata = userdata;

	ret = timeout_add(t, tv);
	if (ret != 0) {
		free(t);
		return NULL;
	}

	AVAHI_LLIST_PREPEND(AvahiTimeout, timeouts, ep->timeouts, t);

	return t;
}

static void
timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
	struct timespec new;

	if (tv) {
		new.tv_sec = tv->tv_sec;
		new.tv_nsec = 1000 * tv->tv_usec;
	} else
		new = null_ts;

	t->ev.flags |= TMO_ABS;
	event_mod_timeout(&t->ev, &new);
}

static void
timeout_free(AvahiTimeout *t)
{
	AvahiMiniPoll *ep = t->eventpoll;

	timeout_update(t, NULL);

	AVAHI_LLIST_REMOVE(AvahiTimeout, timeouts, ep->timeouts, t);
	free(t);
}

AvahiMiniPoll *
avahi_mini_poll_new(struct dispatcher *base)
{
	AvahiMiniPoll *ep = avahi_new(AvahiMiniPoll, 1);

	ep->base = base;

	ep->api.userdata = ep;

	ep->api.watch_new = watch_new;
	ep->api.watch_free = watch_free;
	ep->api.watch_update = watch_update;
	ep->api.watch_get_events = watch_get_events;

	ep->api.timeout_new = timeout_new;
	ep->api.timeout_free = timeout_free;
	ep->api.timeout_update = timeout_update;

	AVAHI_LLIST_HEAD_INIT(AvahiWatch, ep->watches);
	AVAHI_LLIST_HEAD_INIT(AvahiTimeout, ep->timeouts);

	return ep;
}

void
avahi_mini_poll_free(AvahiMiniPoll *ep)
{
	assert(ep);

	for (AvahiWatch *w_next, *w = ep->watches; w; w = w_next) {
		w_next = w->watches_next;

		watch_free(w);
	}

	for (AvahiTimeout *t_next, *t = ep->timeouts; t; t = t_next) {
		t_next = t->timeouts_next;

		timeout_free(t);
	}

	free(ep);
}

void
avahi_mini_poll_quit(AvahiMiniPoll *ep)
{
	assert(ep);

	/* we don't actually have anything to do, since events are
	 * associated with watches and timeouts, not with this
	 * polling object itself.
	 */
}

const AvahiPoll *
avahi_mini_poll_get(AvahiMiniPoll *ep)
{
	assert(ep);

	return &ep->api;
}

