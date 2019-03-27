/*	$NetBSD: cpuregs.h,v 1.70 2006/05/15 02:26:54 simonb Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)machConst.h 8.1 (Berkeley) 6/10/93
 *
 * machConst.h --
 *
 *	Machine dependent constants.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machConst.h,
 *	v 9.2 89/10/21 15:55:22 jhh Exp	 SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAddrs.h,
 *	v 1.2 89/08/15 18:28:21 rab Exp	 SPRITE (DECWRL)
 * from: Header: /sprite/src/kernel/vm/ds3100.md/RCS/vmPmaxConst.h,
 *	v 9.1 89/09/18 17:33:00 shirriff Exp  SPRITE (DECWRL)
 *
 * $FreeBSD$
 */

#ifndef _MIPS_CPUREGS_H_
#define	_MIPS_CPUREGS_H_

#ifndef _KVM_MINIDUMP
#include <machine/cca.h>
#endif

/*
 * Address space.
 * 32-bit mips CPUS partition their 32-bit address space into four segments:
 *
 * kuseg   0x00000000 - 0x7fffffff  User virtual mem,  mapped
 * kseg0   0x80000000 - 0x9fffffff  Physical memory, cached, unmapped
 * kseg1   0xa0000000 - 0xbfffffff  Physical memory, uncached, unmapped
 * kseg2   0xc0000000 - 0xffffffff  kernel-virtual,  mapped
 *
 * Caching of mapped addresses is controlled by bits in the TLB entry.
 */

#define	MIPS_KSEG0_LARGEST_PHYS		(0x20000000)
#define	MIPS_KSEG0_PHYS_MASK		(0x1fffffff)
#define	MIPS_XKPHYS_LARGEST_PHYS	(0x10000000000)  /* 40 bit PA */
#define	MIPS_XKPHYS_PHYS_MASK		(0x0ffffffffff)

#ifndef LOCORE
#define	MIPS_KUSEG_START		0x00000000
#define	MIPS_KSEG0_START		((intptr_t)(int32_t)0x80000000)
#define	MIPS_KSEG0_END			((intptr_t)(int32_t)0x9fffffff)
#define	MIPS_KSEG1_START		((intptr_t)(int32_t)0xa0000000)
#define	MIPS_KSEG1_END			((intptr_t)(int32_t)0xbfffffff)
#define	MIPS_KSSEG_START		((intptr_t)(int32_t)0xc0000000)
#define	MIPS_KSSEG_END			((intptr_t)(int32_t)0xdfffffff)
#define	MIPS_KSEG3_START		((intptr_t)(int32_t)0xe0000000)
#define	MIPS_KSEG3_END			((intptr_t)(int32_t)0xffffffff)
#define MIPS_KSEG2_START		MIPS_KSSEG_START
#define MIPS_KSEG2_END			MIPS_KSSEG_END
#endif

#define	MIPS_PHYS_TO_KSEG0(x)		((uintptr_t)(x) | MIPS_KSEG0_START)
#define	MIPS_PHYS_TO_KSEG1(x)		((uintptr_t)(x) | MIPS_KSEG1_START)
#define	MIPS_KSEG0_TO_PHYS(x)		((uintptr_t)(x) & MIPS_KSEG0_PHYS_MASK)
#define	MIPS_KSEG1_TO_PHYS(x)		((uintptr_t)(x) & MIPS_KSEG0_PHYS_MASK)

#define	MIPS_IS_KSEG0_ADDR(x)					\
	(((vm_offset_t)(x) >= MIPS_KSEG0_START) &&		\
	    ((vm_offset_t)(x) <= MIPS_KSEG0_END))
#define	MIPS_IS_KSEG1_ADDR(x)					\
	(((vm_offset_t)(x) >= MIPS_KSEG1_START) &&		\
	    ((vm_offset_t)(x) <= MIPS_KSEG1_END))
#define	MIPS_IS_VALID_PTR(x)		(MIPS_IS_KSEG0_ADDR(x) || \
					    MIPS_IS_KSEG1_ADDR(x))

#define	MIPS_PHYS_TO_XKPHYS(cca,x) \
	((0x2ULL << 62) | ((unsigned long long)(cca) << 59) | (x))
#define	MIPS_PHYS_TO_XKPHYS_CACHED(x) \
	((0x2ULL << 62) | ((unsigned long long)(MIPS_CCA_CACHED) << 59) | (x))
