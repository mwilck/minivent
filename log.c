/*
 * Copyright (C) 2021 SUSE LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This file implements basic logging functionality.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include "log.h"
#include "cleanup.h"

#ifndef LOG_CLOCK
#define LOG_CLOCK CLOCK_MONOTONIC
#endif

int log_level = DEFAULT_LOGLEVEL;
bool log_timestamp;
bool log_pid;

int __attribute__((format(printf, 3, 4)))
__msg(int lvl, const char *func, const char *format, ...)
{
	va_list ap;
	int ret;
	char pidbuf[16];
	char timebuf[32];
	static const char *const formats[] = {
		"%s%s%s",
		"%s%s%s: ",
		"%s<%s>%s ",
		"%s<%s> %s: ",
		"[%s] %s%s ",
		"[%s]%s %s: ",
		"[%s] <%s>%s ",
		"[%s] <%s> %s: ",
	};
	char *header __cleanup__(cleanup_charp) = NULL;
	char *message __cleanup__(cleanup_charp) = NULL;
	int idx;

	if (lvl > log_level)
		return 0;

	if (log_timestamp) {
		struct timespec now;

		clock_gettime(LOG_CLOCK, &now);
		snprintf(timebuf, sizeof(timebuf), "%6ld.%06ld",
			 (long)now.tv_sec, now.tv_nsec / 1000);
	} else
		*timebuf = '\0';

	if (log_pid)
		snprintf(pidbuf, sizeof(pidbuf), "%ld", (long)getpid());
	else
		*pidbuf = '\0';

	idx = ((log_timestamp ? 1 : 0) << 2) |
		((log_pid ? 1 : 0) << 1) | (func ? 1 : 0);

	if (asprintf(&header, formats[idx], timebuf, pidbuf, func ? func : "")
	    == -1)
		header = NULL;

	va_start(ap, format);
	if (vasprintf(&message, format, ap) == -1)
		message = NULL;
	va_end(ap);

	ret = fprintf(stderr, "%s%s", header ? header : "<error>",
		      message ? message : "<error>");

	return ret;
}
