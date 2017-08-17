CFLAGS += -O3 -Wl,-no-as-needed -Wall
LDFLAGS += -lrt -lpthread -lm

# these are all "safe" tests that don't modify
# system time or require escalated privileges
TEST_GEN_PROGS = posix_timers nanosleep nsleep-lat set-timer-lat mqueue-lat \
	     inconsistency-check raw_skew threadtest rtctest

TEST_GEN_PROGS_EXTENDED = alarmtimer-suspend valid-adjtimex adjtick change_skew \
		      skew_consistency clocksource-switch freq-step leap-a-day \
		      leapcrash set-tai set-2038 set-tz rtctest_setdate


include ../lib.mk

define RUN_DESTRUCTIVE_TESTS
	@for TEST in $(TEST_GEN_PROGS_EXTENDED); do \
		BASENAME_TEST=`basename $$TEST`;	\
		if [ ! -x $$BASENAME_TEST ]; then	\
			echo "selftests: Warning: file $$BASENAME_TEST is not executable, correct this.";\
			echo "selftests: $$BASENAME_TEST [FAIL]"; \
		else					\
			cd `dirname $$TEST`; (./$$BASENAME_TEST && echo "selftests: $$BASENAME_TEST [PASS]") || echo "selftests:  $$BASENAME_TEST [FAIL]"; cd -;\
		fi;					\
	done;
endef

# these tests require escalated privileges
# and may modify the system time or trigger
# other behavior like suspend
run_destructive_tests: run_tests
	$(RUN_DESTRUCTIVE_TESTS)
