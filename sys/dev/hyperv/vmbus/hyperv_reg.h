/*-
 * Copyright (c) 2016 Microsoft Corp.
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

#ifndef _HYPERV_REG_H_
#define _HYPERV_REG_H_

#include <sys/param.h>
#include <sys/systm.h>

/*
 * Hyper-V Synthetic MSRs
 */

#define MSR_HV_GUEST_OS_ID		0x40000000
#define MSR_HV_GUESTID_BUILD_MASK	0xffffULL
#define MSR_HV_GUESTID_VERSION_MASK	0x0000ffffffff0000ULL
#define MSR_HV_GUESTID_VERSION_SHIFT	16
#define MSR_HV_GUESTID_OSID_MASK	0x00ff000000000000ULL
#define MSR_HV_GUESTID_OSID_SHIFT	48
#define MSR_HV_GUESTID_OSTYPE_MASK	0x7f00000000000000ULL
#define MSR_HV_GUESTID_OSTYPE_SHIFT	56
#define MSR_HV_GUESTID_OPENSRC		0x8000000000000000ULL
#define MSR_HV_GUESTID_OSTYPE_LINUX	\
	((0x01ULL << MSR_HV_GUESTID_OSTYPE_SHIFT) | MSR_HV_GUESTID_OPENSRC)
#define MSR_HV_GUESTID_OSTYPE_FREEBSD	\
	((0x02ULL << MSR_HV_GUESTID_OSTYPE_SHIFT) | MSR_HV_GUESTID_OPENSRC)

#define MSR_HV_HYPERCALL		0x40000001
#define MSR_HV_HYPERCALL_ENABLE		0x0001ULL
#define MSR_HV_HYPERCALL_RSVD_MASK	0x0ffeULL
#define MSR_HV_HYPERCALL_PGSHIFT	12

#define MSR_HV_VP_INDEX			0x40000002

#define MSR_HV_REFERENCE_TSC		0x40000021
#define MSR_HV_REFTSC_ENABLE		0x0001ULL
#define MSR_HV_REFTSC_RSVD_MASK		0x0ffeULL
#define MSR_HV_REFTSC_PGSHIFT		12

#define MSR_HV_SCONTROL			0x40000080
#define MSR_HV_SCTRL_ENABLE		0x0001ULL
#define MSR_HV_SCTRL_RSVD_MASK		0xfffffffffffffffeULL

#define MSR_HV_SIEFP			0x40000082
#define MSR_HV_SIEFP_ENABLE		0x0001ULL
#define MSR_HV_SIEFP_RSVD_MASK		0x0ffeULL
#define MSR_HV_SIEFP_PGSHIFT		12

#define MSR_HV_SIMP			0x40000083
#define MSR_HV_SIMP_ENABLE		0x0001ULL
#define MSR_HV_SIMP_RSVD_MASK		0x0ffeULL
#define MSR_HV_SIMP_PGSHIFT		12

#define MSR_HV_EOM			0x40000084

#define MSR_HV_SINT0			0x40000090
#define MSR_HV_SINT_VECTOR_MASK		0x00ffULL
#define MSR_HV_SINT_RSVD1_MASK		0xff00ULL
#define MSR_HV_SINT_MASKED		0x00010000ULL
#define MSR_HV_SINT_AUTOEOI		0x00020000ULL
#define MSR_HV_SINT_RSVD2_MASK		0xfffffffffffc0000ULL
#define MSR_HV_SINT_RSVD_MASK		(MSR_HV_SINT_RSVD1_MASK |	\
					 MSR_HV_SINT_RSVD2_MASK)

#define MSR_HV_STIMER0_CONFIG		0x400000b0
#define MSR_HV_STIMER_CFG_ENABLE	0x0001ULL
#define MSR_HV_STIMER_CFG_PERIODIC	0x0002ULL
#define MSR_HV_STIMER_CFG_LAZY		0x0004ULL
#define MSR_HV_STIMER_CFG_AUTOEN	0x0008ULL
#define MSR_HV_STIMER_CFG_SINT_MASK	0x000f0000ULL
#define MSR_HV_STIMER_CFG_SINT_SHIFT	16

