# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := ptrace-gpr ptrace-tm-gpr ptrace-tm-spd-gpr \
              ptrace-tar ptrace-tm-tar ptrace-tm-spd-tar ptrace-vsx ptrace-tm-vsx \
              ptrace-tm-spd-vsx ptrace-tm-spr ptrace-hwbreak ptrace-pkey core-pkey \
              perf-hwbreak ptrace-syscall

top_srcdir = ../../../../..
include ../../lib.mk

CFLAGS += -m64 -I../../../../../usr/include -I../tm -mhtm -fno-pie

$(OUTPUT)/ptrace-pkey $(OUTPUT)/core-pkey: child.h
$(OUTPUT)/ptrace-pkey $(OUTPUT)/core-pkey: LDLIBS += -pthread

$(TEST_GEN_PROGS): ../harness.c ../utils.c ../lib/reg.S ptrace.h
