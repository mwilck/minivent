#ifndef _UTIL_H
#define _UTIL_H

#define STEAL_PTR(p) ({ typeof(p) __tmp = (p); (p) = NULL; __tmp; })

#endif
