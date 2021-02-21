/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _LOG_H
#define _LOG_H

#ifndef MAX_LOGLEVEL
#  define MAX_LOGLEVEL LOG_DEBUG
#endif
#ifndef DEFAULT_LOGLEVEL
#  define DEFAULT_LOGLEVEL LOG_NOTICE
#endif

extern int log_level;
extern bool log_timestamp;

int __attribute__((format(printf, 3, 4)))
__msg(int lvl, const char *func, const char *format, ...);

#define msg(lvl, format, ...)						\
	do {								\
		if ((lvl) <= MAX_LOGLEVEL)				\
			__msg(lvl, __func__, format, ##__VA_ARGS__);	\
	} while (0)

#endif /* _LOG_H */
