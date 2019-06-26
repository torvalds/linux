// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test for TAR, PPR, DSCR registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "ptrace-tar.h"

/* Tracer and Tracee Shared Data */
int shm_id;
int *cptr;
int *pptr;

void tar(void)
{
	unsigned long reg[3];
	int ret;

	cptr = (int *)shmat(shm_id, NULL, 0);
	printf("%-30s TAR: %u PPR: %lx DSCR: %u\n",
			user_write, TAR_1, PPR_1, DSCR_1);

	mtspr(SPRN_TAR, TAR_1);
	mtspr(SPRN_PPR, PPR_1);
	mtspr(SPRN_DSCR, DSCR_1);

	cptr[2] = 1;

	/* Wait on parent */
	while (!cptr[0])
		asm volatile("" : : : "memory");

	reg[0] = mfspr(SPRN_TAR);
	reg[1] = mfspr(SPRN_PPR);
	reg[2] = mfspr(SPRN_DSCR);

	printf("%-30s TAR: %lu PPR: %lx DSCR: %lu\n",
			user_read, reg[0], reg[1], reg[2]);

	/* Unblock the parent now */
	cptr[1] = 1;
	shmdt((int *)cptr);

	ret = validate_tar_registers(reg, TAR_2, PPR_2, DSCR_2);
	if (ret)
		exit(1);
	exit(0);
}

int trace_tar(pid_t child)
{
	unsigned long reg[3];

	FAIL_IF(start_trace(child));
	FAIL_IF(show_tar_registers(child, reg));
	printf("%-30s TAR: %lu PPR: %lx DSCR: %lu\n",
			ptrace_read_running, reg[0], reg[1], reg[2]);

	FAIL_IF(validate_tar_registers(reg, TAR_1, PPR_1, DSCR_1));
	FAIL_IF(stop_trace(child));
	return TEST_PASS;
}

int trace_tar_write(pid_t child)
{
	FAIL_IF(start_trace(child));
	FAIL_IF(write_tar_registers(child, TAR_2, PPR_2, DSCR_2));
	printf("%-30s TAR: %u PPR: %lx DSCR: %u\n",
			ptrace_write_running, TAR_2, PPR_2, DSCR_2);

	FAIL_IF(stop_trace(child));
	return TEST_PASS;
}

int ptrace_tar(void)
{
	pid_t pid;
	int ret, status;

	shm_id = shmget(IPC_PRIVATE, sizeof(int) * 3, 0777|IPC_CREAT);
	pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		return TEST_FAIL;
	}

	if (pid == 0)
		tar();

	if (pid) {
		pptr = (int *)shmat(shm_id, NULL, 0);
		pptr[0] = 0;
		pptr[1] = 0;

		while (!pptr[2])
			asm volatile("" : : : "memory");
		ret = trace_tar(pid);
		if (ret)
			return ret;

		ret = trace_tar_write(pid);
		if (ret)
			return ret;

		/* Unblock the child now */
		pptr[0] = 1;

		/* Wait on child */
		while (!pptr[1])
			asm volatile("" : : : "memory");

		shmdt((int *)pptr);

		ret = wait(&status);
		shmctl(shm_id, IPC_RMID, NULL);
		if (ret != pid) {
			printf("Child's exit status not captured\n");
			return TEST_PASS;
		}

		return (WIFEXITED(status) && WEXITSTATUS(status)) ? TEST_FAIL :
			TEST_PASS;
	}
	return TEST_PASS;
}

int main(int argc, char *argv[])
{
	return test_harness(ptrace_tar, "ptrace_tar");
}
