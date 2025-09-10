# SPDX-License-Identifier: GPL-2.0-only
CFLAGS += -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES) -pthread -Wall

TEST_GEN_PROGS := pidfd_test pidfd_fdinfo_test pidfd_open_test \
	pidfd_poll_test pidfd_wait pidfd_getfd_test pidfd_setns_test \
	pidfd_file_handle_test pidfd_bind_mount pidfd_info_test \
	pidfd_xattr_test pidfd_setattr_test

TEST_GEN_PROGS_EXTENDED := pidfd_exec_helper

include ../lib.mk