#define	MIPS_PHYS_TO_XKPHYS_UNCACHED(x) \
	((0x2ULL << 62) | ((unsigned long long)(MIPS_CCA_UNCACHED) << 59) | (x))

#define	MIPS_XKPHYS_TO_PHYS(x)		((uintptr_t)(x) & MIPS_XKPHYS_PHYS_MASK)

#define	MIPS_XKPHYS_START		0x8000000000000000
#define	MIPS_XKPHYS_END			0xbfffffffffffffff
#define	MIPS_XUSEG_START		0x0000000000000000
#define	MIPS_XUSEG_END			0x0000010000000000
#define	MIPS_XKSEG_START		0xc000000000000000
#define	MIPS_XKSEG_END			0xc00000ff80000000
#define	MIPS_XKSEG_COMPAT32_START	0xffffffff80000000
#define	MIPS_XKSEG_COMPAT32_END		0xffffffffffffffff
#define	MIPS_XKSEG_TO_COMPAT32(va)	((va) & 0xffffffff)

#ifdef __mips_n64
#define	MIPS_DIRECT_MAPPABLE(pa)	1
#define	MIPS_PHYS_TO_DIRECT(pa)		MIPS_PHYS_TO_XKPHYS_CACHED(pa)
#define	MIPS_PHYS_TO_DIRECT_UNCACHED(pa)	MIPS_PHYS_TO_XKPHYS_UNCACHED(pa)
#define	MIPS_DIRECT_TO_PHYS(va)		MIPS_XKPHYS_TO_PHYS(va)
#else
#define	MIPS_DIRECT_MAPPABLE(pa)	((pa) < MIPS_KSEG0_LARGEST_PHYS)
#define	MIPS_PHYS_TO_DIRECT(pa)		MIPS_PHYS_TO_KSEG0(pa)
#define	MIPS_PHYS_TO_DIRECT_UNCACHED(pa)	MIPS_PHYS_TO_KSEG1(pa)
#define	MIPS_DIRECT_TO_PHYS(va)		MIPS_KSEG0_TO_PHYS(va)
#endif

/* CPU dependent mtc0 hazard hook */
#if defined(CPU_CNMIPS) || defined(CPU_RMI)
#define	COP0_SYNC
#elif defined(CPU_NLM)
#define	COP0_SYNC	.word 0xc0	/* ehb */
#elif defined(CPU_SB1)
#define COP0_SYNC  ssnop; ssnop; ssnop; ssnop; ssnop; ssnop; ssnop; ssnop; ssnop
#elif defined(CPU_MIPS24K) || defined(CPU_MIPS34K) ||		\
      defined(CPU_MIPS74K) || defined(CPU_MIPS1004K)  ||	\
      defined(CPU_MIPS1074K) || defined(CPU_INTERAPTIV) ||	\
      defined(CPU_PROAPTIV)
/*
 * According to MIPS32tm Architecture for Programmers, Vol.II, rev. 2.00:
 * "As EHB becomes standard in MIPS implementations, the previous SSNOPs can be
 *  removed, leaving only the EHB".
 * Also, all MIPS32 Release 2 implementations have the EHB instruction, which
 * resolves all execution hazards. The same goes for MIPS32 Release 3.
 */
#define	COP0_SYNC	.word 0xc0	/* ehb */
#else
/*
 * Pick a reasonable default based on the "typical" spacing described in the
 * "CP0 Hazards" chapter of MIPS Architecture Book Vol III.
 */
#define	COP0_SYNC  ssnop; ssnop; ssnop; ssnop; .word 0xc0;
#endif
#define	COP0_HAZARD_FPUENABLE	nop; nop; nop; nop;

/*
 * The bits in the cause register.
 *
 * Bits common to r3000 and r4000:
 *
 *	MIPS_CR_BR_DELAY	Exception happened in branch delay slot.
 *	MIPS_CR_COP_ERR		Coprocessor error.
 *	MIPS_CR_IP		Interrupt pending bits defined below.
 *				(same meaning as in CAUSE register).
 *	MIPS_CR_EXC_CODE	The exception type (see exception codes below).
 *
 * Differences:
 *  r3k has 4 bits of execption type, r4k has 5 bits.
 */
