all: get_size

get_size: get_size.c
	$(CC) -static -ffreestanding -nostartfiles -s $< -o $@

TEST_PROGS := get_size

include ../lib.mk

clean:
	$(RM) get_size
