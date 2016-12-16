CFLAGS += -O2 -g -std=gnu89 -pthread -Wall -Wextra
CFLAGS += -I../../../../usr/include/
LDFLAGS += -pthread

TEST_PROGS = sync_test

all: $(TEST_PROGS)

include ../lib.mk

OBJS = sync_test.o sync.o

TESTS += sync_alloc.o
TESTS += sync_fence.o
TESTS += sync_merge.o
TESTS += sync_wait.o
TESTS += sync_stress_parallelism.o
TESTS += sync_stress_consumer.o
TESTS += sync_stress_merge.o

sync_test: $(OBJS) $(TESTS)

clean:
	$(RM) sync_test $(OBJS) $(TESTS)