#define MSR_HV_STIMER0_COUNT		0x400000b1

/*
 * CPUID leaves
 */

#define CPUID_LEAF_HV_MAXLEAF		0x40000000

#define CPUID_LEAF_HV_INTERFACE		0x40000001
#define CPUID_HV_IFACE_HYPERV		0x31237648	/* HV#1 */

#define CPUID_LEAF_HV_IDENTITY		0x40000002

#define CPUID_LEAF_HV_FEATURES		0x40000003
/* EAX: features include/hyperv.h CPUID_HV_MSR */
/* ECX: power management features */
#define CPUPM_HV_CSTATE_MASK		0x000f	/* deepest C-state */
#define CPUPM_HV_C3_HPET		0x0010	/* C3 requires HPET */
#define CPUPM_HV_CSTATE(f)		((f) & CPUPM_HV_CSTATE_MASK)
/* EDX: features3 */
#define CPUID3_HV_MWAIT			0x0001	/* MWAIT */
#define CPUID3_HV_XMM_HYPERCALL		0x0010	/* Hypercall input through
						 * XMM regs */
#define CPUID3_HV_GUEST_IDLE		0x0020	/* guest idle */
#define CPUID3_HV_NUMA			0x0080	/* NUMA distance query */
#define CPUID3_HV_TIME_FREQ		0x0100	/* timer frequency query
						 * (TSC, LAPIC) */
#define CPUID3_HV_MSR_CRASH		0x0400	/* MSRs for guest crash */

#define CPUID_LEAF_HV_RECOMMENDS	0x40000004
#define CPUID_LEAF_HV_LIMITS		0x40000005
#define CPUID_LEAF_HV_HWFEATURES	0x40000006

/*
 * Hyper-V Monitor Notification Facility
 */
struct hyperv_mon_param {
	uint32_t	mp_connid;
	uint16_t	mp_evtflag_ofs;
	uint16_t	mp_rsvd;
} __packed;

/*
 * Hyper-V message types
 */
#define HYPERV_MSGTYPE_NONE		0
#define HYPERV_MSGTYPE_CHANNEL		1
#define HYPERV_MSGTYPE_TIMER_EXPIRED	0x80000010

/*
 * Hypercall status codes
 */
#define HYPERCALL_STATUS_SUCCESS	0x0000

/*
 * Hypercall input values
 */
#define HYPERCALL_POST_MESSAGE		0x005c
#define HYPERCALL_SIGNAL_EVENT		0x005d

/*
 * Hypercall input parameters
 */
#define HYPERCALL_PARAM_ALIGN		8
#if 0
/*
 * XXX
 * <<Hypervisor Top Level Functional Specification 4.0b>> requires
 * input parameters size to be multiple of 8, however, many post
 * message input parameters do _not_ meet this requirement.
 */
#define HYPERCALL_PARAM_SIZE_ALIGN	8
#endif

/*
 * HYPERCALL_POST_MESSAGE
 */
#define HYPERCALL_POSTMSGIN_DSIZE_MAX	240
#define HYPERCALL_POSTMSGIN_SIZE	256

struct hypercall_postmsg_in {
	uint32_t	hc_connid;
	uint32_t	hc_rsvd;
	uint32_t	hc_msgtype;	/* HYPERV_MSGTYPE_ */
	uint32_t	hc_dsize;
	uint8_t		hc_data[HYPERCALL_POSTMSGIN_DSIZE_MAX];
} __packed;
CTASSERT(sizeof(struct hypercall_postmsg_in) == HYPERCALL_POSTMSGIN_SIZE);

/*
 * HYPERCALL_SIGNAL_EVENT
 *
 * struct hyperv_mon_param.
 */

#endif	/* !_HYPERV_REG_H_ */
