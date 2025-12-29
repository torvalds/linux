# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := open_tree_ns_test

CFLAGS := -Wall -Werror -g $(KHDR_INCLUDES)
LDLIBS := -lcap

include ../../lib.mk

$(OUTPUT)/open_tree_ns_test: open_tree_ns_test.c ../utils.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
