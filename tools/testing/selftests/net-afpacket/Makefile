# Makefile for net-socket selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

CFLAGS += -I../../../../usr/include/

AF_PACKET_PROGS = psock_fanout

all: $(AF_PACKET_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	@/bin/sh ./run_afpackettests || echo "afpackettests: [FAIL]"

clean:
	$(RM) $(AF_PACKET_PROGS)