#define	MIPS_CR_BR_DELAY	0x80000000
#define	MIPS_CR_COP_ERR		0x30000000
#define	MIPS_CR_EXC_CODE	0x0000007C	/* five bits */
#define	MIPS_CR_IP		0x0000FF00
#define	MIPS_CR_EXC_CODE_SHIFT	2
#define	MIPS_CR_COP_ERR_SHIFT	28

/*
 * The bits in the status register.  All bits are active when set to 1.
 *
 *	R3000 status register fields:
 *	MIPS_SR_COP_USABILITY	Control the usability of the four coprocessors.
 *	MIPS_SR_TS		TLB shutdown.
 *
 *	MIPS_SR_INT_IE		Master (current) interrupt enable bit.
 *
 * Differences:
 *	r3k has cache control is via frobbing SR register bits, whereas the
 *	r4k cache control is via explicit instructions.
 *	r3k has a 3-entry stack of kernel/user bits, whereas the
 *	r4k has kernel/supervisor/user.
 */
#define	MIPS_SR_COP_USABILITY	0xf0000000
#define	MIPS_SR_COP_0_BIT	0x10000000
#define	MIPS_SR_COP_1_BIT	0x20000000
#define MIPS_SR_COP_2_BIT       0x40000000

	/* r4k and r3k differences, see below */

#define	MIPS_SR_MX		0x01000000	/* MIPS64 */
#define	MIPS_SR_PX		0x00800000	/* MIPS64 */
#define	MIPS_SR_BEV		0x00400000	/* Use boot exception vector */
#define	MIPS_SR_TS		0x00200000
#define MIPS_SR_DE		0x00010000

#define	MIPS_SR_INT_IE		0x00000001
/*#define MIPS_SR_MBZ		0x0f8000c0*/	/* Never used, true for r3k */
#define MIPS_SR_INT_MASK	0x0000ff00

/*
 * R4000 status register bit definitons,
 * where different from r2000/r3000.
 */
#define	MIPS_SR_XX		0x80000000
#define	MIPS_SR_RP		0x08000000
#define	MIPS_SR_FR		0x04000000
#define	MIPS_SR_RE		0x02000000

#define	MIPS_SR_DIAG_DL	0x01000000		/* QED 52xx */
#define	MIPS_SR_DIAG_IL	0x00800000		/* QED 52xx */
#define	MIPS_SR_SR		0x00100000
#define	MIPS_SR_NMI		0x00080000		/* MIPS32/64 */
#define	MIPS_SR_DIAG_CH	0x00040000
#define	MIPS_SR_DIAG_CE	0x00020000
#define	MIPS_SR_DIAG_PE	0x00010000
#define	MIPS_SR_EIE		0x00010000		/* TX79/R5900 */
#define	MIPS_SR_KX		0x00000080
#define	MIPS_SR_SX		0x00000040
#define	MIPS_SR_UX		0x00000020
#define	MIPS_SR_KSU_MASK	0x00000018
#define	MIPS_SR_KSU_USER	0x00000010
#define	MIPS_SR_KSU_SUPER	0x00000008
#define	MIPS_SR_KSU_KERNEL	0x00000000
#define	MIPS_SR_ERL		0x00000004
#define	MIPS_SR_EXL		0x00000002

/*
 * The interrupt masks.
 * If a bit in the mask is 1 then the interrupt is enabled (or pending).
 */
#define	MIPS_INT_MASK		0xff00
#define	MIPS_INT_MASK_5		0x8000
#define	MIPS_INT_MASK_4		0x4000
#define	MIPS_INT_MASK_3		0x2000
#define	MIPS_INT_MASK_2		0x1000
#define	MIPS_INT_MASK_1		0x0800
#define	MIPS_INT_MASK_0		0x0400
#define	MIPS_HARD_INT_MASK	0xfc00
#define	MIPS_SOFT_INT_MASK_1	0x0200
#define	MIPS_SOFT_INT_MASK_0	0x0100

/*
 * The bits in the MIPS3 config register.
 *
 *	bit 0..5: R/W, Bit 6..31: R/O
 */

/* kseg0 coherency algorithm - see MIPS3_TLB_ATTR values */
#define	MIPS_CONFIG_K0_MASK	0x00000007

/*
 * R/W Update on Store Conditional
 *	0: Store Conditional uses coherency algorithm specified by TLB
 *	1: Store Conditional uses cacheable coherent update on write
 */
