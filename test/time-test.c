/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: GPL-2.1-or-newer
 */
#define NTV 1000
#define NR 1000

#define T_RANDOM(TT, TYPE, MEMB)                \
void TT##_random(TYPE *tv)                      \
{                                               \
        tv->tv_sec = random() %  2000 - 1000;   \
        tv->MEMB = random() % LONG_MAX / 2;     \
}

#define T_PRINT(TT, TYPE, MEMB, FACT)                                \
void __attribute__((unused))                                         \
TT##_print(const TYPE *tv)                                           \
{                                                                    \
        fprintf(stderr, "%ld.%06ld ",                                \
                (long)tv->tv_sec, (long)tv->MEMB / (FACT / 1000000L));	\
}

/*
 * Array initialized with random values
 * Duplicate values are highly unlikely
 */
#define INIT_NO_EQUALS				\
	do {					\
		tq[i] = tv[i];			\
	} while (0)

/*
 * Array initialized with values drawn randomly from another array
 * Duplicate values are highly likely
 */
#define INIT_WITH_EQUALS			\
	do {					\
		int j = random() % NTV;		\
                tq[i] = tv[j];			\
	} while (0)

#define __T_TEST(TT, TYPE, NAME, INIT)					\
int TT##_##NAME(void)                                                   \
{                                                                       \
        int i, errors = 0;						\
        TYPE tv[NTV], tq[NTV];                                          \
        TYPE *ptv[NTV], *qtv[NTV];					\
        size_t ntv = 0;                                                 \
                                                                        \
        for (i = 0; i < NTV; i++)  {					\
                TT##_random(&tv[i]);                                    \
	}								\
        for (i = 0; i < NTV; i++) {                                     \
		INIT;							\
	}								\
        for (i = 0; i < NTV; i++) {                                     \
		tv[i] = tq[i];						\
		qtv[i] = &tq[i];					\
                TT##_insert(ptv, &ntv, NTV, &tv[i]);			\
        }                                                               \
                                                                        \
        for(i = 0; i < NTV; i++) {					\
                TT##_normalize(qtv[i]);                                 \
	}								\
                                                                        \
        TT##_sort(qtv, NTV);                                            \
                                                                        \
        for (i = 0; i < NTV; i++) {                                     \
		if (i > 0 && TT##_compare(ptv[i-1], ptv[i]) > 0)	\
			errors++;					\
        }                                                               \
									\
        for (i = 0; i < NTV; i++) {                                     \
		if (TT##_compare(ptv[i], qtv[i]) != 0)			\
			errors++;					\
        }                                                               \
									\
	return errors;							\
}

#define T_TEST(TT, TYPE) __T_TEST(TT, TYPE, test, INIT_NO_EQUALS)
#define T_TEST1(TT, TYPE) __T_TEST(TT, TYPE, test1, INIT_WITH_EQUALS)

#define TEST_FUNCTIONS(TT, TYPE, MEMB, FACT)        \
        static T_RANDOM(TT, TYPE, MEMB);            \
        static T_PRINT(TT, TYPE, MEMB, FACT);       \
        static T_TEST(TT, TYPE);		    \
        static T_TEST1(TT, TYPE);

#if GEN_TV == 1
TEST_FUNCTIONS(tv, struct timeval, tv_usec, 1000000L);
#endif
#if GEN_TS == 1
TEST_FUNCTIONS(ts, struct timespec, tv_nsec, 1000000000L);
#endif

int main(void)
{
        int i, n_err = 0;

#if GEN_TV == 1
        for (i = 0; i < NR; i++)
                n_err += tv_test();
        for (i = 0; i < NR; i++)
                n_err += tv_test1();
#endif
#if GEN_TS == 1
        for (i = 0; i < NR; i++)
                n_err += ts_test();
        for (i = 0; i < NR; i++)
                n_err += ts_test1();
#endif
	fprintf(stderr, "TESTS FINISHED, %d errors (#items: %d, #runs: %d)\n",
		n_err, NTV, NR);

        return n_err ? 1 : 0;
}
