# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 ARM Limited
#
# In order to avoid interaction with the toolchain and dynamic linker the
# portions of these tests that interact with the GCS are implemented using
# nolibc.
#

TEST_GEN_PROGS := basic-gcs libc-gcs

LDLIBS+=-lpthread

include ../../lib.mk

$(OUTPUT)/basic-gcs: basic-gcs.c
	$(CC) -g -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
		-static -include ../../../../include/nolibc/nolibc.h \
		-I../../../../../usr/include \
		-std=gnu99 -I../.. -g \
		-ffreestanding -Wall $^ -o $@ -lgcc
