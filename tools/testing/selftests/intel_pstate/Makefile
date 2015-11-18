CC := $(CROSS_COMPILE)gcc
CFLAGS := $(CFLAGS) -Wall -D_GNU_SOURCE
LDFLAGS := $(LDFLAGS) -lm

TARGETS := msr aperf

TEST_PROGS := $(TARGETS) run.sh

.PHONY: all clean
all: $(TARGETS)

$(TARGETS): $(HEADERS)

clean:
	rm -f $(TARGETS)
