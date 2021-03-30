/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#include <sys/types.h>
#include <sys/timerfd.h>
#include <inttypes.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>
#include <math.h>
#include <sched.h>
#include "log.h"
#include "event.h"
#include "ts-util.h"
#include "timeout.h"

#include "helpers.c"

/* Number of event sources / timeouts */
#define DEF_N_EVENTS 100
static int n_events = DEF_N_EVENTS;

/* desired runtime */
#define DEF_RUNTIME 20
static int runtime = DEF_RUNTIME;

/* Error thresholds for callback delay */
#define DEF_THRESH 1000
static int max_threshold = 10 * DEF_THRESH;
static int avg_threshold = DEF_THRESH;

static bool stop_signal;

/*
 * interval: 1, 2, 3, or 4 s
 * start value 0.5, 0.6, 0.7, ..., 0.9s
 */
static void start_event_1(int i __attribute__((unused)),
			  struct itimerspec *it,
			  unsigned short *flags __attribute__((unused)))
{
	it->it_value.tv_sec = 0;
	it->it_value.tv_nsec = (random() % 5 + 5) * 100 * 1000 * 1000L;
	it->it_interval.tv_sec = random() % 4 + 1;
	it->it_interval.tv_nsec = 0;
	ts_normalize(&it->it_value);
	ts_normalize(&it->it_interval);
}

/* 1, 2, or 3 s, or 0 (no timeout) */
static void new_timeout_1(struct timespec *ts,
			  unsigned short *flags __attribute__((unused)))
{
	ts->tv_sec = random() % 4;
	ts->tv_nsec = 0;
}

/*
 * interval: 1-2s
 * start value 0.5, 0.6, 0.7, ..., 1.4s
 */
static void start_event_2(int i __attribute__((unused)),
			  struct itimerspec *it,
			  unsigned short *flags __attribute__((unused)))
{
	it->it_value.tv_sec = 0;
	it->it_value.tv_nsec = (random() % 5 + 10) * 100 * 1000 * 1000L;
	it->it_interval.tv_sec = 1;
	it->it_interval.tv_nsec = random() % 1000000000L;
	ts_normalize(&it->it_value);
	ts_normalize(&it->it_interval);
}

/* 1-2s */
static void new_timeout_2(struct timespec *ts,
			  unsigned short *flags __attribute__((unused)))
{
	ts->tv_sec = 1;
	ts->tv_nsec = random() % 1000000000L;
}

static void disable_2(bool *disabled)
{
	*disabled = (random() % 10 == 0);
}

/*
 * Absolute times
 * interval: 1.0, 1.25, ..., 2.0
 * start value 1.0, 1.1, ..., 2.0
 */
static void start_event_3(int i __attribute__((unused)),
			  struct itimerspec *it,
			  unsigned short *flags)
{
	struct timespec now;

	clock_gettime(LOG_CLOCK, &now);
	it->it_value.tv_sec = now.tv_sec + 1;
	it->it_value.tv_nsec = (random() % 11) * 100000000;
	it->it_interval.tv_sec = 1;
	it->it_interval.tv_nsec = (random() % 4) * 250000000;
	ts_normalize(&it->it_value);
	ts_normalize(&it->it_interval);
	*flags |= TMO_ABS;
}

/* absolute times
 * 1.0, 1.25, ..., 2.0s
 */
static void new_timeout_3(struct timespec *ts,
			  unsigned short *flags __attribute__((unused)))
{
	struct timespec now;

	clock_gettime(LOG_CLOCK, &now);
	ts->tv_sec = now.tv_sec + 1;
	ts->tv_nsec = (random() % 4) * 250000000;
	*flags |= TMO_ABS;
}

static char *format_ts(const struct timespec *ts, char *buf, size_t sz)
{
	if (ts->tv_sec >= 0)
		snprintf(buf, sz, " %ld.%06lds",
			 (long)ts->tv_sec % 1000, ts->tv_nsec / 1000);
	else
		snprintf(buf, sz, "-%ld.%06lds",
			 ((long)-ts->tv_sec - 1) % 1000,
			 (1000000000L - ts->tv_nsec) / 1000);
	return buf;
}

