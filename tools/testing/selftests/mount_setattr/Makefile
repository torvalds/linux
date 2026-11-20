# SPDX-License-Identifier: GPL-2.0
# Makefile for mount selftests.
CFLAGS = -g $(KHDR_INCLUDES) -Wall -O2 -pthread -D_FILE_OFFSET_BITS=64

LOCAL_HDRS += ../filesystems/wrappers.h

TEST_GEN_PROGS := mount_setattr_test

include ../lib.mk
