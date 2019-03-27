/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory with support from ARM Ltd.
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

#ifndef _DEV_HWPMC_RISCV_H_
#define	_DEV_HWPMC_RISCV_H_

#define	RISCV_PMC_CAPS		(PMC_CAP_INTERRUPT | PMC_CAP_USER |	\
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

#define	RISCV_RELOAD_COUNT_TO_PERFCTR_VALUE(R)	(-(R))
#define	RISCV_PERFCTR_VALUE_TO_RELOAD_COUNT(P)	(-(P))
#define	EVENT_ID_MASK	0xFF

#ifdef _KERNEL
/* MD extension for 'struct pmc' */
struct pmc_md_riscv_pmc {
	uint32_t	pm_riscv_evsel;
};
#endif /* _KERNEL */
#endif /* _DEV_HWPMC_RISCV_H_ */