struct itevent {
	struct event e;
	int instance;
	int count;
	int ev_count;
	int race_count;
	int err_count;
	double deviation, max_deviation, sq_deviation;
	int expect;
	struct timespec expected;
	bool disabled;
	void (*new_timeout)(struct timespec *, unsigned short *);
	void (*disable)(bool *);
};

static void evaluate(struct itevent *it,
		     int *total_count, int *event_count, int *race_count,
		     int *err_count,
		     double *total_max,
		     double *total_dev, double *total_sq,
		     const struct timespec *stop_ts)
{
	double avg, stdev;
	unsigned short reason = it->e.reason;

	if (reason == REASON_TIMEOUT || reason == REASON_EVENT_OCCURED)
		event_remove(&it->e);
	else {
		/* test termination */
		char tb0[24], tb1[24];

		if (ts_compare(&it->expected, stop_ts) <= 0)
			/* the expected time should be in the future */
			msg(LOG_ERR, "%d: MISSED EVENT at %s (stop: %s)\n",
			    it->instance,
			    format_ts(&it->expected, tb0, sizeof(tb0)),
			    format_ts(stop_ts, tb1, sizeof(tb1)));

		event_remove(&it->e);
	}
	close(it->e.fd);

	avg = it->deviation / it->count;
	stdev = sqrt((it->sq_deviation - it->count * avg * avg) / (it->count - 1));
	msg(LOG_NOTICE, "%d: count=%d events=%d races=%d err=%d max=%.0f avg=%.0f stdev=%.0f us\n",
	    it->instance, it->count, it->ev_count,it->race_count, it->err_count,
	    it->max_deviation, avg, stdev);

	*total_count += it->count;
	*event_count +=it->ev_count;
	*race_count += it->race_count;
	*err_count += it->err_count;
	if (*total_max < it->max_deviation)
		*total_max = it->max_deviation;
	*total_dev += it->deviation;
	*total_sq += it->sq_deviation;
}

