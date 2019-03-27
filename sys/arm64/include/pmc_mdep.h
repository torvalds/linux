/*-
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

#define	PMC_MDEP_CLASS_INDEX_ARMV8	1
/*
 * On the ARMv8 platform we support the following PMCs.
 *
 * ARMV8	ARM Cortex-A53/57/72 processors
 */
#include <dev/hwpmc/hwpmc_arm64.h>

union pmc_md_op_pmcallocate {
	uint64_t		__pad[4];
};

/* Logging */
#define	PMCLOG_READADDR		PMCLOG_READ64
#define	PMCLOG_EMITADDR		PMCLOG_EMIT64

#ifdef	_KERNEL
union pmc_md_pmc {
	struct pmc_md_arm64_pmc		pm_arm64;
};

#define	PMC_IN_KERNEL_STACK(S,START,END)		\
	((S) >= (START) && (S) < (END))
#define	PMC_IN_KERNEL(va)	INKERNEL((va))
#define	PMC_IN_USERSPACE(va) ((va) <= VM_MAXUSER_ADDRESS)
#define	PMC_TRAPFRAME_TO_PC(TF)		((TF)->tf_lr)
#define	PMC_TRAPFRAME_TO_FP(TF)		((TF)->tf_x[29])

/*
 * Prototypes
 */
struct pmc_mdep *pmc_arm64_initialize(void);
void	pmc_arm64_finalize(struct pmc_mdep *_md);
#endif /* _KERNEL */

#endif /* !_MACHINE_PMC_MDEP_H_ */
