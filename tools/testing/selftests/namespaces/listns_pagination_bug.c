// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest_harness.h"
#include "../filesystems/utils.h"
#include "wrappers.h"

/*
 * Minimal test case to reproduce KASAN out-of-bounds in listns pagination.
 *
 * The bug occurs when:
 * 1. Filtering by a specific namespace type (e.g., CLONE_NEWUSER)
 * 2. Using pagination (req.ns_id != 0)
 * 3. The lookup_ns_id_at() call in do_listns() passes ns_type=0 instead of
 *    the filtered type, causing it to search the unified tree and potentially
 *    return a namespace of the wrong type.
 */
TEST(pagination_with_type_filter)
{
	struct ns_id_req req = {
		.size = sizeof(req),
		.spare = 0,
		.ns_id = 0,
		.ns_type = CLONE_NEWUSER,  /* Filter by user namespace */
		.spare2 = 0,
		.user_ns_id = 0,
	};
	pid_t pids[10];
	int num_children = 10;
	int i;
	int sv[2];
	__u64 first_batch[3];
	ssize_t ret;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

	/* Create children with user namespaces */
	for (i = 0; i < num_children; i++) {
		pids[i] = fork();
		ASSERT_GE(pids[i], 0);

		if (pids[i] == 0) {
			char c;
			close(sv[0]);

			if (setup_userns() < 0) {
				close(sv[1]);
				exit(1);
			}

			/* Signal parent we're ready */
			if (write(sv[1], &c, 1) != 1) {
				close(sv[1]);
				exit(1);
			}

			/* Wait for parent signal to exit */
			if (read(sv[1], &c, 1) != 1) {
				close(sv[1]);
				exit(1);
			}

			close(sv[1]);
			exit(0);
		}
	}

	close(sv[1]);

	/* Wait for all children to signal ready */
	for (i = 0; i < num_children; i++) {
		char c;
		if (read(sv[0], &c, 1) != 1) {
			close(sv[0]);
			for (int j = 0; j < num_children; j++)
				kill(pids[j], SIGKILL);
			for (int j = 0; j < num_children; j++)
				waitpid(pids[j], NULL, 0);
			ASSERT_TRUE(false);
		}
	}

	/* First batch - this should work */
	ret = sys_listns(&req, first_batch, 3, 0);
	if (ret < 0) {
		if (errno == ENOSYS) {
			close(sv[0]);
			for (i = 0; i < num_children; i++)
				kill(pids[i], SIGKILL);
			for (i = 0; i < num_children; i++)
				waitpid(pids[i], NULL, 0);
			SKIP(return, "listns() not supported");
		}
		ASSERT_GE(ret, 0);
	}

	TH_LOG("First batch returned %zd entries", ret);

	if (ret == 3) {
		__u64 second_batch[3];

		/* Second batch - pagination triggers the bug */
		req.ns_id = first_batch[2];  /* Continue from last ID */
		ret = sys_listns(&req, second_batch, 3, 0);

		TH_LOG("Second batch returned %zd entries", ret);
		ASSERT_GE(ret, 0);
	}

	/* Signal all children to exit */
	for (i = 0; i < num_children; i++) {
		char c = 'X';
		if (write(sv[0], &c, 1) != 1) {
			close(sv[0]);
			for (int j = i; j < num_children; j++)
				kill(pids[j], SIGKILL);
			for (int j = 0; j < num_children; j++)
				waitpid(pids[j], NULL, 0);
			ASSERT_TRUE(false);
		}
	}

	close(sv[0]);

	/* Cleanup */
	for (i = 0; i < num_children; i++) {
		int status;
		waitpid(pids[i], &status, 0);
	}
}

TEST_HARNESS_MAIN
