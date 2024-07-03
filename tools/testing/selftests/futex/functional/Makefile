# SPDX-License-Identifier: GPL-2.0
INCLUDES := -I../include -I../../ $(KHDR_INCLUDES)
CFLAGS := $(CFLAGS) -g -O2 -Wall -D_GNU_SOURCE= -pthread $(INCLUDES) $(KHDR_INCLUDES)
LDLIBS := -lpthread -lrt

LOCAL_HDRS := \
	../include/futextest.h \
	../include/atomic.h \
	../include/logging.h
TEST_GEN_PROGS := \
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
DEFAULT_INSTALL_HDR_PATH := 1
include ../../lib.mk
