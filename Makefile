# Override this with a list of directories to search for includes
INCLUDE ?= -Iexternal
export INCLUDE
ifeq ($(COV),1)
OPTFLAGS := -O0
COV_CFLAGS += -fprofile-arcs -ftest-coverage -fPIC
LDFLAGS += -fprofile-arcs
else
OPTFLAGS := -O2
endif
WARNFLAGS := -Wall -Wextra -Werror
COMMON_CFLAGS += $(OPTFLAGS)
COMMON_CFLAGS += "-DLOG_CLOCK=CLOCK_REALTIME" -DLOG_FUNCNAME=1 -g -std=gnu99 $(WARNFLAGS) -MMD -MP
export COMMON_CFLAGS
export LDFLAGS
CFLAGS += $(INCLUDE) $(COMMON_CFLAGS) $(COV_CFLAGS) -fPIC

LIBEV_OBJS := event.o timeout.o ts-util.o tv-util.o
LIB := libminivent.so
STATIC := libminivent.a
OBJS = $(LIBEV_OBJS)

.PHONY:	test

$(LIB):	$(LIBEV_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^

static:	$(STATIC)

$(STATIC):	$(LIBEV_OBJS)
	$(AR) r $@ $^

ts-util.c:	time-inc.c time-util.c
	cat time-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(CPP) -P -DGEN_TS=1 time-util.c | indent -linux >>$@

tv-util.c:	time-inc.c time-util.c
	cat time-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(CPP) -P -DGEN_TV=1 time-util.c | indent -linux >>$@

test:	$(LIB)
	$(MAKE) -C $@

run-test: test
	$(MAKE) -C test run

clean:
	$(MAKE) -C test clean
	$(RM) *.o *~ $(LIB) $(STATIC) *.d *.gcno *.gcda *.gcov

include $(wildcard $(OBJS:.o=.d))
