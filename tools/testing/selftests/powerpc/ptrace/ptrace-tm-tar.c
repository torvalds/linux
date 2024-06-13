// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test for TAR, PPR, DSCR registers in the TM context
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "tm.h"
#include "ptrace-tar.h"

int shm_id;
unsigned long *cptr, *pptr;


void tm_tar(void)
{
	unsigned long result, texasr;
	unsigned long regs[3];
	int ret;

	cptr = (unsigned long *)shmat(shm_id, NULL, 0);

trans:
	cptr[1] = 0;
	asm __volatile__(
		"li	4, %[tar_1];"
		"mtspr %[sprn_tar],  4;"	/* TAR_1 */
		"li	4, %[dscr_1];"
		"mtspr %[sprn_dscr], 4;"	/* DSCR_1 */
		"or     31,31,31;"		/* PPR_1*/

		"1: ;"
		"tbegin.;"
		"beq 2f;"

		"li	4, %[tar_2];"
		"mtspr %[sprn_tar],  4;"	/* TAR_2 */
		"li	4, %[dscr_2];"
		"mtspr %[sprn_dscr], 4;"	/* DSCR_2 */
		"or     1,1,1;"			/* PPR_2 */
		"tsuspend.;"
		"li 0, 1;"
		"stw 0, 0(%[cptr1]);"
		"tresume.;"
		"b .;"

		"tend.;"
		"li 0, 0;"
		"ori %[res], 0, 0;"
		"b 3f;"

		/* Transaction abort handler */
		"2: ;"
		"li 0, 1;"
		"ori %[res], 0, 0;"
		"mfspr %[texasr], %[sprn_texasr];"

		"3: ;"

		: [res] "=r" (result), [texasr] "=r" (texasr)
		: [sprn_dscr]"i"(SPRN_DSCR), [sprn_tar]"i"(SPRN_TAR),
		[sprn_ppr]"i"(SPRN_PPR), [sprn_texasr]"i"(SPRN_TEXASR),
		[tar_1]"i"(TAR_1), [dscr_1]"i"(DSCR_1), [tar_2]"i"(TAR_2),
		[dscr_2]"i"(DSCR_2), [cptr1] "b" (&cptr[1])
		: "memory", "r0", "r3", "r4", "r5", "r6"
		);

	/* TM failed, analyse */
	if (result) {
		if (!cptr[0])
			goto trans;

		regs[0] = mfspr(SPRN_TAR);
		regs[1] = mfspr(SPRN_PPR);
		regs[2] = mfspr(SPRN_DSCR);

		shmdt(&cptr);
		printf("%-30s TAR: %lu PPR: %lx DSCR: %lu\n",
				user_read, regs[0], regs[1], regs[2]);

		ret = validate_tar_registers(regs, TAR_4, PPR_4, DSCR_4);
		if (ret)
			exit(1);
		exit(0);
	}
	shmdt(&cptr);
	exit(1);
}

int trace_tm_tar(pid_t child)
{
	unsigned long regs[3];

	FAIL_IF(start_trace(child));
	FAIL_IF(show_tar_registers(child, regs));
	printf("%-30s TAR: %lu PPR: %lx DSCR: %lu\n",
			ptrace_read_running, regs[0], regs[1], regs[2]);

	FAIL_IF(validate_tar_registers(regs, TAR_2, PPR_2, DSCR_2));
	FAIL_IF(show_tm_checkpointed_state(child, regs));
	printf("%-30s TAR: %lu PPR: %lx DSCR: %lu\n",
			ptrace_read_ckpt, regs[0], regs[1], regs[2]);

	FAIL_IF(validate_tar_registers(regs, TAR_1, PPR_1, DSCR_1));
	FAIL_IF(write_ckpt_tar_registers(child, TAR_4, PPR_4, DSCR_4));
	printf("%-30s TAR: %u PPR: %lx DSCR: %u\n",
			ptrace_write_ckpt, TAR_4, PPR_4, DSCR_4);

	pptr[0] = 1;
	FAIL_IF(stop_trace(child));
	return TEST_PASS;
}

int ptrace_tm_tar(void)
{
	pid_t pid;
	int ret, status;

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());
	shm_id = shmget(IPC_PRIVATE, sizeof(int) * 2, 0777|IPC_CREAT);
	pid = fork();
	if (pid == 0)
		tm_tar();

	pptr = (unsigned long *)shmat(shm_id, NULL, 0);
	pptr[0] = 0;

	if (pid) {
		while (!pptr[1])
			asm volatile("" : : : "memory");
		ret = trace_tm_tar(pid);
		if (ret) {
			kill(pid, SIGTERM);
			shmdt(&pptr);
			shmctl(shm_id, IPC_RMID, NULL);
			return TEST_FAIL;
		}
		shmdt(&pptr);

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
	return test_harness(ptrace_tm_tar, "ptrace_tm_tar");
}
