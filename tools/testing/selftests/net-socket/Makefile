# Makefile for net-socket selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

NET_SOCK_PROGS = socket

all: $(NET_SOCK_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	@/bin/sh ./run_netsocktests || echo "vmtests: [FAIL]"

clean:
	$(RM) $(NET_SOCK_PROGS)
