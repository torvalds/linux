// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test for GPR/FPR registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "ptrace-gpr.h"
#include "reg.h"
#include <time.h>

/* Tracer and Tracee Shared Data */
int shm_id;
int *cptr, *pptr;

extern void gpr_child_loop(int *read_flag, int *write_flag,
			   unsigned long *gpr_buf, double *fpr_buf);

unsigned long child_gpr_val, parent_gpr_val;
double child_fpr_val, parent_fpr_val;

static int child(void)
{
	unsigned long gpr_buf[32];
	double fpr_buf[32];
	int i;

	cptr = (int *)shmat(shm_id, NULL, 0);
	memset(gpr_buf, 0, sizeof(gpr_buf));
	memset(fpr_buf, 0, sizeof(fpr_buf));

	for (i = 0; i < 32; i++) {
		gpr_buf[i] = child_gpr_val;
		fpr_buf[i] = child_fpr_val;
	}

	gpr_child_loop(&cptr[0], &cptr[1], gpr_buf, fpr_buf);

	shmdt((void *)cptr);

	FAIL_IF(validate_gpr(gpr_buf, parent_gpr_val));
	FAIL_IF(validate_fpr_double(fpr_buf, parent_fpr_val));

	return 0;
}

int trace_gpr(pid_t child)
{
	__u64 tmp, fpr[32], *peeked_fprs;
	unsigned long gpr[18];

	FAIL_IF(start_trace(child));

	// Check child GPRs match what we expect using GETREGS
	FAIL_IF(show_gpr(child, gpr));
	FAIL_IF(validate_gpr(gpr, child_gpr_val));

	// Check child FPRs match what we expect using GETFPREGS
	FAIL_IF(show_fpr(child, fpr));
	memcpy(&tmp, &child_fpr_val, sizeof(tmp));
	FAIL_IF(validate_fpr(fpr, tmp));

	// Check child FPRs match what we expect using PEEKUSR
	peeked_fprs = peek_fprs(child);
	FAIL_IF(!peeked_fprs);
	FAIL_IF(validate_fpr(peeked_fprs, tmp));
	free(peeked_fprs);

	// Write child GPRs using SETREGS
	FAIL_IF(write_gpr(child, parent_gpr_val));

	// Write child FPRs using SETFPREGS
	memcpy(&tmp, &parent_fpr_val, sizeof(tmp));
	FAIL_IF(write_fpr(child, tmp));

	// Check child FPRs match what we just set, using PEEKUSR
	peeked_fprs = peek_fprs(child);
	FAIL_IF(!peeked_fprs);
	FAIL_IF(validate_fpr(peeked_fprs, tmp));

	// Write child FPRs using POKEUSR
	FAIL_IF(poke_fprs(child, (unsigned long *)peeked_fprs));

	// Child will check its FPRs match before exiting
	FAIL_IF(stop_trace(child));

	return TEST_PASS;
}

#ifndef __LONG_WIDTH__
#define __LONG_WIDTH__ (sizeof(long) * 8)
#endif

static uint64_t rand_reg(void)
{
	uint64_t result;
	long r;

	r = random();

	// Small values are typical
	result = r & 0xffff;
	if (r & 0x10000)
		return result;

	// Pointers tend to have high bits set
	result |= random() << (__LONG_WIDTH__ - 31);
	if (r & 0x100000)
		return result;

	// And sometimes we want a full 64-bit value
	result ^= random() << 16;

	return result;
}

int ptrace_gpr(void)
{
	unsigned long seed;
	int ret, status;
	pid_t pid;

	seed = getpid() ^ time(NULL);
	printf("srand(%lu)\n", seed);
	srand(seed);

	child_gpr_val = rand_reg();
	child_fpr_val = rand_reg();
	parent_gpr_val = rand_reg();
	parent_fpr_val = rand_reg();

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
