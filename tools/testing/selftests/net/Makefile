# Makefile for net selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

CFLAGS += -I../../../../usr/include/

NET_PROGS = socket psock_fanout

all: $(NET_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	@/bin/sh ./run_netsocktests || echo "sockettests: [FAIL]"
	@/bin/sh ./run_afpackettests || echo "afpackettests: [FAIL]"

clean:
	$(RM) $(NET_PROGS)
