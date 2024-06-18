// SPDX-License-Identifier: GPL-2.0

/*
 * Based on Christian Brauner's clone3() example.
 * These tests are assuming to be running in the host's
 * PID namespace.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>

#include "../kselftest.h"
#include "clone3_selftests.h"

#define MAX_PID_NS_LEVEL 32

static int pipe_1[2];
static int pipe_2[2];

static void child_exit(int ret)
{
	fflush(stdout);
	fflush(stderr);
	_exit(ret);
}

static int call_clone3_set_tid(pid_t *set_tid,
			       size_t set_tid_size,
			       int flags,
			       int expected_pid,
			       bool wait_for_it)
{
	int status;
	pid_t pid = -1;

	struct __clone_args args = {
		.flags = flags,
		.exit_signal = SIGCHLD,
		.set_tid = ptr_to_u64(set_tid),
		.set_tid_size = set_tid_size,
	};

	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0) {
		ksft_print_msg("%s - Failed to create new process\n",
			       strerror(errno));
		return -errno;
	}

	if (pid == 0) {
		int ret;
		char tmp = 0;
		int exit_code = EXIT_SUCCESS;

		ksft_print_msg("I am the child, my PID is %d (expected %d)\n",
			       getpid(), set_tid[0]);
		if (wait_for_it) {
			ksft_print_msg("[%d] Child is ready and waiting\n",
				       getpid());

			/* Signal the parent that the child is ready */
			close(pipe_1[0]);
			ret = write(pipe_1[1], &tmp, 1);
			if (ret != 1) {
				ksft_print_msg(
					"Writing to pipe returned %d", ret);
				exit_code = EXIT_FAILURE;
			}
			close(pipe_1[1]);
			close(pipe_2[1]);
			ret = read(pipe_2[0], &tmp, 1);
			if (ret != 1) {
				ksft_print_msg(
					"Reading from pipe returned %d", ret);
				exit_code = EXIT_FAILURE;
			}
			close(pipe_2[0]);
		}

		if (set_tid[0] != getpid())
			child_exit(EXIT_FAILURE);
		child_exit(exit_code);
	}

	if (expected_pid == 0 || expected_pid == pid) {
		ksft_print_msg("I am the parent (%d). My child's pid is %d\n",
			       getpid(), pid);
	} else {
		ksft_print_msg(
			"Expected child pid %d does not match actual pid %d\n",
			expected_pid, pid);
		return -1;
	}

	if (waitpid(pid, &status, 0) < 0) {
		ksft_print_msg("Child returned %s\n", strerror(errno));
		return -errno;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static void test_clone3_set_tid(const char *desc,
				pid_t *set_tid,
				size_t set_tid_size,
				int flags,
				int expected,
				int expected_pid,
				bool wait_for_it)
{
	int ret;

	ksft_print_msg(
		"[%d] Trying clone3() with CLONE_SET_TID to %d and 0x%x\n",
		getpid(), set_tid[0], flags);
	ret = call_clone3_set_tid(set_tid, set_tid_size, flags, expected_pid,
				  wait_for_it);
	ksft_print_msg(
		"[%d] clone3() with CLONE_SET_TID %d says: %d - expected %d\n",
		getpid(), set_tid[0], ret, expected);

	ksft_test_result(ret == expected, "%s with %zu TIDs and flags 0x%x\n",
			 desc, set_tid_size, flags);
}

int main(int argc, char *argv[])
{
	FILE *f;
	char buf;
	char *line;
	int status;
	int ret = -1;
	size_t len = 0;
	int pid_max = 0;
	uid_t uid = getuid();
	char proc_path[100] = {0};
	pid_t pid, ns1, ns2, ns3, ns_pid;
	pid_t set_tid[MAX_PID_NS_LEVEL * 2];

	ksft_print_header();
	ksft_set_plan(29);
	test_clone3_supported();

	if (pipe(pipe_1) < 0 || pipe(pipe_2) < 0)
		ksft_exit_fail_msg("pipe() failed\n");

	f = fopen("/proc/sys/kernel/pid_max", "r");
	if (f == NULL)
		ksft_exit_fail_msg(
			"%s - Could not open /proc/sys/kernel/pid_max\n",
			strerror(errno));
	fscanf(f, "%d", &pid_max);
	fclose(f);
	ksft_print_msg("/proc/sys/kernel/pid_max %d\n", pid_max);

	/* Try invalid settings */
	memset(&set_tid, 0, sizeof(set_tid));
	test_clone3_set_tid("invalid size, 0 TID",
			    set_tid, MAX_PID_NS_LEVEL + 1, 0, -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, 0 TID",
			    set_tid, MAX_PID_NS_LEVEL * 2, 0, -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, 0 TID",
			    set_tid, MAX_PID_NS_LEVEL * 2 + 1, 0,
			    -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, 0 TID",
			    set_tid, MAX_PID_NS_LEVEL * 42, 0, -EINVAL, 0, 0);

	/*
	 * This can actually work if this test running in a MAX_PID_NS_LEVEL - 1
	 * nested PID namespace.
	 */
	test_clone3_set_tid("invalid size, 0 TID",
			    set_tid, MAX_PID_NS_LEVEL - 1, 0, -EINVAL, 0, 0);

	memset(&set_tid, 0xff, sizeof(set_tid));
	test_clone3_set_tid("invalid size, TID all 1s",
			    set_tid, MAX_PID_NS_LEVEL + 1, 0, -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, TID all 1s",
			    set_tid, MAX_PID_NS_LEVEL * 2, 0, -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, TID all 1s",
			    set_tid, MAX_PID_NS_LEVEL * 2 + 1, 0,
			    -EINVAL, 0, 0);

	test_clone3_set_tid("invalid size, TID all 1s",
			    set_tid, MAX_PID_NS_LEVEL * 42, 0, -EINVAL, 0, 0);

	/*
	 * This can actually work if this test running in a MAX_PID_NS_LEVEL - 1
	 * nested PID namespace.
	 */
	test_clone3_set_tid("invalid size, TID all 1s",
			    set_tid, MAX_PID_NS_LEVEL - 1, 0, -EINVAL, 0, 0);

	memset(&set_tid, 0, sizeof(set_tid));
	/* Try with an invalid PID */
	set_tid[0] = 0;
	test_clone3_set_tid("valid size, 0 TID",
			    set_tid, 1, 0, -EINVAL, 0, 0);

	set_tid[0] = -1;
	test_clone3_set_tid("valid size, -1 TID",
			    set_tid, 1, 0, -EINVAL, 0, 0);

	/* Claim that the set_tid array actually contains 2 elements. */
	test_clone3_set_tid("2 TIDs, -1 and 0",
			    set_tid, 2, 0, -EINVAL, 0, 0);

	/* Try it in a new PID namespace */
	if (uid == 0)
		test_clone3_set_tid("valid size, -1 TID",
				    set_tid, 1, CLONE_NEWPID, -EINVAL, 0, 0);
	else
		ksft_test_result_skip("Clone3() with set_tid requires root\n");

	/* Try with a valid PID (1) this should return -EEXIST. */
	set_tid[0] = 1;
	if (uid == 0)
		test_clone3_set_tid("duplicate PID 1",
				    set_tid, 1, 0, -EEXIST, 0, 0);
	else
		ksft_test_result_skip("Clone3() with set_tid requires root\n");

	/* Try it in a new PID namespace */
	if (uid == 0)
		test_clone3_set_tid("duplicate PID 1",
				    set_tid, 1, CLONE_NEWPID, 0, 0, 0);
	else
		ksft_test_result_skip("Clone3() with set_tid requires root\n");

	/* pid_max should fail everywhere */
	set_tid[0] = pid_max;
	test_clone3_set_tid("set TID to maximum",
			    set_tid, 1, 0, -EINVAL, 0, 0);

	if (uid == 0)
		test_clone3_set_tid("set TID to maximum",
				    set_tid, 1, CLONE_NEWPID, -EINVAL, 0, 0);
	else
		ksft_test_result_skip("Clone3() with set_tid requires root\n");

	if (uid != 0) {
		/*
		 * All remaining tests require root. Tell the framework
		 * that all those tests are skipped as non-root.
		 */
		ksft_cnt.ksft_xskip += ksft_plan - ksft_test_num();
		goto out;
	}

	/* Find the current active PID */
	pid = fork();
	if (pid == 0) {
		ksft_print_msg("Child has PID %d\n", getpid());
		child_exit(EXIT_SUCCESS);
	}
	if (waitpid(pid, &status, 0) < 0)
		ksft_exit_fail_msg("Waiting for child %d failed", pid);

	/* After the child has finished, its PID should be free. */
	set_tid[0] = pid;
	test_clone3_set_tid("reallocate child TID",
			    set_tid, 1, 0, 0, 0, 0);

	/* This should fail as there is no PID 1 in that namespace */
	test_clone3_set_tid("duplicate child TID",
			    set_tid, 1, CLONE_NEWPID, -EINVAL, 0, 0);

	/*
	 * Creating a process with PID 1 in the newly created most nested
	 * PID namespace and PID 'pid' in the parent PID namespace. This
	 * needs to work.
	 */
	set_tid[0] = 1;
	set_tid[1] = pid;
	test_clone3_set_tid("create PID 1 in new NS",
			    set_tid, 2, CLONE_NEWPID, 0, pid, 0);

	ksft_print_msg("unshare PID namespace\n");
	if (unshare(CLONE_NEWPID) == -1)
		ksft_exit_fail_msg("unshare(CLONE_NEWPID) failed: %s\n",
				strerror(errno));

	set_tid[0] = pid;

	/* This should fail as there is no PID 1 in that namespace */
	test_clone3_set_tid("duplicate PID 1",
			    set_tid, 1, 0, -EINVAL, 0, 0);

	/* Let's create a PID 1 */
	ns_pid = fork();
	if (ns_pid == 0) {
		/*
		 * This and the next test cases check that all pid-s are
		 * released on error paths.
		 */
		set_tid[0] = 43;
		set_tid[1] = -1;
		test_clone3_set_tid("check leak on invalid TID -1",
				    set_tid, 2, 0, -EINVAL, 0, 0);

		set_tid[0] = 43;
		set_tid[1] = pid;
		test_clone3_set_tid("check leak on invalid specific TID",
				    set_tid, 2, 0, 0, 43, 0);

		ksft_print_msg("Child in PID namespace has PID %d\n", getpid());
		set_tid[0] = 2;
		test_clone3_set_tid("create PID 2 in child NS",
				    set_tid, 1, 0, 0, 2, 0);

		set_tid[0] = 1;
		set_tid[1] = -1;
		set_tid[2] = pid;
		/* This should fail as there is invalid PID at level '1'. */
		test_clone3_set_tid("fail due to invalid TID at level 1",
				    set_tid, 3, CLONE_NEWPID, -EINVAL, 0, 0);

		set_tid[0] = 1;
		set_tid[1] = 42;
		set_tid[2] = pid;
		/*
		 * This should fail as there are not enough active PID
		 * namespaces. Again assuming this is running in the host's
		 * PID namespace. Not yet nested.
		 */
		test_clone3_set_tid("fail due to too few active PID NSs",
				    set_tid, 4, CLONE_NEWPID, -EINVAL, 0, 0);

		/*
		 * This should work and from the parent we should see
		 * something like 'NSpid:	pid	42	1'.
		 */
		test_clone3_set_tid("verify that we have 3 PID NSs",
				    set_tid, 3, CLONE_NEWPID, 0, 42, true);

		child_exit(ksft_cnt.ksft_fail);
	}

	close(pipe_1[1]);
	close(pipe_2[0]);
	while (read(pipe_1[0], &buf, 1) > 0) {
		ksft_print_msg("[%d] Child is ready and waiting\n", getpid());
		break;
	}

	snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", pid);
	f = fopen(proc_path, "r");
	if (f == NULL)
		ksft_exit_fail_msg(
			"%s - Could not open %s\n",
			strerror(errno), proc_path);

	while (getline(&line, &len, f) != -1) {
		if (strstr(line, "NSpid")) {
			int i;

			/* Verify that all generated PIDs are as expected. */
			i = sscanf(line, "NSpid:\t%d\t%d\t%d",
				   &ns3, &ns2, &ns1);
			if (i != 3) {
				ksft_print_msg(
					"Unexpected 'NSPid:' entry: %s",
					line);
				ns1 = ns2 = ns3 = 0;
			}
			break;
		}
	}
	fclose(f);
	free(line);
	close(pipe_2[0]);

	/* Tell the clone3()'d child to finish. */
	write(pipe_2[1], &buf, 1);
	close(pipe_2[1]);

	if (waitpid(ns_pid, &status, 0) < 0) {
		ksft_print_msg("Child returned %s\n", strerror(errno));
		ret = -errno;
		goto out;
	}

	if (!WIFEXITED(status))
		ksft_test_result_fail("Child error\n");

	ksft_cnt.ksft_pass += 6 - (ksft_cnt.ksft_fail - WEXITSTATUS(status));
	ksft_cnt.ksft_fail = WEXITSTATUS(status);

	ksft_print_msg("Expecting PIDs %d, 42, 1\n", pid);
	ksft_print_msg("Have PIDs in namespaces: %d, %d, %d\n", ns3, ns2, ns1);
	ksft_test_result(ns3 == pid && ns2 == 42 && ns1 == 1,
			 "PIDs in all namespaces as expected\n");
out:
	ret = 0;

	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
}
