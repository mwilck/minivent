/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-newer
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <errno.h>
#include <getopt.h>

#include "common.h"
#include "log.h"
#include "cleanup.h"
#include "../ts-util.h"
#include "../event.h"

#include "helpers.c"

#define BUFSIZE 256

static const struct sockaddr_un minivent_sa = {
	.sun_family = AF_UNIX,
	.sun_path = "\0minivent",
};

struct cfg {
	int n_clients;
	int accept_s;
	int wait;
} echo_cfg = {
	.n_clients = 1,
	.accept_s = 30,
	/*
	 * Default: 2100 ms - together with RECV_TMO_SECS, this
	 * causes a ~5% probability for a timeout on server side.
	 */
	.wait = 2100,

};

/* struct event at offset 0 to be able to use convenience macros */
struct echo_event {
	struct event e;
	char buf[BUFSIZE];
};

struct clt_event {
	struct event e;
	pid_t pid;
	unsigned int n;
	struct timespec start;
	struct timespec max_duration;
};

#define SEND_TMO_SECS 1
#define RECV_TMO_SECS 2
#define CLT_DELAY_SECS 0

static struct timespec accept_tmo = { .tv_sec = 30, };
static const struct timespec recv_tmo = { .tv_sec = RECV_TMO_SECS, };
static const struct timespec send_tmo = { .tv_sec = SEND_TMO_SECS, };

static void close_fd(int *pfd)
{
	if (*pfd != -1)
		close(*pfd);
}
static DEFINE_CLEANUP_FUNC(free_dsp, struct dispatcher *,
			   free_dispatcher);

static DEFINE_CLEANUP_FUNC(free_echo, struct echo_event *, free);

static int set_socketflags(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		msg(LOG_ERR, "F_GETFL failed: %m\n");
		return -errno;
	}
	if (fcntl(fd, F_SETFL, flags|O_NONBLOCK) == -1) {
		msg(LOG_ERR, "F_SETFL failed: %m\n");
		return -errno;
	}
	if ((flags = fcntl(fd, F_GETFD, 0)) == -1) {
		msg(LOG_ERR, "F_GETFD failed: %m\n");
		return -errno;
	}
	if (fcntl(fd, F_SETFD, flags|FD_CLOEXEC) == -1) {
		msg(LOG_ERR, "F_SETFD failed: %m\n");
		return -errno;
	}
	return 0;
}

static void clt_cleanup(struct event *evt)
{
	struct clt_event *clt = container_of(evt, struct clt_event, e);

	if (evt->fd != -1)
		close(evt->fd);

	msg(LOG_NOTICE, "stopped: %u requests, max duration %ld.%06lds\n",
	    clt->n, (long)clt->max_duration.tv_sec,
	    clt->max_duration.tv_nsec / 1000);
}

static void stop_client(const struct clt_event *clt)
{
	kill(clt->pid, SIGTERM);
}

