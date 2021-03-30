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
ALL_TESTS := event-test ts-test $(if $(DISABLE_TV),,tv-test) avahi-test \
	echo-test dgram-test mini-test
ALL_MOCKS := array-mock

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifndef V
	QUIET_CC	= @echo '   ' CC $@;
	QUIET_RUN	= @echo '   ' RUN $<;
endif
endif

%.o:	%.c
	$(QUIET_CC) $(CC) $(CFLAGS) -c -o $@ $<

%.out:	%-test
	$(QUIET_RUN) LD_LIBRARY_PATH=.. ./$< >$@ 2>&1

%.out:	%-mock
	$(QUIET_RUN) LD_LIBRARY_PATH=.. ./$< >$@ 2>&1

all:	$(ALL_TESTS) $(ALL_MOCKS)

run:	$(ALL_TESTS:%-test=%.out) $(ALL_MOCKS:%-mock=%.out)

event-test:     $(EVENT-TEST_OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS) -lm

ts-test:    $(TS-TEST_OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

tv-test:    $(TV-TEST_OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

mini-test:    $(MINI-TEST-OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

avahi.o avahi-test.o:	CFLAGS += $(shell pkg-config --cflags avahi-core) -MMD -MP

avahi-test:	$(AVAHI-TEST_OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs avahi-core) $(LIBS)

echo-test:	$(ECHO-TEST-OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

dgram-test:	$(DGRAM-TEST-OBJS)
	$(QUIET_CC) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

ts-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "ts-util.h"' >>$@
	$(QUIET_CC) $(CPP) -P -DGEN_TS=1 time-test.c | indent -linux >>$@

tv-test.c:	time-test-inc.c time-test.c
	cat time-test-inc.c >$@
	echo '#include "tv-util.h"' >>$@
	$(QUIET_CC) $(CPP) -P -DGEN_TV=1 time-test.c | indent -linux >>$@

clean:
	$(RM) *.o *~ *.d *-mock *-test *.out

include $(wildcard $(OBJS:.o=.d))

%.o.wrap:	%.c
	@sed -n 's/^.*__wrap_\([a-zA-Z0-9_]*\).*$$/-Wl,--wrap=\1/p' $< | \
		sort -u | tr '\n' ' ' >$@

# COLON will get expanded during second expansion below
COLON:=:
.SECONDEXPANSION:
%-mock:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) Makefile
	$(QUIET_CC) $(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) $(EXT_OBJS) \
		$(LIBS) -lcmocka $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
