/*	$OpenBSD: mips_cpu.h,v 1.11 2022/12/11 05:07:25 visa Exp $	*/

/*-
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
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 *	from: @(#)cpu.h	8.4 (Berkeley) 1/4/94
 */

#ifndef _MIPS64_CPUREGS_H_
#define	_MIPS64_CPUREGS_H_

#if defined(_KERNEL) || defined(_STANDALONE)

/*
 * Status register.
 */

#define	SR_COP_USABILITY	0x30000000	/* CP0 and CP1 only */
#define	SR_COP_0_BIT		0x10000000
#define	SR_COP_1_BIT		0x20000000
#define	SR_COP_2_BIT		0x40000000
#define	SR_RP			0x08000000
#define	SR_FR_32		0x04000000
#define	SR_RE			0x02000000
#define	SR_DSD			0x01000000	/* Only on R12000 */
#define	SR_BOOT_EXC_VEC		0x00400000
#define	SR_TLB_SHUTDOWN		0x00200000
#define	SR_SOFT_RESET		0x00100000
#define	SR_DIAG_CH		0x00040000
#define	SR_DIAG_CE		0x00020000
#define	SR_DIAG_DE		0x00010000
#define	SR_KX			0x00000080
#define	SR_SX			0x00000040
#define	SR_UX			0x00000020
#define	SR_ERL			0x00000004
#define	SR_EXL			0x00000002
#define	SR_INT_ENAB		0x00000001

#define	SOFT_INT_MASK_0		0x00000100
#define	SOFT_INT_MASK_1		0x00000200
#define	SR_INT_MASK_0		0x00000400
#define	SR_INT_MASK_1		0x00000800
#define	SR_INT_MASK_2		0x00001000
#define	SR_INT_MASK_3		0x00002000
#define	SR_INT_MASK_4		0x00004000
#define	SR_INT_MASK_5		0x00008000

#define	SR_XX			0x80000000
#define	SR_KSU_MASK		0x00000018
#define	SR_KSU_SUPER		0x00000008
#define	SR_KSU_KERNEL		0x00000000
#define	SR_INT_MASK		0x0000ff00
/* SR_KSU_USER is in <mips64/cpu.h> for CLKF_USERMODE() */
#ifndef SR_KSU_USER
#define	SR_KSU_USER		0x00000010
#endif

#define	SOFT_INT_MASK		(SOFT_INT_MASK_0 | SOFT_INT_MASK_1)

/*
 * Cause register.
 */

#define	CR_BR_DELAY		0x80000000
#define	CR_BR_DELAY_SHIFT	31
#define	CR_EXC_CODE		0x0000007c
#define	CR_EXC_CODE_SHIFT	2
#define	CR_COP_ERR		0x30000000
#define	CR_COP1_ERR		0x10000000
#define	CR_COP2_ERR		0x20000000
#define	CR_COP3_ERR		0x20000000
#define	CR_INT_SOFT0		0x00000100
#define	CR_INT_SOFT1		0x00000200
#define	CR_INT_0		0x00000400
#define	CR_INT_1		0x00000800
#define	CR_INT_2		0x00001000
#define	CR_INT_3		0x00002000
#define	CR_INT_4		0x00004000
#define	CR_INT_5		0x00008000

#define	CR_INT_MASK		0x003fff00

/*
 * Config register.
 */

#define	CFGR_CCA_MASK		0x00000007
#define	CFGR_CU			0x00000008
#define	CFGR_ICE		0x0000000200000000
#define	CFGR_SMM		0x0000000400000000

/*
 * Location of exception vectors.
 */

#define	RESET_EXC_VEC		(CKSEG1_BASE + 0x1fc00000)
#define	TLB_MISS_EXC_VEC	(CKSEG1_BASE + 0x00000000)
#define	XTLB_MISS_EXC_VEC	(CKSEG1_BASE + 0x00000080)
#define	CACHE_ERR_EXC_VEC	(CKSEG1_BASE + 0x00000100)
#define	GEN_EXC_VEC		(CKSEG1_BASE + 0x00000180)

/*
 * Coprocessor 0 registers
 */

/* Common subset */
#define	COP_0_COUNT		$9
#define	COP_0_TLB_HI		$10
#define	COP_0_STATUS_REG	$12
#define	COP_0_CAUSE_REG		$13
#define	COP_0_EXC_PC		$14
#define	COP_0_PRID		$15
#define	COP_0_CONFIG		$16

/* MIPS64 release 2 */
#define	COP_0_USERLOCAL		$4, 2
#define	COP_0_TLB_PG_GRAIN	$5, 1
#define	COP_0_EBASE		$15, 1

