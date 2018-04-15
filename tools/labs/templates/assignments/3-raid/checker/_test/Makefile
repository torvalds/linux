CFLAGS = -Wall -Wextra -Wno-unused-function -g -m32
LDFLAGS = -static -m32

.PHONY: all clean

all: run-test

run-test: run-test.o test.o

run-test.o: run-test.c run-test.h

test.o: test.c run-test.h

clean:
	-rm -f *~ test.o run-test.o run-test test
