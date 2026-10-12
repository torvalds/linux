# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := open_tree_ns_test open_tree_ns_covered_test

CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS := -lcap

include ../../lib.mk

$(OUTPUT)/open_tree_ns_test: open_tree_ns_test.c ../utils.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
