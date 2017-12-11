# SPDX-License-Identifier: GPL-2.0
TEST_PROGS := ptrace-gpr ptrace-tm-gpr ptrace-tm-spd-gpr \
              ptrace-tar ptrace-tm-tar ptrace-tm-spd-tar ptrace-vsx ptrace-tm-vsx \
              ptrace-tm-spd-vsx ptrace-tm-spr

include ../../lib.mk

all: $(TEST_PROGS)

CFLAGS += -m64 -I../../../../../usr/include -I../tm -mhtm -fno-pie

$(TEST_PROGS): ../harness.c ../utils.c ../lib/reg.S ptrace.h

clean:
	rm -f $(TEST_PROGS) *.o
