# SPDX-License-Identifier: GPL-2.0

CFLAGS = -g -Wall -O2 -D_FORTIFY_SOURCE=2
CFLAGS += $(KHDR_INCLUDES)

TEST_GEN_PROGS := resctrl_tests

include ../lib.mk

$(OUTPUT)/resctrl_tests: $(wildcard *.[ch])
