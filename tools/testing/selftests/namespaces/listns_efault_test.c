// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/nsfs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "../pidfd/pidfd.h"
#include "wrappers.h"

/*
 * Test listns() error handling with invalid buffer addresses.
 *
 * When the buffer pointer is invalid (e.g., crossing page boundaries
 * into unmapped memory), listns() returns EINVAL.
 *
 * This test also creates mount namespaces that get destroyed during
 * iteration, testing that namespace cleanup happens outside the RCU
 * read lock.
 */
TEST(listns_partial_fault_with_ns_cleanup)
{
	void *map;
	__u64 *ns_ids;
	ssize_t ret;
	long page_size;
	pid_t pid, iter_pid;
	int pidfds[5];
	int sv[5][2];
	int iter_pidfd;
	int i, status;
	char c;

	page_size = sysconf(_SC_PAGESIZE);
	ASSERT_GT(page_size, 0);

	/*
	 * Map two pages:
	 * - First page: readable and writable
	 * - Second page: will be unmapped to trigger EFAULT
	 */
	map = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(map, MAP_FAILED);

	/* Unmap the second page */
	ret = munmap((char *)map + page_size, page_size);
	ASSERT_EQ(ret, 0);

	/*
	 * Position the buffer pointer so there's room for exactly one u64
	 * before the page boundary. The second u64 would fall into the
	 * unmapped page.
	 */
	ns_ids = ((__u64 *)((char *)map + page_size)) - 1;

	/*
	 * Create a separate process to run listns() in a loop concurrently
	 * with namespace creation and destruction.
	 */
	iter_pid = create_child(&iter_pidfd, 0);
	ASSERT_NE(iter_pid, -1);

	if (iter_pid == 0) {
		struct ns_id_req req = {
			.size = sizeof(req),
			.spare = 0,
			.ns_id = 0,
			.ns_type = 0,  /* All types */
			.spare2 = 0,
			.user_ns_id = 0,  /* Global listing */
		};
		int iter_ret;

		/*
		 * Loop calling listns() until killed.
		 * The kernel should:
		 * 1. Successfully write the first namespace ID (within valid page)
		 * 2. Fail with EFAULT when trying to write the second ID (unmapped page)
		 * 3. Handle concurrent namespace destruction without deadlock
		 */
		while (1) {
			iter_ret = sys_listns(&req, ns_ids, 2, 0);

			if (iter_ret == -1 && errno == ENOSYS)
				_exit(PIDFD_SKIP);
		}
	}

	/* Small delay to let iterator start looping */
	usleep(50000);

	/*
	 * Create several child processes, each in its own mount namespace.
	 * These will be destroyed while the iterator is running listns().
	 */
	for (i = 0; i < 5; i++) {
		/* Create socketpair for synchronization */
		ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]), 0);

		pid = create_child(&pidfds[i], CLONE_NEWNS);
		ASSERT_NE(pid, -1);

		if (pid == 0) {
			close(sv[i][0]); /* Close parent end */

			if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0))
				_exit(1);

			/* Child: create a couple of tmpfs mounts */
			if (mkdir("/tmp/test_mnt1", 0755) == -1 && errno != EEXIST)
				_exit(1);
			if (mkdir("/tmp/test_mnt2", 0755) == -1 && errno != EEXIST)
				_exit(1);

			if (mount("tmpfs", "/tmp/test_mnt1", "tmpfs", 0, NULL) == -1)
				_exit(1);
			if (mount("tmpfs", "/tmp/test_mnt2", "tmpfs", 0, NULL) == -1)
				_exit(1);

			/* Signal parent that setup is complete */
			if (write_nointr(sv[i][1], "R", 1) != 1)
				_exit(1);

			/* Wait for parent to signal us to exit */
			if (read_nointr(sv[i][1], &c, 1) != 1)
				_exit(1);

			close(sv[i][1]);
			_exit(0);
		}

		close(sv[i][1]); /* Close child end */
	}

	/* Wait for all children to finish setup */
	for (i = 0; i < 5; i++) {
		ret = read_nointr(sv[i][0], &c, 1);
		ASSERT_EQ(ret, 1);
		ASSERT_EQ(c, 'R');
	}

	/*
	 * Signal children to exit. This will destroy their mount namespaces
	 * while listns() is iterating the namespace tree.
	 * This tests that cleanup happens outside the RCU read lock.
	 */
	for (i = 0; i < 5; i++)
		write_nointr(sv[i][0], "X", 1);

	/* Wait for all mount namespace children to exit and cleanup */
	for (i = 0; i < 5; i++) {
		waitpid(-1, NULL, 0);
		close(sv[i][0]);
		close(pidfds[i]);
	}

	/* Kill iterator and wait for it */
	sys_pidfd_send_signal(iter_pidfd, SIGKILL, NULL, 0);
	ret = waitpid(iter_pid, &status, 0);
	ASSERT_EQ(ret, iter_pid);
	close(iter_pidfd);

	/* Should have been killed */
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGKILL);

	/* Clean up */
	munmap(map, page_size);
}

