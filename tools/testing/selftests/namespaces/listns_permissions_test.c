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
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

/*
 * Test that unprivileged users can only see namespaces they're currently in.
 * Create a namespace, drop privileges, verify we can only see our own namespaces.
 */
TEST(listns_unprivileged_current_only)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWNET,
		.spare2 = 0,
		.user_ns_id = 0,
	};
	__u64 ns_ids[100];
	ssize_t ret;
	int pipefd[2];
	pid_t pid;
	int status;
	bool found_ours;
	int unexpected_count;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 our_netns_id;
		bool found_ours;
		int unexpected_count;

		close(pipefd[0]);

		/* Create user namespace to be unprivileged */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Create a network namespace */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get our network namespace ID */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &our_netns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Now we're unprivileged - list all network namespaces */
		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* We should only see our own network namespace */
		found_ours = false;
		unexpected_count = 0;

		for (ssize_t i = 0; i < ret; i++) {
			if (ns_ids[i] == our_netns_id) {
				found_ours = true;
			} else {
				/* This is either init_net (which we can see) or unexpected */
				unexpected_count++;
			}
		}

		/* Send results to parent */
		write(pipefd[1], &found_ours, sizeof(found_ours));
		write(pipefd[1], &unexpected_count, sizeof(unexpected_count));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	found_ours = false;
	unexpected_count = 0;
	read(pipefd[0], &found_ours, sizeof(found_ours));
	read(pipefd[0], &unexpected_count, sizeof(unexpected_count));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	/* Child should have seen its own namespace */
	ASSERT_TRUE(found_ours);

	TH_LOG("Unprivileged child saw its own namespace, plus %d others (likely init_net)",
			unexpected_count);
}

/*
 * Test that users with CAP_SYS_ADMIN in a user namespace can see
 * all namespaces owned by that user namespace.
 */
TEST(listns_cap_sys_admin_in_userns)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = 0,  /* All types */
		.spare2 = 0,
		.user_ns_id = 0,  /* Will be set to our created user namespace */
	};
	__u64 ns_ids[100];
	int pipefd[2];
	pid_t pid;
	int status;
	bool success;
	ssize_t count;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 userns_id;
		ssize_t ret;
		int min_expected;
		bool success;

		close(pipefd[0]);

		/* Create user namespace - we'll have CAP_SYS_ADMIN in it */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get the user namespace ID */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &userns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Create several namespaces owned by this user namespace */
		unshare(CLONE_NEWNET);
		unshare(CLONE_NEWUTS);
		unshare(CLONE_NEWIPC);

		/* List namespaces owned by our user namespace */
		req.user_ns_id = userns_id;
		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);
		if (ret < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/*
		 * We have CAP_SYS_ADMIN in this user namespace,
		 * so we should see all namespaces owned by it.
		 * That includes: net, uts, ipc, and the user namespace itself.
		 */
		min_expected = 4;
		success = (ret >= min_expected);

		write(pipefd[1], &success, sizeof(success));
		write(pipefd[1], &ret, sizeof(ret));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	success = false;
	count = 0;
	read(pipefd[0], &success, sizeof(success));
	read(pipefd[0], &count, sizeof(count));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(success);
	TH_LOG("User with CAP_SYS_ADMIN saw %zd namespaces owned by their user namespace",
			count);
}

/*
 * Test that users cannot see namespaces from unrelated user namespaces.
 * Create two sibling user namespaces, verify they can't see each other's
 * owned namespaces.
 */
TEST(listns_cannot_see_sibling_userns_namespaces)
{
	int pipefd[2];
	pid_t pid1, pid2;
	int status;
	__u64 netns_a_id;
	int pipefd2[2];
	bool found_sibling_netns;

	ASSERT_EQ(pipe(pipefd), 0);

	/* Fork first child - creates user namespace A */
	pid1 = fork();
	ASSERT_GE(pid1, 0);

	if (pid1 == 0) {
		int fd;
		__u64 netns_a_id;
		char buf;

		close(pipefd[0]);

		/* Create user namespace A */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Create network namespace owned by user namespace A */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get network namespace ID */
		fd = open("/proc/self/ns/net", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &netns_a_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Send namespace ID to parent */
		write(pipefd[1], &netns_a_id, sizeof(netns_a_id));

		/* Keep alive for sibling to check */
		read(pipefd[1], &buf, 1);
		close(pipefd[1]);
		exit(0);
	}

	/* Parent reads namespace A ID */
	close(pipefd[1]);
	netns_a_id = 0;
	read(pipefd[0], &netns_a_id, sizeof(netns_a_id));

	TH_LOG("User namespace A created network namespace with ID %llu",
	       (unsigned long long)netns_a_id);

	/* Fork second child - creates user namespace B */
	ASSERT_EQ(pipe(pipefd2), 0);

	pid2 = fork();
	ASSERT_GE(pid2, 0);

	if (pid2 == 0) {
		struct ns_id_req req = {
			.size = sizeof(req),
			.spare = 0,
			.ns_id = 0,
			.ns_type = CLONE_NEWNET,
			.spare2 = 0,
			.user_ns_id = 0,
		};
		__u64 ns_ids[100];
		ssize_t ret;
		bool found_sibling_netns;

		close(pipefd[0]);
		close(pipefd2[0]);

		/* Create user namespace B (sibling to A) */
		if (setup_userns() < 0) {
			close(pipefd2[1]);
			exit(1);
		}

		/* Try to list all network namespaces */
		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

		found_sibling_netns = false;
		if (ret > 0) {
			for (ssize_t i = 0; i < ret; i++) {
				if (ns_ids[i] == netns_a_id) {
					found_sibling_netns = true;
					break;
				}
			}
		}

		/* We should NOT see the sibling's network namespace */
		write(pipefd2[1], &found_sibling_netns, sizeof(found_sibling_netns));
		close(pipefd2[1]);
		exit(0);
	}

	/* Parent reads result from second child */
	close(pipefd2[1]);
	found_sibling_netns = false;
	read(pipefd2[0], &found_sibling_netns, sizeof(found_sibling_netns));
	close(pipefd2[0]);

	/* Signal first child to exit */
	close(pipefd[0]);

	/* Wait for both children */
	waitpid(pid2, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));

	waitpid(pid1, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));

	/* Second child should NOT have seen first child's namespace */
	ASSERT_FALSE(found_sibling_netns);
	TH_LOG("User namespace B correctly could not see sibling namespace A's network namespace");
}

/*
 * Test permission checking with LISTNS_CURRENT_USER.
 * Verify that listing with LISTNS_CURRENT_USER respects permissions.
 */
TEST(listns_current_user_permissions)
{
	int pipefd[2];
	pid_t pid;
	int status;
	bool success;
	ssize_t count;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
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
		bool success;

		close(pipefd[0]);

		/* Create user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Create some namespaces owned by this user namespace */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (unshare(CLONE_NEWUTS) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* List with LISTNS_CURRENT_USER - should see our owned namespaces */
		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

		success = (ret >= 3);  /* At least user, net, uts */
		write(pipefd[1], &success, sizeof(success));
		write(pipefd[1], &ret, sizeof(ret));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	success = false;
	count = 0;
	read(pipefd[0], &success, sizeof(success));
	read(pipefd[0], &count, sizeof(count));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(success);
	TH_LOG("LISTNS_CURRENT_USER returned %zd namespaces", count);
}

TEST_HARNESS_MAIN
