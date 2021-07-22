// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test for VMX/VSX registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "ptrace-vsx.h"

/* Tracer and Tracee Shared Data */
int shm_id;
int *cptr, *pptr;

unsigned long fp_load[VEC_MAX];
unsigned long fp_load_new[VEC_MAX];
unsigned long fp_store[VEC_MAX];

void vsx(void)
{
	int ret;

	cptr = (int *)shmat(shm_id, NULL, 0);
	loadvsx(fp_load, 0);
	cptr[1] = 1;

	while (!cptr[0])
		asm volatile("" : : : "memory");
	shmdt((void *) cptr);

	storevsx(fp_store, 0);
	ret = compare_vsx_vmx(fp_store, fp_load_new);
	if (ret)
		exit(1);
	exit(0);
}

int trace_vsx(pid_t child)
{
	unsigned long vsx[VSX_MAX];
	unsigned long vmx[VMX_MAX + 2][2];

	FAIL_IF(start_trace(child));
	FAIL_IF(show_vsx(child, vsx));
	FAIL_IF(validate_vsx(vsx, fp_load));
	FAIL_IF(show_vmx(child, vmx));
	FAIL_IF(validate_vmx(vmx, fp_load));

	memset(vsx, 0, sizeof(vsx));
	memset(vmx, 0, sizeof(vmx));
	load_vsx_vmx(fp_load_new, vsx, vmx);

	FAIL_IF(write_vsx(child, vsx));
	FAIL_IF(write_vmx(child, vmx));
	FAIL_IF(stop_trace(child));

	return TEST_PASS;
}

int ptrace_vsx(void)
{
	pid_t pid;
	int ret, status, i;

	SKIP_IF(!have_hwcap(PPC_FEATURE_HAS_VSX));

	shm_id = shmget(IPC_PRIVATE, sizeof(int) * 2, 0777|IPC_CREAT);

	for (i = 0; i < VEC_MAX; i++)
		fp_load[i] = i + rand();

	for (i = 0; i < VEC_MAX; i++)
		fp_load_new[i] = i + 2 * rand();

	pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		return TEST_FAIL;
	}

	if (pid == 0)
		vsx();

	if (pid) {
		pptr = (int *)shmat(shm_id, NULL, 0);
		while (!pptr[1])
			asm volatile("" : : : "memory");

		ret = trace_vsx(pid);
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
	return test_harness(ptrace_vsx, "ptrace_vsx");
}