/*
 * Test listns() error handling when the entire buffer is invalid.
 * This is a sanity check that basic invalid pointer detection works.
 */
TEST(listns_complete_fault)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 *ns_ids;
	ssize_t ret;

	/* Use a clearly invalid pointer */
	ns_ids = (__u64 *)0xdeadbeef;

	ret = sys_listns(&req, ns_ids, 10, 0);

	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "listns() not supported");

	/* Should fail with EFAULT */
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EFAULT);
}

/*
 * Test listns() error handling when the buffer is NULL.
 */
TEST(listns_null_buffer)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	ssize_t ret;

	/* NULL buffer with non-zero count should fail */
	ret = sys_listns(&req, NULL, 10, 0);

	if (ret == -1 && errno == ENOSYS)
		SKIP(return, "listns() not supported");

	/* Should fail with EFAULT */
	ASSERT_EQ(ret, -1);
	ASSERT_EQ(errno, EFAULT);
}

/*
 * Test listns() with a buffer that becomes invalid mid-iteration
 * (after several successful writes), combined with mount namespace
 * destruction to test RCU cleanup logic.
 */
TEST(listns_late_fault_with_ns_cleanup)
{
	void *map;
	__u64 *ns_ids;
	ssize_t ret;
	long page_size;
	pid_t pid, iter_pid;
	int pidfds[10];
	int sv[10][2];
	int iter_pidfd;
	int i, status;
	char c;

	page_size = sysconf(_SC_PAGESIZE);
	ASSERT_GT(page_size, 0);

	/* Map two pages */
	map = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(map, MAP_FAILED);

	/* Unmap the second page */
	ret = munmap((char *)map + page_size, page_size);
	ASSERT_EQ(ret, 0);

	/*
	 * Position buffer so we can write several u64s successfully
	 * before hitting the page boundary.
	 */
	ns_ids = ((__u64 *)((char *)map + page_size)) - 5;

	/*
	 * Create a separate process to run listns() concurrently.
	 */
	iter_pid = create_child(&iter_pidfd, 0);
	ASSERT_NE(iter_pid, -1);

	if (iter_pid == 0) {
		struct ns_id_req req = {
			.size = sizeof(req),
			.spare = 0,
			.ns_id = 0,
			.ns_type = 0,
			.spare2 = 0,
			.user_ns_id = 0,
		};
		int iter_ret;

		/*
		 * Loop calling listns() until killed.
		 * Request 10 namespace IDs while namespaces are being destroyed.
		 * This tests:
		 * 1. EFAULT handling when buffer becomes invalid
		 * 2. Namespace cleanup outside RCU read lock during iteration
		 */
		while (1) {
			iter_ret = sys_listns(&req, ns_ids, 10, 0);

			if (iter_ret == -1 && errno == ENOSYS)
				_exit(PIDFD_SKIP);
		}
	}

	/* Small delay to let iterator start looping */
	usleep(50000);

	/*
	 * Create more children with mount namespaces to increase the
	 * likelihood that namespace cleanup happens during iteration.
	 */
	for (i = 0; i < 10; i++) {
		/* Create socketpair for synchronization */
		ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]), 0);

		pid = create_child(&pidfds[i], CLONE_NEWNS);
		ASSERT_NE(pid, -1);

		if (pid == 0) {
			close(sv[i][0]); /* Close parent end */

			if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0))
				_exit(1);

			/* Child: create tmpfs mounts */
			if (mkdir("/tmp/test_mnt1", 0755) == -1 && errno != EEXIST)
				_exit(1);
			if (mkdir("/tmp/test_mnt2", 0755) == -1 && errno != EEXIST)
				_exit(1);

			if (mount("tmpfs", "/tmp/test_mnt1", "tmpfs", 0, NULL) == -1)
				_exit(1);
			if (mount("tmpfs", "/tmp/test_mnt2", "tmpfs", 0, NULL) == -1)
				_exit(1);

			/* Signal parent that setup is complete */
			if (write_nointr(sv[i][1], "R", 1) != 1)
				_exit(1);

			/* Wait for parent to signal us to exit */
			if (read_nointr(sv[i][1], &c, 1) != 1)
				_exit(1);

			close(sv[i][1]);
			_exit(0);
		}

		close(sv[i][1]); /* Close child end */
	}

	/* Wait for all children to finish setup */
	for (i = 0; i < 10; i++) {
		ret = read_nointr(sv[i][0], &c, 1);
		ASSERT_EQ(ret, 1);
		ASSERT_EQ(c, 'R');
	}

	/* Kill half the children */
	for (i = 0; i < 5; i++)
		write_nointr(sv[i][0], "X", 1);

	/* Small delay to let some exit */
	usleep(10000);

	/* Kill remaining children */
	for (i = 5; i < 10; i++)
		write_nointr(sv[i][0], "X", 1);

	/* Wait for all children and cleanup */
	for (i = 0; i < 10; i++) {
		waitpid(-1, NULL, 0);
		close(sv[i][0]);
		close(pidfds[i]);
	}

	/* Kill iterator and wait for it */
	sys_pidfd_send_signal(iter_pidfd, SIGKILL, NULL, 0);
	ret = waitpid(iter_pid, &status, 0);
	ASSERT_EQ(ret, iter_pid);
	close(iter_pidfd);

	/* Should have been killed */
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGKILL);

	/* Clean up */
	munmap(map, page_size);
}

