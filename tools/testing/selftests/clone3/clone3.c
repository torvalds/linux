// SPDX-License-Identifier: GPL-2.0

/* Based on Christian Brauner's clone3() example */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <stdbool.h>
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
		ksft_print_msg("waitpid() returned %s\n", strerror(errno));
		return -errno;
	}
	if (!WIFEXITED(status)) {
		ksft_print_msg("Child did not exit normally, status 0x%x\n",
			       status);
		return EXIT_FAILURE;
	}
	if (WEXITSTATUS(status))
		return WEXITSTATUS(status);

	return 0;
}

static bool test_clone3(uint64_t flags, size_t size, int expected,
			enum test_mode test_mode)
{
	int ret;

	ksft_print_msg(
		"[%d] Trying clone3() with flags %#" PRIx64 " (size %zu)\n",
		getpid(), flags, size);
	ret = call_clone3(flags, size, test_mode);
	ksft_print_msg("[%d] clone3() with flags says: %d expected %d\n",
			getpid(), ret, expected);
	if (ret != expected) {
		ksft_print_msg(
			"[%d] Result (%d) is different than expected (%d)\n",
			getpid(), ret, expected);
		return false;
	}

	return true;
}

typedef bool (*filter_function)(void);
typedef size_t (*size_function)(void);

static bool not_root(void)
{
	if (getuid() != 0) {
		ksft_print_msg("Not running as root\n");
		return true;
	}

	return false;
}

static bool no_timenamespace(void)
{
	if (not_root())
		return true;

	if (!access("/proc/self/ns/time", F_OK))
		return false;

	ksft_print_msg("Time namespaces are not supported\n");
	return true;
}

static size_t page_size_plus_8(void)
{
	return getpagesize() + 8;
}

struct test {
	const char *name;
	uint64_t flags;
	size_t size;
	size_function size_function;
	int expected;
	enum test_mode test_mode;
	filter_function filter;
};

static const struct test tests[] = {
	{
		.name = "simple clone3()",
		.flags = 0,
		.size = 0,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "clone3() in a new PID_NS",
		.flags = CLONE_NEWPID,
		.size = 0,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
		.filter = not_root,
	},
	{
		.name = "CLONE_ARGS_SIZE_VER0",
		.flags = 0,
		.size = CLONE_ARGS_SIZE_VER0,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "CLONE_ARGS_SIZE_VER0 - 8",
		.flags = 0,
		.size = CLONE_ARGS_SIZE_VER0 - 8,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "sizeof(struct clone_args) + 8",
		.flags = 0,
		.size = sizeof(struct __clone_args) + 8,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "exit_signal with highest 32 bits non-zero",
		.flags = 0,
		.size = 0,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_INVAL_EXIT_SIGNAL_BIG,
	},
	{
		.name = "negative 32-bit exit_signal",
		.flags = 0,
		.size = 0,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_INVAL_EXIT_SIGNAL_NEG,
	},
	{
		.name = "exit_signal not fitting into CSIGNAL mask",
		.flags = 0,
		.size = 0,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_INVAL_EXIT_SIGNAL_CSIG,
	},
	{
		.name = "NSIG < exit_signal < CSIG",
		.flags = 0,
		.size = 0,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_INVAL_EXIT_SIGNAL_NSIG,
	},
	{
		.name = "Arguments sizeof(struct clone_args) + 8",
		.flags = 0,
		.size = sizeof(struct __clone_args) + 8,
		.expected = 0,
		.test_mode = CLONE3_ARGS_ALL_0,
	},
	{
		.name = "Arguments sizeof(struct clone_args) + 16",
		.flags = 0,
		.size = sizeof(struct __clone_args) + 16,
		.expected = -E2BIG,
		.test_mode = CLONE3_ARGS_ALL_0,
	},
	{
		.name = "Arguments sizeof(struct clone_arg) * 2",
		.flags = 0,
		.size = sizeof(struct __clone_args) + 16,
		.expected = -E2BIG,
		.test_mode = CLONE3_ARGS_ALL_0,
	},
	{
		.name = "Arguments > page size",
		.flags = 0,
		.size_function = page_size_plus_8,
		.expected = -E2BIG,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "CLONE_ARGS_SIZE_VER0 in a new PID NS",
		.flags = CLONE_NEWPID,
		.size = CLONE_ARGS_SIZE_VER0,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
		.filter = not_root,
	},
	{
		.name = "CLONE_ARGS_SIZE_VER0 - 8 in a new PID NS",
		.flags = CLONE_NEWPID,
		.size = CLONE_ARGS_SIZE_VER0 - 8,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "sizeof(struct clone_args) + 8 in a new PID NS",
		.flags = CLONE_NEWPID,
		.size = sizeof(struct __clone_args) + 8,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
		.filter = not_root,
	},
	{
		.name = "Arguments > page size in a new PID NS",
		.flags = CLONE_NEWPID,
		.size_function = page_size_plus_8,
		.expected = -E2BIG,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
	{
		.name = "New time NS",
		.flags = CLONE_NEWTIME,
		.size = 0,
		.expected = 0,
		.test_mode = CLONE3_ARGS_NO_TEST,
		.filter = no_timenamespace,
	},
	{
		.name = "exit signal (SIGCHLD) in flags",
		.flags = SIGCHLD,
		.size = 0,
		.expected = -EINVAL,
		.test_mode = CLONE3_ARGS_NO_TEST,
	},
};

int main(int argc, char *argv[])
{
	size_t size;
	int i;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests));
	test_clone3_supported();

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (tests[i].filter && tests[i].filter()) {
			ksft_test_result_skip("%s\n", tests[i].name);
			continue;
		}

		if (tests[i].size_function)
			size = tests[i].size_function();
		else
			size = tests[i].size;

		ksft_print_msg("Running test '%s'\n", tests[i].name);

		ksft_test_result(test_clone3(tests[i].flags, size,
					     tests[i].expected,
					     tests[i].test_mode),
				 "%s\n", tests[i].name);
	}

	ksft_finished();
}
