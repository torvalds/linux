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

#ifndef _DEV_HWPMC_XSCALE_H_
#define _DEV_HWPMC_XSCALE_H_

#define	XSCALE_PMC_CAPS		(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)


#define	XSCALE_PMNC_ENABLE		0x01	/* Enable all Counters */
#define	XSCALE_PMNC_PMNRESET		0x02	/* Performance Counter Reset */
#define	XSCALE_PMNC_CCNTRESET		0x04	/* Clock Counter Reset */
#define	XSCALE_PMNC_CCNTDIV		0x08	/* Clock Counter Divider */

#define	XSCALE_INTEN_CCNT		0x01	/* Enable Clock Counter Int. */
#define	XSCALE_INTEN_PMN0		0x02	/* Enable PMN0 Interrupts */
#define	XSCALE_INTEN_PMN1		0x04	/* Enable PMN1 Interrupts */
#define	XSCALE_INTEN_PMN2		0x08	/* Enable PMN2 Interrupts */
#define	XSCALE_INTEN_PMN3		0x10	/* Enable PMN3 Interrupts */

#define	XSCALE_EVTSEL_EVT0_MASK		0x000000ff
#define	XSCALE_EVTSEL_EVT1_MASK		0x0000ff00
#define	XSCALE_EVTSEL_EVT2_MASK		0x00ff0000
#define	XSCALE_EVTSEL_EVT3_MASK		0xff000000

#define	XSCALE_FLAG_CCNT_OVERFLOW	0x01
#define	XSCALE_FLAG_PMN0_OVERFLOW	0x02
#define	XSCALE_FLAG_PMN1_OVERFLOW	0x04
#define	XSCALE_FLAG_PMN2_OVERFLOW	0x08
#define	XSCALE_FLAG_PMN3_OVERFLOW	0x10

#define	XSCALE_RELOAD_COUNT_TO_PERFCTR_VALUE(R)	(-(R))
#define	XSCALE_PERFCTR_VALUE_TO_RELOAD_COUNT(P)	(-(P))

#ifdef _KERNEL
/* MD extension for 'struct pmc' */
struct pmc_md_xscale_pmc {
	uint32_t	pm_xscale_evsel;
};
#endif /* _KERNEL */
#endif /* _DEV_HWPMC_XSCALE_H_ */
