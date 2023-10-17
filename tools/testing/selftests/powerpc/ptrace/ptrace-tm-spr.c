// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ptrace test TM SPR registers
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#include "ptrace.h"
#include "tm.h"

/* Tracee and tracer shared data */
struct shared {
	int flag;
	struct tm_spr_regs regs;
};
unsigned long tfhar;

int shm_id;
struct shared *cptr, *pptr;

int shm_id1;
int *cptr1, *pptr1;

#define TM_KVM_SCHED   0xe0000001ac000001
int validate_tm_spr(struct tm_spr_regs *regs)
{
	FAIL_IF(regs->tm_tfhar != tfhar);
	FAIL_IF((regs->tm_texasr == TM_KVM_SCHED) && (regs->tm_tfiar != 0));

	return TEST_PASS;
}

void tm_spr(void)
{
	unsigned long result, texasr;
	int ret;

	cptr = (struct shared *)shmat(shm_id, NULL, 0);
	cptr1 = (int *)shmat(shm_id1, NULL, 0);

trans:
	cptr1[0] = 0;
	asm __volatile__(
		"1: ;"
		/* TM failover handler should follow "tbegin.;" */
		"mflr 31;"
		"bl 4f;"	/* $ = TFHAR - 12 */
		"4: ;"
		"mflr %[tfhar];"
		"mtlr 31;"

		"tbegin.;"
		"beq 2f;"

		"tsuspend.;"
		"li 8, 1;"
		"sth 8, 0(%[cptr1]);"
		"tresume.;"
		"b .;"

		"tend.;"
		"li 0, 0;"
		"ori %[res], 0, 0;"
		"b 3f;"

		"2: ;"

		"li 0, 1;"
		"ori %[res], 0, 0;"
		"mfspr %[texasr], %[sprn_texasr];"

		"3: ;"
		: [tfhar] "=r" (tfhar), [res] "=r" (result),
		[texasr] "=r" (texasr), [cptr1] "=b" (cptr1)
		: [sprn_texasr] "i"  (SPRN_TEXASR)
		: "memory", "r0", "r8", "r31"
		);

	/* There are 2 32bit instructions before tbegin. */
	tfhar += 12;

	if (result) {
		if (!cptr->flag)
			goto trans;

		ret = validate_tm_spr((struct tm_spr_regs *)&cptr->regs);
		shmdt((void *)cptr);
		shmdt((void *)cptr1);
		if (ret)
			exit(1);
		exit(0);
	}
	shmdt((void *)cptr);
	shmdt((void *)cptr1);
	exit(1);
}

int trace_tm_spr(pid_t child)
{
	FAIL_IF(start_trace(child));
	FAIL_IF(show_tm_spr(child, (struct tm_spr_regs *)&pptr->regs));

	printf("TFHAR: %lx TEXASR: %lx TFIAR: %lx\n", pptr->regs.tm_tfhar,
				pptr->regs.tm_texasr, pptr->regs.tm_tfiar);

	pptr->flag = 1;
	FAIL_IF(stop_trace(child));

	return TEST_PASS;
}

int ptrace_tm_spr(void)
{
	pid_t pid;
	int ret, status;

	SKIP_IF_MSG(!have_htm(), "Don't have transactional memory");
	SKIP_IF_MSG(htm_is_synthetic(), "Transactional memory is synthetic");
	shm_id = shmget(IPC_PRIVATE, sizeof(struct shared), 0777|IPC_CREAT);
	shm_id1 = shmget(IPC_PRIVATE, sizeof(int), 0777|IPC_CREAT);
	pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		return TEST_FAIL;
	}

	if (pid == 0)
		tm_spr();

	if (pid) {
		pptr = (struct shared *)shmat(shm_id, NULL, 0);
		pptr1 = (int *)shmat(shm_id1, NULL, 0);

		while (!pptr1[0])
			asm volatile("" : : : "memory");
		ret = trace_tm_spr(pid);
		if (ret) {
			kill(pid, SIGKILL);
			shmdt((void *)pptr);
			shmdt((void *)pptr1);
			shmctl(shm_id, IPC_RMID, NULL);
			shmctl(shm_id1, IPC_RMID, NULL);
			return TEST_FAIL;
		}

		shmdt((void *)pptr);
		shmdt((void *)pptr1);
		ret = wait(&status);
		shmctl(shm_id, IPC_RMID, NULL);
		shmctl(shm_id1, IPC_RMID, NULL);
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
	return test_harness(ptrace_tm_spr, "ptrace_tm_spr");
}
