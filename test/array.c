/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdbool.h>
#include <cmocka.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include "log.h"
#include "../event.h"

#define LEN_CHUNK 8
#define N_EV 1000
#define N_DICE 50000
#define __U__ __attribute__((unused))
#define ZZZ void **dummy __U__

static struct dispatcher *dsp;
static struct event events[N_EV];

static int cln_cnt;

static void reset_count(void)
{
	cln_cnt = 0;
}

void check_count(int c)
{
	int cc = cln_cnt;

	reset_count();
	assert_int_equal(c, cc);
}

static void cleanup_cb(struct event *evt __U__)
{
	cln_cnt++;
}

static void free_cb(struct event *evt)
{
	cln_cnt++;
	free(evt);
}

static int callback(struct event *evt __U__, unsigned int event __U__)
{
	assert_true(false);
	return EVENTCB_CONTINUE;
}

static void setup_events(void)
{
	int i;
	for (i = 0; i < N_EV; i++) {
		struct event *ev = &events[i];

		ev->dsp = NULL;
		ev->fd = -1;
		ev->callback = callback;
		ev->cleanup = cleanup_cb;
		ev->tmo.tv_sec = 1;
	}
}

static int setup(ZZZ)
{
	dsp = new_dispatcher(CLOCK_REALTIME);
	setup_events();

	return 0;
}

static int teardown(ZZZ)
{
	free_dispatcher(dsp);
	return 0;
}

static void test_arr_0(ZZZ)
{
	struct event *ev = NULL;

	assert_int_equal(event_remove(ev), -EINVAL);
	assert_int_equal(event_add(dsp, ev), -EINVAL);
}

static void test_arr_1(ZZZ)
{
	assert_int_equal(event_remove(&events[0]), -EINVAL);
	assert_int_equal(event_add(dsp, &events[0]), 0);
	assert_int_equal(event_add(dsp, &events[0]), -EEXIST);
	assert_int_equal(event_remove(&events[1]), -EINVAL);
	assert_int_equal(event_remove(&events[0]), 0);
	assert_int_equal(event_remove(&events[0]), -EINVAL);
}

static void test_arr_2(ZZZ)
{
	int i;

	for (i = 0; i < 5 * LEN_CHUNK; i++) {
		assert_int_equal(event_add(dsp, &events[i]), 0);
		if (i > 0)
			assert_int_equal(event_add(dsp, &events[i - 1]), -EEXIST);
	}
	cleanup_dispatcher(dsp);
	check_count(5 * LEN_CHUNK);
}

#define N_ADD (6 * LEN_CHUNK + 6)  // should be multiple of 6

static void test_arr_3(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_remove(&events[i]), 0);
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_4(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);
	for (i = 1; i < N_ADD; i += 2)
		assert_int_equal(event_remove(&events[i]), 0);
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_5(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);
	for (i = N_ADD - 1; i >= 0; i -= 2)
		assert_int_equal(event_remove(&events[i]), 0);
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_6(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);
	for (i = N_ADD - 2; i >= 0; i -= 2)
		assert_int_equal(event_remove(&events[i]), 0);
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_7(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);

	for (i = 0; i < N_ADD; i += 6) {
		assert_int_equal(event_remove(&events[i]), 0);
		assert_int_equal(event_remove(&events[i + 1]), 0);
		assert_int_equal(event_remove(&events[i + 2]), 0);
	}
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_8(ZZZ)
{
	int i;

	for (i = 0; i < N_ADD; i++)
		assert_int_equal(event_add(dsp, &events[i]), 0);

	for (i = 3; i < N_ADD; i += 6) {
		assert_int_equal(event_remove(&events[i]), 0);
		assert_int_equal(event_remove(&events[i + 1]), 0);
		assert_int_equal(event_remove(&events[i + 2]), 0);
	}
	for (i = 0; i < N_ADD; i += 2)
		assert_int_equal(event_add(dsp, &events[i + N_ADD]), 0);

	cleanup_dispatcher(dsp);
	check_count(N_ADD);
}

static void test_arr_9(ZZZ)
{
	int i;
	struct event *evt;

	for (i = 0; i < N_ADD; i++) {
		evt = malloc(sizeof(*evt));
		assert_non_null(evt);
		*evt = events[i];
		evt->cleanup = free_cb;
		assert_int_equal(event_add(dsp, evt), 0);
		if (i % 3) {
			assert_int_equal(event_remove(evt), 0);
			free(evt);
		}
	}

	cleanup_dispatcher(dsp);
	check_count(N_ADD / 3);
}

