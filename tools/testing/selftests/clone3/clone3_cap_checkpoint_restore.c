// SPDX-License-Identifier: GPL-2.0

/*
 * Based on Christian Brauner's clone3() example.
 * These tests are assuming to be running in the host's
 * PID namespace.
 */

/* capabilities related code based on selftests/bpf/test_verifier.c */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>

#include "../kselftest_harness.h"
#include "clone3_selftests.h"

#ifndef MAX_PID_NS_LEVEL
#define MAX_PID_NS_LEVEL 32
#endif

static void child_exit(int ret)
{
	fflush(stdout);
	fflush(stderr);
	_exit(ret);
}

static int call_clone3_set_tid(struct __test_metadata *_metadata,
			       pid_t *set_tid, size_t set_tid_size)
{
	int status;
	pid_t pid = -1;

	struct __clone_args args = {
		.exit_signal = SIGCHLD,
		.set_tid = ptr_to_u64(set_tid),
		.set_tid_size = set_tid_size,
	};

	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0) {
		TH_LOG("%s - Failed to create new process", strerror(errno));
		return -errno;
	}

	if (pid == 0) {
		int ret;
		char tmp = 0;

		TH_LOG("I am the child, my PID is %d (expected %d)", getpid(), set_tid[0]);

		if (set_tid[0] != getpid())
			child_exit(EXIT_FAILURE);
		child_exit(EXIT_SUCCESS);
	}

	TH_LOG("I am the parent (%d). My child's pid is %d", getpid(), pid);

	if (waitpid(pid, &status, 0) < 0) {
		TH_LOG("Child returned %s", strerror(errno));
		return -errno;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static int test_clone3_set_tid(struct __test_metadata *_metadata,
			       pid_t *set_tid, size_t set_tid_size)
{
	int ret;

	TH_LOG("[%d] Trying clone3() with CLONE_SET_TID to %d", getpid(), set_tid[0]);
	ret = call_clone3_set_tid(_metadata, set_tid, set_tid_size);
	TH_LOG("[%d] clone3() with CLONE_SET_TID %d says:%d", getpid(), set_tid[0], ret);
	return ret;
}

struct libcap {
	struct __user_cap_header_struct hdr;
	struct __user_cap_data_struct data[2];
};

static int set_capability(void)
{
	cap_value_t cap_values[] = { CAP_SETUID, CAP_SETGID };
	struct libcap *cap;
	int ret = -1;
	cap_t caps;

	caps = cap_get_proc();
	if (!caps) {
		perror("cap_get_proc");
		return -1;
	}

	/* Drop all capabilities */
	if (cap_clear(caps)) {
		perror("cap_clear");
		goto out;
	}

	cap_set_flag(caps, CAP_EFFECTIVE, 2, cap_values, CAP_SET);
	cap_set_flag(caps, CAP_PERMITTED, 2, cap_values, CAP_SET);

	cap = (struct libcap *) caps;

	/* 40 -> CAP_CHECKPOINT_RESTORE */
	cap->data[1].effective |= 1 << (40 - 32);
	cap->data[1].permitted |= 1 << (40 - 32);

	if (cap_set_proc(caps)) {
		perror("cap_set_proc");
		goto out;
	}
	ret = 0;
out:
	if (cap_free(caps))
		perror("cap_free");
	return ret;
}

TEST(clone3_cap_checkpoint_restore)
{
	pid_t pid;
	int status;
	int ret = 0;
	pid_t set_tid[1];

	test_clone3_supported();

	EXPECT_EQ(getuid(), 0)
		XFAIL(return, "Skipping all tests as non-root\n");

	memset(&set_tid, 0, sizeof(set_tid));

	/* Find the current active PID */
	pid = fork();
	if (pid == 0) {
		TH_LOG("Child has PID %d", getpid());
		child_exit(EXIT_SUCCESS);
	}
	ASSERT_GT(waitpid(pid, &status, 0), 0)
		TH_LOG("Waiting for child %d failed", pid);

	/* After the child has finished, its PID should be free. */
	set_tid[0] = pid;

	ASSERT_EQ(set_capability(), 0)
		TH_LOG("Could not set CAP_CHECKPOINT_RESTORE");

	ASSERT_EQ(prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0), 0);

	EXPECT_EQ(setgid(65534), 0)
		TH_LOG("Failed to setgid(65534)");
	ASSERT_EQ(setuid(65534), 0);

	set_tid[0] = pid;
	/* This would fail without CAP_CHECKPOINT_RESTORE */
	ASSERT_EQ(test_clone3_set_tid(_metadata, set_tid, 1), -EPERM);
	ASSERT_EQ(set_capability(), 0)
		TH_LOG("Could not set CAP_CHECKPOINT_RESTORE");
	/* This should work as we have CAP_CHECKPOINT_RESTORE as non-root */
	ASSERT_EQ(test_clone3_set_tid(_metadata, set_tid, 1), 0);
}

TEST_HARNESS_MAIN
