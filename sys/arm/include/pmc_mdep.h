/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rui Paulo <rpaulo@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PMC_MDEP_H_
#define	_MACHINE_PMC_MDEP_H_

#define	PMC_MDEP_CLASS_INDEX_XSCALE	1
#define	PMC_MDEP_CLASS_INDEX_ARMV7	1
/*
 * On the ARM platform we support the following PMCs.
 *
 * XSCALE	Intel XScale processors
 * ARMV7	ARM Cortex-A processors
 */
#include <dev/hwpmc/hwpmc_xscale.h>
#include <dev/hwpmc/hwpmc_armv7.h>

union pmc_md_op_pmcallocate {
	uint64_t	__pad[4];
};

/* Logging */
#define	PMCLOG_READADDR		PMCLOG_READ32
#define	PMCLOG_EMITADDR		PMCLOG_EMIT32

#ifdef	_KERNEL
union pmc_md_pmc {
	struct pmc_md_xscale_pmc	pm_xscale;
	struct pmc_md_armv7_pmc		pm_armv7;
};

#define	PMC_IN_KERNEL_STACK(S,START,END)		\
	((S) >= (START) && (S) < (END))
#define	PMC_IN_KERNEL(va)	INKERNEL((va))

#define	PMC_IN_USERSPACE(va) ((va) <= VM_MAXUSER_ADDRESS)

#define	PMC_TRAPFRAME_TO_PC(TF)		((TF)->tf_pc)
#define	PMC_TRAPFRAME_TO_FP(TF)		((TF)->tf_r11)
#define	PMC_TRAPFRAME_TO_SVC_SP(TF)	((TF)->tf_svc_sp)
#define	PMC_TRAPFRAME_TO_USR_SP(TF)	((TF)->tf_usr_sp)
#define	PMC_TRAPFRAME_TO_SVC_LR(TF)	((TF)->tf_svc_lr)
#define	PMC_TRAPFRAME_TO_USR_LR(TF)	((TF)->tf_usr_lr)

/* Build a fake kernel trapframe from current instruction pointer. */
#define PMC_FAKE_TRAPFRAME(TF)						\
	do {								\
	(TF)->tf_spsr = PSR_SVC32_MODE;					\
	__asm __volatile("mov %0, pc" : "=r" ((TF)->tf_pc));		\
	__asm __volatile("mov %0, r11" : "=r" ((TF)->tf_r11));		\
	} while (0)

/*
 * Prototypes
 */
struct pmc_mdep *pmc_xscale_initialize(void);
void		pmc_xscale_finalize(struct pmc_mdep *_md);
struct pmc_mdep *pmc_armv7_initialize(void);
void		pmc_armv7_finalize(struct pmc_mdep *_md);
#endif /* _KERNEL */

#endif /* !_MACHINE_PMC_MDEP_H_ */
