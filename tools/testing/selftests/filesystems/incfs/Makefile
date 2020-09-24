# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall
CFLAGS += -I../.. -I../../../../..

LDLIBS := -llz4 -lcrypto
EXTRA_SOURCES := utils.c
TEST_GEN_PROGS := incfs_test

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)

include ../../lib.mk
