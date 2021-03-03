/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include "ts-util.h"
static void ts_random(struct timespec *tv)
{
	tv->tv_sec = random() % 2000 - 1000;
	tv->tv_nsec = random() % LONG_MAX / 2;
};

static void __attribute__((unused)) ts_print(const struct timespec *tv)
{
	fprintf(stderr, "%ld.%06ld ", (long)tv->tv_sec,
		(long)tv->tv_nsec / (1000000000L / 1000000L));
};

static int ts_test(void)
{
	int i, errors = 0;
	struct timespec tv[1000], tq[1000];
	struct timespec *ptv[1000], *qtv[1000];
	size_t ntv = 0;
	for (i = 0; i < 1000; i++) {
		ts_random(&tv[i]);
	}
	for (i = 0; i < 1000; i++) {
		do {
			tq[i] = tv[i];
		} while (0);
	}
	for (i = 0; i < 1000; i++) {
		tv[i] = tq[i];
		qtv[i] = &tq[i];
		ts_insert(ptv, &ntv, 1000, &tv[i]);
	}
	for (i = 0; i < 1000; i++) {
		ts_normalize(qtv[i]);
	}
	ts_sort(qtv, 1000);
	for (i = 0; i < 1000; i++) {
		if (i > 0 && ts_compare(ptv[i - 1], ptv[i]) > 0)
			errors++;
	}
	for (i = 0; i < 1000; i++) {
		if (ts_compare(ptv[i], qtv[i]) != 0)
			errors++;
	}
	return errors;
};

static int ts_test1(void)
{
	int i, errors = 0;
	struct timespec tv[1000], tq[1000];
	struct timespec *ptv[1000], *qtv[1000];
	size_t ntv = 0;
	for (i = 0; i < 1000; i++) {
		ts_random(&tv[i]);
	}
	for (i = 0; i < 1000; i++) {
		do {
			int j = random() % 1000;
			tq[i] = tv[j];
		} while (0);
	}
	for (i = 0; i < 1000; i++) {
		tv[i] = tq[i];
		qtv[i] = &tq[i];
		ts_insert(ptv, &ntv, 1000, &tv[i]);
	}
	for (i = 0; i < 1000; i++) {
		ts_normalize(qtv[i]);
	}
	ts_sort(qtv, 1000);
	for (i = 0; i < 1000; i++) {
		if (i > 0 && ts_compare(ptv[i - 1], ptv[i]) > 0)
			errors++;
	}
	for (i = 0; i < 1000; i++) {
		if (ts_compare(ptv[i], qtv[i]) != 0)
			errors++;
	}
	return errors;
};;

int main(void)
{
	int i, n_err = 0;
	for (i = 0; i < 1000; i++)
		n_err += ts_test();
	for (i = 0; i < 1000; i++)
		n_err += ts_test1();
	fprintf(stderr, "TESTS FINISHED, %d errors (#items: %d, #runs: %d)\n",
		n_err, 1000, 1000);
	return n_err ? 1 : 0;
}
