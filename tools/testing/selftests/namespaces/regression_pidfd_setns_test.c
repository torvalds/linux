// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../pidfd/pidfd.h"
#include "../kselftest_harness.h"

/*
 * Regression tests for the setns(pidfd) active reference counting bug.
 *
 * These tests are based on the reproducers that triggered the race condition
 * fixed by commit 1c465d0518dc ("ns: handle setns(pidfd, ...) cleanly").
 *
 * The bug: When using setns() with a pidfd, if the target task exits between
 * prepare_nsset() and commit_nsset(), the namespaces would become inactive.
 * Then ns_ref_active_get() would increment from 0 without properly resurrecting
 * the owner chain, causing active reference count underflows.
 */

/*
 * Simple pidfd setns test using create_child()+unshare().
 *
 * Without the fix, this would trigger active refcount warnings when the
 * parent exits after doing setns(pidfd) on a child that has already exited.
 */
TEST(simple_pidfd_setns)
{
	pid_t child_pid;
	int pidfd = -1;
	int ret;
	int sv[2];
	char c;

	/* Ignore SIGCHLD for autoreap */
	ASSERT_NE(signal(SIGCHLD, SIG_IGN), SIG_ERR);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	/* Create a child process without namespaces initially */
	child_pid = create_child(&pidfd, 0);
	ASSERT_GE(child_pid, 0);

	if (child_pid == 0) {
		close(sv[0]);

		if (unshare(CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWUSER) < 0) {
			close(sv[1]);
			_exit(1);
		}

		/* Signal parent that namespaces are ready */
		if (write_nointr(sv[1], "1", 1) < 0) {
			close(sv[1]);
			_exit(1);
		}

		close(sv[1]);
		_exit(0);
	}
	ASSERT_GE(pidfd, 0);
	EXPECT_EQ(close(sv[1]), 0);

	ret = read_nointr(sv[0], &c, 1);
	ASSERT_EQ(ret, 1);
	EXPECT_EQ(close(sv[0]), 0);

	/* Set to child's namespaces via pidfd */
	ret = setns(pidfd, CLONE_NEWUTS | CLONE_NEWIPC);
	TH_LOG("setns() returned %d", ret);
	close(pidfd);
}

/*
 * Simple pidfd setns test using create_child().
 *
 * This variation uses create_child() with namespace flags directly.
 * Namespaces are created immediately at clone time.
 */
TEST(simple_pidfd_setns_clone)
{
	pid_t child_pid;
	int pidfd = -1;
	int ret;

	/* Ignore SIGCHLD for autoreap */
	ASSERT_NE(signal(SIGCHLD, SIG_IGN), SIG_ERR);

	/* Create a child process with new namespaces using create_child() */
	child_pid = create_child(&pidfd, CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET);
	ASSERT_GE(child_pid, 0);

	if (child_pid == 0) {
		/* Child: sleep for a while so parent can setns to us */
		sleep(2);
		_exit(0);
	}

	/* Parent: pidfd was already created by create_child() */
	ASSERT_GE(pidfd, 0);

	/* Set to child's namespaces via pidfd */
	ret = setns(pidfd, CLONE_NEWUTS | CLONE_NEWIPC);
	close(pidfd);
	TH_LOG("setns() returned %d", ret);
}

TEST_HARNESS_MAIN
