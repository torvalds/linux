BUILD_FLAGS = -DKTEST
CFLAGS += -O3 -Wl,-no-as-needed -Wall $(BUILD_FLAGS)
LDFLAGS += -lrt -lpthread

# these are all "safe" tests that don't modify
# system time or require escalated privileges
TEST_GEN_PROGS = posix_timers nanosleep nsleep-lat set-timer-lat mqueue-lat \
	     inconsistency-check raw_skew threadtest rtctest

TEST_GEN_PROGS_EXTENDED = alarmtimer-suspend valid-adjtimex adjtick change_skew \
		      skew_consistency clocksource-switch leap-a-day \
		      leapcrash set-tai set-2038 set-tz


include ../lib.mk

# these tests require escalated privileges
# and may modify the system time or trigger
# other behavior like suspend
run_destructive_tests: run_tests
	./alarmtimer-suspend
	./valid-adjtimex
	./adjtick
	./change_skew
	./skew_consistency
	./clocksource-switch
	./leap-a-day -s -i 10
	./leapcrash
	./set-tz
	./set-tai
	./set-2038

