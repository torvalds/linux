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

/*
 * Test that CAP_SYS_ADMIN in parent user namespace allows seeing
 * child user namespace's owned namespaces.
 */
TEST(listns_parent_userns_cap_sys_admin)
{
	int pipefd[2];
	pid_t pid;
	int status;
	bool found_child_userns;
	ssize_t count;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 parent_userns_id;
		__u64 child_userns_id;
		struct ns_id_req req;
		__u64 ns_ids[100];
		ssize_t ret;
		bool found_child_userns;

		close(pipefd[0]);

		/* Create parent user namespace - we have CAP_SYS_ADMIN in it */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get parent user namespace ID */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &parent_userns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Create child user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get child user namespace ID */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &child_userns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* Create namespaces owned by child user namespace */
		if (unshare(CLONE_NEWNET) < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* List namespaces owned by parent user namespace */
		req.size = sizeof(req);
		req.spare = 0;
		req.ns_id = 0;
		req.ns_type = 0;
		req.spare2 = 0;
		req.user_ns_id = parent_userns_id;

		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

		/* Should see child user namespace in the list */
		found_child_userns = false;
		if (ret > 0) {
			for (ssize_t i = 0; i < ret; i++) {
				if (ns_ids[i] == child_userns_id) {
					found_child_userns = true;
					break;
				}
			}
		}

		write(pipefd[1], &found_child_userns, sizeof(found_child_userns));
		write(pipefd[1], &ret, sizeof(ret));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	found_child_userns = false;
	count = 0;
	read(pipefd[0], &found_child_userns, sizeof(found_child_userns));
	read(pipefd[0], &count, sizeof(count));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(found_child_userns);
	TH_LOG("Process with CAP_SYS_ADMIN in parent user namespace saw child user namespace (total: %zd)",
			count);
}

/*
 * Test that we can see user namespaces we have CAP_SYS_ADMIN inside of.
 * This is different from seeing namespaces owned by a user namespace.
 */
TEST(listns_cap_sys_admin_inside_userns)
{
	int pipefd[2];
	pid_t pid;
	int status;
	bool found_ours;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int fd;
		__u64 our_userns_id;
		struct ns_id_req req;
		__u64 ns_ids[100];
		ssize_t ret;
		bool found_ours;

		close(pipefd[0]);

		/* Create user namespace - we have CAP_SYS_ADMIN inside it */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Get our user namespace ID */
		fd = open("/proc/self/ns/user", O_RDONLY);
		if (fd < 0) {
			close(pipefd[1]);
			exit(1);
		}

		if (ioctl(fd, NS_GET_ID, &our_userns_id) < 0) {
			close(fd);
			close(pipefd[1]);
			exit(1);
		}
		close(fd);

		/* List all user namespaces globally */
		req.size = sizeof(req);
		req.spare = 0;
		req.ns_id = 0;
		req.ns_type = CLONE_NEWUSER;
		req.spare2 = 0;
		req.user_ns_id = 0;

		ret = sys_listns(&req, ns_ids, ARRAY_SIZE(ns_ids), 0);

		/* We should be able to see our own user namespace */
		found_ours = false;
		if (ret > 0) {
			for (ssize_t i = 0; i < ret; i++) {
				if (ns_ids[i] == our_userns_id) {
					found_ours = true;
					break;
				}
			}
		}

		write(pipefd[1], &found_ours, sizeof(found_ours));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	found_ours = false;
	read(pipefd[0], &found_ours, sizeof(found_ours));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(found_ours);
	TH_LOG("Process can see user namespace it has CAP_SYS_ADMIN inside of");
}

/*
 * Test that dropping CAP_SYS_ADMIN restricts what we can see.
 */
TEST(listns_drop_cap_sys_admin)
{
	cap_t caps;
	cap_value_t cap_list[1] = { CAP_SYS_ADMIN };

	/* This test needs to start with CAP_SYS_ADMIN */
	caps = cap_get_proc();
	if (!caps) {
		SKIP(return, "Cannot get capabilities");
	}

	cap_flag_value_t cap_val;
	if (cap_get_flag(caps, CAP_SYS_ADMIN, CAP_EFFECTIVE, &cap_val) < 0) {
		cap_free(caps);
		SKIP(return, "Cannot check CAP_SYS_ADMIN");
	}

	if (cap_val != CAP_SET) {
		cap_free(caps);
		SKIP(return, "Test needs CAP_SYS_ADMIN to start");
	}
	cap_free(caps);

	int pipefd[2];
	pid_t pid;
	int status;
	bool correct;
	ssize_t count_before, count_after;

	ASSERT_EQ(pipe(pipefd), 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		struct ns_id_req req = {
			.size = sizeof(req),
			.spare = 0,
			.ns_id = 0,
			.ns_type = CLONE_NEWNET,
			.spare2 = 0,
			.user_ns_id = LISTNS_CURRENT_USER,
		};
		__u64 ns_ids_before[100];
		ssize_t count_before;
		__u64 ns_ids_after[100];
		ssize_t count_after;
		bool correct;

		close(pipefd[0]);

		/* Create user namespace */
		if (setup_userns() < 0) {
			close(pipefd[1]);
			exit(1);
		}

		/* Count namespaces with CAP_SYS_ADMIN */
		count_before = sys_listns(&req, ns_ids_before, ARRAY_SIZE(ns_ids_before), 0);

		/* Drop CAP_SYS_ADMIN */
		caps = cap_get_proc();
		if (caps) {
			cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_CLEAR);
			cap_set_flag(caps, CAP_PERMITTED, 1, cap_list, CAP_CLEAR);
			cap_set_proc(caps);
			cap_free(caps);
		}

		/* Ensure we can't regain the capability */
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

		/* Count namespaces without CAP_SYS_ADMIN */
		count_after = sys_listns(&req, ns_ids_after, ARRAY_SIZE(ns_ids_after), 0);

		/* Without CAP_SYS_ADMIN, we should see same or fewer namespaces */
		correct = (count_after <= count_before);

		write(pipefd[1], &correct, sizeof(correct));
		write(pipefd[1], &count_before, sizeof(count_before));
		write(pipefd[1], &count_after, sizeof(count_after));
		close(pipefd[1]);
		exit(0);
	}

	/* Parent */
	close(pipefd[1]);

	correct = false;
	count_before = 0;
	count_after = 0;
	read(pipefd[0], &correct, sizeof(correct));
	read(pipefd[0], &count_before, sizeof(count_before));
	read(pipefd[0], &count_after, sizeof(count_after));
	close(pipefd[0]);

	waitpid(pid, &status, 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(WEXITSTATUS(status), 0);

	ASSERT_TRUE(correct);
	TH_LOG("With CAP_SYS_ADMIN: %zd namespaces, without: %zd namespaces",
			count_before, count_after);
}

TEST_HARNESS_MAIN
