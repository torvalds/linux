CFLAGS = -Wall -g -static -m32

.PHONY: all run clean

all: test link_handler.ko

test: _test/test.o
	$(CC) $(CFLAGS) -o $@ $^

link_handler.ko: _test/link_handler.ko
	ln -s $< $@

clean:
	-rm -f *~ test _test/test.o link_handler.ko
