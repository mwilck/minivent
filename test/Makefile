CFLAGS += -I.. $(COMMON_CFLAGS)

LIB := ../libev.a
EVENT-TEST_OBJS := event-test.o
AVAHI-TEST_OBJS := avahi.o avahi-test.o
MINI-TEST-OBJS := mini-test.o
TS-TEST_OBJS := ts-test.o
TV-TEST_OBJS := tv-test.o
ECHO-TEST-OBJS := echo-test.o 
OBJS = $(EVENT-TEST_OBJS) $(AVAHI-TEST_OBS) $(TS-TEST_OBJS) $(TV-TEST_OBJS) \
	$(ECHO-TEST-OBJS) $(MINI-TEST_OBJS)

all:	event-test ts-test tv-test avahi-test echo-test array-mock mini-test

event-test:     $(EVENT-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

ts-test:    $(TS-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

tv-test:    $(TV-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

mini-test:    $(MINI-TEST-OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

avahi.o avahi-test.o:	CFLAGS += $(shell pkg-config --cflags avahi-core) -MMD -MP

avahi-test:	$(AVAHI-TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs avahi-core)

echo-test:	$(ECHO-TEST-OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

ts-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(CPP) -P -DGEN_TS=1 time-test.c | indent -linux >>$@

tv-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(CPP) -P -DGEN_TV=1 time-test.c | indent -linux >>$@

clean:
	$(RM) *.o *~ *.d *-mock *-test

include $(wildcard $(OBJS:.o=.d))

%.o.wrap:	%.c
	@sed -n 's/^.*__wrap_\([a-zA-Z0-9_]*\).*$$/-Wl,--wrap=\1/p' $< | \
		sort -u | tr '\n' ' ' >$@

# COLON will get expanded during second expansion below
COLON:=:
.SECONDEXPANSION:
%-mock:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) $(LIB) Makefile
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) \
		$(LIB) -lcmocka $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
