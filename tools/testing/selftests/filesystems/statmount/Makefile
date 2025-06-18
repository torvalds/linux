# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := statmount_test statmount_test_ns listmount_test

include ../../lib.mk

$(OUTPUT)/statmount_test_ns: ../utils.c
