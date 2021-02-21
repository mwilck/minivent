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
                tv->tv_sec, tv->MEMB / (FACT / 1000000L));           \
}

#define T_TEST(TT, TYPE)                                                \
void TT##_test(void)                                                    \
{                                                                       \
        int i;                                                          \
                                                                        \
        TYPE tv[NTV], tq[NTV];                                          \
        TYPE *ptv[NTV], *qtv[NTV];					\
        size_t ntv = 0;                                                 \
                                                                        \
        for (i = 0; i < NTV; i++) {                                     \
                TT##_random(&tv[i]);                                    \
                tq[i] = tv[i];                                          \
                qtv[i] = &tq[i];                                        \
                TT##_insert(ptv, &ntv, NTV, &tv[i]);			\
        }                                                               \
                                                                        \
        for(i = 0; i < NTV; i++)                                        \
                TT##_normalize(qtv[i]);                                 \
                                                                        \
        TT##_sort(qtv, NTV);                                            \
                                                                        \
        /* fprintf(stderr, "\nInsert:     ");	*/			\
        for (i = 0; i < NTV; i++) {                                     \
                /* TT##_print(ptv[i]); */				\
		if (i > 0)						\
			assert(TT##_compare(ptv[i-1], ptv[i]) <= 0);	\
        }                                                               \
									\
        /* fprintf(stderr, "OK\nQsort:     ");	*/			\
        for (i = 0; i < NTV; i++) {                                     \
                /* TT##_print(qtv[i]);	*/				\
		assert(TT##_compare(ptv[i], qtv[i]) == 0);		\
        }                                                               \
        /* fprintf(stderr, "OK\n");	*/				\
}

#define T_TEST1(TT, TYPE)                                               \
void TT##_test1(void)                                                   \
{                                                                       \
        int i;                                                          \
                                                                        \
        TYPE tv[NTV], tq[NTV];                                          \
        TYPE *ptv[NTV], *qtv[NTV];					\
        size_t ntv = 0;                                                 \
                                                                        \
        for (i = 0; i < NTV; i++)  {					\
                TT##_random(&tv[i]);                                    \
	}								\
        for (i = 0; i < NTV; i++) {                                     \
		int j = random() % NTV;					\
                tq[i] = tv[j];                                          \
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
        /* fprintf(stderr, "\nInsert:     ");	*/			\
        for (i = 0; i < NTV; i++) {                                     \
                /* TT##_print(ptv[i]); */				\
		if (i > 0)						\
			assert(TT##_compare(ptv[i-1], ptv[i]) <= 0);	\
        }                                                               \
        /* fprintf(stderr, "OK\nQsort:     ");	*/			\
        for (i = 0; i < NTV; i++) {                                     \
                /* TT##_print(qtv[i]);	*/				\
		assert(TT##_compare(ptv[i], qtv[i]) == 0);		\
        }                                                               \
        /* fprintf(stderr, "OK\n");	*/				\
}

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
        int i;

#if GEN_TV == 1
        for (i = 0; i < NR; i++)
                tv_test();
        for (i = 0; i < NR; i++)
                tv_test1();
#endif
#if GEN_TS == 1
        for (i = 0; i < NR; i++)
                ts_test();
        for (i = 0; i < NR; i++)
                ts_test1();
#endif
	fprintf(stderr, "TESTS SUCCESSFUL (#items: %d, #runs: %d)\n", NTV, NR);
        return 0;
}
