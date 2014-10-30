# Makefile for net selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -g

CFLAGS += -I../../../../usr/include/

NET_PROGS = socket psock_fanout psock_tpacket

all: $(NET_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	@/bin/sh ./run_netsocktests || echo "sockettests: [FAIL]"
	@/bin/sh ./run_afpackettests || echo "afpackettests: [FAIL]"
	./test_bpf.sh
clean:
	$(RM) $(NET_PROGS)
