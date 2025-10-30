// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/nsfs.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

/*
 * Test basic listns() functionality with the unified namespace tree.
 * List all active namespaces globally.
 */
TEST(listns_basic_unified)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,  /* All types */
		.spare2 = 0,
		.user_ns_id = 0,  /* Global listing */
	};
	__u64 ns_ids[100];
	ssize_t ret;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}

	/* Should find at least the initial namespaces */
	ASSERT_GT(ret, 0);
	TH_LOG("Found %zd active namespaces", ret);

	/* Verify all returned IDs are non-zero */
	for (ssize_t i = 0; i < ret; i++) {
		ASSERT_NE(ns_ids[i], 0);
		TH_LOG("  [%zd] ns_id: %llu", i, (unsigned long long)ns_ids[i]);
	}
}

/*
 * Test listns() with type filtering.
 * List only network namespaces.
 */
TEST(listns_filter_by_type)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWNET,  /* Only network namespaces */
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids[100];
	ssize_t ret;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}
	ASSERT_GE(ret, 0);

	/* Should find at least init_net */
	ASSERT_GT(ret, 0);
	TH_LOG("Found %zd active network namespaces", ret);

	/* Verify we can open each namespace and it's actually a network namespace */
	for (ssize_t i = 0; i < ret && i < 5; i++) {
		struct nsfs_file_handle nsfh = {
			.ns_id = ns_ids[i],
			.ns_type = CLONE_NEWNET,
			.ns_inum = 0,
		};
		struct file_handle *fh;
		int fd;

		fh = (struct file_handle *)malloc(sizeof(*fh) + sizeof(nsfh));
		ASSERT_NE(fh, NULL);
		fh->handle_bytes = sizeof(nsfh);
		fh->handle_type = 0;
		memcpy(fh->f_handle, &nsfh, sizeof(nsfh));

		fd = open_by_handle_at(-10003, fh, O_RDONLY);
		free(fh);

		if (fd >= 0) {
			int ns_type;
			/* Verify it's a network namespace via ioctl */
			ns_type = ioctl(fd, NS_GET_NSTYPE);
			if (ns_type >= 0) {
				ASSERT_EQ(ns_type, CLONE_NEWNET);
			}
			close(fd);
		}
	}
}

/*
 * Test listns() pagination.
 * List namespaces in batches.
 */
TEST(listns_pagination)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 batch1[2], batch2[2];
	ssize_t ret1, ret2;

	/* Get first batch */
	ret1 = sys_listns(&req, batch1, ARRAY_SIZE(batch1), 0);
	if (ret1 < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}
	ASSERT_GE(ret1, 0);

	if (ret1 == 0)
		SKIP(return, "No namespaces found");

	TH_LOG("First batch: %zd namespaces", ret1);

	/* Get second batch using last ID from first batch */
	if (ret1 == ARRAY_SIZE(batch1)) {
		req.ns_id = batch1[ret1 - 1];
		ret2 = sys_listns(&req, batch2, ARRAY_SIZE(batch2), 0);
		ASSERT_GE(ret2, 0);

		TH_LOG("Second batch: %zd namespaces (after ns_id=%llu)",
		       ret2, (unsigned long long)req.ns_id);

		/* If we got more results, verify IDs are monotonically increasing */
		if (ret2 > 0) {
			ASSERT_GT(batch2[0], batch1[ret1 - 1]);
			TH_LOG("Pagination working: %llu > %llu",
			       (unsigned long long)batch2[0],
			       (unsigned long long)batch1[ret1 - 1]);
		}
	} else {
		TH_LOG("All namespaces fit in first batch");
	}
}

/*
 * Test listns() with LISTNS_CURRENT_USER.
 * List namespaces owned by current user namespace.
 */
TEST(listns_current_user)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = LISTNS_CURRENT_USER,
	};
	__u64 ns_ids[100];
	ssize_t ret;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}
	ASSERT_GE(ret, 0);

	/* Should find at least the initial namespaces if we're in init_user_ns */
	TH_LOG("Found %zd namespaces owned by current user namespace", ret);

	for (ssize_t i = 0; i < ret; i++)
		TH_LOG("  [%zd] ns_id: %llu", i, (unsigned long long)ns_ids[i]);
}

/*
 * Test that listns() only returns active namespaces.
 * Create a namespace, let it become inactive, verify it's not listed.
 */
