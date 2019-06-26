# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := switch_endian_test

ASFLAGS += -O2 -Wall -g -nostdlib -m64

EXTRA_CLEAN = $(OUTPUT)/*.o $(OUTPUT)/check-reversed.S

top_srcdir = ../../../../..
include ../../lib.mk

$(OUTPUT)/switch_endian_test: ASFLAGS += -I $(OUTPUT)
$(OUTPUT)/switch_endian_test: $(OUTPUT)/check-reversed.S

$(OUTPUT)/check-reversed.o: $(OUTPUT)/check.o
	$(CROSS_COMPILE)objcopy -j .text --reverse-bytes=4 -O binary $< $@

$(OUTPUT)/check-reversed.S: $(OUTPUT)/check-reversed.o
	hexdump -v -e '/1 ".byte 0x%02X\n"' $< > $@
