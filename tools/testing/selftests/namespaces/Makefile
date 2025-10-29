# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := nsid_test file_handle_test init_ino_test ns_active_ref_test

include ../lib.mk

$(OUTPUT)/ns_active_ref_test: ../filesystems/utils.c

