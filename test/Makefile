CFLAGS += -I.. $(patsubst -I%,-I../%,$(INCLUDE)) $(COMMON_CFLAGS)
# Override this to pull in logging funcionality from somewhere else
LOG_O ?= ../external/log.o
CLEANUP_O ?= ../external/cleanup.o
EXT_OBJS := $(LOG_O) $(CLEANUP_O)

LIBS := -L.. -lminivent

LIB := libev.so
EVENT-TEST_OBJS := event-test.o $(EXT_OBJS)
AVAHI-TEST_OBJS := avahi.o avahi-test.o $(EXT_OBJS)
MINI-TEST-OBJS := mini-test.o $(EXT_OBJS)
TS-TEST_OBJS := ts-test.o $(EXT_OBJS)
TV-TEST_OBJS := tv-test.o $(EXT_OBJS)
ECHO-TEST-OBJS := echo-test.o $(EXT_OBJS)
DGRAM-TEST-OBJS := dgram-test.o $(EXT_OBJS)
OBJS = $(EVENT-TEST_OBJS) $(AVAHI-TEST_OBS) $(TS-TEST_OBJS) $(TV-TEST_OBJS) \
	$(ECHO-TEST-OBJS) $(DGRAM-TEST-OBJS) $(MINI-TEST-OBJS)
ALL_TESTS := event-test ts-test tv-test avahi-test echo-test dgram-test array-mock mini-test

all:	$(ALL_TESTS)

run:	all
	@for i in $(ALL_TESTS); do \
		echo == running $$i == >&2; \
		LD_LIBRARY_PATH=.. ./$$i; \
	done

event-test:     $(EVENT-TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) -lm

ts-test:    $(TS-TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

tv-test:    $(TV-TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

mini-test:    $(MINI-TEST-OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

avahi.o avahi-test.o:	CFLAGS += $(shell pkg-config --cflags avahi-core) -MMD -MP

avahi-test:	$(AVAHI-TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs avahi-core) $(LIBS)

echo-test:	$(ECHO-TEST-OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

dgram-test:	$(DGRAM-TEST-OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

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
%-mock:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) Makefile
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) $(EXT_OBJS) \
		$(LIBS) -lcmocka $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
