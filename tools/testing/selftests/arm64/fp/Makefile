# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I../../../../../usr/include/
TEST_GEN_PROGS := sve-ptrace sve-probe-vls
TEST_PROGS_EXTENDED := fpsimd-test fpsimd-stress sve-test sve-stress vlset

all: $(TEST_GEN_PROGS) $(TEST_PROGS_EXTENDED)

fpsimd-test: fpsimd-test.o
	$(CC) -nostdlib $^ -o $@
sve-ptrace: sve-ptrace.o sve-ptrace-asm.o
sve-probe-vls: sve-probe-vls.o
sve-test: sve-test.o
	$(CC) -nostdlib $^ -o $@
vlset: vlset.o

include ../../lib.mk