static int clt_cb(struct event *evt, uint32_t events)
{
	struct clt_event *clt = container_of(evt, struct clt_event, e);
	char buf[BUFSIZE];
	int rc;
	struct timespec tmo;

	if (evt->reason == REASON_TIMEOUT && evt->ep.events & (EPOLLIN|EPOLLOUT)) {
		msg(LOG_WARNING, "timeout\n");
		close(evt->fd);
		evt->fd = -1;
		stop_client(clt);
		return EVENTCB_CONTINUE;
	} else if (events & EPOLLHUP) {
		msg(LOG_ERR, "server hangup\n");
		/*
		 * Closing is necessary here, otherwise SIGTERM won't be seen in
		 * event_wait(), as we'll get EPOLLHUP immediately again.
		 * We could also call event_remove(), but then our cleanup function
		 * wouldn't get called.
		 * Clearing all epoll flags is not sufficient!
		 */
		close(evt->fd);
		evt->fd = -1;
		stop_client(clt);
		return EVENTCB_CONTINUE;
	} else if (evt->reason == REASON_TIMEOUT) {
		evt->ep.events = EPOLLOUT|EPOLLHUP;
		tmo = send_tmo;
	} else if (events & EPOLLOUT) {
		int n;

		clock_gettime(CLOCK_REALTIME, &clt->start);
		n = snprintf(buf, sizeof(buf),
			     "Hello, this is %ld", (long)clt->pid);
		if ((rc = write(evt->fd, buf, n + 1)) != n + 1) {
			msg(LOG_ERR, "write: %d (%m), expected %d\n", rc, n + 1);
			stop_client(clt);
		}
		evt->ep.events = EPOLLIN|EPOLLHUP;
		tmo = recv_tmo;
	} else {
		long res;
		char dummy;
		struct timespec now;

		if ((rc = read(evt->fd, buf, sizeof(buf))) <= 0) {
			msg(LOG_ERR, "read: %d (%m)\n", rc);
			stop_client(clt);
		}
		clock_gettime(CLOCK_REALTIME, &now);
		ts_subtract(&now, &clt->start);

		buf[rc == sizeof(buf) ? rc - 1 : rc] = '\0';
		if (sscanf(buf, "Hello, this is %ld%c", &res, &dummy) != 1 ||
		    res != clt->pid)
			msg(LOG_ERR, "response BAD: %s\n", buf);
		else {
			clt->n++;
			msg(LOG_INFO, "response %u OK, time=%ld.%06lds\n",
			    clt->n, (long)now.tv_sec, now.tv_nsec / 1000);

			if (ts_compare(&now, &clt->max_duration) > 0)
				clt->max_duration = now;
		}

		if (echo_cfg.wait == 0) {
			evt->ep.events = EPOLLOUT|EPOLLHUP;
			tmo = send_tmo;
		} else {
			evt->ep.events = 0;

			tmo.tv_sec = 0;
			tmo.tv_nsec = ((random() % echo_cfg.wait) + 1) * 1000000;
			ts_normalize(&tmo);

			msg(LOG_DEBUG, "response: \"%s\", next in %ld.%06lds\n",
			    buf, (long)tmo.tv_sec, tmo.tv_nsec / 1000);
		}
	}

	if ((rc = event_modify(evt)) < 0) {
		msg(LOG_ERR, "event_modify: %s\n", strerror(-rc));
		stop_client(clt);
	}

	if ((rc = event_mod_timeout(evt, &tmo)) < 0) {
		msg(LOG_ERR, "event_mod_timeout: %s\n", strerror(-rc));
		stop_client(clt);
	}
	return EVENTCB_CONTINUE;
}

static int client(int num)
{
	struct dispatcher *dsp __cleanup__(free_dsp) = NULL;
	int sfd __cleanup__(close_fd) = -1;
	struct clt_event clt = { .n = 0, };
	sigset_t mask;
	int rc;

	dsp = new_dispatcher(CLOCK_REALTIME);
	if (!dsp) {
		msg(LOG_ERR, "failed to create dispatcher: %m");
		return errno ? -errno : -1;
	}

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd == -1) {
		msg(LOG_ERR, "failed to create socket: %m\n");
		return -errno;
	}

	if ((rc = set_socketflags(sfd)) < 0)
		return rc;

	if ((rc = connect(sfd, (struct sockaddr *)&minivent_sa,
			  sizeof(minivent_sa))) == -1) {
		msg(LOG_ERR, "error connecting to server: %m\n");
		return -errno;
	}

	/* Start with timer. Events will be set on first callback invocation */
	clt.e = EVENT_W_TMO_ON_STACK(clt_cb, sfd, 0,
				     CLT_DELAY_SECS * 1000000 + 1);
	clt.e.cleanup = clt_cleanup;
	clt.pid = getpid();

	if ((rc = event_add(dsp, &clt.e)) < 0) {
		msg(LOG_ERR, "event_add: %s\n", strerror(-rc));
		return rc;
	}
	sfd = -1;

	sigfillset(&mask);
	sigdelset(&mask, SIGTERM);

	/* without this, every client gets the same "random" numbers */
	srandom(random() % 1000 + num);

	msg(LOG_NOTICE, "client %d running with pid %ld\n", num, (long)clt.pid);
	rc = event_loop(dsp, &mask, NULL);

	return (rc == -EINTR ? 0 : -rc);
}

static void start_clt(void *arg)
{
	struct dispatcher *dsp = arg;
	pid_t pid;
	static int num;

	num++;
	if ((pid = fork()) == -1) {
		msg(LOG_ERR, "fork: %m\n");
		return;
	} else if (pid > 0)
		return;

	/* No return to the dispatcher from here */
	free_dispatcher(dsp);

	exit (client(num));
}

static DEFINE_CLEANUP_FUNC(free_tim, struct timer_event *, free);

static int start_clients(struct dispatcher *dsp)
{
	int i;

	for (i = 0; i < echo_cfg.n_clients; i++) {
		struct timer_event *tim __cleanup__(free_tim);
		int rc;

		tim = calloc(1, sizeof(*tim));
		if (!tim)
			return -ENOMEM;
		*tim = TIMER_ON_HEAP(start_clt, dsp, (random() % 11) * 1000);
		if ((rc = event_add(dsp, &tim->e)))
			msg(LOG_ERR, "event_add: %s\n", strerror(-rc));
		else {
			msg(LOG_INFO, "client %d scheduled\n", i);
			tim = NULL;
		}
	}
	return 0;
}

