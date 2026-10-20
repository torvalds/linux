# SPDX-License-Identifier: GPL-2.0

CFLAGS += $(KHDR_INCLUDES)
TEST_GEN_PROGS := devpts_pts anon_inode_test kernfs_test fclog ustat_test rw_hint_test
TEST_GEN_PROGS += idmapped_tmpfile
TEST_GEN_PROGS_EXTENDED := dnotify_test

include ../lib.mk

$(OUTPUT)/idmapped_tmpfile: LDLIBS += -lcap
$(OUTPUT)/idmapped_tmpfile: utils.c
