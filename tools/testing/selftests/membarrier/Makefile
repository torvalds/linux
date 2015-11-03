CFLAGS += -g -I../../../../usr/include/

all:
	$(CC) $(CFLAGS) membarrier_test.c -o membarrier_test

TEST_PROGS := membarrier_test

include ../lib.mk

clean:
	$(RM) membarrier_test
