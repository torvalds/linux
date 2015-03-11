# Makefile for net selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -g

CFLAGS += -I../../../../usr/include/

NET_PROGS = socket psock_fanout psock_tpacket

all: $(NET_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh

include ../lib.mk

clean:
	$(RM) $(NET_PROGS)
