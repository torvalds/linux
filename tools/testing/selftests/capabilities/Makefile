# SPDX-License-Identifier: GPL-2.0
TEST_GEN_FILES := validate_cap
TEST_GEN_PROGS := test_execve

CFLAGS += -O2 -g -std=gnu99 -Wall $(KHDR_INCLUDES)
LDLIBS += -lcap-ng -lrt -ldl

include ../lib.mk

