all:

include ../lib.mk

.PHONY: all clean

TARGETS := validate_cap test_execve
TEST_PROGS := test_execve

CFLAGS := -O2 -g -std=gnu99 -Wall -lcap-ng

all: $(TARGETS)

clean:
	$(RM) $(TARGETS)

$(TARGETS): %: %.c
	$(CC) -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl
