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

#include <stdio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>
#include "log.h"

#define TIME_FMT "[%ld.%06ld]"
#ifdef LOG_FUNCNAME
#define PRINT_HDR_W_TIME(ts, func) \
	fprintf(stderr, TIME_FMT " %s: ", ts.tv_sec, ts.tv_nsec / 1000, func)
#define PRINT_HDR(func) \
	fprintf(stderr, "%s: ", func)
#else
#define PRINT_HDR_W_TIME(ts, func) \
	fprintf(stderr, TIME_FMT ": ", ts.tv_sec, ts.tv_nsec / 1000)
#define PRINT_HDR(func) /* nothing */
#endif

int log_level = DEFAULT_LOGLEVEL;
bool log_timestamp;

int __attribute__((format(printf, 3, 4)))
__msg(int lvl, const char *func __attribute__((unused)), const char *format, ...)
{
	va_list ap;
	int ret;

	if (lvl > log_level)
		return 0;

	if (log_timestamp) {
		struct timespec ts;

		clock_gettime(LOG_CLOCK, &ts);
		PRINT_HDR_W_TIME(ts, func);
	} else
		PRINT_HDR(func);

	va_start(ap, format);
	ret = vfprintf(stderr, format, ap);
	va_end(ap);

	return ret;
}
