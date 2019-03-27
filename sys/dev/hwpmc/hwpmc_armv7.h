/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#ifndef _DEV_HWPMC_ARMV7_H_
#define _DEV_HWPMC_ARMV7_H_

#define	ARMV7_PMC_CAPS		(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

#define	ARMV7_PMNC_ENABLE	(1 << 0) /* Enable all counters */
#define	ARMV7_PMNC_P		(1 << 1) /* Reset all counters */
#define	ARMV7_PMNC_C		(1 << 2) /* Cycle counter reset */
#define	ARMV7_PMNC_D		(1 << 3) /* CCNT counts every 64th cpu cycle */  
#define	ARMV7_PMNC_X		(1 << 4) /* Export to ext. monitoring (ETM) */
#define	ARMV7_PMNC_DP		(1 << 5) /* Disable CCNT if non-invasive debug*/
#define	ARMV7_PMNC_N_SHIFT	11       /* Number of counters implemented */
#define	ARMV7_PMNC_N_MASK	0x1f
#define	ARMV7_PMNC_MASK		0x3f     /* Writable bits */
#define	ARMV7_IDCODE_SHIFT	16       /* Identification code */
#define	ARMV7_IDCODE_MASK	(0xff << ARMV7_IDCODE_SHIFT)
#define	ARMV7_IDCODE_CORTEX_A9	9
#define	ARMV7_IDCODE_CORTEX_A8	8

#define	ARMV7_RELOAD_COUNT_TO_PERFCTR_VALUE(R)	(-(R))
#define	ARMV7_PERFCTR_VALUE_TO_RELOAD_COUNT(P)	(-(P))
#define	EVENT_ID_MASK	0xFF

#ifdef _KERNEL
/* MD extension for 'struct pmc' */
struct pmc_md_armv7_pmc {
	uint32_t	pm_armv7_evsel;
};
#endif /* _KERNEL */
#endif /* _DEV_HWPMC_ARMV7_H_ */
