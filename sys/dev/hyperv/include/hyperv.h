/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2012,2016-2017 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _HYPERV_H_
#define _HYPERV_H_

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>

#define MSR_HV_TIME_REF_COUNT		0x40000020

#define CPUID_HV_MSR_TIME_REFCNT	0x0002	/* MSR_HV_TIME_REF_COUNT */
#define CPUID_HV_MSR_SYNIC		0x0004	/* MSRs for SynIC */
#define CPUID_HV_MSR_SYNTIMER		0x0008	/* MSRs for SynTimer */
#define CPUID_HV_MSR_APIC		0x0010	/* MSR_HV_{EOI,ICR,TPR} */
#define CPUID_HV_MSR_HYPERCALL		0x0020	/* MSR_HV_GUEST_OS_ID
						 * MSR_HV_HYPERCALL */
#define CPUID_HV_MSR_VP_INDEX		0x0040	/* MSR_HV_VP_INDEX */
#define CPUID_HV_MSR_REFERENCE_TSC	0x0200	/* MSR_HV_REFERENCE_TSC */
#define CPUID_HV_MSR_GUEST_IDLE		0x0400	/* MSR_HV_GUEST_IDLE */

#ifndef NANOSEC
#define NANOSEC				1000000000ULL
#endif
#define HYPERV_TIMER_NS_FACTOR		100ULL
#define HYPERV_TIMER_FREQ		(NANOSEC / HYPERV_TIMER_NS_FACTOR)

#endif	/* _KERNEL */

#define HYPERV_REFTSC_DEVNAME		"hv_tsc"

/*
 * Hyper-V Reference TSC
 */
struct hyperv_reftsc {
	volatile uint32_t		tsc_seq;
	volatile uint32_t		tsc_rsvd1;
	volatile uint64_t		tsc_scale;
	volatile int64_t		tsc_ofs;
} __packed __aligned(PAGE_SIZE);
#ifdef CTASSERT
CTASSERT(sizeof(struct hyperv_reftsc) == PAGE_SIZE);
#endif

#ifdef _KERNEL

struct hyperv_guid {
	uint8_t				hv_guid[16];
} __packed;

#define HYPERV_GUID_STRLEN		40

typedef uint64_t			(*hyperv_tc64_t)(void);

int			hyperv_guid2str(const struct hyperv_guid *, char *,
			    size_t);

/*
 * hyperv_tc64 could be NULL, if there were no suitable Hyper-V
 * specific timecounter.
 */
extern hyperv_tc64_t	hyperv_tc64;
extern u_int		hyperv_features;	/* CPUID_HV_MSR_ */
extern u_int		hyperv_ver_major;

#endif	/* _KERNEL */

#endif  /* _HYPERV_H_ */