static int test_cb(struct event *evt, uint32_t __attribute__((unused)) events)
{

	uint64_t val;
	struct timespec new_tmo = { .tv_sec = 0, };
	struct itevent *itev = (struct itevent *)evt;
	struct timespec now;
	static const struct timespec null_ts;
	struct itimerspec cur;
	double dev;
	int rc;
	char tb0[24], tb1[24], tb2[24];
	unsigned short reason = evt->reason;
	bool disabled = false;

	itev->count++;
	clock_gettime(dispatcher_get_clocksource(evt->dsp), &now);
	ts_subtract(&now, &itev->expected);

	if (itev->expect != reason)
		msg(LOG_NOTICE, "%d UNEXPECTED @%d: %s=>%s (delta t=%s)\n",
		    itev->instance, itev->count,
		    reason_str[itev->expect], reason_str[reason],
		    format_ts(&now, tb0, sizeof(tb0)));
	else
		msg(LOG_INFO, "%d %d: %s (%s)\n",
		    itev->instance, itev->count, reason_str[reason],
		    format_ts(&now, tb0, sizeof(tb0)));

	if (now.tv_sec < 0) {
		msg(LOG_ERR, "%d EARLY EVENT %d: %s (%s)\n",
		    itev->instance, itev->count, reason_str[reason],
		    format_ts(&now, tb0, sizeof(tb0)));
		itev->err_count++;
	}

	dev =  ts_to_us(&now);
	itev->deviation += dev;
	itev->sq_deviation += dev * dev;
	if (dev > itev->max_deviation)
		itev->max_deviation = dev;
	msg(LOG_DEBUG, "%d: %.1f %.1f %.1f %.1f\n",
	    itev->instance, dev, itev->deviation, itev->max_deviation,
	    itev->sq_deviation);
	if (reason == REASON_EVENT_OCCURED)
		itev->ev_count++;

check_timer:

	clock_gettime(dispatcher_get_clocksource(evt->dsp), &now);
	if (timerfd_gettime(evt->fd, &cur) == -1) {
		msg(LOG_ERR, "timerfd_gettime: %m\n");
		itev->err_count++;
	}

	/*
	 * The event can have occured between the return of epoll_wait() and the
	 * call to timerfd_gettime(). If it did, timerfd_gettime() will read the
	 * time of the *next* timer event, but the next epoll_wait() call will
	 * immediately return, signalling the past event, and making us believe we were
	 * woken up too early.
	 * Thus read the fd after running timerfd gettime().
	 * If we get EAGAIN here, it means that timerfd_gettime() has returned the
	 * a time for a future event, which is ok.
	 * If the read is successful in timepout case, it means an event has occured.
	 * It could have happened between the timerfd_gettime and the read, thus
	 * retry.
	 */
	rc = read(evt->fd, &val, sizeof(val));
	if (rc == -1 && errno != EAGAIN) {
		msg(LOG_ERR, "failed to read timerfd: %m\n");
		itev->err_count++;
	}
	if (rc != -1 && reason == REASON_TIMEOUT) {
		msg(LOG_NOTICE, "%d race detected @%d: event after timeout, next event %s\n",
		    itev->instance, itev->count,
		    format_ts(&cur.it_value, tb0, sizeof(tb0)));
		itev->race_count++;
		reason = REASON_EVENT_OCCURED;
		goto check_timer;
	}

	if (itev->disable)
		itev->disable(&disabled);
	if (disabled != itev->disabled) {
		msg(LOG_NOTICE, "%d %sabling event\n",
		    itev->instance, disabled ? "dis" : "en");
		itev->e.ep.events = disabled ? 0 : EPOLLIN;
		if (event_modify(&itev->e) == 0)
			itev->disabled = disabled;
		else {
			msg(LOG_ERR, "ERROR: event_modify: %m\n");
			itev->err_count++;
		}
	}

	itev->new_timeout(&new_tmo, &evt->flags);
	ts_normalize(&new_tmo);

	/* Avoid both timout and event being disabled */
	if (itev->disabled && ts_compare(&new_tmo, &null_ts) == 0) {
		msg(LOG_WARNING, "%d overriding timeout for disabled event\n",
		    itev->instance);
		new_tmo.tv_sec++;
	}

	if (!(evt->flags & TMO_ABS)) {
		itev->expected = now;
		if (ts_compare(&new_tmo, &null_ts) == 0 ||
		    (!itev->disabled &&
		     ts_compare(&cur.it_value, &new_tmo) <= 0)) {
			ts_add(&itev->expected, &cur.it_value);
			itev->expect = REASON_EVENT_OCCURED;
		} else {
			ts_add(&itev->expected, &new_tmo);
			itev->expect = REASON_TIMEOUT;
		}
	} else {
		ts_add(&cur.it_value, &now);
		if (ts_compare(&new_tmo, &null_ts) == 0 ||
		    (!itev->disabled &&
		     ts_compare(&cur.it_value, &new_tmo) <= 0)) {
			itev->expected = cur.it_value;
			itev->expect = REASON_EVENT_OCCURED;
		} else {
			itev->expected = new_tmo;
			itev->expect = REASON_TIMEOUT;
		}
	}
	msg(LOG_INFO, "%d: expecting %s @%s (ev %s tmo %s)\n",
	    itev->instance, reason_str[itev->expect],
	    format_ts(&itev->expected, tb0, sizeof(tb0)),
	    itev->disabled ? "disabled" : format_ts(&cur.it_value, tb1, sizeof(tb1)),
	    format_ts(&new_tmo, tb2, sizeof(tb2)));

	if ((rc = event_mod_timeout(evt, &new_tmo)) < 0 &&
	    (rc != -ENOENT)) {
		msg(LOG_ERR, "failed to set new timeout: %s\n", strerror(-rc));
		itev->err_count++;
	}

	return EVENTCB_CONTINUE;
}

static void free_dsp(struct dispatcher **dsp) {
	if (*dsp)
		free_dispatcher(*dsp);
}

static int fini_cb(struct event *evt, uint32_t __attribute__((unused)) events)
{
	msg(LOG_INFO, "%s\n", reason_str[evt->reason]);
	exit_main_loop();
	return EVENTCB_CONTINUE;
}

static void cleanup_itevent(struct itevent **it)
{
	free(*it);
}

static void cleanup_event(struct event ***ev)
{
	free(*ev);
}

