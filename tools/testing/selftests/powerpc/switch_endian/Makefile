TEST_GEN_PROGS := switch_endian_test

ASFLAGS += -O2 -Wall -g -nostdlib -m64

include ../../lib.mk

switch_endian_test: check-reversed.S

check-reversed.o: check.o
	$(CROSS_COMPILE)objcopy -j .text --reverse-bytes=4 -O binary $< $@

check-reversed.S: check-reversed.o
	hexdump -v -e '/1 ".byte 0x%02X\n"' $< > $@

clean:
	$(RM) $(TEST_GEN_PROGS) *.o check-reversed.S
