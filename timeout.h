/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _TIMEOUT_H
#define _TIMEOUT_H

struct event;

/**
 * free_timeout_event() - free resources associated with a timeout event
 * @tmo_event: a struct event returned from new_timeout_event().
 */
void free_timeout_event(struct event *tmo_event);

/**
 * new_timeout_event() - create a new timeout event object
 * @source: One of the supported clock sources of the sytstem, see clock_gettime(2).
 *
 * Return: a new timeout event object on success, NULL on failure.
 */
struct event *new_timeout_event(int source);

/**
 * timeout_add() - add an event to the timeout list.
 * @tmo_event: struct event returned from new_timeout_event().
 * @event: a struct event
 *
 * This function adds @event to the list of timeouts handled, using
 * the @event->tmo and @event->flags to determine the expiry of the timeout.
 * When the timeout expires, timeout_event() will call @event->callback()
 * with @event->reason set to @REASON_TIMEOUT.
 * If @event->tmo is {0, 0}, nothing is done.
 *
 * Return: 0 on success. On error, a negative error code.
 *  -EEXIST: the event is already in the list of timeouts handled.
 *  -ENOMEM: failed to allocate memory to insert the new element.
 *  -EINVAL: invalid input parameters.
 */
int timeout_add(struct event *tmo_event, struct event *event);

/**
 * timeout_modify() - modify the timeout value of a previously added event
 * @tmo_event: struct event returned from new_timeout_event().
 * @event: the event to modify
 * @new: the new timeout (doesn't need to be normalized)
 *
 * Moves the event in the timeout list to a new position according to
 * the new timeout value in @new. If the event isn't currently in the list,
 * timeout_add() will be called. If @new is {0, 0} (no timeout), timeout_cancel()
 * is called. On successful return, @event->tmo will be set
 * to @new, and normalized.
 * IMPORTANT: don't set @event->tmo to @new before calling this function.
 *
 * Return: 0 on success, negative error code on failure. Error codes can be
 * from timeout_add() or timeout_cancel().
 */
int timeout_modify(struct event *tmo_event, struct event *event, struct timespec *new);

/**
 * timeout_cancel() - remove an event from the timeout list
 * @tmo_event: struct event returned from new_timeout_event().
 * @event: the event to modify
 *
 * Subsequent calls to timeout_event() will not call this event's callback any more.
 * But if the timeout event has already happened and delivered to the event dispatcher,
 * this function will return -ENOENT, and the callback will be called later on.
 *
 * Return: 0 on success, negative error code on failure.
 *  -ENOENT: the event wasn't found in the timeout list. Either the timeout event
 *  had happened already, or the event had never been added / already cancelled.
 */
int timeout_cancel(struct event *tmo_event, struct event *);

/**
 * timeout_event() - handle timeout events
 * @tmo_event: struct event returned from new_timeout_event().
 * @events: epoll event bitmask, see epoll_wait(2); expected to be EPOLLIN.
 *
 * This function is invoked by the event dispatcher if the @tmo_event has occured,
 * meaning that one or more timeouts in the list handled by @tmo_ev have expired.
 * timeout_event() removes the expired events from the list and calls the respective
 * callbacks for the timed-out events with the @reason field set to @REASON_TIMEOUT.
 *
 * If the callback wants to extend or otherwise re-arm the timeout, it must call
 * timeout_add() or (preferrably) timeout_modify().
 */
void timeout_event(struct event *tmo_event, uint32_t events);

/**
 * timeout_get_clocksource() - obtain clock source used
 * @tmo_event: struct event returned from new_timeout_event().
 *
 * Return: the clock source passed to new_timeout_event() when @tmo_ev
 * was created.
 */
int timeout_get_clocksource(const struct event *tmo_event);

#endif
