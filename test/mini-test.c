/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "event.h"

static void sighandler(int sig __attribute__((unused)))
{
}

static int cb(struct event *evt, uint32_t events __attribute__((unused)))
{
	fprintf(stderr, "Hello world! (%s)\n", reason_str[evt->reason]);

	/* "commit suicide" - this will case event_loop() to return */
	kill(getpid(), SIGINT);
	return EVENTCB_CLEANUP;
}

int main(void)
{
	struct sigaction sa = { .sa_handler = sighandler, };
	sigset_t mask;
	struct event evt;
	struct dispatcher *dsp;

	/* Proper signal handling is required to make the "suicide" above work */
	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigdelset(&mask, SIGINT);

	dsp = new_dispatcher(CLOCK_REALTIME);
	evt = TIMER_EVENT_ON_STACK(cb, 1000000);
	event_add(dsp, &evt);
	event_loop(dsp, &mask, NULL);
	free_dispatcher(dsp);

	return 0;
}
