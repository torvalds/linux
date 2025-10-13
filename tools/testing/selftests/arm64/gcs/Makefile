# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 ARM Limited
#
# In order to avoid interaction with the toolchain and dynamic linker the
# portions of these tests that interact with the GCS are implemented using
# nolibc.
#

TEST_GEN_PROGS := basic-gcs libc-gcs gcs-locking gcs-stress gcspushm gcsstr
TEST_GEN_PROGS_EXTENDED := gcs-stress-thread

LDLIBS+=-lpthread

include ../../lib.mk

$(OUTPUT)/basic-gcs: basic-gcs.c
	$(CC) $(CFLAGS) -fno-asynchronous-unwind-tables -fno-ident -s -nostdlib -nostdinc \
		-static -I../../../../include/nolibc -include ../../../../include/nolibc/nolibc.h \
		-I../../../../../usr/include \
		-std=gnu99 -I../.. -g \
		-ffreestanding $^ -o $@ -lgcc

$(OUTPUT)/gcs-stress-thread: gcs-stress-thread.S
	$(CC) -nostdlib $^ -o $@

$(OUTPUT)/gcspushm: gcspushm.S
	$(CC) -nostdlib $^ -o $@

$(OUTPUT)/gcsstr: gcsstr.S
	$(CC) -nostdlib $^ -o $@
