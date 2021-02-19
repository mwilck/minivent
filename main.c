#include <sys/types.h>
#include <sys/timerfd.h>
#include <inttypes.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include "log.h"
#include "event.h"
#include "timeout.h"

#include "helpers.c"

#define N 20
#define RUNTIME 20
#define MAX_CALLS 50

struct itevent {
	struct event e;
	int instance;
	int count;
};

static void fini_cb(struct event __attribute__((unused)) *evt,
		    uint32_t __attribute__((unused)) events)
{
	msg(LOG_NOTICE, "%s\n", reason_str[evt->reason]);
	exit_main_loop();
}

static void mini_cb(struct event *evt, uint32_t __attribute__((unused)) events)
{

	uint64_t val;
	struct timespec new_tmo = { .tv_sec = 0, };
	struct itevent *itev = (struct itevent *)evt;
	int rc;

        msg(LOG_NOTICE, "%d %d: %s\n", itev->instance, itev->count,
	    reason_str[evt->reason]);
	if (evt->reason == REASON_TIMEOUT) {
		if (itev->count++ >= MAX_CALLS) {
			event_remove(evt);
			close(evt->fd);
			return;
		}
	} else {
		if (read(evt->fd, &val, sizeof(val)) == -1)
			msg(LOG_ERR, "failed to read timerfd: %m\n");
		else
			msg(LOG_DEBUG, "read: %"PRIu64"\n", val);

		if (itev->count++ >= MAX_CALLS) {
			event_finished(evt);
			close(evt->fd);
			return;
		}
	}
	evt->flags &= ~TMO_ABS;
	new_tmo.tv_sec = random() % 4;
	if ((rc = event_mod_timeout(evt, &new_tmo)) < 0 &&
	    (evt->reason != REASON_TIMEOUT || rc != -ENOENT))
		msg(LOG_ERR, "failed to set new timeout: %s\n", strerror(-rc));
}

static void free_dsp(struct dispatcher **dsp) {
	if (*dsp)
		free_dispatcher(*dsp);
}

int main(void)
{
        struct event evt0 = {
		.ep.events = EPOLLIN,
		.callback = mini_cb,
		.flags = TMO_ABS,
	};
	struct event evt_fini = {
		.fd = -1,
		.callback = fini_cb,
		.tmo.tv_sec = RUNTIME,
	};
	struct itevent itev[N];
	struct event *evt[N + 1];

	sigset_t ep_mask;
	int i, rc;

	log_level = LOG_DEBUG;
	log_timestamp = true;

	if (init_signals() != 0) {
                msg(LOG_ERR, "failed to set up signals: %m\n");
                return 1;
        }

	struct dispatcher *dsp __attribute__((cleanup(free_dsp))) = NULL;
	dsp = new_dispatcher(LOG_CLOCK);
	if (!dsp) {
		msg(LOG_ERR, "failed to create dispatcher: %m\n");
		return -errno;
	}

	for (i = 0; i < N; i++) {
		struct itimerspec it = {0, };
		itev[i].e = evt0;
		itev[i].e.ep.data.ptr = &itev[i].e;
		itev[i].e.fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
		if (itev[i].e.fd == -1) {
			msg(LOG_ERR, "timerfd_create: %m\n");
			return 1;
		}
		it.it_value.tv_nsec = (random() % 5 + 5) * 100 * 1000 * 1000L;
		it.it_interval.tv_sec = random() % 4 + 1;
		if (timerfd_settime(itev[i].e.fd, 0, &it, NULL) == -1) {
			msg(LOG_ERR, "timerfd_settime: %m\n");
			close(itev[i].e.fd);
			itev[i].e.fd = -1;
			continue;
		}
		itev[i].instance = i;
		itev[i].count = 0;
		evt[i] = &itev[i].e;
	}

	evt[N] = &evt_fini;

	for (i = 0; i < N + 1; i++) {
		if (event_add(dsp, evt[i]) != 0)
			msg(LOG_ERR, "failed to add event %d: %m\n", i);
	}

	sigfillset(&ep_mask);
	sigdelset(&ep_mask, SIGTERM);
	sigdelset(&ep_mask, SIGINT);

	msg(LOG_NOTICE, "start\n");

	rc = event_loop(dsp, &ep_mask);

	if (rc != -EINTR || !must_exit) {
		msg(LOG_WARNING, "unexpected exit from: %s\n", strerror(-rc));
		return 1;
	} else {
		msg(LOG_INFO, "exit signal received\n");
		return 0;
	}
}