#define	MIPS_CONFIG_CU		0x00000008

#define	MIPS_CONFIG_DB		0x00000010	/* Primary D-cache line size */
#define	MIPS_CONFIG_IB		0x00000020	/* Primary I-cache line size */
#define	MIPS_CONFIG_CACHE_L1_LSIZE(config, bit) \
	(((config) & (bit)) ? 32 : 16)

#define	MIPS_CONFIG_DC_MASK	0x000001c0	/* Primary D-cache size */
#define	MIPS_CONFIG_DC_SHIFT	6
#define	MIPS_CONFIG_IC_MASK	0x00000e00	/* Primary I-cache size */
#define	MIPS_CONFIG_IC_SHIFT	9
#define	MIPS_CONFIG_C_DEFBASE	0x1000		/* default base 2^12 */

/* Cache size mode indication: available only on Vr41xx CPUs */
#define	MIPS_CONFIG_CS		0x00001000
#define	MIPS_CONFIG_C_4100BASE	0x0400		/* base is 2^10 if CS=1 */
#define	MIPS_CONFIG_CACHE_SIZE(config, mask, base, shift) \
	((base) << (((config) & (mask)) >> (shift)))

/* External cache enable: Controls L2 for R5000/Rm527x and L3 for Rm7000 */
#define	MIPS_CONFIG_SE		0x00001000

/* Block ordering: 0: sequential, 1: sub-block */
#define	MIPS_CONFIG_EB		0x00002000

/* ECC mode - 0: ECC mode, 1: parity mode */
#define	MIPS_CONFIG_EM		0x00004000

/* BigEndianMem - 0: kernel and memory are little endian, 1: big endian */
#define	MIPS_CONFIG_BE		0x00008000

/* Dirty Shared coherency state - 0: enabled, 1: disabled */
#define	MIPS_CONFIG_SM		0x00010000

/* Secondary Cache - 0: present, 1: not present */
#define	MIPS_CONFIG_SC		0x00020000

/* System Port width - 0: 64-bit, 1: 32-bit (QED RM523x), 2,3: reserved */
#define	MIPS_CONFIG_EW_MASK	0x000c0000
#define	MIPS_CONFIG_EW_SHIFT	18

/* Secondary Cache port width - 0: 128-bit data path to S-cache, 1: reserved */
#define	MIPS_CONFIG_SW		0x00100000

/* Split Secondary Cache Mode - 0: I/D mixed, 1: I/D separated by SCAddr(17) */
#define	MIPS_CONFIG_SS		0x00200000

/* Secondary Cache line size */
#define	MIPS_CONFIG_SB_MASK	0x00c00000
#define	MIPS_CONFIG_SB_SHIFT	22
#define	MIPS_CONFIG_CACHE_L2_LSIZE(config) \
	(0x10 << (((config) & MIPS_CONFIG_SB_MASK) >> MIPS_CONFIG_SB_SHIFT))

/* Write back data rate */
#define	MIPS_CONFIG_EP_MASK	0x0f000000
#define	MIPS_CONFIG_EP_SHIFT	24

/* System clock ratio - this value is CPU dependent */
#define	MIPS_CONFIG_EC_MASK	0x70000000
#define	MIPS_CONFIG_EC_SHIFT	28

/* Master-Checker Mode - 1: enabled */
#define	MIPS_CONFIG_CM		0x80000000

/*
 * The bits in the MIPS4 config register.
 */

/*
 * Location of exception vectors.
 *
 * Common vectors:  reset and UTLB miss.
 */
#define	MIPS_RESET_EXC_VEC	((intptr_t)(int32_t)0xBFC00000)
#define	MIPS_UTLB_MISS_EXC_VEC	((intptr_t)(int32_t)0x80000000)

/*
 * MIPS-III exception vectors
 */
#define	MIPS_XTLB_MISS_EXC_VEC ((intptr_t)(int32_t)0x80000080)
#define	MIPS_CACHE_ERR_EXC_VEC ((intptr_t)(int32_t)0x80000100)
#define	MIPS_GEN_EXC_VEC	((intptr_t)(int32_t)0x80000180)

/*
 * MIPS32/MIPS64 (and some MIPS3) dedicated interrupt vector.
 */
#define	MIPS_INTR_EXC_VEC	0x80000200

