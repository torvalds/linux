/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2006-2012 Semihalf.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_TLB_H_
#define	_MACHINE_TLB_H_

#if defined(BOOKE_E500)

/*  PowerPC E500 MAS registers */
#define MAS0_TLBSEL(x)		((x << 28) & 0x10000000)
#define MAS0_ESEL(x)		((x << 16) & 0x003F0000)

#define MAS0_TLBSEL1		0x10000000
#define MAS0_TLBSEL0		0x00000000
#define MAS0_ESEL_TLB1MASK	0x000F0000
#define MAS0_ESEL_TLB0MASK	0x00030000
#define MAS0_ESEL_SHIFT		16
#define MAS0_NV_MASK		0x00000003
#define MAS0_NV_SHIFT		0

#define MAS1_VALID		0x80000000
#define MAS1_IPROT		0x40000000
#define MAS1_TID_MASK		0x00FF0000
#define MAS1_TID_SHIFT		16
#define MAS1_TS_MASK		0x00001000
#define MAS1_TS_SHIFT		12
#define MAS1_TSIZE_MASK		0x00000F00
#define MAS1_TSIZE_SHIFT	8

#define	TLB_SIZE_4K		1
#define	TLB_SIZE_16K		2
#define	TLB_SIZE_64K		3
#define	TLB_SIZE_256K		4
#define	TLB_SIZE_1M		5
#define	TLB_SIZE_4M		6
#define	TLB_SIZE_16M		7
#define	TLB_SIZE_64M		8
#define	TLB_SIZE_256M		9
#define	TLB_SIZE_1G		10
#define	TLB_SIZE_4G		11

#ifdef __powerpc64__
#define	MAS2_EPN_MASK		0xFFFFFFFFFFFFF000UL
#else
#define	MAS2_EPN_MASK		0xFFFFF000
#endif
#define	MAS2_EPN_SHIFT		12
#define	MAS2_X0			0x00000040
#define	MAS2_X1			0x00000020
#define	MAS2_W			0x00000010
#define	MAS2_I			0x00000008
#define	MAS2_M			0x00000004
#define	MAS2_G			0x00000002
#define	MAS2_E			0x00000001
#define	MAS2_WIMGE_MASK		0x0000007F

#define	MAS3_RPN		0xFFFFF000
#define	MAS3_RPN_SHIFT		12
#define	MAS3_U0			0x00000200
#define	MAS3_U1			0x00000100
#define	MAS3_U2			0x00000080
#define	MAS3_U3			0x00000040
#define	MAS3_UX			0x00000020
#define	MAS3_SX			0x00000010
#define	MAS3_UW			0x00000008
#define	MAS3_SW			0x00000004
#define	MAS3_UR			0x00000002
#define	MAS3_SR			0x00000001

#define MAS4_TLBSELD1		0x10000000
#define MAS4_TLBSELD0		0x00000000
#define MAS4_TIDSELD_MASK	0x00030000
#define MAS4_TIDSELD_SHIFT	16
#define MAS4_TSIZED_MASK	0x00000F00
#define MAS4_TSIZED_SHIFT	8
#define MAS4_X0D		0x00000040
#define MAS4_X1D		0x00000020
#define MAS4_WD			0x00000010
#define MAS4_ID			0x00000008
#define MAS4_MD			0x00000004
#define MAS4_GD			0x00000002
#define MAS4_ED			0x00000001

#define MAS6_SPID0_MASK		0x00FF0000
#define MAS6_SPID0_SHIFT	16
#define MAS6_SAS		0x00000001

#define MAS7_RPN		0x0000000F

#define MAS1_GETTID(mas1)	(((mas1) & MAS1_TID_MASK) >> MAS1_TID_SHIFT)

#define MAS2_TLB0_ENTRY_IDX_MASK	0x0007f000
#define MAS2_TLB0_ENTRY_IDX_SHIFT	12

/*
 * Maximum number of TLB1 entries used for a permanent mapping of kernel
 * region (kernel image plus statically allocated data).
 */
#define KERNEL_REGION_MAX_TLB_ENTRIES   4

/*
 * Use MAS2_X0 to mark entries which will be copied
 * to AP CPUs during SMP bootstrap. As result entries
 * marked with _TLB_ENTRY_SHARED will be shared by all CPUs.
 */
