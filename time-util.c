/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#define T_NORMALIZE(TT, TYPE, MEMB, FACT)       \
void TT##_normalize(TYPE *tv)                   \
{                                               \
	long quot, rem;				\
                                                \
        if (tv->MEMB >= 0 && tv->MEMB < FACT)   \
                return;                         \
	quot = tv->MEMB / FACT;			\
	rem = tv->MEMB % FACT;			\
	if (rem < 0) {				\
                rem += FACT;			\
                quot--;				\
        }                                       \
        tv->tv_sec += quot;			\
        tv->MEMB = rem;				\
}

#define T_SUB(TT, TYPE, MEMB)			\
void TT##_subtract(TYPE *t1, const TYPE *t2)	\
{						\
	t1->tv_sec -= t2->tv_sec;		\
	t1->MEMB -= t2->MEMB;			\
	TT##_normalize(t1);			\
	return;					\
}

#define T_ADD(TT, TYPE, MEMB)			\
void TT##_add(TYPE *t1, const TYPE *t2)		\
{						\
	t1->tv_sec += t2->tv_sec;		\
	t1->MEMB += t2->MEMB;			\
	TT##_normalize(t1);			\
	return;					\
}

#define T_COMPARE(TT, TYPE, MEMB) \
int TT##_compare(const TYPE *t1, const TYPE *t2)        \
{                                                       \
                if (t1->tv_sec < t2->tv_sec)            \
                        return -1;                      \
                if (t1->tv_sec > t2->tv_sec)            \
                        return 1;                       \
                if (t1->MEMB < t2->MEMB)                \
                        return -1;                      \
                if (t1->MEMB > t2->MEMB)                \
                        return 1;                       \
                return 0;                               \
        }

#define T_COMPARE_Q(TT, TYPE)                                        \
int TT##_compare_q(const TYPE **pt1, const TYPE **pt2)               \
{                                                                    \
        return TT##_compare(*pt1, *pt2);                             \
}

#define T_SORT(TT, TYPE)                                               \
void TT##_sort(TYPE **tvs, size_t size)                                \
{                                                                      \
        qsort(tvs, size, sizeof(TYPE *),                               \
              (int (*)(const void *, const void *)) TT##_compare_q);   \
        return;							       \
}

#define T_SEARCH(TT, TYPE)                                              \
long TT##_search(TYPE * const *tvs, size_t size, TYPE *new)		\
{                                                                       \
        long low, high, mid;                                            \
                                                                        \
        if (!new || !tvs|| size > LONG_MAX)				\
                return -EINVAL;                                         \
                                                                        \
        TT##_normalize(new);                                            \
                                                                        \
        if (size == 0)							\
                return 0;                                               \
                                                                        \
        high = size - 1;						\
        if (TT##_compare(new, tvs[high]) > 0)				\
                return size;						\
									\
        low = 0;                                                        \
        while (high - low > 1) {                                        \
                mid = low + (high - low) / 2;                           \
                if (TT##_compare(new, tvs[mid]) <= 0)                   \
                        high = mid;                                     \
                else                                                    \
                        low = mid;                                      \
        }                                                               \
        if (high > low && TT##_compare(new, tvs[low]) > 0)              \
                return high;						\
	else								\
		return low;						\
}

#define T_INSERT(TT, TYPE)					\
long TT##_insert(TYPE **tvs, size_t *len, size_t size, TYPE *new)\
{								\
	long pos;						\
								\
	if (!len || size <= *len)				\
		return -EOVERFLOW;				\
								\
	pos = TT##_search(tvs, *len, new);			\
	if (pos < 0)						\
		return pos;					\
								\
	memmove(&tvs[pos + 1], &tvs[pos], (*len - pos) * sizeof(*tvs));	\
	(*len)++;						\
	tvs[pos] = new;						\
	return pos;						\
}

#define T_FUNCTIONS(TT, TYPE, MEMB, FACT)  \
        T_NORMALIZE(TT, TYPE, MEMB, FACT)  \
        T_ADD(TT, TYPE, MEMB)              \
        T_SUB(TT, TYPE, MEMB)              \
        T_COMPARE(TT, TYPE, MEMB)          \
        static T_COMPARE_Q(TT, TYPE)       \
        T_SEARCH(TT, TYPE)                 \
        T_INSERT(TT, TYPE)                 \
        T_SORT(TT, TYPE)                   \

#if GEN_TV == 1
T_FUNCTIONS(tv, struct timeval, tv_usec, 1000000L)
#endif
#if GEN_TS == 1
T_FUNCTIONS(ts, struct timespec, tv_nsec, 1000000000L)
#endif