/*
 * Coprocessor 0 registers:
 *
 *				v--- width for mips I,III,32,64
 *				     (3=32bit, 6=64bit, i=impl dep)
 *  0	MIPS_COP_0_TLB_INDEX	3333 TLB Index.
 *  1	MIPS_COP_0_TLB_RANDOM	3333 TLB Random.
 *  2	MIPS_COP_0_TLB_LO0	.636 r4k TLB entry low.
 *  3	MIPS_COP_0_TLB_LO1	.636 r4k TLB entry low, extended.
 *  4	MIPS_COP_0_TLB_CONTEXT	3636 TLB Context.
 *  4/2	MIPS_COP_0_USERLOCAL	..36 UserLocal.
 *  5	MIPS_COP_0_TLB_PG_MASK	.333 TLB Page Mask register.
 *  6	MIPS_COP_0_TLB_WIRED	.333 Wired TLB number.
 *  7	MIPS_COP_0_HWRENA	..33 rdHWR Enable.
 *  8	MIPS_COP_0_BAD_VADDR	3636 Bad virtual address.
 *  9	MIPS_COP_0_COUNT	.333 Count register.
 * 10	MIPS_COP_0_TLB_HI	3636 TLB entry high.
 * 11	MIPS_COP_0_COMPARE	.333 Compare (against Count).
 * 12	MIPS_COP_0_STATUS	3333 Status register.
 * 12/1	MIPS_COP_0_INTCTL	..33 Interrupt setup (MIPS32/64 r2).
 * 13	MIPS_COP_0_CAUSE	3333 Exception cause register.
 * 14	MIPS_COP_0_EXC_PC	3636 Exception PC.
 * 15	MIPS_COP_0_PRID		3333 Processor revision identifier.
 * 16	MIPS_COP_0_CONFIG	3333 Configuration register.
 * 16/1	MIPS_COP_0_CONFIG1	..33 Configuration register 1.
 * 16/2	MIPS_COP_0_CONFIG2	..33 Configuration register 2.
 * 16/3	MIPS_COP_0_CONFIG3	..33 Configuration register 3.
 * 16/4 MIPS_COP_0_CONFIG4	..33 Configuration register 4.
 * 17	MIPS_COP_0_LLADDR	.336 Load Linked Address.
 * 18	MIPS_COP_0_WATCH_LO	.336 WatchLo register.
 * 19	MIPS_COP_0_WATCH_HI	.333 WatchHi register.
 * 20	MIPS_COP_0_TLB_XCONTEXT .6.6 TLB XContext register.
 * 23	MIPS_COP_0_DEBUG	.... Debug JTAG register.
 * 24	MIPS_COP_0_DEPC		.... DEPC JTAG register.
 * 25	MIPS_COP_0_PERFCNT	..36 Performance Counter register.
 * 26	MIPS_COP_0_ECC		.3ii ECC / Error Control register.
 * 27	MIPS_COP_0_CACHE_ERR	.3ii Cache Error register.
 * 28/0	MIPS_COP_0_TAG_LO	.3ii Cache TagLo register (instr).
 * 28/1	MIPS_COP_0_DATA_LO	..ii Cache DataLo register (instr).
 * 28/2	MIPS_COP_0_TAG_LO	..ii Cache TagLo register (data).
 * 28/3	MIPS_COP_0_DATA_LO	..ii Cache DataLo register (data).
 * 29/0	MIPS_COP_0_TAG_HI	.3ii Cache TagHi register (instr).
 * 29/1	MIPS_COP_0_DATA_HI	..ii Cache DataHi register (instr).
 * 29/2	MIPS_COP_0_TAG_HI	..ii Cache TagHi register (data).
 * 29/3	MIPS_COP_0_DATA_HI	..ii Cache DataHi register (data).
 * 30	MIPS_COP_0_ERROR_PC	.636 Error EPC register.
 * 31	MIPS_COP_0_DESAVE	.... DESAVE JTAG register.
 */

/* Deal with inclusion from an assembly file. */
#if defined(_LOCORE) || defined(LOCORE)
#define	_(n)	$n
#else
#define	_(n)	n
#endif


#define	MIPS_COP_0_TLB_INDEX	_(0)
#define	MIPS_COP_0_TLB_RANDOM	_(1)
	/* Name and meaning of	TLB bits for $2 differ on r3k and r4k. */

