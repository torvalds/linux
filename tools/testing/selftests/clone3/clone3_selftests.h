/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _CLONE3_SELFTESTS_H
#define _CLONE3_SELFTESTS_H

#define _GNU_SOURCE
#include <sched.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <stdint.h>
#include <syscall.h>
#include <sys/wait.h>

#include "../kselftest.h"

#define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

#ifndef __NR_clone3
#define __NR_clone3 -1
#endif

struct __clone_args {
	__aligned_u64 flags;
	__aligned_u64 pidfd;
	__aligned_u64 child_tid;
	__aligned_u64 parent_tid;
	__aligned_u64 exit_signal;
	__aligned_u64 stack;
	__aligned_u64 stack_size;
	__aligned_u64 tls;
	__aligned_u64 set_tid;
	__aligned_u64 set_tid_size;
	__aligned_u64 cgroup;
};

static pid_t sys_clone3(struct __clone_args *args, size_t size)
{
	fflush(stdout);
	fflush(stderr);
	return syscall(__NR_clone3, args, size);
}

static inline void test_clone3_supported(void)
{
	pid_t pid;
	struct __clone_args args = {};

	if (__NR_clone3 < 0)
		ksft_exit_skip("clone3() syscall is not supported\n");

	/* Set to something that will always cause EINVAL. */
	args.exit_signal = -1;
	pid = sys_clone3(&args, sizeof(args));
	if (!pid)
		exit(EXIT_SUCCESS);

	if (pid > 0) {
		wait(NULL);
		ksft_exit_fail_msg(
			"Managed to create child process with invalid exit_signal\n");
	}

	if (errno == ENOSYS)
		ksft_exit_skip("clone3() syscall is not supported\n");

	ksft_print_msg("clone3() syscall supported\n");
}

#endif /* _CLONE3_SELFTESTS_H */
