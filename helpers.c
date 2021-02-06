
static sig_atomic_t must_exit;

static void int_handler(int sig __attribute__((unused)))
{
	must_exit = 1;
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
	return 0;
}


static __attribute__((unused))
void exit_main_loop(void)
{
        log(LOG_NOTICE, "sending exit signal\n");
        if (kill(getpid(), SIGINT) == -1)
                log(LOG_ERR, "kill: %m\n");
}
