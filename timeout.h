/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _TIMEOUT_H
#define _TIMEOUT_H

struct event;
struct dispatcher;

void free_timeout_event(struct event *th);
struct event *new_timeout_event(int source);

int timeout_add(struct event *tmo_event, struct event *event);
int timeout_modify(struct event *tmo_event, struct event *event, struct timespec *new);
int timeout_cancel(struct event *tmo_event, struct event *);
void timeout_event(struct event *ev, uint32_t);
int timeout_get_clocksource(const struct event *);

#endif
