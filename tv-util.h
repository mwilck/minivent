/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _TV_UTIL_H
#define _TV_UTIL_H
#ifndef __GLIBC__
#include <sys/time.h>
#endif

/*
 * Utility functions for dealing with "struct timeval".
 * See also ts_util.h, which has the same set of functions
 * for "struct timespec", with more detailed documentation.
 */

static inline uint64_t tv_to_us(const struct timeval *ts)
{
	return ts->tv_sec * 1000000ULL + ts->tv_usec;
}

static inline void us_to_tv(uint64_t us, struct timeval *tv)
{
	tv->tv_sec = us / 1000000L;
	tv->tv_usec = us % 1000000L;
}

void tv_normalize(struct timeval *tv);
void tv_add(struct timeval *t1, const struct timeval *t2);
void tv_subtract(struct timeval *t1, const struct timeval *t2);
int tv_compare(const struct timeval *tv1, const struct timeval *tv2);
void tv_sort(struct timeval **tvs, size_t len);
long tv_search(struct timeval *const *tvs, size_t len, struct timeval *new);
long tv_insert(struct timeval **tvs, size_t *len, size_t size, struct timeval *new);

#endif
