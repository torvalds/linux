# SPDX-License-Identifier: GPL-2.0
CFLAGS += -O2 -g -std=gnu89 -pthread -Wall -Wextra
CFLAGS += -I../../../../usr/include/
LDFLAGS += -pthread

.PHONY: all clean

include ../lib.mk

# lib.mk TEST_CUSTOM_PROGS var is for custom tests that need special
# build rules. lib.mk will run and install them.

TEST_CUSTOM_PROGS := $(OUTPUT)/sync_test
all: $(TEST_CUSTOM_PROGS)

OBJS = sync_test.o sync.o

TESTS += sync_alloc.o
TESTS += sync_fence.o
TESTS += sync_merge.o
TESTS += sync_wait.o
TESTS += sync_stress_parallelism.o
TESTS += sync_stress_consumer.o
TESTS += sync_stress_merge.o

OBJS := $(patsubst %,$(OUTPUT)/%,$(OBJS))
TESTS := $(patsubst %,$(OUTPUT)/%,$(TESTS))

$(TEST_CUSTOM_PROGS): $(TESTS) $(OBJS)
	$(CC) -o $(TEST_CUSTOM_PROGS) $(OBJS) $(TESTS) $(CFLAGS) $(LDFLAGS)

$(OBJS): $(OUTPUT)/%.o: %.c
	$(CC) -c $^ -o $@

$(TESTS): $(OUTPUT)/%.o: %.c
	$(CC) -c $^ -o $@

EXTRA_CLEAN := $(TEST_CUSTOM_PROGS) $(OBJS) $(TESTS)
