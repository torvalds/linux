CC = $(CROSS_COMPILE)gcc
BUILD_FLAGS = -DKTEST
CFLAGS += -O3 -Wl,-no-as-needed -Wall $(BUILD_FLAGS)
LDFLAGS += -lrt -lpthread
bins = posix_timers

all: ${bins}

run_tests: all
	./posix_timers

clean:
	rm -f ${bins}
