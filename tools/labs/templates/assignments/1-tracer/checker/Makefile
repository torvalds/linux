LDFLAGS = -m32

.PHONY: all clean

all: tracer_test

tracer_test: tracer_test.o

tracer_test.o: _test/tracer_test.c
	make -C _test
	ln -sfn _test/tracer_test.o $@

run: all
	./_checker

clean:
	-rm -f *~
	-rm -f tracer_test.o tracer_test
	-make -C _test clean
