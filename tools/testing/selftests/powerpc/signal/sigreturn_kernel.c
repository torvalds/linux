// SPDX-License-Identifier: GPL-2.0
/*
 * Test that we can't sigreturn to kernel addresses, or to kernel mode.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define MSR_PR (1ul << 14)

static volatile unsigned long long sigreturn_addr;
static volatile unsigned long long sigreturn_msr_mask;

static void sigusr1_handler(int signo, siginfo_t *si, void *uc_ptr)
{
	ucontext_t *uc = (ucontext_t *)uc_ptr;

	if (sigreturn_addr)
		UCONTEXT_NIA(uc) = sigreturn_addr;

	if (sigreturn_msr_mask)
		UCONTEXT_MSR(uc) &= sigreturn_msr_mask;
}

static pid_t fork_child(void)
{
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		raise(SIGUSR1);
		exit(0);
	}

	return pid;
}

static int expect_segv(pid_t pid)
{
	int child_ret;

	waitpid(pid, &child_ret, 0);
	FAIL_IF(WIFEXITED(child_ret));
	FAIL_IF(!WIFSIGNALED(child_ret));
	FAIL_IF(WTERMSIG(child_ret) != 11);

	return 0;
}

int test_sigreturn_kernel(void)
{
	struct sigaction act;
	int child_ret, i;
	pid_t pid;

	act.sa_sigaction = sigusr1_handler;
	act.sa_flags = SA_SIGINFO;
	sigemptyset(&act.sa_mask);

	FAIL_IF(sigaction(SIGUSR1, &act, NULL));

	for (i = 0; i < 2; i++) {
		// Return to kernel
		sigreturn_addr = 0xcull << 60;
		pid = fork_child();
		expect_segv(pid);

		// Return to kernel virtual
		sigreturn_addr = 0xc008ull << 48;
		pid = fork_child();
		expect_segv(pid);

		// Return out of range
		sigreturn_addr = 0xc010ull << 48;
		pid = fork_child();
		expect_segv(pid);

		// Return to no-man's land, just below PAGE_OFFSET
		sigreturn_addr = (0xcull << 60) - (64 * 1024);
		pid = fork_child();
		expect_segv(pid);

		// Return to no-man's land, above TASK_SIZE_4PB
		sigreturn_addr = 0x1ull << 52;
		pid = fork_child();
		expect_segv(pid);

		// Return to 0xd space
		sigreturn_addr = 0xdull << 60;
		pid = fork_child();
		expect_segv(pid);

		// Return to 0xe space
		sigreturn_addr = 0xeull << 60;
		pid = fork_child();
		expect_segv(pid);

		// Return to 0xf space
		sigreturn_addr = 0xfull << 60;
		pid = fork_child();
		expect_segv(pid);

		// Attempt to set PR=0 for 2nd loop (should be blocked by kernel)
		sigreturn_msr_mask = ~MSR_PR;
	}

	printf("All children killed as expected\n");

	// Don't change address, just MSR, should return to user as normal
	sigreturn_addr = 0;
	sigreturn_msr_mask = ~MSR_PR;
	pid = fork_child();
	waitpid(pid, &child_ret, 0);
	FAIL_IF(!WIFEXITED(child_ret));
	FAIL_IF(WIFSIGNALED(child_ret));
	FAIL_IF(WEXITSTATUS(child_ret) != 0);

	return 0;
}

int main(void)
{
	return test_harness(test_sigreturn_kernel, "sigreturn_kernel");
}