#define	MIPS_COP_0_TLB_CONTEXT	_(4)
					/* $5 and $6 new with MIPS-III */
#define	MIPS_COP_0_BAD_VADDR	_(8)
#define	MIPS_COP_0_TLB_HI	_(10)
#define	MIPS_COP_0_STATUS	_(12)
#define	MIPS_COP_0_CAUSE	_(13)
#define	MIPS_COP_0_EXC_PC	_(14)
#define	MIPS_COP_0_PRID		_(15)

/* MIPS-III */
#define	MIPS_COP_0_TLB_LO0	_(2)
#define	MIPS_COP_0_TLB_LO1	_(3)

#define	MIPS_COP_0_TLB_PG_MASK	_(5)
#define	MIPS_COP_0_TLB_WIRED	_(6)

#define	MIPS_COP_0_COUNT	_(9)
#define	MIPS_COP_0_COMPARE	_(11)
#ifdef CPU_XBURST
#define	MIPS_COP_0_XBURST_C12	_(12)
#endif
#define	MIPS_COP_0_CONFIG	_(16)
#define	MIPS_COP_0_LLADDR	_(17)
#define	MIPS_COP_0_WATCH_LO	_(18)
#define	MIPS_COP_0_WATCH_HI	_(19)
#define	MIPS_COP_0_TLB_XCONTEXT _(20)
#ifdef CPU_XBURST
#define	MIPS_COP_0_XBURST_MBOX	_(20)
#endif

#define	MIPS_COP_0_ECC		_(26)
#define	MIPS_COP_0_CACHE_ERR	_(27)
#define	MIPS_COP_0_TAG_LO	_(28)
#define	MIPS_COP_0_TAG_HI	_(29)
#define	MIPS_COP_0_ERROR_PC	_(30)

/* MIPS32/64 */
#define	MIPS_COP_0_USERLOCAL	_(4)	/* sel 2 is userlevel register */
#define	MIPS_COP_0_HWRENA	_(7)
#define	MIPS_COP_0_INTCTL	_(12)
#define	MIPS_COP_0_DEBUG	_(23)
#define	MIPS_COP_0_DEPC		_(24)
#define	MIPS_COP_0_PERFCNT	_(25)
#define	MIPS_COP_0_DATA_LO	_(28)
#define	MIPS_COP_0_DATA_HI	_(29)
#define	MIPS_COP_0_DESAVE	_(31)

/* MIPS32 Config register definitions */
#define MIPS_MMU_NONE			0x00		/* No MMU present */
#define MIPS_MMU_TLB			0x01		/* Standard TLB */
#define MIPS_MMU_BAT			0x02		/* Standard BAT */
#define MIPS_MMU_FIXED			0x03		/* Standard fixed mapping */

/*
 * IntCtl Register Fields
 */
#define	MIPS_INTCTL_IPTI_MASK	0xE0000000	/* bits 31..29 timer intr # */
#define	MIPS_INTCTL_IPTI_SHIFT	29
#define	MIPS_INTCTL_IPPCI_MASK	0x1C000000	/* bits 26..29 perf counter intr # */
#define	MIPS_INTCTL_IPPCI_SHIFT	26
#define	MIPS_INTCTL_VS_MASK	0x000001F0	/* bits 5..9 vector spacing */
#define	MIPS_INTCTL_VS_SHIFT	4

/*
 * Config Register Fields
 * (See "MIPS Architecture for Programmers Volume III", MD00091, Table 9.39)
 */
#define	MIPS_CONFIG0_M		0x80000000 	/* Flag: Config1 is present. */
#define	MIPS_CONFIG0_MT_MASK	0x00000380	/* bits 9..7 MMU Type */
#define	MIPS_CONFIG0_MT_SHIFT	7
#define	MIPS_CONFIG0_BE		0x00008000	/* data is big-endian */
#define	MIPS_CONFIG0_VI		0x00000008	/* inst cache is virtual */
 
/*
 * Config1 Register Fields
 * (See "MIPS Architecture for Programmers Volume III", MD00091, Table 9-1)
 */
#define	MIPS_CONFIG1_M		0x80000000	/* Flag: Config2 is present. */
#define MIPS_CONFIG1_TLBSZ_MASK		0x7E000000	/* bits 30..25 # tlb entries minus one */
#define MIPS_CONFIG1_TLBSZ_SHIFT	25

