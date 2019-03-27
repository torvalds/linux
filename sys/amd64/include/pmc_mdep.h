/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008 Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* Machine dependent interfaces */

#ifndef _MACHINE_PMC_MDEP_H
#define	_MACHINE_PMC_MDEP_H 1

#ifdef	_KERNEL
struct pmc_mdep;
#endif

#include <dev/hwpmc/hwpmc_amd.h>
#include <dev/hwpmc/hwpmc_core.h>
#include <dev/hwpmc/hwpmc_tsc.h>
#include <dev/hwpmc/hwpmc_uncore.h>

/*
 * Intel processors implementing V2 and later of the Intel performance
 * measurement architecture have PMCs of the following classes: TSC,
 * IAF, IAP, UCF and UCP.
 */
#define	PMC_MDEP_CLASS_INDEX_TSC	1
#define	PMC_MDEP_CLASS_INDEX_K8		2
#define	PMC_MDEP_CLASS_INDEX_P4		2
#define	PMC_MDEP_CLASS_INDEX_IAP	2
#define	PMC_MDEP_CLASS_INDEX_IAF	3
#define	PMC_MDEP_CLASS_INDEX_UCP	4
#define	PMC_MDEP_CLASS_INDEX_UCF	5

/*
 * On the amd64 platform we support the following PMCs.
 *
 * TSC		The timestamp counter
 * K8		AMD Athlon64 and Opteron PMCs in 64 bit mode.
 * PIV		Intel P4/HTT and P4/EMT64
 * IAP		Intel Core/Core2/Atom CPUs in 64 bits mode.
 * IAF		Intel fixed-function PMCs in Core2 and later CPUs.
 * UCP		Intel Uncore programmable PMCs.
 * UCF		Intel Uncore fixed-function PMCs.
 */

union pmc_md_op_pmcallocate  {
	struct pmc_md_amd_op_pmcallocate	pm_amd;
	struct pmc_md_iap_op_pmcallocate	pm_iap;
	struct pmc_md_ucf_op_pmcallocate	pm_ucf;
	struct pmc_md_ucp_op_pmcallocate	pm_ucp;
	uint64_t				__pad[4];
};

/* Logging */
#define	PMCLOG_READADDR		PMCLOG_READ64
#define	PMCLOG_EMITADDR		PMCLOG_EMIT64

#ifdef	_KERNEL

union pmc_md_pmc {
	struct pmc_md_amd_pmc	pm_amd;
	struct pmc_md_iaf_pmc	pm_iaf;
	struct pmc_md_iap_pmc	pm_iap;
	struct pmc_md_ucf_pmc	pm_ucf;
	struct pmc_md_ucp_pmc	pm_ucp;
};

#define	PMC_TRAPFRAME_TO_PC(TF)	((TF)->tf_rip)
#define	PMC_TRAPFRAME_TO_FP(TF)	((TF)->tf_rbp)
#define	PMC_TRAPFRAME_TO_USER_SP(TF)	((TF)->tf_rsp)
#define	PMC_TRAPFRAME_TO_KERNEL_SP(TF)	((TF)->tf_rsp)

#define	PMC_AT_FUNCTION_PROLOGUE_PUSH_BP(I)		\
	(((I) & 0xffffffff) == 0xe5894855) /* pushq %rbp; movq %rsp,%rbp */
#define	PMC_AT_FUNCTION_PROLOGUE_MOV_SP_BP(I)		\
	(((I) & 0x00ffffff) == 0x00e58948) /* movq %rsp,%rbp */
#define	PMC_AT_FUNCTION_EPILOGUE_RET(I)			\
	(((I) & 0xFF) == 0xC3)		   /* ret */

#define	PMC_IN_TRAP_HANDLER(PC) 			\
	((PC) >= (uintptr_t) start_exceptions &&	\
	 (PC) < (uintptr_t) end_exceptions)

#define	PMC_IN_KERNEL_STACK(S,START,END)		\
	((S) >= (START) && (S) < (END))
#define	PMC_IN_KERNEL(va) INKERNEL(va)

#define	PMC_IN_USERSPACE(va) ((va) <= VM_MAXUSER_ADDRESS)

/* Build a fake kernel trapframe from current instruction pointer. */
#define PMC_FAKE_TRAPFRAME(TF)						\
	do {								\
	(TF)->tf_cs = 0; (TF)->tf_rflags = 0;				\
	__asm __volatile("movq %%rbp,%0" : "=r" ((TF)->tf_rbp));	\
	__asm __volatile("movq %%rsp,%0" : "=r" ((TF)->tf_rsp));	\
	__asm __volatile("call 1f \n\t1: pop %0" : "=r"((TF)->tf_rip));	\
	} while (0)

/*
 * Prototypes
 */

void	start_exceptions(void), end_exceptions(void);

struct pmc_mdep *pmc_amd_initialize(void);
void	pmc_amd_finalize(struct pmc_mdep *_md);
struct pmc_mdep *pmc_intel_initialize(void);
void	pmc_intel_finalize(struct pmc_mdep *_md);

#endif /* _KERNEL */
#endif /* _MACHINE_PMC_MDEP_H */
