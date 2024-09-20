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
#include "clone3_selftests.h"

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
	struct __clone_args args = {};
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

	ksft_exit_pass();
}
