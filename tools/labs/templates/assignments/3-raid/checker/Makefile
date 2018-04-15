CFLAGS = -Wall -Wextra -g -m32
LDFLAGS = -static -m32

.PHONY: all clean

all:

test:
	make -C _test/
	ln -sf _test/run-test run-test

clean:
	-make -C _test/ clean
	rm -rf run-test
