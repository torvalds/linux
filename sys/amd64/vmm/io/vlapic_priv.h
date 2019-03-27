/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef _VLAPIC_PRIV_H_
#define	_VLAPIC_PRIV_H_

#include <x86/apicreg.h>

/*
 * APIC Register:		Offset	   Description
 */
#define APIC_OFFSET_ID		0x20	/* Local APIC ID		*/
#define APIC_OFFSET_VER		0x30	/* Local APIC Version		*/
#define APIC_OFFSET_TPR		0x80	/* Task Priority Register	*/
#define APIC_OFFSET_APR		0x90	/* Arbitration Priority		*/
#define APIC_OFFSET_PPR		0xA0	/* Processor Priority Register	*/
#define APIC_OFFSET_EOI		0xB0	/* EOI Register			*/
#define APIC_OFFSET_RRR		0xC0	/* Remote read			*/
#define APIC_OFFSET_LDR		0xD0	/* Logical Destination		*/
#define APIC_OFFSET_DFR		0xE0	/* Destination Format Register	*/
#define APIC_OFFSET_SVR		0xF0	/* Spurious Vector Register	*/
#define APIC_OFFSET_ISR0	0x100	/* In Service Register		*/
#define APIC_OFFSET_ISR1	0x110
#define APIC_OFFSET_ISR2	0x120
#define APIC_OFFSET_ISR3	0x130
#define APIC_OFFSET_ISR4	0x140
#define APIC_OFFSET_ISR5	0x150
#define APIC_OFFSET_ISR6	0x160
#define APIC_OFFSET_ISR7	0x170
#define APIC_OFFSET_TMR0	0x180	/* Trigger Mode Register	*/
#define APIC_OFFSET_TMR1	0x190
#define APIC_OFFSET_TMR2	0x1A0
#define APIC_OFFSET_TMR3	0x1B0
#define APIC_OFFSET_TMR4	0x1C0
#define APIC_OFFSET_TMR5	0x1D0
#define APIC_OFFSET_TMR6	0x1E0
#define APIC_OFFSET_TMR7	0x1F0
#define APIC_OFFSET_IRR0	0x200	/* Interrupt Request Register	*/
#define APIC_OFFSET_IRR1	0x210
#define APIC_OFFSET_IRR2	0x220
#define APIC_OFFSET_IRR3	0x230
#define APIC_OFFSET_IRR4	0x240
#define APIC_OFFSET_IRR5	0x250
#define APIC_OFFSET_IRR6	0x260
#define APIC_OFFSET_IRR7	0x270
#define APIC_OFFSET_ESR		0x280	/* Error Status Register	*/
#define APIC_OFFSET_CMCI_LVT	0x2F0	/* Local Vector Table (CMCI)	*/
#define APIC_OFFSET_ICR_LOW	0x300	/* Interrupt Command Register	*/
#define APIC_OFFSET_ICR_HI	0x310
#define APIC_OFFSET_TIMER_LVT	0x320	/* Local Vector Table (Timer)	*/
#define APIC_OFFSET_THERM_LVT	0x330	/* Local Vector Table (Thermal)	*/
#define APIC_OFFSET_PERF_LVT	0x340	/* Local Vector Table (PMC)	*/
#define APIC_OFFSET_LINT0_LVT	0x350	/* Local Vector Table (LINT0)	*/
#define APIC_OFFSET_LINT1_LVT	0x360	/* Local Vector Table (LINT1)	*/
#define APIC_OFFSET_ERROR_LVT	0x370	/* Local Vector Table (ERROR)	*/
#define APIC_OFFSET_TIMER_ICR	0x380	/* Timer's Initial Count	*/
#define APIC_OFFSET_TIMER_CCR	0x390	/* Timer's Current Count	*/
#define APIC_OFFSET_TIMER_DCR	0x3E0	/* Timer's Divide Configuration	*/
#define	APIC_OFFSET_SELF_IPI	0x3F0	/* Self IPI register */

#define	VLAPIC_CTR0(vlapic, format)					\
	VCPU_CTR0((vlapic)->vm, (vlapic)->vcpuid, format)