TEST(listns_only_active)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWNET,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids_before[100], ns_ids_after[100];
	ssize_t ret_before, ret_after;
	int pipefd[2];
	pid_t pid;
	__u64 new_ns_id = 0;
	int status;

	/* Get initial list */
	ret_before = sys_listns(&req, ns_ids_before, ARRAY_SIZE(ns_ids_before), 0);
	if (ret_before < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}
	ASSERT_GE(ret_before, 0);

	TH_LOG("Before: %zd active network namespaces", ret_before);

	/* Create a new namespace in a child process and get its ID */
	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 ns_id;

		close(pipefd[0]);

		/* Create new network namespace */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get its ID */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &ns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Send ID to parent */
		write(pipefd[1], &ns_id, sizeof(ns_id));
		close(pipefd[1]);

		/* Keep namespace active briefly */
		usleep(100000);
		exit(0);
	}

	/* Parent reads the new namespace ID */
	{
		int bytes;

		close(pipefd[1]);
		bytes = read(pipefd[0], &new_ns_id, sizeof(new_ns_id));
		close(pipefd[0]);

		if (bytes == sizeof(new_ns_id)) {
			__u64 ns_ids_during[100];
			int ret_during;

			TH_LOG("Child created namespace with ID %llu", (unsigned long long)new_ns_id);

			/* List namespaces while child is still alive - should see new one */
			ret_during = sys_listns(&req, ns_ids_during, ARRAY_SIZE(ns_ids_during), 0);
			ASSERT_GE(ret_during, 0);
			TH_LOG("During: %d active network namespaces", ret_during);

			/* Should have more namespaces than before */
			ASSERT_GE(ret_during, ret_before);
		}
	}

	/* Wait for child to exit */
	waitpid(pid, &status, 0);

	/* Give time for namespace to become inactive */
	usleep(100000);

	/* List namespaces after child exits - should not see new one */
	ret_after = sys_listns(&req, ns_ids_after, ARRAY_SIZE(ns_ids_after), 0);
	ASSERT_GE(ret_after, 0);
	TH_LOG("After: %zd active network namespaces", ret_after);

	/* Verify the new namespace ID is not in the after list */
	if (new_ns_id != 0) {
		bool found = false;

		for (ssize_t i = 0; i < ret_after; i++) {
			if (ns_ids_after[i] == new_ns_id) {
				found = true;
				break;
			}
		}
		ASSERT_FALSE(found);
	}
}

/*
 * Test listns() with specific user namespace ID.
 * Create a user namespace and list namespaces it owns.
 */
TEST(listns_specific_userns)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = 0,  /* Will be filled with created userns ID */
	};
	__u64 ns_ids[100];
	int sv[2];
	pid_t pid;
	int status;
	__u64 user_ns_id = 0;
	int bytes;
	ssize_t ret;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 ns_id;
		char buf;

		close(sv[0]);

		/* Create new user namespace */
		if (setup_userns() < 0) {
			close(sv[1]);
			exit(1);
		}

		/* Get user namespace ID */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(sv[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &ns_id) < 0) {
			close(fd);
			close(sv[1]);
			exit(1);
		}
		close(fd);

		/* Send ID to parent */
		if (write(sv[1], &ns_id, sizeof(ns_id)) != sizeof(ns_id)) {
			close(sv[1]);
			exit(1);
		}

		/* Create some namespaces owned by this user namespace */
		unshare(CLONE_NEWNET);
		unshare(CLONE_NEWUTS);

		/* Wait for parent signal */
		if (read(sv[1], &buf, 1) != 1) {
			close(sv[1]);
			exit(1);
		}
		close(sv[1]);
		exit(0);
	}

	/* Parent */
	close(sv[1]);
	bytes = read(sv[0], &user_ns_id, sizeof(user_ns_id));

	if (bytes != sizeof(user_ns_id)) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to get user namespace ID from child");
	}

	TH_LOG("Child created user namespace with ID %llu", (unsigned long long)user_ns_id);

	/* List namespaces owned by this user namespace */
	req.user_ns_id = user_ns_id;
	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

	if (ret < 0) {
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		if (errno == ENOSYS) {
			SKIP(return, "listns() not supported");
		}
		ASSERT_GE(ret, 0);
	}

	TH_LOG("Found %zd namespaces owned by user namespace %llu", ret,
	       (unsigned long long)user_ns_id);

	/* Should find at least the network and UTS namespaces we created */
	if (ret > 0) {
		for (ssize_t i = 0; i < ret && i < 10; i++)
			TH_LOG("  [%zd] ns_id: %llu", i, (unsigned long long)ns_ids[i]);
	}

	/* Signal child to exit */
	if (write(sv[0], "X", 1) != 1) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		ASSERT_TRUE(false);
	}
	close(sv[0]);
	waitpid(pid, &status, 0);
}

/*
 * Test listns() with multiple namespace types filter.
 */
