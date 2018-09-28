# SPDX-License-Identifier: GPL-2.0
TEST_PROGS := cache_shape

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c ../utils.c

top_srcdir = ../../../../..
include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
