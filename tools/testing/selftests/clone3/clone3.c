// SPDX-License-Identifier: GPL-2.0

/* Based on Christian Brauner's clone3() example */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>

#include "../kselftest.h"
#include "clone3_selftests.h"

enum test_mode {
	CLONE3_ARGS_NO_TEST,
	CLONE3_ARGS_ALL_0,
	CLONE3_ARGS_INVAL_EXIT_SIGNAL_BIG,
	CLONE3_ARGS_INVAL_EXIT_SIGNAL_NEG,
	CLONE3_ARGS_INVAL_EXIT_SIGNAL_CSIG,
	CLONE3_ARGS_INVAL_EXIT_SIGNAL_NSIG,
};

static int call_clone3(uint64_t flags, size_t size, enum test_mode test_mode)
{
	struct __clone_args args = {
		.flags = flags,
		.exit_signal = SIGCHLD,
	};

	struct clone_args_extended {
		struct __clone_args args;
		__aligned_u64 excess_space[2];
	} args_ext;

	pid_t pid = -1;
	int status;

	memset(&args_ext, 0, sizeof(args_ext));
	if (size > sizeof(struct __clone_args))
		args_ext.excess_space[1] = 1;

	if (size == 0)
		size = sizeof(struct __clone_args);

	switch (test_mode) {
	case CLONE3_ARGS_NO_TEST:
		/*
		 * Uses default 'flags' and 'SIGCHLD'
		 * assignment.
		 */
		break;
	case CLONE3_ARGS_ALL_0:
		args.flags = 0;
		args.exit_signal = 0;
		break;
	case CLONE3_ARGS_INVAL_EXIT_SIGNAL_BIG:
		args.exit_signal = 0xbadc0ded00000000ULL;
		break;
	case CLONE3_ARGS_INVAL_EXIT_SIGNAL_NEG:
		args.exit_signal = 0x0000000080000000ULL;
		break;
	case CLONE3_ARGS_INVAL_EXIT_SIGNAL_CSIG:
		args.exit_signal = 0x0000000000000100ULL;
		break;
	case CLONE3_ARGS_INVAL_EXIT_SIGNAL_NSIG:
		args.exit_signal = 0x00000000000000f0ULL;
		break;
	}

	memcpy(&args_ext.args, &args, sizeof(struct __clone_args));

	pid = sys_clone3((struct __clone_args *)&args_ext, size);
	if (pid < 0) {
		ksft_print_msg("%s - Failed to create new process\n",
				strerror(errno));
		return -errno;
	}

	if (pid == 0) {
		ksft_print_msg("I am the child, my PID is %d\n", getpid());
		_exit(EXIT_SUCCESS);
	}

	ksft_print_msg("I am the parent (%d). My child's pid is %d\n",
			getpid(), pid);

	if (waitpid(-1, &status, __WALL) < 0) {
		ksft_print_msg("Child returned %s\n", strerror(errno));
		return -errno;
	}
	if (WEXITSTATUS(status))
		return WEXITSTATUS(status);

	return 0;
}

static void test_clone3(uint64_t flags, size_t size, int expected,
		       enum test_mode test_mode)
{
	int ret;

	ksft_print_msg(
		"[%d] Trying clone3() with flags %#" PRIx64 " (size %zu)\n",
		getpid(), flags, size);
	ret = call_clone3(flags, size, test_mode);
	ksft_print_msg("[%d] clone3() with flags says: %d expected %d\n",
			getpid(), ret, expected);
	if (ret != expected)
		ksft_test_result_fail(
			"[%d] Result (%d) is different than expected (%d)\n",
			getpid(), ret, expected);
	else
		ksft_test_result_pass(
			"[%d] Result (%d) matches expectation (%d)\n",
			getpid(), ret, expected);
}

int main(int argc, char *argv[])
{
	uid_t uid = getuid();

	ksft_print_header();
	ksft_set_plan(19);
	test_clone3_supported();

	/* Just a simple clone3() should return 0.*/
	test_clone3(0, 0, 0, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() in a new PID NS.*/
	if (uid == 0)
		test_clone3(CLONE_NEWPID, 0, 0, CLONE3_ARGS_NO_TEST);
	else
		ksft_test_result_skip("Skipping clone3() with CLONE_NEWPID\n");

	/* Do a clone3() with CLONE_ARGS_SIZE_VER0. */
	test_clone3(0, CLONE_ARGS_SIZE_VER0, 0, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with CLONE_ARGS_SIZE_VER0 - 8 */
	test_clone3(0, CLONE_ARGS_SIZE_VER0 - 8, -EINVAL, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with sizeof(struct clone_args) + 8 */
	test_clone3(0, sizeof(struct __clone_args) + 8, 0, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with exit_signal having highest 32 bits non-zero */
	test_clone3(0, 0, -EINVAL, CLONE3_ARGS_INVAL_EXIT_SIGNAL_BIG);

	/* Do a clone3() with negative 32-bit exit_signal */
	test_clone3(0, 0, -EINVAL, CLONE3_ARGS_INVAL_EXIT_SIGNAL_NEG);

	/* Do a clone3() with exit_signal not fitting into CSIGNAL mask */
	test_clone3(0, 0, -EINVAL, CLONE3_ARGS_INVAL_EXIT_SIGNAL_CSIG);

	/* Do a clone3() with NSIG < exit_signal < CSIG */
	test_clone3(0, 0, -EINVAL, CLONE3_ARGS_INVAL_EXIT_SIGNAL_NSIG);

	test_clone3(0, sizeof(struct __clone_args) + 8, 0, CLONE3_ARGS_ALL_0);

	test_clone3(0, sizeof(struct __clone_args) + 16, -E2BIG,
			CLONE3_ARGS_ALL_0);

	test_clone3(0, sizeof(struct __clone_args) * 2, -E2BIG,
			CLONE3_ARGS_ALL_0);

	/* Do a clone3() with > page size */
	test_clone3(0, getpagesize() + 8, -E2BIG, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with CLONE_ARGS_SIZE_VER0 in a new PID NS. */
	if (uid == 0)
		test_clone3(CLONE_NEWPID, CLONE_ARGS_SIZE_VER0, 0,
				CLONE3_ARGS_NO_TEST);
	else
		ksft_test_result_skip("Skipping clone3() with CLONE_NEWPID\n");

	/* Do a clone3() with CLONE_ARGS_SIZE_VER0 - 8 in a new PID NS */
	test_clone3(CLONE_NEWPID, CLONE_ARGS_SIZE_VER0 - 8, -EINVAL,
			CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with sizeof(struct clone_args) + 8 in a new PID NS */
	if (uid == 0)
		test_clone3(CLONE_NEWPID, sizeof(struct __clone_args) + 8, 0,
				CLONE3_ARGS_NO_TEST);
	else
		ksft_test_result_skip("Skipping clone3() with CLONE_NEWPID\n");

	/* Do a clone3() with > page size in a new PID NS */
	test_clone3(CLONE_NEWPID, getpagesize() + 8, -E2BIG,
			CLONE3_ARGS_NO_TEST);

	/* Do a clone3() in a new time namespace */
	test_clone3(CLONE_NEWTIME, 0, 0, CLONE3_ARGS_NO_TEST);

	/* Do a clone3() with exit signal (SIGCHLD) in flags */
	test_clone3(SIGCHLD, 0, -EINVAL, CLONE3_ARGS_NO_TEST);

	ksft_finished();
}