TEST(listns_multiple_types)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWNET | CLONE_NEWUTS,  /* Network and UTS */
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids[100];
	ssize_t ret;

	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (ret < 0) {
		if (errno == ENOSYS)
			SKIP(return, "listns() not supported");
		TH_LOG("listns failed: %s (errno=%d)", strerror(errno), errno);
		ASSERT_TRUE(false);
	}
	ASSERT_GE(ret, 0);

	TH_LOG("Found %zd active network/UTS namespaces", ret);

	for (ssize_t i = 0; i < ret; i++)
		TH_LOG("  [%zd] ns_id: %llu", i, (unsigned long long)ns_ids[i]);
}

/*
 * Test that hierarchical active reference propagation keeps parent
 * user namespaces visible in listns().
 */
TEST(listns_hierarchical_visibility)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWUSER,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 parent_ns_id = 0, child_ns_id = 0;
	int sv[2];
	pid_t pid;
	int status;
	int bytes;
	__u64 ns_ids[100];
	ssize_t ret;
	bool found_parent, found_child;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		char buf;

		close(sv[0]);

		/* Create parent user namespace */
		if (setup_userns() < 0) {
			close(sv[1]);
			exit(1);
		}

		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(sv[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &parent_ns_id) < 0) {
			close(fd);
			close(sv[1]);
			exit(1);
		}
		close(fd);

		/* Create child user namespace */
		if (setup_userns() < 0) {
			close(sv[1]);
			exit(1);
		}

		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(sv[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &child_ns_id) < 0) {
			close(fd);
			close(sv[1]);
			exit(1);
		}
		close(fd);

		/* Send both IDs to parent */
		if (write(sv[1], &parent_ns_id, sizeof(parent_ns_id)) != sizeof(parent_ns_id)) {
			close(sv[1]);
			exit(1);
		}
		if (write(sv[1], &child_ns_id, sizeof(child_ns_id)) != sizeof(child_ns_id)) {
			close(sv[1]);
			exit(1);
		}

		/* Wait for parent signal */
		if (read(sv[1], &buf, 1) != 1) {
			close(sv[1]);
			exit(1);
		}
		close(sv[1]);
		exit(0);
	}

	/* Parent */
	close(sv[1]);

	/* Read both namespace IDs */
	bytes = read(sv[0], &parent_ns_id, sizeof(parent_ns_id));
	bytes += read(sv[0], &child_ns_id, sizeof(child_ns_id));

	if (bytes != (int)(2 * sizeof(__u64))) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		SKIP(return, "Failed to get namespace IDs from child");
	}

	TH_LOG("Parent user namespace ID: %llu", (unsigned long long)parent_ns_id);
	TH_LOG("Child user namespace ID: %llu", (unsigned long long)child_ns_id);

	/* List all user namespaces */
	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

	if (ret < 0 && errno == ENOSYS) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		SKIP(return, "listns() not supported");
	}

	ASSERT_GE(ret, 0);
	TH_LOG("Found %zd active user namespaces", ret);

	/* Both parent and child should be visible (active due to child process) */
	found_parent = false;
	found_child = false;
	for (ssize_t i = 0; i < ret; i++) {
		if (ns_ids[i] == parent_ns_id)
			found_parent = true;
		if (ns_ids[i] == child_ns_id)
			found_child = true;
	}

	TH_LOG("Parent namespace %s, child namespace %s",
	       found_parent ? "found" : "NOT FOUND",
	       found_child ? "found" : "NOT FOUND");

	ASSERT_TRUE(found_child);
	/* With hierarchical propagation, parent should also be active */
	ASSERT_TRUE(found_parent);

	/* Signal child to exit */
	if (write(sv[0], "X", 1) != 1) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		ASSERT_TRUE(false);
	}
	close(sv[0]);
	waitpid(pid, &status, 0);
}

/*
 * Test error cases for listns().
 */
TEST(listns_error_cases)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids[10];
	int ret;

	/* Test with invalid flags */
	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0xFFFF);
	if (errno == ENOSYS) {
		/* listns() not supported, skip this check */
	} else {
		ASSERT_LT(ret, 0);
		ASSERT_EQ(errno, EINVAL);
	}

	/* Test with NULL ns_ids array */
	ret = sys_listns(&req, NULL, 10, 0);
	ASSERT_LT(ret, 0);

	/* Test with invalid spare field */
	req.spare = 1;
	ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
	if (errno == ENOSYS) {
		/* listns() not supported, skip this check */
	} else {
		ASSERT_LT(ret, 0);
		ASSERT_EQ(errno, EINVAL);
	}
	req.spare = 0;

	/* Test with huge nr_ns_ids */
	ret = sys_listns(&req, ns_ids, 2000000, 0);
	if (errno == ENOSYS) {
		/* listns() not supported, skip this check */
	} else {
		ASSERT_LT(ret, 0);
	}
}

TEST_HARNESS_MAIN
