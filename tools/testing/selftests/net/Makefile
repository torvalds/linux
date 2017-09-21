# Makefile for net selftests

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../usr/include/

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh netdevice.sh
TEST_GEN_FILES =  socket
TEST_GEN_FILES += psock_fanout psock_tpacket
TEST_GEN_FILES += reuseport_bpf reuseport_bpf_cpu reuseport_bpf_numa
TEST_GEN_FILES += reuseport_dualstack

include ../lib.mk

$(OUTPUT)/reuseport_bpf_numa: LDFLAGS += -lnuma
