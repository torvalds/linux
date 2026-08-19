# SPDX-License-Identifier: GPL-2.0

CFLAGS += $(KHDR_INCLUDES)
TEST_GEN_PROGS := devpts_pts file_stressor anon_inode_test kernfs_test fclog
TEST_GEN_PROGS += idmapped_tmpfile
TEST_GEN_PROGS_EXTENDED := dnotify_test

include ../lib.mk

$(OUTPUT)/idmapped_tmpfile: LDLIBS += -lcap
$(OUTPUT)/idmapped_tmpfile: utils.c
