/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This test attempts to see if the VMX registers change across a syscall (fork).
 */

#include <altivec.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "utils.h"

vector int varray[] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10,11,12},
	{13,14,15,16},{17,18,19,20},{21,22,23,24},
	{25,26,27,28},{29,30,31,32},{33,34,35,36},
	{37,38,39,40},{41,42,43,44},{45,46,47,48}};

extern int test_vmx(vector int *varray, pid_t *pid);

int vmx_syscall(void)
{
	pid_t fork_pid;
	int i;
	int ret;
	int child_ret;
	for (i = 0; i < 1000; i++) {
		/* test_vmx will fork() */
		ret = test_vmx(varray, &fork_pid);
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

int test_vmx_syscall(void)
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
	ret = vmx_syscall();
	/* Can't FAIL_IF(pid2 == -1); because we've already forked */
	if (pid2 == -1) {
		/*
		 * Couldn't fork, ensure child_ret is set and is a fail
		 */
		ret = child_ret = 1;
	} else {
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
	return test_harness(test_vmx_syscall, "vmx_syscall");

}
