/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Justin Hibbits
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

#ifndef _DEV_HWPMC_POWERPC_H_
#define	_DEV_HWPMC_POWERPC_H_ 1

#ifdef _KERNEL

#define	POWERPC_PMC_CAPS	(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

#define POWERPC_PMC_KERNEL_ENABLE	(0x1 << 30)
#define POWERPC_PMC_USER_ENABLE		(0x1 << 31)

#define POWERPC_PMC_ENABLE	(POWERPC_PMC_KERNEL_ENABLE | POWERPC_PMC_USER_ENABLE)
#define	POWERPC_RELOAD_COUNT_TO_PERFCTR_VALUE(V)	(0x80000000-(V))
#define	POWERPC_PERFCTR_VALUE_TO_RELOAD_COUNT(P)	(0x80000000-(P))

struct powerpc_cpu {
	struct pmc_hw   *pc_ppcpmcs;
	enum pmc_class	 pc_class;
};

extern struct powerpc_cpu **powerpc_pcpu;

extern int pmc_e500_initialize(struct pmc_mdep *pmc_mdep);
extern int pmc_mpc7xxx_initialize(struct pmc_mdep *pmc_mdep);
extern int pmc_ppc970_initialize(struct pmc_mdep *pmc_mdep);

extern int powerpc_describe(int cpu, int ri, struct pmc_info *pi, struct pmc **ppmc);
extern int powerpc_get_config(int cpu, int ri, struct pmc **ppm);
#endif /* _KERNEL */

#endif	/* _DEV_HWPMC_POWERPC_H_ */
