# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := fsmount_ns_test

CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS := -lcap

include ../../lib.mk

$(OUTPUT)/fsmount_ns_test: fsmount_ns_test.c ../utils.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
