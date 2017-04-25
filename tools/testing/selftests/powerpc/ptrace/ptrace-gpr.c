/*
 * Ptrace test for GPR/FPR registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "ptrace.h"
#include "ptrace-gpr.h"
#include "reg.h"

/* Tracer and Tracee Shared Data */
int shm_id;
int *cptr, *pptr;

float a = FPR_1;
float b = FPR_2;
float c = FPR_3;

void gpr(void)
{
	unsigned long gpr_buf[18];
	float fpr_buf[32];

	cptr = (int *)shmat(shm_id, NULL, 0);

	asm __volatile__(
		ASM_LOAD_GPR_IMMED(gpr_1)
		ASM_LOAD_FPR_SINGLE_PRECISION(flt_1)
		:
		: [gpr_1]"i"(GPR_1), [flt_1] "r" (&a)
		: "memory", "r6", "r7", "r8", "r9", "r10",
		"r11", "r12", "r13", "r14", "r15", "r16", "r17",
		"r18", "r19", "r20", "r21", "r22", "r23", "r24",
		"r25", "r26", "r27", "r28", "r29", "r30", "r31"
		);

	cptr[1] = 1;

	while (!cptr[0])
		asm volatile("" : : : "memory");

	shmdt((void *)cptr);
	store_gpr(gpr_buf);
	store_fpr_single_precision(fpr_buf);

	if (validate_gpr(gpr_buf, GPR_3))
		exit(1);

	if (validate_fpr_float(fpr_buf, c))
		exit(1);

	exit(0);
}

int trace_gpr(pid_t child)
{
	unsigned long gpr[18];
	unsigned long fpr[32];

	FAIL_IF(start_trace(child));
	FAIL_IF(show_gpr(child, gpr));
	FAIL_IF(validate_gpr(gpr, GPR_1));
	FAIL_IF(show_fpr(child, fpr));
	FAIL_IF(validate_fpr(fpr, FPR_1_REP));
	FAIL_IF(write_gpr(child, GPR_3));
	FAIL_IF(write_fpr(child, FPR_3_REP));
	FAIL_IF(stop_trace(child));

	return TEST_PASS;
}

int ptrace_gpr(void)
{
	pid_t pid;
	int ret, status;

	shm_id = shmget(IPC_PRIVATE, sizeof(int) * 2, 0777|IPC_CREAT);
	pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		return TEST_FAIL;
	}
	if (pid == 0)
		gpr();

	if (pid) {
		pptr = (int *)shmat(shm_id, NULL, 0);
		while (!pptr[1])
			asm volatile("" : : : "memory");

		ret = trace_gpr(pid);
		if (ret) {
			kill(pid, SIGTERM);
			shmdt((void *)pptr);
			shmctl(shm_id, IPC_RMID, NULL);
			return TEST_FAIL;
		}

		pptr[0] = 1;
		shmdt((void *)pptr);

		ret = wait(&status);
		shmctl(shm_id, IPC_RMID, NULL);
		if (ret != pid) {
			printf("Child's exit status not captured\n");
			return TEST_FAIL;
		}

		return (WIFEXITED(status) && WEXITSTATUS(status)) ? TEST_FAIL :
			TEST_PASS;
	}

	return TEST_PASS;
}

int main(int argc, char *argv[])
{
	return test_harness(ptrace_gpr, "ptrace_gpr");
}
