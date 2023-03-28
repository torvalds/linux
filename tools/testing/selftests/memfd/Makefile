# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += $(KHDR_INCLUDES)

TEST_GEN_PROGS := memfd_test
TEST_PROGS := run_fuse_test.sh run_hugetlbfs_test.sh
TEST_GEN_FILES := fuse_test fuse_mnt

VAR_CFLAGS := $(shell pkg-config fuse --cflags 2>/dev/null)
ifeq ($(VAR_CFLAGS),)
VAR_CFLAGS := -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse
endif

VAR_LDLIBS := $(shell pkg-config fuse --libs 2>/dev/null)
ifeq ($(VAR_LDLIBS),)
VAR_LDLIBS := -lfuse -pthread
endif

fuse_mnt.o: CFLAGS += $(VAR_CFLAGS)

include ../lib.mk

$(OUTPUT)/fuse_mnt: LDLIBS += $(VAR_LDLIBS)

$(OUTPUT)/memfd_test: memfd_test.c common.c
$(OUTPUT)/fuse_test: fuse_test.c common.c

EXTRA_CLEAN = $(OUTPUT)/common.o
