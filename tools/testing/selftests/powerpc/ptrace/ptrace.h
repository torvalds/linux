/*
 * Ptrace interface test helper functions
 *
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/user.h>
#include <linux/elf.h>
#include <linux/types.h>
#include <linux/auxvec.h>
#include "reg.h"
#include "utils.h"

#define TEST_PASS 0
#define TEST_FAIL 1

struct fpr_regs {
	unsigned long fpr[32];
	unsigned long fpscr;
};


#ifndef NT_PPC_TAR
#define NT_PPC_TAR	0x103
#define NT_PPC_PPR	0x104
#define NT_PPC_DSCR	0x105
#define NT_PPC_EBB	0x106
#define NT_PPC_PMU	0x107
#define NT_PPC_TM_CGPR	0x108
#define NT_PPC_TM_CFPR	0x109
#define NT_PPC_TM_CVMX	0x10a
#define NT_PPC_TM_CVSX	0x10b
#define NT_PPC_TM_SPR	0x10c
#define NT_PPC_TM_CTAR	0x10d
#define NT_PPC_TM_CPPR	0x10e
#define NT_PPC_TM_CDSCR	0x10f
#endif

/* Basic ptrace operations */
int start_trace(pid_t child)
{
	int ret;

	ret = ptrace(PTRACE_ATTACH, child, NULL, NULL);
	if (ret) {
		perror("ptrace(PTRACE_ATTACH) failed");
		return TEST_FAIL;
	}
	ret = waitpid(child, NULL, 0);
	if (ret != child) {
		perror("waitpid() failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

int stop_trace(pid_t child)
{
	int ret;

	ret = ptrace(PTRACE_DETACH, child, NULL, NULL);
	if (ret) {
		perror("ptrace(PTRACE_DETACH) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

int cont_trace(pid_t child)
{
	int ret;

	ret = ptrace(PTRACE_CONT, child, NULL, NULL);
	if (ret) {
		perror("ptrace(PTRACE_CONT) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

/* TAR, PPR, DSCR */
int show_tar_registers(pid_t child, unsigned long *out)
{
	struct iovec iov;
	unsigned long *reg;
	int ret;

	reg = malloc(sizeof(unsigned long));
	if (!reg) {
		perror("malloc() failed");
		return TEST_FAIL;
	}
	iov.iov_base = (u64 *) reg;
	iov.iov_len = sizeof(unsigned long);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TAR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[0] = *reg;

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_PPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[1] = *reg;

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_DSCR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[2] = *reg;

	free(reg);
	return TEST_PASS;
fail:
	free(reg);
	return TEST_FAIL;
}

int write_tar_registers(pid_t child, unsigned long tar,
		unsigned long ppr, unsigned long dscr)
{
	struct iovec iov;
	unsigned long *reg;
	int ret;

	reg = malloc(sizeof(unsigned long));
	if (!reg) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	iov.iov_base = (u64 *) reg;
	iov.iov_len = sizeof(unsigned long);

	*reg = tar;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TAR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_SETREGSET) failed");
		goto fail;
	}

	*reg = ppr;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_PPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_SETREGSET) failed");
		goto fail;
	}

	*reg = dscr;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_DSCR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_SETREGSET) failed");
		goto fail;
	}

	free(reg);
	return TEST_PASS;
fail:
	free(reg);
	return TEST_FAIL;
}

int show_tm_checkpointed_state(pid_t child, unsigned long *out)
{
	struct iovec iov;
	unsigned long *reg;
	int ret;

	reg = malloc(sizeof(unsigned long));
	if (!reg) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	iov.iov_base = (u64 *) reg;
	iov.iov_len = sizeof(unsigned long);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CTAR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[0] = *reg;

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CPPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[1] = *reg;

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CDSCR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}
	if (out)
		out[2] = *reg;

	free(reg);
	return TEST_PASS;

fail:
	free(reg);
	return TEST_FAIL;
}

int write_ckpt_tar_registers(pid_t child, unsigned long tar,
		unsigned long ppr, unsigned long dscr)
{
	struct iovec iov;
	unsigned long *reg;
	int ret;

	reg = malloc(sizeof(unsigned long));
	if (!reg) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	iov.iov_base = (u64 *) reg;
	iov.iov_len = sizeof(unsigned long);

	*reg = tar;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TM_CTAR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}

	*reg = ppr;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TM_CPPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}

	*reg = dscr;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TM_CDSCR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		goto fail;
	}

	free(reg);
	return TEST_PASS;
fail:
	free(reg);
	return TEST_FAIL;
}

