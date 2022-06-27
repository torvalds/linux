// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test for GPR/FPR registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "ptrace-gpr.h"
#include "reg.h"

/* Tracer and Tracee Shared Data */
int shm_id;
int *cptr, *pptr;

double a = FPR_1;
double b = FPR_2;
double c = FPR_3;

extern void gpr_child_loop(int *read_flag, int *write_flag,
			   unsigned long *gpr_buf, double *fpr_buf);

static int child(void)
{
	unsigned long gpr_buf[32];
	double fpr_buf[32];
	int i;

	cptr = (int *)shmat(shm_id, NULL, 0);
	memset(gpr_buf, 0, sizeof(gpr_buf));
	memset(fpr_buf, 0, sizeof(fpr_buf));

	for (i = 0; i < 32; i++) {
		gpr_buf[i] = GPR_1;
		fpr_buf[i] = a;
	}

	gpr_child_loop(&cptr[0], &cptr[1], gpr_buf, fpr_buf);

	shmdt((void *)cptr);

	FAIL_IF(validate_gpr(gpr_buf, GPR_3));
	FAIL_IF(validate_fpr_double(fpr_buf, c));

	return 0;
}

int trace_gpr(pid_t child)
{
	unsigned long gpr[18];
	__u64 fpr[32];

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
		exit(child());

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
