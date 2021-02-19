CFLAGS += "-DLOG_CLOCK=CLOCK_REALTIME" -DLOG_FUNCNAME=1 -O2 -g -Wall -Wextra $(shell pkg-config --cflags avahi-core) -MMD -MP
AV_LIBS := $(shell pkg-config --libs avahi-core)
LIBEV_OBJS := event.o timeout.o ts-util.o tv-util.o log.o
LIB := libev.a
EVENT-TEST_OBJS = main.o
AVAHI-TEST_OBJS = avahi.o avahi-test.o
TS-TEST_OBJS = ts-test.o
TV-TEST_OBJS = tv-test.o
OBJS = $(LIBEV_OBJS) $(EVENT-TEST_OBJS) $(AVAHI-TEST_OBS) $(TS-TEST_OBJS) $(TV-TEST_OBJS)

$(LIB):	$(LIBEV_OBJS)
	$(AR) r $@ $^

event-test:     $(EVENT-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

ts-test:    $(TS-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

tv-test:    $(TV-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

avahi-test:	$(AVAHI-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(AV_LIBS)

clean:
	$(RM) *.o event-test tu avahi-test ts-test tv-test ts-*.c tv-*.c *~ $(LIB)
	$(RM) -r *.d

include $(wildcard $(OBJS:.o=.d))

ts-util.c:	time-inc.c time-util.c
	cat time-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(CPP) -P -DGEN_TS=1 time-util.c | indent -linux >>$@

tv-util.c:	time-inc.c time-util.c
	cat time-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(CPP) -P -DGEN_TV=1 time-util.c | indent -linux >>$@

ts-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(CPP) -P -DGEN_TS=1 time-test.c | indent -linux >>$@

tv-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(CPP) -P -DGEN_TV=1 time-test.c | indent -linux >>$@

