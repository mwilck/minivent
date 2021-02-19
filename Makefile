CFLAGS += "-DLOG_CLOCK=CLOCK_REALTIME" -DLOG_FUNCNAME=1 -O2 -g -Wall -Wextra -MMD -MP
export CFLAGS
LIBEV_OBJS := event.o timeout.o ts-util.o tv-util.o log.o
LIB := libev.a
OBJS = $(LIBEV_OBJS)

.PHONY:	test

$(LIB):	$(LIBEV_OBJS)
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

clean:
	$(MAKE) -C test clean
	$(RM) *.o *~ $(LIB) *.d
	$(RM) -r *.d

include $(wildcard $(OBJS:.o=.d))
