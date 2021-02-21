/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef LIBEVENT_WATCH_H
#define LIBEVENT_WATCH_H

#include "event.h"

#include <avahi-common/cdecl.h>
#include <avahi-common/watch.h>

AVAHI_C_DECL_BEGIN

/** libevent main loop adapter */
typedef struct AvahiMiniPoll AvahiMiniPoll;

/** Create a new libevent main loop adapter attached to the specified
 event_base. */
AvahiMiniPoll *avahi_mini_poll_new(struct dispatcher *base);

/** Free libevent main loop adapter */
void avahi_mini_poll_free(AvahiMiniPoll *ep);

/** Quit libevent main loop adapter's thread if it has one */
void avahi_mini_poll_quit(AvahiMiniPoll *ep);

/** Return the abstract poll API structure for this object. This will
 * return the same pointer to an internally allocated structure on each
 * call */
const AvahiPoll *avahi_mini_poll_get(AvahiMiniPoll *ep);

AVAHI_C_DECL_END

#endif
