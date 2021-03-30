# Override this with a list of directories to search for includes
INCLUDE ?= -Iexternal
export INCLUDE
# Set this to a non-empty string to disable struct timeval support
DISABLE_TV ?=
export DISABLE_TV
# Set this to override library defaults
DEFINES ?= "-DLOG_CLOCK=CLOCK_REALTIME" -DLOG_FUNCNAME=1

# Set OPTFLAGS to override optimization parameters
ifeq ($(COV),1)
OPTFLAGS ?= -O0 -g
COV_CFLAGS += -fprofile-arcs -ftest-coverage -fPIC
LDFLAGS += -fprofile-arcs
else
OPTFLAGS ?= -O2 -g
endif
WARNFLAGS := -Wall -Wextra -Werror
COMMON_CFLAGS += $(OPTFLAGS) $(DEFINES) -std=gnu99 $(WARNFLAGS) -MMD -MP
export COMMON_CFLAGS
export LDFLAGS
CFLAGS += $(INCLUDE) $(COMMON_CFLAGS) $(COV_CFLAGS) -fPIC

LIBEV_OBJS := event.o timeout.o ts-util.o $(if $(DISABLE_TV),,tv-util.o)
LIB := libminivent.so
STATIC := libminivent.a
OBJS = $(LIBEV_OBJS)

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET_CC	= @echo '   ' CC $@;
	QUIET_AR	= @echo '   ' AR $@;
endif
endif

.PHONY:	test
.c.o:
	$(QUIET_CC) $(CC) $(CFLAGS) -c -o $@ $<

$(LIB):	$(LIBEV_OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -shared -o $@ $^

static:	$(STATIC)

$(STATIC):	$(LIBEV_OBJS)
	$(QUIET_AR) $(AR) cr $@ $^

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
