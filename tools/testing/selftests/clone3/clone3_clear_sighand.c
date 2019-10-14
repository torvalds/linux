/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "../kselftest.h"

#ifndef CLONE_CLEAR_SIGHAND
#define CLONE_CLEAR_SIGHAND 0x100000000ULL
#endif

#ifndef __NR_clone3
#define __NR_clone3 -1
struct clone_args {
	__aligned_u64 flags;
	__aligned_u64 pidfd;
	__aligned_u64 child_tid;
	__aligned_u64 parent_tid;
	__aligned_u64 exit_signal;
	__aligned_u64 stack;
	__aligned_u64 stack_size;
	__aligned_u64 tls;
};
#endif

static pid_t sys_clone3(struct clone_args *args, size_t size)
{
	return syscall(__NR_clone3, args, size);
}

static void test_clone3_supported(void)
{
	pid_t pid;
	struct clone_args args = {};

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

static void nop_handler(int signo)
{
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static void test_clone3_clear_sighand(void)
{
	int ret;
	pid_t pid;
	struct clone_args args = {};
	struct sigaction act;

	/*
	 * Check that CLONE_CLEAR_SIGHAND and CLONE_SIGHAND are mutually
	 * exclusive.
	 */
	args.flags |= CLONE_CLEAR_SIGHAND | CLONE_SIGHAND;
	args.exit_signal = SIGCHLD;
	pid = sys_clone3(&args, sizeof(args));
	if (pid > 0)
		ksft_exit_fail_msg(
			"clone3(CLONE_CLEAR_SIGHAND | CLONE_SIGHAND) succeeded\n");

	act.sa_handler = nop_handler;
	ret = sigemptyset(&act.sa_mask);
	if (ret < 0)
		ksft_exit_fail_msg("%s - sigemptyset() failed\n",
				   strerror(errno));

	act.sa_flags = 0;

	/* Register signal handler for SIGUSR1 */
	ret = sigaction(SIGUSR1, &act, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s - sigaction(SIGUSR1, &act, NULL) failed\n",
			strerror(errno));

	/* Register signal handler for SIGUSR2 */
	ret = sigaction(SIGUSR2, &act, NULL);
	if (ret < 0)
		ksft_exit_fail_msg(
			"%s - sigaction(SIGUSR2, &act, NULL) failed\n",
			strerror(errno));

	/* Check that CLONE_CLEAR_SIGHAND works. */
	args.flags = CLONE_CLEAR_SIGHAND;
	pid = sys_clone3(&args, sizeof(args));
	if (pid < 0)
		ksft_exit_fail_msg("%s - clone3(CLONE_CLEAR_SIGHAND) failed\n",
				   strerror(errno));

	if (pid == 0) {
		ret = sigaction(SIGUSR1, NULL, &act);
		if (ret < 0)
			exit(EXIT_FAILURE);

		if (act.sa_handler != SIG_DFL)
			exit(EXIT_FAILURE);

		ret = sigaction(SIGUSR2, NULL, &act);
		if (ret < 0)
			exit(EXIT_FAILURE);

		if (act.sa_handler != SIG_DFL)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	ret = wait_for_pid(pid);
	if (ret)
		ksft_exit_fail_msg(
			"Failed to clear signal handler for child process\n");

	ksft_test_result_pass("Cleared signal handlers for child process\n");
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(1);

	test_clone3_supported();
	test_clone3_clear_sighand();

	return ksft_exit_pass();
}
