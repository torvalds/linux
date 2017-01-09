# Makefile for net selftests

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../usr/include/

NET_PROGS =  socket
NET_PROGS += psock_fanout psock_tpacket
NET_PROGS += reuseport_bpf reuseport_bpf_cpu reuseport_bpf_numa
NET_PROGS += reuseport_dualstack

all: $(NET_PROGS)
reuseport_bpf_numa: LDFLAGS += -lnuma
%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh
TEST_FILES := $(NET_PROGS)

include ../lib.mk

clean:
	$(RM) $(NET_PROGS)
