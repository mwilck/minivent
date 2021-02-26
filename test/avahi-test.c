/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <stdbool.h>

#include <avahi-common/watch.h>
#include <avahi-common/timeval.h>
#include <avahi-common/gccmacro.h>

#include "log.h"
#include "event.h"
#include "avahi.h"

#include "helpers.c"

static const AvahiPoll *api = NULL;
static struct dispatcher *base = NULL;

static void callback(AvahiWatch *w, int fd, AvahiWatchEvent event,
		     AVAHI_GCC_UNUSED void *userdata)
{
    if (event & AVAHI_WATCH_IN) {
        ssize_t r;
        char c;

        if ((r = read(fd, &c, 1)) <= 0) {
		msg(LOG_WARNING, "read() failed: %s\n",
		    r < 0 ? strerror(errno) : "EOF");
		api->watch_free(w);
            return;
        }

        msg(LOG_INFO, "Read: %c\n", c >= 32 && c < 127 ? c : '.');
    }
}

static void wakeup(AvahiTimeout *t, AVAHI_GCC_UNUSED void *userdata)
{
    struct timeval tv;
    static unsigned i = 0;

    msg(LOG_INFO, "Wakeup #%u\n", i++);

    if (i > 10)
	    exit_main_loop();

    avahi_elapse_time(&tv, 1000, 0);
    api->timeout_update(t, &tv);
}

int main(AVAHI_GCC_UNUSED int argc, AVAHI_GCC_UNUSED char *argv[])
{
    AvahiMiniPoll *ep;
    sigset_t ep_mask;
    struct timeval tv;

    log_level = LOG_DEBUG;
    log_timestamp = true;

    if (init_signals() != 0) {
	    msg(LOG_ERR, "failed to set up signals: %m\n");
	    return 1;
    }

    base = new_dispatcher(CLOCK_REALTIME);
    assert(base);

    ep = avahi_mini_poll_new(base);
    assert(ep);

    api = avahi_mini_poll_get(ep);

    api->watch_new(api, 0, AVAHI_WATCH_IN, callback, NULL);

    avahi_elapse_time(&tv, 1000, 0);
    api->timeout_new(api, &tv, wakeup, NULL);

    sigfillset(&ep_mask);
    sigdelset(&ep_mask, SIGTERM);
    sigdelset(&ep_mask, SIGINT);

    msg(LOG_NOTICE, "start\n");
    event_loop(base, &ep_mask, NULL);

    avahi_mini_poll_free(ep);
    free_dispatcher(base);

    return 0;
}
