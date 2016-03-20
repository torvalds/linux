TEST_PROGS := ipc_unmuxed

CFLAGS += -I../../../../../usr/include

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
