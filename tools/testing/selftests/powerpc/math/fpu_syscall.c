// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This test attempts to see if the FPU registers change across a syscall (fork).
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "utils.h"

extern int test_fpu(double *darray, pid_t *pid);

double darray[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0,
		     1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0,
		     2.1};

int syscall_fpu(void)
{
	pid_t fork_pid;
	int i;
	int ret;
	int child_ret;
	for (i = 0; i < 1000; i++) {
		/* test_fpu will fork() */
		ret = test_fpu(darray, &fork_pid);
		if (fork_pid == -1)
			return -1;
		if (fork_pid == 0)
			exit(ret);
		waitpid(fork_pid, &child_ret, 0);
		if (ret || child_ret)
			return 1;
	}

	return 0;
}

int test_syscall_fpu(void)
{
	/*
	 * Setup an environment with much context switching
	 */
	pid_t pid2;
	pid_t pid = fork();
	int ret;
	int child_ret;
	FAIL_IF(pid == -1);

	pid2 = fork();
	/* Can't FAIL_IF(pid2 == -1); because already forked once */
	if (pid2 == -1) {
		/*
		 * Couldn't fork, ensure test is a fail
		 */
		child_ret = ret = 1;
	} else {
		ret = syscall_fpu();
		if (pid2)
			waitpid(pid2, &child_ret, 0);
		else
			exit(ret);
	}

	ret |= child_ret;

	if (pid)
		waitpid(pid, &child_ret, 0);
	else
		exit(ret);

	FAIL_IF(ret || child_ret);
	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_syscall_fpu, "syscall_fpu");

}
