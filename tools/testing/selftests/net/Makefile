# Makefile for net selftests

CFLAGS = -Wall -O2 -g

CFLAGS += -I../../../../usr/include/

NET_PROGS = socket psock_fanout psock_tpacket reuseport_bpf reuseport_bpf_cpu

all: $(NET_PROGS)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh
TEST_FILES := $(NET_PROGS)

include ../lib.mk

clean:
	$(RM) $(NET_PROGS)
