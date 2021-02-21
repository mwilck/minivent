/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "tv-util.h"
void tv_normalize(struct timeval *tv)
{
	long quot, rem;
	if (tv->tv_usec >= 0 && tv->tv_usec < 1000000L)
		return;
	quot = tv->tv_usec / 1000000L;
	rem = tv->tv_usec % 1000000L;
	if (rem < 0) {
		rem += 1000000L;
		quot--;
	}
	tv->tv_sec += quot;
	tv->tv_usec = rem;
}

void tv_add(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	tv_normalize(t1);
	return;
}

void tv_subtract(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	tv_normalize(t1);
	return;
}

int tv_compare(const struct timeval *t1, const struct timeval *t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	if (t1->tv_sec > t2->tv_sec)
		return 1;
	if (t1->tv_usec < t2->tv_usec)
		return -1;
	if (t1->tv_usec > t2->tv_usec)
		return 1;
	return 0;
}

static int tv_compare_q(const struct timeval **pt1, const struct timeval **pt2)
{
	return tv_compare(*pt1, *pt2);
}

long tv_search(struct timeval *const *tvs, size_t size, struct timeval *new)
{
	long low, high, mid;
	if (!new || !tvs || size > LONG_MAX)
		return -EINVAL;
	tv_normalize(new);
	if (size == 0)
		return 0;
	high = size - 1;
	if (tv_compare(new, tvs[high]) > 0)
		return size;
	low = 0;
	while (high - low > 1) {
		mid = low + (high - low) / 2;
		if (tv_compare(new, tvs[mid]) <= 0)
			high = mid;
		else
			low = mid;
	}
	if (high > low && tv_compare(new, tvs[low]) > 0)
		return high;
	else
		return low;
}

long tv_insert(struct timeval **tvs, size_t *len, size_t size,
	       struct timeval *new)
{
	long pos;
	if (!len || size <= *len)
		return -EOVERFLOW;
	pos = tv_search(tvs, *len, new);
	if (pos < 0)
		return pos;
	memmove(&tvs[pos + 1], &tvs[pos], (*len - pos) * sizeof(*tvs));
	(*len)++;
	tvs[pos] = new;
	return pos;
}

void tv_sort(struct timeval **tvs, size_t size)
{
	qsort(tvs, size, sizeof(struct timeval *),
	      (int (*)(const void *, const void *))tv_compare_q);
	return;
}
