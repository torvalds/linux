# SPDX-License-Identifier: GPL-2.0

CURDIR := $(abspath .)
TOOLSDIR := $(abspath ../../..)
VSOCK_TEST_DIR := $(TOOLSDIR)/testing/vsock
VSOCK_TEST_SRCS := $(wildcard $(VSOCK_TEST_DIR)/*.c $(VSOCK_TEST_DIR)/*.h)

$(OUTPUT)/vsock_test: $(VSOCK_TEST_DIR)/vsock_test
	install -m 755 $< $@

$(VSOCK_TEST_DIR)/vsock_test: $(VSOCK_TEST_SRCS)
	$(MAKE) -C $(VSOCK_TEST_DIR) vsock_test
TEST_PROGS += vmtest.sh
TEST_GEN_FILES := vsock_test

include ../lib.mk

