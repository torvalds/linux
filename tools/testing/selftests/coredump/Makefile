# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -Wall -O0 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)

TEST_GEN_PROGS := stackdump_test \
		  coredump_socket_test \
		  coredump_socket_protocol_test
TEST_FILES := stackdump

include ../lib.mk

$(OUTPUT)/stackdump_test: coredump_test_helpers.c
$(OUTPUT)/coredump_socket_test: coredump_test_helpers.c
$(OUTPUT)/coredump_socket_protocol_test: coredump_test_helpers.c