static bool must_close;

static int conn_cb(struct event *ev, uint32_t events)
{
	struct echo_event *echo = container_of(ev, struct echo_event, e);
	int rc;
	const struct timespec *new_tmo;

	if (ev->reason == REASON_TIMEOUT) {
		msg(LOG_WARNING, "timeout\n");
		return EVENTCB_CLEANUP;
	} else if (must_close) {
		msg(LOG_WARNING, "closing socket\n");
		return EVENTCB_CLEANUP;
	} else if (events & EPOLLHUP) {
		msg(LOG_WARNING, "peer hung up\n");
		return EVENTCB_CLEANUP;
	} else if (events & EPOLLIN) {
		rc = read(ev->fd, echo->buf, sizeof(echo->buf));
		if (rc <= 0) {
			msg(LOG_ERR, "read: %m\n");
			return EVENTCB_CLEANUP;
		}
		ev->ep.events = EPOLLOUT|EPOLLHUP;
		new_tmo = &send_tmo;
	} else {
		rc = write(ev->fd, echo->buf,
			   strnlen(echo->buf, sizeof(echo->buf)));
		if (rc == -1) {
			msg(LOG_ERR, "write: %m\n");
			return EVENTCB_CLEANUP;
		}
		ev->ep.events = EPOLLIN|EPOLLHUP;
		new_tmo = &recv_tmo;
	}

	if ((rc = event_modify(ev)) < 0) {
		msg(LOG_ERR, "event_modify: %s\n", strerror(-rc));
		return EVENTCB_CLEANUP;
	}

	if ((rc = event_mod_timeout(ev, new_tmo)) < 0) {
		msg(LOG_ERR, "event_mod_timeout: %s\n", strerror(-rc));
		return EVENTCB_CLEANUP;
	}

	return EVENTCB_CONTINUE;
}

static int kill_server(void)
{
	kill(getpid(), SIGINT);
	return EVENTCB_CONTINUE;

}

static int accept_cb(struct event *ev, uint32_t events __attribute__((unused)))
{
	struct echo_event __cleanup__(free_echo) *conn_event = NULL;
	int cfd __cleanup__(close_fd) = -1;
	int rc;

	if (ev->reason == REASON_TIMEOUT) {
		msg(LOG_NOTICE, "timeout in accept, server\n");
		must_close = true;
		return EVENTCB_CLEANUP;
	}

	if ((cfd = accept(ev->fd, NULL, NULL)) == -1) {
		msg(LOG_ERR, "error in accept: %m\n");
		return kill_server();
	}

	msg(LOG_DEBUG, "new connetion\n");
	if ((rc = set_socketflags(cfd)) < 0)
		return kill_server();

	if ((conn_event = calloc(1, sizeof(*conn_event))) == NULL)
		return kill_server();

	conn_event->e = EVENT_W_TMO_ON_HEAP(conn_cb, cfd, EPOLLIN|EPOLLHUP,
					    RECV_TMO_SECS * 1000000);

	if ((rc = event_add(ev->dsp, &conn_event->e)) < 0) {
		msg(LOG_ERR, "event_add: %s\n", strerror(-rc));
		return kill_server();
	}

	cfd = -1;
	conn_event = NULL;

	return EVENTCB_CONTINUE;
}

int n_terminated;

static int handle_intr(int errcode)
{
	pid_t pid;

	msg(LOG_DEBUG, "%s %d %d\n", strerror(-errcode), must_exit, got_sigchld);
	if (errcode != -EINTR)
		return errcode;
	else if (must_exit) {
		msg(LOG_NOTICE, "exit signal received\n");
		return ELOOP_QUIT;
	} else if (!got_sigchld) {
		msg(LOG_WARNING, "unexpected interruption, ignoring\n");
		return ELOOP_CONTINUE;
	}

	got_sigchld = 0;
	do {
		int wstatus;

		pid = waitpid(-1, &wstatus, WNOHANG);
		switch(pid) {
		case -1:
			if (errno != ECHILD)
				msg(LOG_ERR, "error in waitpid: %m\n");
		case 0:
			/* fallthrough */
			break;
		default:
			n_terminated++;
			if (!WIFEXITED(wstatus))
				msg(LOG_WARNING, "child %ld didn't exit normally\n",
				    (long)pid);
			else if (WEXITSTATUS(wstatus) != 0)
				msg(LOG_NOTICE, "child %ld exited with status \"%s\"\n",
				    (long)pid, strerror(WEXITSTATUS(wstatus)));
			else
				msg(LOG_DEBUG, "child %ld exited normally\n",
				    (long)pid);
			break;
		}
	} while (pid > 0);

	msg(LOG_DEBUG, "%d clients stopped\n", n_terminated);
	if (n_terminated >= echo_cfg.n_clients)
		return ELOOP_QUIT;
	else
		return ELOOP_CONTINUE;
}