static int do_test(const char *name,
		   void (*start_times)(int i, struct itimerspec *, unsigned short *),
		   void (*new_timeout)(struct timespec *, unsigned short *),
		   void (*disable)(bool*))
{
	struct itevent *itev __attribute__((cleanup(cleanup_itevent))) = NULL;
	struct event **evt __attribute__((cleanup(cleanup_event))) = NULL;;
	struct event ev_stop;

	sigset_t ep_mask;
	int i, rc;
	int total_count = 0, event_count = 0, race_count = 0, err_count = 0;
	double total_dev = 0, total_max = 0, total_sq = 0;
	double avg, stdev;
	char tb0[24], tb1[24];
	struct timespec start_ts;

	struct dispatcher *dsp __attribute__((cleanup(free_dsp))) = NULL;

	if ((itev = calloc(n_events, sizeof(*itev))) == NULL)
		return -ENOMEM;
	if ((evt = calloc(n_events + 1, sizeof(*evt))) == NULL)
		return -ENOMEM;

	dsp = new_dispatcher(LOG_CLOCK);
	if (!dsp) {
		msg(LOG_ERR, "failed to create dispatcher: %m\n");
		return -errno;
	}

	for (i = 0; i < n_events; i++) {
		struct itimerspec it = {0, };
		int ifd;

		memset(&itev[i], 0, sizeof(itev[i]));
		ifd = timerfd_create(LOG_CLOCK, TFD_NONBLOCK|TFD_CLOEXEC);
		if (ifd == -1) {
			msg(LOG_ERR, "timerfd_create: %m\n");
			return 1;
		}
		itev[i].e = EVENT_ON_STACK(test_cb, ifd,  EPOLLIN);
		start_times(i, &it, &itev[i].e.flags);
		if (!(itev[i].e.flags & TMO_ABS)) {
			clock_gettime(LOG_CLOCK, &itev[i].expected);
			ts_add(&itev[i].expected, &it.it_value);
		} else
			itev[i].expected = it.it_value;
		itev[i].expect = REASON_EVENT_OCCURED;
		if (timerfd_settime(itev[i].e.fd,
				    itev[i].e.flags & TMO_ABS ? TFD_TIMER_ABSTIME : 0,
				    &it, NULL) == -1) {
			msg(LOG_ERR, "timerfd_settime: %m\n");
			close(itev[i].e.fd);
			itev[i].e.fd = -1;
			continue;
		}
		itev[i].instance = i;
		itev[i].new_timeout = new_timeout;
		itev[i].disable = disable;
		evt[i] = &itev[i].e;
		msg(LOG_INFO, "event %d: start %s (%s), interval %s\n",
		    itev[i].instance,
		    format_ts(&it.it_value, tb0, sizeof(tb0)),
		    itev[i].e.flags & TMO_ABS ? "absolute" : "relative",
		    format_ts(&it.it_interval, tb1, sizeof(tb1)));
	}

	for (i = 0; i < n_events; i++) {
		if (event_add(dsp, evt[i]) != 0)
			msg(LOG_ERR, "failed to add event %d: %m\n", i);
	}

	set_wait_mask(&ep_mask);

	if (!stop_signal) {
		ev_stop = TIMER_EVENT_ON_STACK(fini_cb, runtime * 1000000);
		if (event_add(dsp, &ev_stop))
			msg(LOG_ERR, "failed to add stop event: %m\n");
	} else {
		sigdelset(&ep_mask, SIGALRM);
		alarm(runtime);
	}

	clock_gettime(LOG_CLOCK, &start_ts);
	msg(LOG_NOTICE, "%s: started @%s, #events=%d, duration: %ds\n",
	    name, format_ts(&start_ts, tb0, sizeof(tb0)), n_events, runtime);

	rc = event_loop(dsp, &ep_mask, NULL);

	if (rc != -EINTR || !must_exit) {
		msg(LOG_WARNING, "unexpected exit from: %s\n", strerror(-rc));
		return 100;
	}

	msg(LOG_INFO, "exit signal received\n");
	start_ts.tv_sec += runtime;
	for (i = 0; i < n_events; i++)
		evaluate(&itev[i], &total_count, &event_count,
			 &race_count, &err_count,
			 &total_max, &total_dev, &total_sq, &start_ts);

	avg = total_dev / total_count;
	stdev = sqrt((total_sq - total_count * avg * avg) / (total_count - 1));
	printf("%s: count=%d, events=%d, races=%d, errors=%d, delay: max=%.0f avg=%.0f stdev=%.0f us\n",
	       name, total_count, event_count, race_count, err_count,
	       total_max, avg, stdev);

	rc = 0;
	if (err_count > 0) {
		msg(LOG_ERR, "ERROR: %d errors occured\n", err_count);
		rc++;
	}
	if (avg > avg_threshold) {
		msg(LOG_ERR, "ERROR: avg-threshold exceeded: %.0f > %d\n",
		    avg, avg_threshold);
		rc++;
	}
	if (total_max > max_threshold) {
		msg(LOG_ERR, "ERROR: max-threshold exceeded: %.0f > %d\n",
		    total_max, max_threshold);
		rc++;
	}
	return rc;
}

