# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)

TEST_GEN_PROGS := stackdump_test \
		  coredump_socket_test \
		  coredump_socket_protocol_test \
		  coredump_notify_signal_test \
		  coredump_signal_test \
		  coredump_worker_test
# Spawned by the kernel as the |helper, not a test of its own.
TEST_GEN_FILES := coredump_notify_signal_helper
TEST_FILES := stackdump

include ../lib.mk

$(OUTPUT)/stackdump_test: coredump_test_helpers.c
$(OUTPUT)/coredump_socket_test: coredump_test_helpers.c
$(OUTPUT)/coredump_socket_protocol_test: coredump_test_helpers.c
$(OUTPUT)/coredump_notify_signal_test: coredump_test_helpers.c
$(OUTPUT)/coredump_notify_signal_helper: coredump_test_helpers.c
$(OUTPUT)/coredump_signal_test: coredump_test_helpers.c
$(OUTPUT)/coredump_worker_test: coredump_test_helpers.c
