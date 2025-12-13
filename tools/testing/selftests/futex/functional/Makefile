# SPDX-License-Identifier: GPL-2.0
PKG_CONFIG ?= pkg-config
LIBNUMA_TEST = $(shell sh -c "$(PKG_CONFIG) numa --atleast-version 2.0.16 > /dev/null 2>&1 && echo SUFFICIENT || echo NO")

INCLUDES := -I../include -I../../ $(KHDR_INCLUDES)
CFLAGS := $(CFLAGS) -g -O2 -Wall -pthread -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 $(INCLUDES) $(KHDR_INCLUDES) -DLIBNUMA_VER_$(LIBNUMA_TEST)=1
LDLIBS := -lpthread -lrt -lnuma

LOCAL_HDRS := \
	../include/futextest.h \
	../include/atomic.h
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
	futex_priv_hash \
	futex_numa_mpol \
	futex_waitv \
	futex_numa

TEST_PROGS := run.sh

top_srcdir = ../../../../..
DEFAULT_INSTALL_HDR_PATH := 1
include ../../lib.mk