/*
 * Test specifically focused on mount namespace cleanup during EFAULT.
 * Filter for mount namespaces only.
 */
TEST(listns_mnt_ns_cleanup_on_fault)
{
	void *map;
	__u64 *ns_ids;
	ssize_t ret;
	long page_size;
	pid_t pid, iter_pid;
	int pidfds[8];
	int sv[8][2];
	int iter_pidfd;
	int i, status;
	char c;

	page_size = sysconf(_SC_PAGESIZE);
	ASSERT_GT(page_size, 0);

	/* Set up partial fault buffer */
	map = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(map, MAP_FAILED);

	ret = munmap((char *)map + page_size, page_size);
	ASSERT_EQ(ret, 0);

	/* Position for 3 successful writes, then fault */
	ns_ids = ((__u64 *)((char *)map + page_size)) - 3;

	/*
	 * Create a separate process to run listns() concurrently.
	 */
	iter_pid = create_child(&iter_pidfd, 0);
	ASSERT_NE(iter_pid, -1);

	if (iter_pid == 0) {
		struct ns_id_req req = {
			.size = sizeof(req),
			.spare = 0,
			.ns_id = 0,
			.ns_type = CLONE_NEWNS,  /* Only mount namespaces */
			.spare2 = 0,
			.user_ns_id = 0,
		};
		int iter_ret;

		/*
		 * Loop calling listns() until killed.
		 * Call listns() to race with namespace destruction.
		 */
		while (1) {
			iter_ret = sys_listns(&req, ns_ids, 10, 0);

			if (iter_ret == -1 && errno == ENOSYS)
				_exit(PIDFD_SKIP);
		}
	}

	/* Small delay to let iterator start looping */
	usleep(50000);

	/* Create children with mount namespaces */
	for (i = 0; i < 8; i++) {
		/* Create socketpair for synchronization */
		ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]), 0);

		pid = create_child(&pidfds[i], CLONE_NEWNS);
		ASSERT_NE(pid, -1);

		if (pid == 0) {
			close(sv[i][0]); /* Close parent end */

			if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0))
				_exit(1);

			/* Do some mount operations to make cleanup more interesting */
			if (mkdir("/tmp/test_mnt1", 0755) == -1 && errno != EEXIST)
				_exit(1);
			if (mkdir("/tmp/test_mnt2", 0755) == -1 && errno != EEXIST)
				_exit(1);

			if (mount("tmpfs", "/tmp/test_mnt1", "tmpfs", 0, NULL) == -1)
				_exit(1);
			if (mount("tmpfs", "/tmp/test_mnt2", "tmpfs", 0, NULL) == -1)
				_exit(1);

			/* Signal parent that setup is complete */
			if (write_nointr(sv[i][1], "R", 1) != 1)
				_exit(1);

			/* Wait for parent to signal us to exit */
			if (read_nointr(sv[i][1], &c, 1) != 1)
				_exit(1);

			close(sv[i][1]);
			_exit(0);
		}

		close(sv[i][1]); /* Close child end */
	}

	/* Wait for all children to finish setup */
	for (i = 0; i < 8; i++) {
		ret = read_nointr(sv[i][0], &c, 1);
		ASSERT_EQ(ret, 1);
		ASSERT_EQ(c, 'R');
	}

	/* Kill children to trigger namespace destruction during iteration */
	for (i = 0; i < 8; i++)
		write_nointr(sv[i][0], "X", 1);

	/* Wait for children and cleanup */
	for (i = 0; i < 8; i++) {
		waitpid(-1, NULL, 0);
		close(sv[i][0]);
		close(pidfds[i]);
	}

	/* Kill iterator and wait for it */
	sys_pidfd_send_signal(iter_pidfd, SIGKILL, NULL, 0);
	ret = waitpid(iter_pid, &status, 0);
	ASSERT_EQ(ret, iter_pid);
	close(iter_pidfd);

	/* Should have been killed */
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGKILL);

	munmap(map, page_size);
}

TEST_HARNESS_MAIN