#define	VLAPIC_CTR1(vlapic, format, p1)					\
	VCPU_CTR1((vlapic)->vm, (vlapic)->vcpuid, format, p1)

#define	VLAPIC_CTR2(vlapic, format, p1, p2)				\
	VCPU_CTR2((vlapic)->vm, (vlapic)->vcpuid, format, p1, p2)

#define	VLAPIC_CTR3(vlapic, format, p1, p2, p3)				\
	VCPU_CTR3((vlapic)->vm, (vlapic)->vcpuid, format, p1, p2, p3)

#define	VLAPIC_CTR_IRR(vlapic, msg)					\
do {									\
	uint32_t *irrptr = &(vlapic)->apic_page->irr0;			\
	irrptr[0] = irrptr[0];	/* silence compiler */			\
	VLAPIC_CTR1((vlapic), msg " irr0 0x%08x", irrptr[0 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr1 0x%08x", irrptr[1 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr2 0x%08x", irrptr[2 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr3 0x%08x", irrptr[3 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr4 0x%08x", irrptr[4 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr5 0x%08x", irrptr[5 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr6 0x%08x", irrptr[6 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr7 0x%08x", irrptr[7 << 2]);	\
} while (0)

#define	VLAPIC_CTR_ISR(vlapic, msg)					\
do {									\
	uint32_t *isrptr = &(vlapic)->apic_page->isr0;			\
	isrptr[0] = isrptr[0];	/* silence compiler */			\
	VLAPIC_CTR1((vlapic), msg " isr0 0x%08x", isrptr[0 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr1 0x%08x", isrptr[1 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr2 0x%08x", isrptr[2 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr3 0x%08x", isrptr[3 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr4 0x%08x", isrptr[4 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr5 0x%08x", isrptr[5 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr6 0x%08x", isrptr[6 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr7 0x%08x", isrptr[7 << 2]);	\
} while (0)

enum boot_state {
	BS_INIT,
	BS_SIPI,
	BS_RUNNING
};

/*
 * 16 priority levels with at most one vector injected per level.
 */
#define	ISRVEC_STK_SIZE		(16 + 1)

#define VLAPIC_MAXLVT_INDEX	APIC_LVT_CMCI

struct vlapic;

struct vlapic_ops {
	int (*set_intr_ready)(struct vlapic *vlapic, int vector, bool level);
	int (*pending_intr)(struct vlapic *vlapic, int *vecptr);
	void (*intr_accepted)(struct vlapic *vlapic, int vector);
	void (*post_intr)(struct vlapic *vlapic, int hostcpu);
	void (*set_tmr)(struct vlapic *vlapic, int vector, bool level);
	void (*enable_x2apic_mode)(struct vlapic *vlapic);
};

struct vlapic {
	struct vm		*vm;
	int			vcpuid;
	struct LAPIC		*apic_page;
	struct vlapic_ops	ops;

	uint32_t		esr_pending;
	int			esr_firing;

	struct callout	callout;	/* vlapic timer */
	struct bintime	timer_fire_bt;	/* callout expiry time */
	struct bintime	timer_freq_bt;	/* timer frequency */
	struct bintime	timer_period_bt; /* timer period */
	struct mtx	timer_mtx;

	/*
	 * The 'isrvec_stk' is a stack of vectors injected by the local apic.
	 * A vector is popped from the stack when the processor does an EOI.
	 * The vector on the top of the stack is used to compute the
	 * Processor Priority in conjunction with the TPR.
	 */
	uint8_t		isrvec_stk[ISRVEC_STK_SIZE];
	int		isrvec_stk_top;

	uint64_t	msr_apicbase;
	enum boot_state	boot_state;

	/*
	 * Copies of some registers in the virtual APIC page. We do this for
	 * a couple of different reasons:
	 * - to be able to detect what changed (e.g. svr_last)
	 * - to maintain a coherent snapshot of the register (e.g. lvt_last)
	 */
	uint32_t	svr_last;
	uint32_t	lvt_last[VLAPIC_MAXLVT_INDEX + 1];
};

void vlapic_init(struct vlapic *vlapic);
void vlapic_cleanup(struct vlapic *vlapic);

#endif	/* _VLAPIC_PRIV_H_ */
