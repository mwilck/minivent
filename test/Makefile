LIB := ../libev.a
EVENT-TEST_OBJS = event-test.o
AVAHI-TEST_OBJS = avahi.o avahi-test.o
TS-TEST_OBJS = ts-test.o
TV-TEST_OBJS = tv-test.o
OBJS = $(EVENT-TEST_OBJS) $(AVAHI-TEST_OBS) $(TS-TEST_OBJS) $(TV-TEST_OBJS)
CFLAGS += -I..

all:	event-test ts-test tv-test avahi-test

event-test:     $(EVENT-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

ts-test:    $(TS-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

tv-test:    $(TV-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

avahi.o avahi-test.o:	CFLAGS += $(shell pkg-config --cflags avahi-core) -MMD -MP

avahi-test:	$(AVAHI-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs avahi-core)

ts-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(CPP) -P -DGEN_TS=1 time-test.c | indent -linux >>$@

tv-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(CPP) -P -DGEN_TV=1 time-test.c | indent -linux >>$@

clean:
	$(RM) *.o event-test tu avahi-test ts-test tv-test ts-*.c tv-*.c *~ *.d

include $(wildcard $(OBJS:.o=.d))
