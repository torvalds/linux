# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I../../../../../usr/include/
TEST_GEN_PROGS := sve-ptrace sve-probe-vls vec-syscfg za-fork za-ptrace
TEST_GEN_PROGS_EXTENDED := fp-pidbench fpsimd-test \
	rdvl-sme rdvl-sve \
	sve-test \
	ssve-test \
	za-test \
	vlset
TEST_PROGS_EXTENDED := fpsimd-stress sve-stress ssve-stress za-stress

# Build with nolibc to avoid effects due to libc's clone() support
fp-pidbench: fp-pidbench.S asm-utils.o
	$(CC) -nostdlib $^ -o $@
fpsimd-test: fpsimd-test.o asm-utils.o
	$(CC) -nostdlib $^ -o $@
rdvl-sme: rdvl-sme.o rdvl.o
rdvl-sve: rdvl-sve.o rdvl.o
sve-ptrace: sve-ptrace.o
sve-probe-vls: sve-probe-vls.o rdvl.o
sve-test: sve-test.o asm-utils.o
	$(CC) -nostdlib $^ -o $@
ssve-test: sve-test.S asm-utils.o
	$(CC) -DSSVE -nostdlib $^ -o $@
vec-syscfg: vec-syscfg.o rdvl.o
vlset: vlset.o
za-fork: za-fork.o za-fork-asm.o
	$(CC) -nostdlib -static $^ -o $@ -lgcc
za-fork.o: za-fork.c
	$(CC) -c -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
		-include ../../../../include/nolibc/nolibc.h \
		-ffreestanding -Wall $^ -o $@
za-test: za-test.o asm-utils.o
	$(CC) -nostdlib $^ -o $@
za-ptrace: za-ptrace.o

include ../../lib.mk
