CC = $(CROSS_COMPILE)gcc
BUILD_FLAGS = -DKTEST
CFLAGS += -O3 -Wl,-no-as-needed -Wall $(BUILD_FLAGS)
LDFLAGS += -lrt -lpthread
bins = posix_timers nanosleep inconsistency-check nsleep-lat raw_skew \
	set-timer-lat threadtest mqueue-lat valid-adjtimex \
	alarmtimer-suspend change_skew skew_consistency

all: ${bins}

# these are all "safe" tests that don't modify
# system time or require escalated privledges
run_tests: all
	./posix_timers
	./nanosleep
	./nsleep-lat
	./set-timer-lat
	./mqueue-lat
	./inconsistency-check
	./raw_skew
	./threadtest -t 30 -n 8

# these tests require escalated privledges
# and may modify the system time or trigger
# other behavior like suspend
run_destructive_tests: run_tests
	./alarmtimer-suspend
	./valid-adjtimex
	./change_skew
	./skew_consistency

clean:
	rm -f ${bins}
