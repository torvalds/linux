TEST_PROGS := switch_endian_test

ASFLAGS += -O2 -Wall -g -nostdlib -m64

all: $(TEST_PROGS)

switch_endian_test: check-reversed.S

check-reversed.o: check.o
	$(CROSS_COMPILE)objcopy -j .text --reverse-bytes=4 -O binary $< $@

check-reversed.S: check-reversed.o
	hexdump -v -e '/1 ".byte 0x%02X\n"' $< > $@

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o check-reversed.S
