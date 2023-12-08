# SPDX-License-Identifier: GPL-2.0

# A proper top_srcdir is needed by KSFT(lib.mk)
top_srcdir = $(realpath ../../../../../)

CFLAGS += $(KHDR_INCLUDES)

TEST_GEN_PROGS := fp-stress \
	sve-ptrace sve-probe-vls \
	vec-syscfg \
	za-fork za-ptrace
TEST_GEN_PROGS_EXTENDED := fp-pidbench fpsimd-test \
	rdvl-sme rdvl-sve \
	sve-test \
	ssve-test \
	za-test \
	vlset
TEST_PROGS_EXTENDED := fpsimd-stress sve-stress ssve-stress za-stress

EXTRA_CLEAN += $(OUTPUT)/asm-utils.o $(OUTPUT)/rdvl.o $(OUTPUT)/za-fork-asm.o

# Build with nolibc to avoid effects due to libc's clone() support
$(OUTPUT)/fp-pidbench: fp-pidbench.S $(OUTPUT)/asm-utils.o
	$(CC) -nostdlib $^ -o $@
$(OUTPUT)/fpsimd-test: fpsimd-test.S $(OUTPUT)/asm-utils.o
	$(CC) -nostdlib $^ -o $@
$(OUTPUT)/rdvl-sve: rdvl-sve.c $(OUTPUT)/rdvl.o
$(OUTPUT)/rdvl-sme: rdvl-sme.c $(OUTPUT)/rdvl.o
$(OUTPUT)/sve-ptrace: sve-ptrace.c
$(OUTPUT)/sve-probe-vls: sve-probe-vls.c $(OUTPUT)/rdvl.o
$(OUTPUT)/sve-test: sve-test.S $(OUTPUT)/asm-utils.o
	$(CC) -nostdlib $^ -o $@
$(OUTPUT)/ssve-test: sve-test.S $(OUTPUT)/asm-utils.o
	$(CC) -DSSVE -nostdlib $^ -o $@
$(OUTPUT)/vec-syscfg: vec-syscfg.c $(OUTPUT)/rdvl.o
$(OUTPUT)/vlset: vlset.c
$(OUTPUT)/za-fork: za-fork.c $(OUTPUT)/za-fork-asm.o
	$(CC) -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
		-include ../../../../include/nolibc/nolibc.h \
		-static -ffreestanding -Wall $^ -o $@
$(OUTPUT)/za-ptrace: za-ptrace.c
$(OUTPUT)/za-test: za-test.S $(OUTPUT)/asm-utils.o
	$(CC) -nostdlib $^ -o $@

include ../../lib.mk
