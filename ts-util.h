/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _TS_UTIL_H
#define _TS_UTIL_H

static inline uint64_t ts_to_us(const struct timespec *ts)
{
	return ts->tv_sec * 1000000ULL + ts->tv_nsec / 1000;
}

static inline void us_to_ts(uint64_t us, struct timespec *ts)
{
	ts->tv_sec = us / 1000000L;
	ts->tv_nsec = (us % 1000000L) * 1000;
}

void ts_normalize(struct timespec *ts);
void ts_add(struct timespec *t1, const struct timespec *t2);
void ts_subtract(struct timespec *t1, const struct timespec *t2);
int ts_compare(const struct timespec *ts1, const struct timespec *ts2);
void ts_sort(struct timespec **tss, size_t len);
long ts_search(struct timespec *const *tvs, size_t len, struct timespec *new);
long ts_insert(struct timespec **tvs, size_t *len, size_t size, struct timespec *new);

#endif
