# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES)

TEST_GEN_PROGS := fusectl_test
TEST_GEN_PROGS += write_extend_eof_test
TEST_GEN_FILES := fuse_mnt

include ../../lib.mk

$(OUTPUT)/write_extend_eof_test: LDLIBS += -lpthread

VAR_CFLAGS := $(shell pkg-config fuse --cflags 2>/dev/null)
ifeq ($(VAR_CFLAGS),)
VAR_CFLAGS := -D_FILE_OFFSET_BITS=64 -I/usr/include/fuse
endif

VAR_LDLIBS := $(shell pkg-config fuse --libs 2>/dev/null)
ifeq ($(VAR_LDLIBS),)
VAR_LDLIBS := -lfuse -pthread
endif

$(OUTPUT)/fuse_mnt: CFLAGS += $(VAR_CFLAGS)
$(OUTPUT)/fuse_mnt: LDLIBS += $(VAR_LDLIBS)
