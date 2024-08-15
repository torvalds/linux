#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/wait.h>

#include "../kselftest_harness.h"
#include "../pidfd/pidfd.h"

/*
 * Regression test for:
 * 35f71bc0a09a ("fork: report pid reservation failure properly")
 * b26ebfe12f34 ("pid: Fix error return value in some cases")
 */
TEST(regression_enomem)
{
	pid_t pid;

	if (geteuid())
		EXPECT_EQ(0, unshare(CLONE_NEWUSER));

	EXPECT_EQ(0, unshare(CLONE_NEWPID));

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(EXIT_SUCCESS);

	EXPECT_EQ(0, wait_for_pid(pid));

	pid = fork();
	ASSERT_LT(pid, 0);
	ASSERT_EQ(errno, ENOMEM);
}

TEST_HARNESS_MAIN
