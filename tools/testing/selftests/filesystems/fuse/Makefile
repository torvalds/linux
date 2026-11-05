# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES)

TEST_GEN_PROGS := fusectl_test test_syncfs
TEST_GEN_PROGS += write_extend_eof_test
TEST_GEN_PROGS += fuse_acl_cache_test
TEST_GEN_PROGS += fuse_symlink_cache_test
TEST_GEN_PROGS += fuse_readdir_cache_test
TEST_GEN_FILES := fuse_mnt

include ../../lib.mk

$(OUTPUT)/write_extend_eof_test: LDLIBS += -lpthread

VAR_CFLAGS := $(shell pkg-config fuse3 --cflags 2>/dev/null)
ifeq ($(VAR_CFLAGS),)
VAR_CFLAGS := -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse3
endif

VAR_LDLIBS := $(shell pkg-config fuse3 --libs 2>/dev/null)
ifeq ($(VAR_LDLIBS),)
VAR_LDLIBS := -lfuse3 -pthread
endif

CFLAGS += $(VAR_CFLAGS)
LDLIBS += $(VAR_LDLIBS)

$(OUTPUT)/fuse_mnt: CFLAGS += $(VAR_CFLAGS)
$(OUTPUT)/fuse_mnt: LDLIBS += $(VAR_LDLIBS)

$(OUTPUT)/fuse_acl_cache_test: fuse_common.c fuse_acl_cache_test.c
$(OUTPUT)/fuse_symlink_cache_test: fuse_common.c fuse_symlink_cache_test.c
$(OUTPUT)/fuse_readdir_cache_test: fuse_common.c fuse_readdir_cache_test.c

EXTRA_CLEAN := fuse_common.o
