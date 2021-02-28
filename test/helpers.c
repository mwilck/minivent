/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.1-or-newer
 */

static sig_atomic_t must_exit;
static sig_atomic_t got_sigchld;

static void int_handler(int sig __attribute__((unused)))
{
	must_exit = 1;
}

static void chld_handler(int sig __attribute__((unused)))
{
	got_sigchld = 1;
}

static sigset_t orig_sigmask;

static int init_signals(void)
{
	sigset_t mask;
	struct sigaction sa = { .sa_handler = int_handler, };

	/*
	 * Block all signals. They will be unblocked when we wait
	 * for events.
	 */
	sigfillset(&mask);
	if (sigprocmask(SIG_BLOCK, &mask, &orig_sigmask) == -1)
		return -errno;
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		return -errno;
	if (sigaction(SIGINT, &sa, NULL) == -1)
		return -errno;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		return -errno;
	sa.sa_handler = chld_handler;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		return -errno;
	return 0;
}

static __attribute__((unused))
void set_wait_mask(sigset_t *mask)
{
	sigfillset(mask);
	sigdelset(mask, SIGTERM);
	sigdelset(mask, SIGINT);
	sigdelset(mask, SIGCHLD);
}

static __attribute__((unused))
void exit_main_loop(void)
{
        msg(LOG_INFO, "sending exit signal\n");
        if (kill(getpid(), SIGINT) == -1)
                msg(LOG_ERR, "kill: %m\n");
}
