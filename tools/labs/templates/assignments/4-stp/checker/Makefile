objects = _test/stp_test.o

.PHONY: all clean  _test_subdir_all _test_subdir_clean

all: stp_test

stp_test: _test_subdir_all $(objects)
	$(CC) -Wall -g -m32 -static $(objects) -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -o $@

_test_subdir_all:
	make -C _test

clean: _test_subdir_clean
	-rm -f stp_test *~

_test_subdir_clean:
	make -C _test clean
