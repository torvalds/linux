# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I../../../../../usr/include/
TEST_GEN_PROGS := sve-ptrace sve-probe-vls vec-syscfg za-ptrace
TEST_PROGS_EXTENDED := fp-pidbench fpsimd-test fpsimd-stress \
	rdvl-sme rdvl-sve \
	sve-test sve-stress \
	ssve-test ssve-stress \
	za-test za-stress \
	vlset

all: $(TEST_GEN_PROGS) $(TEST_PROGS_EXTENDED)

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
za-test: za-test.o asm-utils.o
	$(CC) -nostdlib $^ -o $@
za-ptrace: za-ptrace.o

include ../../lib.mk
