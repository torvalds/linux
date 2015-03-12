CC = $(CROSS_COMPILE)gcc
BUILD_FLAGS = -DKTEST
CFLAGS += -O3 -Wl,-no-as-needed -Wall $(BUILD_FLAGS)
LDFLAGS += -lrt -lpthread
bins = posix_timers nanosleep inconsistency-check nsleep-lat raw_skew \
	set-timer-lat

all: ${bins}

run_tests: all
	./posix_timers
	./nanosleep
	./nsleep-lat
	./set-timer-lat
	./inconsistency-check
	./raw_skew

clean:
	rm -f ${bins}