/* FPR */
int show_fpr(pid_t child, unsigned long *fpr)
{
	struct fpr_regs *regs;
	int ret, i;

	regs = (struct fpr_regs *) malloc(sizeof(struct fpr_regs));
	ret = ptrace(PTRACE_GETFPREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	if (fpr) {
		for (i = 0; i < 32; i++)
			fpr[i] = regs->fpr[i];
	}
	return TEST_PASS;
}

int write_fpr(pid_t child, unsigned long val)
{
	struct fpr_regs *regs;
	int ret, i;

	regs = (struct fpr_regs *) malloc(sizeof(struct fpr_regs));
	ret = ptrace(PTRACE_GETFPREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	for (i = 0; i < 32; i++)
		regs->fpr[i] = val;

	ret = ptrace(PTRACE_SETFPREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

int show_ckpt_fpr(pid_t child, unsigned long *fpr)
{
	struct fpr_regs *regs;
	struct iovec iov;
	int ret, i;

	regs = (struct fpr_regs *) malloc(sizeof(struct fpr_regs));
	iov.iov_base = regs;
	iov.iov_len = sizeof(struct fpr_regs);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CFPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	if (fpr) {
		for (i = 0; i < 32; i++)
			fpr[i] = regs->fpr[i];
	}

	return TEST_PASS;
}

int write_ckpt_fpr(pid_t child, unsigned long val)
{
	struct fpr_regs *regs;
	struct iovec iov;
	int ret, i;

	regs = (struct fpr_regs *) malloc(sizeof(struct fpr_regs));
	iov.iov_base = regs;
	iov.iov_len = sizeof(struct fpr_regs);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CFPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	for (i = 0; i < 32; i++)
		regs->fpr[i] = val;

	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TM_CFPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

/* GPR */
int show_gpr(pid_t child, unsigned long *gpr)
{
	struct pt_regs *regs;
	int ret, i;

	regs = (struct pt_regs *) malloc(sizeof(struct pt_regs));
	if (!regs) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	ret = ptrace(PTRACE_GETREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	if (gpr) {
		for (i = 14; i < 32; i++)
			gpr[i-14] = regs->gpr[i];
	}

	return TEST_PASS;
}

int write_gpr(pid_t child, unsigned long val)
{
	struct pt_regs *regs;
	int i, ret;

	regs = (struct pt_regs *) malloc(sizeof(struct pt_regs));
	if (!regs) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	ret = ptrace(PTRACE_GETREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	for (i = 14; i < 32; i++)
		regs->gpr[i] = val;

	ret = ptrace(PTRACE_SETREGS, child, NULL, regs);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

int show_ckpt_gpr(pid_t child, unsigned long *gpr)
{
	struct pt_regs *regs;
	struct iovec iov;
	int ret, i;

	regs = (struct pt_regs *) malloc(sizeof(struct pt_regs));
	if (!regs) {
		perror("malloc() failed");
		return TEST_FAIL;
	}

	iov.iov_base = (u64 *) regs;
	iov.iov_len = sizeof(struct pt_regs);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CGPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	if (gpr) {
		for (i = 14; i < 32; i++)
			gpr[i-14] = regs->gpr[i];
	}

	return TEST_PASS;
}

int write_ckpt_gpr(pid_t child, unsigned long val)
{
	struct pt_regs *regs;
	struct iovec iov;
	int ret, i;

	regs = (struct pt_regs *) malloc(sizeof(struct pt_regs));
	if (!regs) {
		perror("malloc() failed\n");
		return TEST_FAIL;
	}
	iov.iov_base = (u64 *) regs;
	iov.iov_len = sizeof(struct pt_regs);

	ret = ptrace(PTRACE_GETREGSET, child, NT_PPC_TM_CGPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}

	for (i = 14; i < 32; i++)
		regs->gpr[i] = val;

	ret = ptrace(PTRACE_SETREGSET, child, NT_PPC_TM_CGPR, &iov);
	if (ret) {
		perror("ptrace(PTRACE_GETREGSET) failed");
		return TEST_FAIL;
	}
	return TEST_PASS;
}

/* Analyse TEXASR after TM failure */
inline unsigned long get_tfiar(void)
{
	unsigned long ret;

	asm volatile("mfspr %0,%1" : "=r" (ret) : "i" (SPRN_TFIAR));
	return ret;
}

void analyse_texasr(unsigned long texasr)
{
	printf("TEXASR: %16lx\t", texasr);

	if (texasr & TEXASR_FP)
		printf("TEXASR_FP  ");

	if (texasr & TEXASR_DA)
		printf("TEXASR_DA  ");

	if (texasr & TEXASR_NO)
		printf("TEXASR_NO  ");

	if (texasr & TEXASR_FO)
		printf("TEXASR_FO  ");

	if (texasr & TEXASR_SIC)
		printf("TEXASR_SIC  ");

	if (texasr & TEXASR_NTC)
		printf("TEXASR_NTC  ");

	if (texasr & TEXASR_TC)
		printf("TEXASR_TC  ");

	if (texasr & TEXASR_TIC)
		printf("TEXASR_TIC  ");

	if (texasr & TEXASR_IC)
		printf("TEXASR_IC  ");

	if (texasr & TEXASR_IFC)
		printf("TEXASR_IFC  ");

	if (texasr & TEXASR_ABT)
		printf("TEXASR_ABT  ");

	if (texasr & TEXASR_SPD)
		printf("TEXASR_SPD  ");

	if (texasr & TEXASR_HV)
		printf("TEXASR_HV  ");

	if (texasr & TEXASR_PR)
		printf("TEXASR_PR  ");

	if (texasr & TEXASR_FS)
		printf("TEXASR_FS  ");

	if (texasr & TEXASR_TE)
		printf("TEXASR_TE  ");

	if (texasr & TEXASR_ROT)
		printf("TEXASR_ROT  ");

	printf("TFIAR :%lx\n", get_tfiar());
}

void store_gpr(unsigned long *addr);
void store_fpr(float *addr);