#define _TLB_ENTRY_SHARED	(MAS2_X0)	/* XXX under SMP? */
#define _TLB_ENTRY_IO	(MAS2_I | MAS2_G)
#define _TLB_ENTRY_MEM	(MAS2_M)

#define TLB1_MAX_ENTRIES	64

#if !defined(LOCORE)
typedef struct tlb_entry {
	vm_paddr_t phys;
	vm_offset_t virt;
	vm_size_t size;
	uint32_t mas1;
#ifdef __powerpc64__
	uint64_t mas2;
#else
	uint32_t mas2;
#endif
	uint32_t mas3;
	uint32_t mas7;
} tlb_entry_t;

void tlb1_inval_entry(unsigned int);
void tlb1_init(void);
#endif /* !LOCORE */

#elif defined(BOOKE_PPC4XX)

/* TLB Words */
#define	TLB_PAGEID		0
#define	TLB_XLAT		1
#define	TLB_ATTRIB		2

/* Page identification fields */
#define	TLB_EPN_MASK		(0xFFFFFC00 >> 0)
#define	TLB_VALID		(0x80000000 >> 22)
#define	TLB_TS			(0x80000000 >> 23)
#define	TLB_SIZE_1K		(0x00000000 >> 24)
#define	TLB_SIZE_MASK		(0xF0000000 >> 24)

/* Translation fields */
#define	TLB_RPN_MASK		(0xFFFFFC00 >> 0)
#define	TLB_ERPN_MASK		(0xF0000000 >> 28)

/* Storage attribute and access control fields */
#define	TLB_WL1			(0x80000000 >> 11)
#define	TLB_IL1I		(0x80000000 >> 12)
#define	TLB_IL1D		(0x80000000 >> 13)
#define	TLB_IL2I		(0x80000000 >> 14)
#define	TLB_IL2D		(0x80000000 >> 15)
#define	TLB_U0			(0x80000000 >> 16)
#define	TLB_U1			(0x80000000 >> 17)
#define	TLB_U2			(0x80000000 >> 18)
#define	TLB_U3			(0x80000000 >> 19)
#define	TLB_W			(0x80000000 >> 20)
#define	TLB_I			(0x80000000 >> 21)
#define	TLB_M			(0x80000000 >> 22)
#define	TLB_G			(0x80000000 >> 23)
#define	TLB_E			(0x80000000 >> 24)
#define	TLB_UX			(0x80000000 >> 26)
#define	TLB_UW			(0x80000000 >> 27)
#define	TLB_UR			(0x80000000 >> 28)
#define	TLB_SX			(0x80000000 >> 29)
#define	TLB_SW			(0x80000000 >> 30)
#define	TLB_SR			(0x80000000 >> 31)
#define	TLB_SIZE		64

#define	TLB_SIZE_4K		(0x10000000 >> 24)
#define	TLB_SIZE_16K		(0x20000000 >> 24)
#define	TLB_SIZE_64K		(0x30000000 >> 24)
#define	TLB_SIZE_256K		(0x40000000 >> 24)
#define	TLB_SIZE_1M		(0x50000000 >> 24)
#define	TLB_SIZE_16M		(0x70000000 >> 24)
#define	TLB_SIZE_256M		(0x90000000 >> 24)
#define	TLB_SIZE_1G		(0xA0000000 >> 24)

#endif /* BOOKE_E500 */

#define TID_KERNEL	0	/* TLB TID to use for kernel (shared) translations */
#define TID_KRESERVED	1	/* Number of TIDs reserved for kernel */
#define TID_URESERVED	0	/* Number of TIDs reserved for user */
#define TID_MIN		(TID_KRESERVED + TID_URESERVED)
#define TID_MAX		255
#define TID_NONE	-1

#define TLB_UNLOCKED	0

#if !defined(LOCORE)

typedef int tlbtid_t;

struct pmap;

void tlb_lock(uintptr_t *);
void tlb_unlock(uintptr_t *);
void tlb1_ap_prep(void);
int  tlb1_set_entry(vm_offset_t, vm_paddr_t, vm_size_t, uint32_t);

#endif /* !LOCORE */

#endif	/* _MACHINE_TLB_H_ */
