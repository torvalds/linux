# SPDX-License-Identifier: GPL-2.0
# Makefile for net selftests

CFLAGS =  -Wall -Wl,--no-as-needed -O2 -g
CFLAGS += -I../../../../usr/include/

TEST_PROGS := run_netsocktests run_afpackettests test_bpf.sh netdevice.sh rtnetlink.sh
TEST_PROGS += fib_tests.sh fib-onlink-tests.sh in_netns.sh pmtu.sh udpgso.sh
TEST_PROGS += udpgso_bench.sh
TEST_GEN_PROGS_EXTENDED := in_netns.sh
TEST_GEN_FILES =  socket
TEST_GEN_FILES += psock_fanout psock_tpacket msg_zerocopy
TEST_GEN_FILES += tcp_mmap tcp_inq
TEST_GEN_FILES += udpgso udpgso_bench_tx udpgso_bench_rx
TEST_GEN_PROGS = reuseport_bpf reuseport_bpf_cpu reuseport_bpf_numa
TEST_GEN_PROGS += reuseport_dualstack reuseaddr_conflict

include ../lib.mk

$(OUTPUT)/reuseport_bpf_numa: LDFLAGS += -lnuma
$(OUTPUT)/tcp_mmap: LDFLAGS += -lpthread
$(OUTPUT)/tcp_inq: LDFLAGS += -lpthread
