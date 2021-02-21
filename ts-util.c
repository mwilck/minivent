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
#include "ts-util.h"
void ts_normalize(struct timespec *tv)
{
	long quot, rem;
	if (tv->tv_nsec >= 0 && tv->tv_nsec < 1000000000L)
		return;
	quot = tv->tv_nsec / 1000000000L;
	rem = tv->tv_nsec % 1000000000L;
	if (rem < 0) {
		rem += 1000000000L;
		quot--;
	}
	tv->tv_sec += quot;
	tv->tv_nsec = rem;
}

void ts_add(struct timespec *t1, const struct timespec *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_nsec += t2->tv_nsec;
	ts_normalize(t1);
	return;
}

void ts_subtract(struct timespec *t1, const struct timespec *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	ts_normalize(t1);
	return;
}

int ts_compare(const struct timespec *t1, const struct timespec *t2)
{
	if (t1->tv_sec < t2->tv_sec)
		return -1;
	if (t1->tv_sec > t2->tv_sec)
		return 1;
	if (t1->tv_nsec < t2->tv_nsec)
		return -1;
	if (t1->tv_nsec > t2->tv_nsec)
		return 1;
	return 0;
}

static int ts_compare_q(const struct timespec **pt1,
			const struct timespec **pt2)
{
	return ts_compare(*pt1, *pt2);
}

long ts_search(struct timespec *const *tvs, size_t size, struct timespec *new)
{
	long low, high, mid;
	if (!new || !tvs || size > LONG_MAX)
		return -EINVAL;
	ts_normalize(new);
	if (size == 0)
		return 0;
	high = size - 1;
	if (ts_compare(new, tvs[high]) > 0)
		return size;
	low = 0;
	while (high - low > 1) {
		mid = low + (high - low) / 2;
		if (ts_compare(new, tvs[mid]) <= 0)
			high = mid;
		else
			low = mid;
	}
	if (high > low && ts_compare(new, tvs[low]) > 0)
		return high;
	else
		return low;
}

long ts_insert(struct timespec **tvs, size_t *len, size_t size,
	       struct timespec *new)
{
	long pos;
	if (!len || size <= *len)
		return -EOVERFLOW;
	pos = ts_search(tvs, *len, new);
	if (pos < 0)
		return pos;
	memmove(&tvs[pos + 1], &tvs[pos], (*len - pos) * sizeof(*tvs));
	(*len)++;
	tvs[pos] = new;
	return pos;
}

void ts_sort(struct timespec **tvs, size_t size)
{
	qsort(tvs, size, sizeof(struct timespec *),
	      (int (*)(const void *, const void *))ts_compare_q);
	return;
}
