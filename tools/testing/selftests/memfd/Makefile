# SPDX-License-Identifier: GPL-2.0
CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../../../include/uapi/
CFLAGS += -I../../../../include/
CFLAGS += -I../../../../usr/include/

TEST_PROGS := run_tests.sh
TEST_GEN_FILES := memfd_test fuse_mnt fuse_test

fuse_mnt.o: CFLAGS += $(shell pkg-config fuse --cflags)

include ../lib.mk

$(OUTPUT)/fuse_mnt: LDLIBS += $(shell pkg-config fuse --libs)

$(OUTPUT)/memfd_test: memfd_test.c common.o
$(OUTPUT)/fuse_test: fuse_test.c common.o

EXTRA_CLEAN = common.o
