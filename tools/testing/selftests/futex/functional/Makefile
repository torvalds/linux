# SPDX-License-Identifier: GPL-2.0
INCLUDES := -I../include -I../../ -I../../../../../usr/include/ \
	-I$(KBUILD_OUTPUT)/kselftest/usr/include
CFLAGS := $(CFLAGS) -g -O2 -Wall -D_GNU_SOURCE -pthread $(INCLUDES)
LDLIBS := -lpthread -lrt

HEADERS := \
	../include/futextest.h \
	../include/atomic.h \
	../include/logging.h
TEST_GEN_FILES := \
	futex_wait_timeout \
	futex_wait_wouldblock \
	futex_requeue_pi \
	futex_requeue_pi_signal_restart \
	futex_requeue_pi_mismatched_ops \
	futex_wait_uninitialized_heap \
	futex_wait_private_mapped_file \
	futex_wait \
	futex_requeue \
	futex_waitv

TEST_PROGS := run.sh

top_srcdir = ../../../../..
KSFT_KHDR_INSTALL := 1
include ../../lib.mk

$(TEST_GEN_FILES): $(HEADERS)
