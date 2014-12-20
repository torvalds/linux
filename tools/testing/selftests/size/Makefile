CC = $(CROSS_COMPILE)gcc

all: get_size

get_size: get_size.c
	$(CC) -static -ffreestanding -nostartfiles -s $< -o $@

run_tests: all
	./get_size

clean:
	$(RM) get_size
