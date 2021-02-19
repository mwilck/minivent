#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <syslog.h>
#include "log.h"
#include "util.h"
#include "event.h"
#include "timeout.h"

/* size of events array in call to epoll_pwait() */
#define MAX_EVENTS 8

struct dispatcher {
	int epoll_fd;
	struct event *timeout_event;
};

const char * const reason_str[__MAX_CALLBACK_REASON] = {
	[REASON_EVENT_OCCURED] = "event occured",
	[REASON_TIMEOUT] = "timeout",
};

void free_dispatcher(struct dispatcher *dsp)
{
	if (dsp->timeout_event)
		free_timeout_event(dsp->timeout_event);
	if (dsp->epoll_fd != -1)
		close(dsp->epoll_fd);
	free(dsp);
}

static void free_dsp(struct dispatcher **dsp) {
	if (*dsp)
		free_dispatcher(*dsp);
}

struct dispatcher *new_dispatcher(int clocksrc)
{
	struct dispatcher *dsp __attribute__((cleanup(free_dsp))) = NULL;

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

	if (event_add(dsp, dsp->timeout_event) != 0) {
		msg(LOG_ERR, "failed to dispatch timeout event: %m\n");
		return NULL;
	} else
		return STEAL_PTR(dsp);
}

int dispatcher_get_efd(const struct dispatcher *dsp)
{
	return dsp->epoll_fd;
}

int event_add(const struct dispatcher *dsp, struct event *evt)
{
	evt->ep.data.ptr = evt;
	evt->dsp = dsp;
	evt->reason = 0;
	if (evt->fd != -1 &&
	    epoll_ctl(dsp->epoll_fd, EPOLL_CTL_ADD, evt->fd, &evt->ep) == -1) {
		msg(LOG_ERR, "failed to add event: %m\n");
		return -errno;
	}
	return timeout_add(dsp->timeout_event, evt);
}

int event_remove(struct event *evt)
{
	int rc = epoll_ctl(evt->dsp->epoll_fd, EPOLL_CTL_DEL, evt->fd, NULL);

	return rc == -1 ? -errno : 0;
}

int event_finished(struct event *evt)
{
	if (!evt->dsp)
		return -EINVAL;
	timeout_cancel(evt->dsp->timeout_event, evt);
	return event_remove(evt);
}

int event_mod_timeout(struct event *event, struct timespec *tmo)
{
	return timeout_modify(event->dsp->timeout_event, event, tmo);
}

int event_modify(struct event *evt)
{
	int rc = epoll_ctl(evt->dsp->epoll_fd, EPOLL_CTL_MOD,
			   evt->fd, &evt->ep);

	return rc == -1 ? -errno : 0;
}

int event_wait(const struct dispatcher *dsp, const sigset_t *sigmask)
{
	int ep_fd = dispatcher_get_efd(dsp);
	int rc, i;
	struct epoll_event events[MAX_EVENTS];
	struct epoll_event *tmo_event = NULL;

	if (ep_fd < 0)
		return ep_fd;

	rc = epoll_pwait(ep_fd, events, MAX_EVENTS, -1, sigmask);
	if (rc == -1) {
		msg(errno == EINTR ? LOG_DEBUG : LOG_WARNING,
		    "epoll_pwait: %m\n");
		return -errno;
	}

	msg(LOG_INFO, "received %d events\n", rc);
	for (i = 0; i < rc; i++) {
		struct event *ev = events[i].data.ptr;

		if (ev == dsp->timeout_event)
			tmo_event = &events[i];
		else {
			ev->reason = REASON_EVENT_OCCURED;
			ev->callback(ev, events[i].events);
		}
	}

	if (tmo_event) {
		struct event *ev = tmo_event->data.ptr;

		ev->reason = REASON_EVENT_OCCURED;
		ev->callback(ev, tmo_event->events);
	}

	for (i = 0; i < rc; i++) {
		struct event *ev = events[i].data.ptr;
		ev->reason = 0;
	}

	return rc;
}

int event_loop(const struct dispatcher *dsp, const sigset_t *sigmask)
{
	int rc;

	do
		rc = event_wait(dsp, sigmask);
	while (rc >= 0);

	return rc >= 0 ? 0 : rc;
}

int dispatcher_get_clocksource(const struct dispatcher *dsp)
{
	return timeout_get_clocksource(dsp->timeout_event);
}
