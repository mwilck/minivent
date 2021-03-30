/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _COMMON_H
#define _COMMON_H

/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of_const(ptr, type, member) ({	\
	typeof( ((const type *)0)->member ) *__mptr = (ptr);	\
	(const type *)( (const char *)__mptr - offsetof(type,member) );})
#define container_of(ptr, type, member) ({		\
	typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define STEAL_PTR(p) ({ typeof(p) __tmp = (p); (p) = NULL; __tmp; })

#endif