/* R4000/5000/10000 */
#define	COP_0_TLB_INDEX		$0
#define	COP_0_TLB_RANDOM	$1
#define	COP_0_TLB_LO0		$2
#define	COP_0_TLB_LO1		$3
#define	COP_0_TLB_CONTEXT	$4
#define	COP_0_TLB_PG_MASK	$5
#define	COP_0_TLB_WIRED		$6
#define	COP_0_BAD_VADDR		$8
#define	COP_0_COMPARE		$11
#define	COP_0_LLADDR		$17
#define	COP_0_WATCH_LO		$18
#define	COP_0_WATCH_HI		$19
#define	COP_0_TLB_XCONTEXT	$20
#define	COP_0_ECC		$26
#define	COP_0_CACHE_ERR		$27
#define	COP_0_TAG_LO		$28
#define	COP_0_TAG_HI		$29
#define	COP_0_ERROR_PC		$30

/* Loongson-2 specific */
#define	COP_0_DIAG		$22

/* Octeon specific */
#define COP_0_CVMCTL		$9, 7
#define COP_0_CVMMEMCTL		$11, 7

/*
 * COP_0_COUNT speed divider.
 */
#if defined(CPU_OCTEON)
#define	CP0_CYCLE_DIVIDER	1
#else
#define	CP0_CYCLE_DIVIDER	2
#endif

/*
 * The floating point version and status registers.
 */
#define	FPC_ID			$0
#define	FPC_CSR			$31

/*
 * Config1 register
 */
#define	CONFIG1_M		0x80000000u
#define	CONFIG1_MMUSize1	0x7e000000u
#define	CONFIG1_MMUSize1_SHIFT	25
#define	CONFIG1_IS		0x01c00000u
#define	CONFIG1_IS_SHIFT	22
#define	CONFIG1_IL		0x00380000u
#define	CONFIG1_IL_SHIFT	19
#define	CONFIG1_IA		0x00070000u
#define	CONFIG1_IA_SHIFT	16
#define	CONFIG1_DS		0x0000e000u
#define	CONFIG1_DS_SHIFT	13
#define	CONFIG1_DL		0x00001c00u
#define	CONFIG1_DL_SHIFT	10
#define	CONFIG1_DA		0x00000380u
#define	CONFIG1_DA_SHIFT	7
#define	CONFIG1_C2		0x00000040u
#define	CONFIG1_MD		0x00000020u
#define	CONFIG1_PC		0x00000010u
#define	CONFIG1_WR		0x00000008u
#define	CONFIG1_CA		0x00000004u
#define	CONFIG1_EP		0x00000002u
#define	CONFIG1_FP		0x00000001u

/*
 * Config3 register
 */
#define	CONFIG3_M		0x80000000
#define	CONFIG3_BPG		0x40000000
#define	CONFIG3_CMGCR		0x20000000
#define	CONFIG3_IPLW		0x00600000
#define	CONFIG3_MMAR		0x001c0000
#define	CONFIG3_MCU		0x00020000
#define	CONFIG3_ISAOnExc	0x00010000
#define	CONFIG3_ISA		0x0000c000
#define	CONFIG3_ULRI		0x00002000
#define	CONFIG3_RXI		0x00001000
#define	CONFIG3_DSP2P		0x00000800
#define	CONFIG3_DSPP		0x00000400
#define	CONFIG3_CTXTC		0x00000200
#define	CONFIG3_ITL		0x00000100
#define	CONFIG3_LPA		0x00000080
#define	CONFIG3_VEIC		0x00000040
#define	CONFIG3_VInt		0x00000020
#define	CONFIG3_SP		0x00000010
#define	CONFIG3_CDMM		0x00000008
#define	CONFIG3_MT		0x00000004
#define	CONFIG3_SM		0x00000002
#define	CONFIG3_TL		0x00000001

/*
 * Config4 register
 */
#define	CONFIG4_M		0x80000000u
#define	CONFIG4_IE		0x60000000u
#define	CONFIG4_AE		0x10000000u
#define	CONFIG4_VTLBSizeExt	0x0f000000u	/* when MMUExtDef=3 */
#define	CONFIG4_KScrExist	0x00ff0000u
#define	CONFIG4_MMUExtDef	0x0000c000u
#define	CONFIG4_MMUExtDef_SHIFT	14
#define	CONFIG4_FTLBPageSize	0x00001f00u	/* when MMUExtDef=2 or 3 */
#define	CONFIG4_FTLBWays	0x000000f0u	/* when MMUExtDef=2 or 3 */
#define	CONFIG4_FTLBSets	0x0000000fu	/* when MMUExtDef=2 or 3 */
#define	CONFIG4_MMUSizeExt	0x000000ffu	/* when MMUExtDef=1 */

/*
 * PageGrain register
 */
#define	PGRAIN_RIE		0x80000000
#define	PGRAIN_XIE		0x40000000
#define	PGRAIN_ELPA		0x20000000
#define	PGRAIN_ESP		0x10000000
#define	PGRAIN_IEC		0x08000000

/*
 * HWREna register
 */
#define	HWRENA_ULR		0x20000000u
#define	HWRENA_CC		0x00000004u

#endif	/* _KERNEL || _STANDALONE */

#endif /* !_MIPS64_CPUREGS_H_ */