#define MIPS_CONFIG1_IS_MASK		0x01C00000	/* bits 24..22 icache sets per way */
#define MIPS_CONFIG1_IS_SHIFT		22
#define MIPS_CONFIG1_IL_MASK		0x00380000	/* bits 21..19 icache line size */
#define MIPS_CONFIG1_IL_SHIFT		19
#define MIPS_CONFIG1_IA_MASK		0x00070000	/* bits 18..16 icache associativity */
#define MIPS_CONFIG1_IA_SHIFT		16
#define MIPS_CONFIG1_DS_MASK		0x0000E000	/* bits 15..13 dcache sets per way */
#define MIPS_CONFIG1_DS_SHIFT		13
#define MIPS_CONFIG1_DL_MASK		0x00001C00	/* bits 12..10 dcache line size */
#define MIPS_CONFIG1_DL_SHIFT		10
#define MIPS_CONFIG1_DA_MASK		0x00000380	/* bits  9.. 7 dcache associativity */
#define MIPS_CONFIG1_DA_SHIFT		7
#define MIPS_CONFIG1_LOWBITS		0x0000007F
#define MIPS_CONFIG1_C2			0x00000040	/* Coprocessor 2 implemented */
#define MIPS_CONFIG1_MD			0x00000020	/* MDMX ASE implemented (MIPS64) */
#define MIPS_CONFIG1_PC			0x00000010	/* Performance counters implemented */
#define MIPS_CONFIG1_WR			0x00000008	/* Watch registers implemented */
#define MIPS_CONFIG1_CA			0x00000004	/* MIPS16e ISA implemented */
#define MIPS_CONFIG1_EP			0x00000002	/* EJTAG implemented */
#define MIPS_CONFIG1_FP			0x00000001	/* FPU implemented */

#define MIPS_CONFIG2_SA_SHIFT		0		/* Secondary cache associativity */
#define MIPS_CONFIG2_SA_MASK		0xf
#define MIPS_CONFIG2_SL_SHIFT		4		/* Secondary cache line size */
#define MIPS_CONFIG2_SL_MASK		0xf
#define MIPS_CONFIG2_SS_SHIFT		8		/* Secondary cache sets per way */
#define MIPS_CONFIG2_SS_MASK		0xf

#define MIPS_CONFIG3_CMGCR_MASK		(1 << 29)	/* Coherence manager present */

/*
 * Config2 Register Fields
 * (See "MIPS Architecture for Programmers Volume III", MD00091, Table 9.40)
 */
#define	MIPS_CONFIG2_M		0x80000000	/* Flag: Config3 is present. */

/*
 * Config3 Register Fields
 * (See "MIPS Architecture for Programmers Volume III", MD00091, Table 9.41)
 */
#define	MIPS_CONFIG3_M		0x80000000	/* Flag: Config4 is present */
#define	MIPS_CONFIG3_ULR	0x00002000	/* UserLocal reg implemented */

#define MIPS_CONFIG4_MMUSIZEEXT		0x000000FF	/* bits 7.. 0 MMU Size Extension */
#define MIPS_CONFIG4_MMUEXTDEF		0x0000C000	/* bits 15.14 MMU Extension Definition */
#define MIPS_CONFIG4_MMUEXTDEF_MMUSIZEEXT	0x00004000 /* This values denotes CONFIG4 bits  */

/*
 * Values for the code field in a break instruction.
 */
#define	MIPS_BREAK_INSTR	0x0000000d
#define	MIPS_BREAK_VAL_MASK	0x03ff0000
#define	MIPS_BREAK_VAL_SHIFT	16
#define	MIPS_BREAK_KDB_VAL	512
#define	MIPS_BREAK_SSTEP_VAL	513
#define	MIPS_BREAK_BRKPT_VAL	514
#define	MIPS_BREAK_SOVER_VAL	515
#define	MIPS_BREAK_DDB_VAL	516
#define	MIPS_BREAK_KDB		(MIPS_BREAK_INSTR | \
				(MIPS_BREAK_KDB_VAL << MIPS_BREAK_VAL_SHIFT))
#define	MIPS_BREAK_SSTEP	(MIPS_BREAK_INSTR | \
				(MIPS_BREAK_SSTEP_VAL << MIPS_BREAK_VAL_SHIFT))
