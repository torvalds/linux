CFLAGS += -O3 -Wl,-no-as-needed -Wall
LDFLAGS += -lrt -lpthread -lm

# these are all "safe" tests that don't modify
# system time or require escalated privileges
TEST_GEN_PROGS = posix_timers nanosleep nsleep-lat set-timer-lat mqueue-lat \
	     inconsistency-check raw_skew threadtest rtctest

DESTRUCTIVE_TESTS = alarmtimer-suspend valid-adjtimex adjtick change_skew \
		      skew_consistency clocksource-switch freq-step leap-a-day \
		      leapcrash set-tai set-2038 set-tz

TEST_GEN_PROGS_EXTENDED = $(DESTRUCTIVE_TESTS) rtctest_setdate


include ../lib.mk

# these tests require escalated privileges
# and may modify the system time or trigger
# other behavior like suspend
run_destructive_tests: run_tests
	$(call RUN_TESTS, $(DESTRUCTIVE_TESTS))