static int server(void)
{
	struct dispatcher *dsp __cleanup__(free_dsp) =
		new_dispatcher(CLOCK_REALTIME);
	int fd __cleanup__(close_fd) = -1;
	struct event srv_event;
	int rc;
	sigset_t mask;

	if (!dsp) {
		msg(LOG_ERR, "failed to create dispatcher: %m");
		return errno ? -errno : -1;
	}

	if ((rc = start_clients(dsp)) < 0)
		return -1;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		msg(LOG_ERR, "failed to create socket: %m\n");
		return -errno;
	}

	if ((rc = set_socketflags(fd)) < 0)
		return rc;

	if (bind(fd, (struct sockaddr *)&minivent_sa,
		 sizeof(minivent_sa)) == -1) {
		msg(LOG_ERR, "bind() failed: %m\n");
		return -errno;
	}

	if (listen(fd, echo_cfg.n_clients) == -1) {
		msg(LOG_ERR, "listen() failed: %m\n");
		return -errno;
	}

	srv_event = EVENT_W_TMO_ON_STACK(accept_cb, fd, EPOLLIN,
					 echo_cfg.accept_s * 1000000);
	if ((rc = event_add(dsp, &srv_event) < 0))
		return rc;

	/* prevent __cleanup__ from closing, do this in cleanup() cb */
	fd = -1;

	set_wait_mask(&mask);

	rc = event_loop(dsp, &mask, handle_intr);
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
	msg(LOG_ERR,
	    "Usage: %s [options]\n"
	    "Options:\n"
	    "\t[--num-clients|-n] $NUM		set number of clients\n"
	    "\t[--runtime|-t] $SECONDS		set run time\n"
	    "\t[--max-wait|-w] $MILLISECONDS	max time for clients to wait between requests\n"
	    "\t|-q|--quiet]			suppress log messages\n"
	    "\t[-v|--verbose]			verbose messages\n"
	    "\t[-d|--debug]			debug messages\n"
	    "\t[-h|--help]			print this help\n",
	    prog);
}

static int parse_opts(int argc, char * const argv[])
{
	static const char opts[] = "n:t:w:qvdh";
	static const struct option longopts[] = {
		{ "num-clients", true, NULL, 'n', },
		{ "runtime", true, NULL, 't', },
		{ "max-wait", true, NULL, 'w', },
		{ "quiet", false, NULL, 'q'},
		{ "verbose", false, NULL, 'v'},
		{ "debug", false, NULL, 'd'},
		{ "help", false, NULL, 'h'},
	};
	int opt;

	while ((opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (opt) {
		case 'n':
			read_int(optarg, "--num-clients", &echo_cfg.n_clients);
			break;
		case 't':
			read_int(optarg, "--runtime", &echo_cfg.accept_s);
			break;
		case 'w':
			read_int(optarg, "--max-wait", &echo_cfg.wait);
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

	if (optind < argc) {
		usage(argv[0]);
		return -EINVAL;
	}
	if (echo_cfg.n_clients <= 0) {
		msg(LOG_ERR, "number of clients must be positive\n");
		return -EINVAL;
	}
	if (echo_cfg.wait < 0) {
		msg(LOG_ERR, "wait time must be non-negative\n");
		return -EINVAL;
	}
	if (echo_cfg.accept_s <= 0) {
		msg(LOG_ERR, "runtime must be positive\n");
		return -EINVAL;
	} else
		accept_tmo.tv_sec = echo_cfg.accept_s;

	return 0;
}

int main(int argc, char * const argv[])
{
	struct timespec start, stop;
	log_timestamp = true;
	log_pid = true;
	if (parse_opts(argc, argv) < 0)
		return 1;

	if (init_signals() != 0) {
                msg(LOG_ERR, "failed to set up signals: %m\n");
                return 1;
        }

	clock_gettime(CLOCK_REALTIME, &start);
	if (server() < 0)
		return 1;
	clock_gettime(CLOCK_REALTIME, &stop);
	ts_subtract(&stop, &start);

	msg(LOG_NOTICE, "#clients: %d, runtime: %ld.%06ld\n",
	    echo_cfg.n_clients, (long)stop.tv_sec, stop.tv_nsec/1000);

	return 0;
}
