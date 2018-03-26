objects = _test/tracer_test.o

.PHONY: all clean

all: tracer_test

tracer_test:
	$(CC) -Wall -g -m32 -static -o $@ $(objects)


run: all
	./run_tests.bash

clean:
	-rm -f tracer_test *~ tracer_helper.ko
