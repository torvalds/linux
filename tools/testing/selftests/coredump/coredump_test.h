/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __COREDUMP_TEST_H
#define __COREDUMP_TEST_H

#include "../kselftest_harness.h"
#include "coredump_notify_signal.h"

#include "coredump_test_helpers.h"

/* Coredump fixture */
FIXTURE(coredump)
{
	char original_core_pattern[256];
	pid_t pid_coredump_server;
	int fd_tmpfs_detached;
};

/* Inline helper that uses harness types */
static inline void wait_and_check_coredump_server(pid_t pid_coredump_server,
						   struct __test_metadata *const _metadata,
						   FIXTURE_DATA(coredump) *self)
{
	int status;
	waitpid(pid_coredump_server, &status, 0);
	self->pid_coredump_server = -ESRCH;
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);
}

#endif /* __COREDUMP_TEST_H */
