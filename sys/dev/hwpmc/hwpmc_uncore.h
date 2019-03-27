/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Fabien Thomas
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

#ifndef _DEV_HWPMC_UNCORE_H_
#define	_DEV_HWPMC_UNCORE_H_ 1

/*
 * Fixed-function PMCs.
 */
struct pmc_md_ucf_op_pmcallocate {
	uint16_t	pm_ucf_flags;	/* additional flags */
};

#define	UCF_EN		0x1
#define	UCF_PMI		0x4

/*
 * Programmable PMCs.
 */
struct pmc_md_ucp_op_pmcallocate {
	uint32_t	pm_ucp_config;
};

#define	UCP_EVSEL(C)	((C) & 0xFF)
#define	UCP_UMASK(C)	((C) & 0xFF00)
#define	UCP_CTRR	(1 << 17)
#define	UCP_EDGE	(1 << 18)
#define	UCP_INT		(1 << 20)
#define	UCP_EN		(1 << 22)
#define	UCP_INV		(1 << 23)
#define	UCP_CMASK(C)	(((C) & 0xFF) << 24)
#ifdef	_KERNEL

#define	DCTL_FLAG_UNC_PMI	(1ULL << 13)

/*
 * Fixed-function counters.
 */

#define	UCF_MASK				0xF

#define	UCF_CTR0				0x394

#define	UCF_OFFSET				32
#define UCF_OFFSET_SB				29
#define	UCF_CTRL				0x395

/*
 * Programmable counters.
 */

#define	UCP_PMC0				0x3B0
#define	UCP_EVSEL0				0x3C0
#define UCP_OPCODE_MATCH			0x396
#define UCP_CB0_EVSEL0				0x700

/*
 * Simplified programming interface in Intel Performance Architecture
 * v2 and later.
 */

#define	UC_GLOBAL_STATUS			0x392
#define	UC_GLOBAL_CTRL				0x391
#define	UC_GLOBAL_OVF_CTRL			0x393

#define	UC_GLOBAL_STATUS_FLAG_CLRCHG		(1ULL << 63)
#define	UC_GLOBAL_STATUS_FLAG_OVFPMI		(1ULL << 61)
#define	UC_GLOBAL_CTRL_FLAG_FRZ			(1ULL << 63)
#define	UC_GLOBAL_CTRL_FLAG_ENPMICORE0		(1ULL << 48)

/*
 * Model specific registers.
 */

#define MSR_GQ_SNOOP_MESF			0x301

struct pmc_md_ucf_pmc {
	uint64_t	pm_ucf_ctrl;
};

struct pmc_md_ucp_pmc {
	uint32_t	pm_ucp_evsel;
};

/*
 * Prototypes.
 */

int	pmc_uncore_initialize(struct pmc_mdep *_md, int _maxcpu);
void	pmc_uncore_finalize(struct pmc_mdep *_md);

int	pmc_ucf_initialize(struct pmc_mdep *_md, int _maxcpu, int _npmc, int _width);
void	pmc_ucf_finalize(struct pmc_mdep *_md);

int	pmc_ucp_initialize(struct pmc_mdep *_md, int _maxcpu, int _npmc, int _width,
	    int _flags);
void	pmc_ucp_finalize(struct pmc_mdep *_md);

#endif	/* _KERNEL */
#endif	/* _DEV_HWPMC_UNCORE_H */