static void test_rnd_0(ZZZ)
{
	int i, n;
	bool on[N_EV] = { false, };

	setup_events();
	for (i = 0; i < N_EV / 2; i++) {
		assert_int_equal(event_add(dsp, &events[i]), 0);
		on[i] = true;
	}

	n = N_EV / 2;
	for (i = 0; i < N_DICE; i++) {
		int pos = random() % N_EV;

		if (on[pos]) {
			msg(LOG_INFO, "removing %d\n", pos);
			assert_int_equal(event_add(dsp, &events[pos]), -EEXIST);
			assert_int_equal(event_remove(&events[pos]), 0);
			on[pos] = false;
			n--;
		} else {
			msg(LOG_INFO, "adding %d\n", pos);
			assert_int_equal(event_remove(&events[pos]), -EINVAL);
			assert_int_equal(event_add(dsp, &events[pos]), 0);
			on[pos] = true;
			n++;
		}
	}

	msg(LOG_NOTICE, "expecting: %d\n", n);
	cleanup_dispatcher(dsp);
	check_count(n);
}

static void test_rnd_1(ZZZ)
{
	int i, n;
	bool on[N_EV] = { false, };

	setup_events();
	for (i = 0; i < N_EV / 2; i++) {
		assert_int_equal(event_add(dsp, &events[i]), 0);
		on[i] = true;
	}

	n = N_EV / 2;
	for (i = 0; i < N_DICE; i++) {
		int pos = random() % N_EV;
		bool want = random() % 2;

		if (want == on[pos])
			continue;
		if (on[pos]) {
			// msg(LOG_INFO, "removing %d\n", pos);
			assert_int_equal(event_add(dsp, &events[pos]), -EEXIST);
			assert_int_equal(event_remove(&events[pos]), 0);
			on[pos] = false;
			n--;
		} else {
			// msg(LOG_INFO, "adding %d\n", pos);
			assert_int_equal(event_remove(&events[pos]), -EINVAL);
			assert_int_equal(event_add(dsp, &events[pos]), 0);
			on[pos] = true;
			n++;
		}
	}

	msg(LOG_NOTICE, "expecting: %d\n", n);
	cleanup_dispatcher(dsp);
	check_count(n);
}

static void test_rnd_2(ZZZ)
{
	int i, n;
	bool on[N_EV] = { false, };

	setup_events();
	for (i = 0; i < N_EV / 2; i++) {
		assert_int_equal(event_add(dsp, &events[i]), 0);
		on[i] = true;
	}

	n = N_EV / 2;
	for (i = 0; i < N_DICE; i++) {
		int pos = random() % N_EV;
		bool want = !(random() % 10);

		if (want == on[pos])
			continue;
		if (on[pos]) {
			// msg(LOG_INFO, "removing %d\n", pos);
			assert_int_equal(event_add(dsp, &events[pos]), -EEXIST);
			assert_int_equal(event_remove(&events[pos]), 0);
			on[pos] = false;
			n--;
		} else {
			// msg(LOG_INFO, "adding %d\n", pos);
			assert_int_equal(event_remove(&events[pos]), -EINVAL);
			assert_int_equal(event_add(dsp, &events[pos]), 0);
			on[pos] = true;
			n++;
		}
	}

	msg(LOG_NOTICE, "expecting: %d\n", n);
	cleanup_dispatcher(dsp);
	check_count(n);
}

static int test_event_array(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_arr_0),
		cmocka_unit_test(test_arr_1),
		cmocka_unit_test(test_arr_2),
		cmocka_unit_test(test_arr_3),
		cmocka_unit_test(test_arr_4),
		cmocka_unit_test(test_arr_5),
		cmocka_unit_test(test_arr_6),
		cmocka_unit_test(test_arr_7),
		cmocka_unit_test(test_arr_8),
		cmocka_unit_test(test_arr_9),
		cmocka_unit_test(test_rnd_0),
		cmocka_unit_test(test_rnd_1),
		cmocka_unit_test(test_rnd_2),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	log_level = LOG_NOTICE;
	ret += test_event_array();
	return ret;
}