static int read_int(const char *arg, const char *opt, int *val)
{
	int v;
	char dummy;

	if (sscanf(optarg, "%d%c", &v, &dummy) == 1) {
		*val = v;
		return 0;
	} else {
		msg(LOG_ERR, "%s: ignoring invalid argument \"%s\"\n", opt, arg);
		return -EINVAL;
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"Options:\n"
		"\t[-t|--runtime] <time>	runtime per test in s (default: %d)\n"
		"\t[-n|--n-events] <n>		number of event sources (default: %d)\n"
		"\t[-m|--max-threshold] <x>	error threshold for max callback delay in us (default: %d)\n"
		"\t[-a|--avg-threshold] <x>	error threshold for avg callback delay in us (default: %d)\n"
		"\t[-s|--signal]		use signal rather than event for stopping\n"
		"\t|-q|--quiet]			suppress log messages\n"
		"\t[-v|--verbose]		verbose messages\n"
		"\t[-d|--debug]			debug messages\n"
		"\t[-h|--help]			print this help\n",
		prog, DEF_RUNTIME, DEF_N_EVENTS, 10 * DEF_THRESH, DEF_THRESH);
}

static int check_args(int argc, char *const argv[])
{
	static const struct option longopts[] = {
		{ "runtime", 1, NULL, 't' },
		{ "n-events", 1, NULL, 'n' },
		{ "max-threshold", 1, NULL, 'm' },
		{ "avg-threshold", 1, NULL, 'a' },
		{ "signal", 0, NULL, 's' },
		{ "quiet", 0, NULL, 'q' },
		{ "verbose", 0, NULL, 'v' },
		{ "debug", 0, NULL, 'd' },
		{ "help", 0, NULL, 'h' },
		{ 0, },
	};
	static const char optstring[] = "t:n:m:a:sqvdh";
	int opt;

	while((opt = getopt_long(argc, argv, optstring, longopts, NULL)) != -1) {

		switch (opt) {
		case 't':
			read_int(optarg, "--runtime", &runtime);
			break;
		case 'n':
			read_int(optarg, "--n-events", &n_events);
			break;
		case 'm':
			read_int(optarg, "--max-threshold", &max_threshold);
			break;
		case 'a':
			read_int(optarg, "--avg-threshold", &avg_threshold);
			break;
		case 's':
			stop_signal = true;
			break;
		case 'q':
			if (log_level < LOG_INFO)
				log_level = LOG_WARNING;
			break;
		case 'v':
			if (log_level < LOG_DEBUG)
				log_level = LOG_INFO;
			break;
		case 'd':
			log_level = LOG_DEBUG;
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
			usage(argv[0]);
			return -EINVAL;
		}
	}
	return 0;
}

int main(int argc, char *const argv[])
{
	log_level = LOG_WARNING;
	log_timestamp = true;
	struct sched_param sp;
	int rc = 0;

	if (check_args(argc, argv) != 0)
		return 1;

	if (init_signals() != 0) {
                msg(LOG_ERR, "failed to set up signals: %m\n");
                return 1;
        }

	sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
	if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1)
		msg(LOG_WARNING, "failed to set SCHED_FIFO: %m\n");

	rc += do_test("test 1", start_event_1, new_timeout_1, NULL);
	rc += do_test("test 2", start_event_2, new_timeout_2, disable_2);
	rc += do_test("test 3", start_event_3, new_timeout_3, disable_2);
	return rc ? 1 : 0;
}
