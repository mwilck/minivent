/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _UTIL_H
#define _UTIL_H

#define STEAL_PTR(p) ({ typeof(p) __tmp = (p); (p) = NULL; __tmp; })

#endif
