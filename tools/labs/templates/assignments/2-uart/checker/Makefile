CFLAGS = -Wall -g -static -m32

.PHONY: all run clean

all: test solution.ko

test: _test/test.o
	$(CC) $(CFLAGS) -o $@ $^

solution.ko: _test/solution.ko
	ln -s $< $@

clean:
	-rm -f *~ test _test/test.o solution.ko
