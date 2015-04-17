CC := $(CROSS_COMPILE)gcc
PROGS := switch_endian_test

ASFLAGS += -O2 -Wall -g -nostdlib -m64

all: $(PROGS)

switch_endian_test: check-reversed.S

check-reversed.o: check.o
	$(CROSS_COMPILE)objcopy -j .text --reverse-bytes=4 -O binary $< $@

check-reversed.S: check-reversed.o
	hexdump -v -e '/1 ".byte 0x%02X\n"' $< > $@

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) *.o check-reversed.S

.PHONY: all run_tests clean