#define	MIPS_BREAK_BRKPT	(MIPS_BREAK_INSTR | \
				(MIPS_BREAK_BRKPT_VAL << MIPS_BREAK_VAL_SHIFT))
#define	MIPS_BREAK_SOVER	(MIPS_BREAK_INSTR | \
				(MIPS_BREAK_SOVER_VAL << MIPS_BREAK_VAL_SHIFT))
#define	MIPS_BREAK_DDB		(MIPS_BREAK_INSTR | \
				(MIPS_BREAK_DDB_VAL << MIPS_BREAK_VAL_SHIFT))

/*
 * Mininum and maximum cache sizes.
 */
#define	MIPS_MIN_CACHE_SIZE	(16 * 1024)
#define	MIPS_MAX_CACHE_SIZE	(256 * 1024)
#define	MIPS_MAX_PCACHE_SIZE	(32 * 1024)	/* max. primary cache size */

/*
 * The floating point version and status registers.
 */
#define	MIPS_FPU_ID	$0
#define	MIPS_FPU_CSR	$31

/*
 * The floating point coprocessor status register bits.
 */
#define	MIPS_FPU_ROUNDING_BITS		0x00000003
#define	MIPS_FPU_ROUND_RN		0x00000000
#define	MIPS_FPU_ROUND_RZ		0x00000001
#define	MIPS_FPU_ROUND_RP		0x00000002
#define	MIPS_FPU_ROUND_RM		0x00000003
#define	MIPS_FPU_STICKY_BITS		0x0000007c
#define	MIPS_FPU_STICKY_INEXACT		0x00000004
#define	MIPS_FPU_STICKY_UNDERFLOW	0x00000008
#define	MIPS_FPU_STICKY_OVERFLOW	0x00000010
#define	MIPS_FPU_STICKY_DIV0		0x00000020
#define	MIPS_FPU_STICKY_INVALID		0x00000040
#define	MIPS_FPU_ENABLE_BITS		0x00000f80
#define	MIPS_FPU_ENABLE_INEXACT		0x00000080
#define	MIPS_FPU_ENABLE_UNDERFLOW	0x00000100
#define	MIPS_FPU_ENABLE_OVERFLOW	0x00000200
#define	MIPS_FPU_ENABLE_DIV0		0x00000400
#define	MIPS_FPU_ENABLE_INVALID		0x00000800
#define	MIPS_FPU_EXCEPTION_BITS		0x0003f000
#define	MIPS_FPU_EXCEPTION_INEXACT	0x00001000
#define	MIPS_FPU_EXCEPTION_UNDERFLOW	0x00002000
#define	MIPS_FPU_EXCEPTION_OVERFLOW	0x00004000
#define	MIPS_FPU_EXCEPTION_DIV0		0x00008000
#define	MIPS_FPU_EXCEPTION_INVALID	0x00010000
#define	MIPS_FPU_EXCEPTION_UNIMPL	0x00020000
#define	MIPS_FPU_COND_BIT		0x00800000
#define	MIPS_FPU_FLUSH_BIT		0x01000000	/* r4k,	 MBZ on r3k */
#define	MIPS_FPC_MBZ_BITS		0xfe7c0000


/*
 * Constants to determine if have a floating point instruction.
 */
#define	MIPS_OPCODE_SHIFT	26
#define	MIPS_OPCODE_C1		0x11

/* Coherence manager constants */
#define	MIPS_CMGCRB_BASE	11
#define	MIPS_CMGCRF_BASE	(~((1 << MIPS_CMGCRB_BASE) - 1))

/*
 * Bits defined for for the HWREna (CP0 register 7, select 0).
 */
#define	MIPS_HWRENA_CPUNUM	(1<<0)	/* CPU number program is running on */
#define	MIPS_HWRENA_SYNCI_STEP 	(1<<1)	/* Address step sized used with SYNCI */
#define	MIPS_HWRENA_CC		(1<<2)	/* Hi Res cycle counter */
#define	MIPS_HWRENA_CCRES	(1<<3)	/* Cycle counter resolution */
#define	MIPS_HWRENA_UL		(1<<29)	/* UserLocal Register */
#define	MIPS_HWRENA_IMPL30	(1<<30)	/* Implementation-dependent 30 */
#define	MIPS_HWRENA_IMPL31	(1<<31)	/* Implementation-dependent 31 */

#endif /* _MIPS_CPUREGS_H_ */
