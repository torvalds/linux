/*	$OpenBSD: armreg.h,v 1.43 2019/09/30 21:48:32 kettenis Exp $	*/
/*	$NetBSD: armreg.h,v 1.27 2003/09/06 08:43:02 rearnsha Exp $	*/

/*
 * Copyright (c) 1998, 2001 Ben Harris
 * Copyright (c) 1994-1996 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _ARM_ARMREG_H
#define _ARM_ARMREG_H

/* CCSIDR - Current Cache Size ID Register */
#define	CCSIDR_SETS_MASK	0x0fffe000
#define	CCSIDR_SETS_SHIFT	13
#define	CCSIDR_SETS(reg)	\
    ((((reg) & CCSIDR_SETS_MASK) >> CCSIDR_SETS_SHIFT) + 1)
#define	CCSIDR_WAYS_MASK	0x00001ff8
#define	CCSIDR_WAYS_SHIFT	3
#define	CCSIDR_WAYS(reg)	\
    ((((reg) & CCSIDR_WAYS_MASK) >> CCSIDR_WAYS_SHIFT) + 1)
#define	CCSIDR_LINE_MASK	0x00000007
#define	CCSIDR_LINE_SIZE(reg)	(1 << (((reg) & CCSIDR_LINE_MASK) + 4))

/* CLIDR - Cache Level ID Register */
#define	CLIDR_CTYPE_MASK	0x7
#define	CLIDR_CTYPE_INSN	0x1
#define	CLIDR_CTYPE_DATA	0x2
#define	CLIDR_CTYPE_UNIFIED	0x4

/* CSSELR - Cache Size Selection Register */
#define	CSSELR_IND		(1 << 0)
#define	CSSELR_LEVEL_SHIFT	1

/* CTR - Cache Type Register */
#define	CTR_DLINE_SHIFT		16
#define	CTR_DLINE_MASK		(0xf << CTR_DLINE_SHIFT)
#define	CTR_DLINE_SIZE(reg)	(((reg) & CTR_DLINE_MASK) >> CTR_DLINE_SHIFT)
#define	CTR_IL1P_SHIFT		14
#define	CTR_IL1P_MASK		(0x3 << CTR_IL1P_SHIFT)
#define	CTR_IL1P_AIVIVT		(0x1 << CTR_IL1P_SHIFT)
#define	CTR_IL1P_VIPT		(0x2 << CTR_IL1P_SHIFT)
#define	CTR_IL1P_PIPT		(0x3 << CTR_IL1P_SHIFT)
#define	CTR_ILINE_SHIFT		0
#define	CTR_ILINE_MASK		(0xf << CTR_ILINE_SHIFT)
#define	CTR_ILINE_SIZE(reg)	(((reg) & CTR_ILINE_MASK) >> CTR_ILINE_SHIFT)

/*
 * ARM Process Status Register
 *
 * The picture in early ARM manuals looks like this:
 *       3 3 2 2 2 2                            
 *       1 0 9 8 7 6                                   8 7 6 5 4       0
 *      +-+-+-+-+-+-------------------------------------+-+-+-+---------+
 *      |N|Z|C|V|Q|                reserved             |I|F|T|M M M M M|
 *      | | | | | |                                     | | | |4 3 2 1 0|
 *      +-+-+-+-+-+-------------------------------------+-+-+-+---------+
 *
 * The picture in the ARMv7-A manuals looks like this:
 *       3 3 2 2 2 2 2 2 2     2 1     1 1         1
 *       1 0 9 8 7 6 5 4 3     0 9     6 5         0 9 8 7 6 5 4       0
 *      +-+-+-+-+-+---+-+-------+-------+-----------+-+-+-+-+-+---------+
 *      |N|Z|C|V|Q|I I|J|reserv-|G G G G|I I I I I I|E|A|I|F|T|M M M M M|
 *      | | | | | |T T| |ed     |E E E E|T T T T T T| | | | | |         |
 *      | | | | | |1 0| |       |3 2 1 0|7 6 5 4 3 2| | | | | |4 3 2 1 0|
 *      +-+-+-+-+-+---+-+-------+-------+-----------+-+-+-+-+-+---------+
 *      | flags 'f'     | status 's'    | extension 'x' | control 'c'   |
 */

#define PSR_FLAGS 0xf0000000	/* flags */
#define PSR_N	(1U << 31)	/* negative */
#define PSR_Z	(1 << 30)	/* zero */
#define PSR_C	(1 << 29)	/* carry */
#define PSR_V	(1 << 28)	/* overflow */

#define PSR_Q	(1 << 27)	/* saturation */

#define PSR_A	(1 << 8)	/* Asynchronous abort disable */
#define PSR_I	(1 << 7)	/* IRQ disable */
#define PSR_F	(1 << 6)	/* FIQ disable */

#define PSR_T	(1 << 5)	/* Thumb state */
#define PSR_J	(1 << 24)	/* Java mode */

#define PSR_MODE	0x0000001f	/* mode mask */
#define PSR_USR26_MODE	0x00000000
#define PSR_FIQ26_MODE	0x00000001
#define PSR_IRQ26_MODE	0x00000002
#define PSR_SVC26_MODE	0x00000003
#define PSR_USR32_MODE	0x00000010
#define PSR_FIQ32_MODE	0x00000011
#define PSR_IRQ32_MODE	0x00000012
#define PSR_SVC32_MODE	0x00000013
#define PSR_MON32_MODE	0x00000016
#define PSR_ABT32_MODE	0x00000017
#define PSR_HYP32_MODE	0x0000001a
#define PSR_UND32_MODE	0x0000001b
#define PSR_SYS32_MODE	0x0000001f
#define PSR_32_MODE	0x00000010

#define PSR_IN_USR_MODE(psr)	(!((psr) & 3))		/* XXX */

/*
 * Co-processor 15:  The system control co-processor.
 */

#define ARM_CP15_CPU_ID		0

/*
 * The CPU ID register is theoretically structured, but the definitions of
 * the fields keep changing.
 */

/* The high-order byte is always the implementor */
#define CPU_ID_IMPLEMENTOR_MASK	0xff000000
#define CPU_ID_ARM_LTD		0x41000000 /* 'A' */

#define CPU_ID_ARCH_MASK	0x000f0000
#define CPU_ID_ARCH_V6		0x00070000
#define CPU_ID_ARCH_CPUID	0x000f0000
#define CPU_ID_VARIANT_MASK	0x00f00000

/* Next three nybbles are part number */
#define CPU_ID_PARTNO_MASK	0x0000fff0

/* And finally, the revision number. */
#define CPU_ID_REVISION_MASK	0x0000000f

/* Individual CPUs are probably best IDed by everything but the revision. */
#define CPU_ID_CPU_MASK		0xfffffff0
#define CPU_ID_CORTEX_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A5	0x410fc050
#define CPU_ID_CORTEX_A5_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A7	0x410fc070
#define CPU_ID_CORTEX_A7_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A8_R1	0x411fc080
#define CPU_ID_CORTEX_A8_R2	0x412fc080
#define CPU_ID_CORTEX_A8_R3	0x413fc080
#define CPU_ID_CORTEX_A8	0x410fc080
#define CPU_ID_CORTEX_A8_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A9	0x410fc090
#define CPU_ID_CORTEX_A9_R1	0x411fc090
#define CPU_ID_CORTEX_A9_R2	0x412fc090
#define CPU_ID_CORTEX_A9_R3	0x413fc090
#define CPU_ID_CORTEX_A9_R4	0x414fc090
#define CPU_ID_CORTEX_A9_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A12	0x410fc0d0
#define CPU_ID_CORTEX_A12_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A15	0x410fc0f0
#define CPU_ID_CORTEX_A15_R1	0x411fc0f0
#define CPU_ID_CORTEX_A15_R2	0x412fc0f0
#define CPU_ID_CORTEX_A15_R3	0x413fc0f0
#define CPU_ID_CORTEX_A15_R4	0x414fc0f0
#define CPU_ID_CORTEX_A15_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A17	0x410fc0e0
#define CPU_ID_CORTEX_A17_R1	0x411fc0e0
#define CPU_ID_CORTEX_A17_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A32	0x410fd010
#define CPU_ID_CORTEX_A32_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A35	0x410fd040
#define CPU_ID_CORTEX_A35_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A53	0x410fd030
#define CPU_ID_CORTEX_A53_R1	0x411fd030
#define CPU_ID_CORTEX_A53_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A55	0x410fd050
#define CPU_ID_CORTEX_A55_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A57	0x410fd070
#define CPU_ID_CORTEX_A57_R1	0x411fd070
#define CPU_ID_CORTEX_A57_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A72	0x410fd080
#define CPU_ID_CORTEX_A72_R1	0x411fd080
#define CPU_ID_CORTEX_A72_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A73	0x410fd090
#define CPU_ID_CORTEX_A73_MASK	0xff0ffff0
#define CPU_ID_CORTEX_A75	0x410fd0a0
#define CPU_ID_CORTEX_A75_MASK	0xff0ffff0

/* CPUID on >= v7 */
#define ID_MMFR0_VMSA_MASK	0x0000000f

#define VMSA_V7			3
#define VMSA_V7_PXN		4
#define VMSA_V7_LDT		5

/*
 * Post-ARM3 CP15 registers:
 *
 *	1	Control register
 *
 *	2	Translation Table Base
 *
 *	3	Domain Access Control
 *
 *	4	Reserved
 *
 *	5	Fault Status
 *
 *	6	Fault Address
 *
 *	7	Cache/write-buffer Control
 *
 *	8	TLB Control
 *
 *	9	Cache Lockdown
 *
 *	10	TLB Lockdown
 *
 *	11	Reserved
 *
 *	12	Reserved
 *
 *	13	Process ID (for FCSE)
 *
 *	14	Reserved
 *
 *	15	Implementation Dependent
 */

/* Some of the definitions below need cleaning up for V3/V4 architectures */

/* CPU control register (CP15 register 1) */
#define CPU_CONTROL_MMU_ENABLE	0x00000001 /* M: MMU/Protection unit enable */
#define CPU_CONTROL_AFLT_ENABLE	0x00000002 /* A: Alignment fault enable */
#define CPU_CONTROL_DC_ENABLE	0x00000004 /* C: IDC/DC enable */
#define CPU_CONTROL_WBUF_ENABLE 0x00000008 /* W: Write buffer enable */
#define CPU_CONTROL_32BP_ENABLE 0x00000010 /* P: 32-bit exception handlers */
#define CPU_CONTROL_32BD_ENABLE 0x00000020 /* D: 32-bit addressing */
#define CPU_CONTROL_LABT_ENABLE 0x00000040 /* L: Late abort enable */
#define CPU_CONTROL_BEND_ENABLE 0x00000080 /* B: Big-endian mode */
#define CPU_CONTROL_SYST_ENABLE 0x00000100 /* S: System protection bit */
#define CPU_CONTROL_ROM_ENABLE	0x00000200 /* R: ROM protection bit */
#define CPU_CONTROL_CPCLK	0x00000400 /* F: Implementation defined */
#define CPU_CONTROL_BPRD_ENABLE 0x00000800 /* Z: Branch prediction enable */
#define CPU_CONTROL_IC_ENABLE   0x00001000 /* I: IC enable */
#define CPU_CONTROL_VECRELOC	0x00002000 /* V: Vector relocation */
#define CPU_CONTROL_ROUNDROBIN	0x00004000 /* RR: Predictable replacement */
#define CPU_CONTROL_V4COMPAT	0x00008000 /* L4: ARMv4 compat LDR R15 etc */

/* below were added by V6 */
#define CPU_CONTROL_FI		(1<<21) /* FI: fast interrupts */
#define CPU_CONTROL_U		(1<<22) /* U: Unaligned */
#define CPU_CONTROL_VE		(1<<24) /* VE: Vector enable */
#define CPU_CONTROL_EE		(1<<25) /* EE: Exception Endianness */
#define CPU_CONTROL_L2		(1<<25) /* L2: L2 cache enable */

/* added with v7 */
#define CPU_CONTROL_WXN		(1<<19)	/* WXN: Write implies XN */
#define CPU_CONTROL_UWXN	(1<<20)	/* UWXN: Unpriv write implies XN */
#define CPU_CONTROL_NMFI	(1<<27) /* NMFI: Non Maskable fast interrupt */ 
#define CPU_CONTROL_TRE		(1<<28) /* TRE: TEX Remap Enable */
#define CPU_CONTROL_AFE		(1<<29) /* AFE: Access Flag Enable */
#define CPU_CONTROL_TE		(1<<30) /* TE: Thumb Exception Enable */

#define CPU_CONTROL_IDC_ENABLE	CPU_CONTROL_DC_ENABLE

/* Cortex-A9 Auxiliary Control Register (CP15 register 1, opcode 1) */
#define CORTEXA9_AUXCTL_FW	(1 << 0) /* Cache and TLB updates broadcast */
#define CORTEXA9_AUXCTL_L2PE	(1 << 1) /* Prefetch hint enable */
#define CORTEXA9_AUXCTL_L1PE	(1 << 2) /* Data prefetch hint enable */
#define CORTEXA9_AUXCTL_WR_ZERO	(1 << 3) /* Ena. write full line of 0s mode */
#define CORTEXA9_AUXCTL_SMP	(1 << 6) /* Coherency is active */
#define CORTEXA9_AUXCTL_EXCL	(1 << 7) /* Exclusive cache bit */
#define CORTEXA9_AUXCTL_ONEWAY	(1 << 8) /* Allocate in on cache way only */
#define CORTEXA9_AUXCTL_PARITY	(1 << 9) /* Support parity checking */

/* Cache type register definitions */
#define CPU_CT_ISIZE(x)		((x) & 0xfff)		/* I$ info */
#define CPU_CT_DSIZE(x)		(((x) >> 12) & 0xfff)	/* D$ info */
#define CPU_CT_S		(1U << 24)		/* split cache */
#define CPU_CT_CTYPE(x)		(((x) >> 25) & 0xf)	/* cache type */
/* Cache type register definitions for ARM v7 */
#define CPU_CT_IMINLINE(x)	((x) & 0xf)		/* I$ min line size */
#define CPU_CT_DMINLINE(x)	(((x) >> 16) & 0xf)	/* D$ min line size */

#define CPU_CT_CTYPE_WT		0	/* write-through */
#define CPU_CT_CTYPE_WB1	1	/* write-back, clean w/ read */
#define CPU_CT_CTYPE_WB2	2	/* w/b, clean w/ cp15,7 */
#define CPU_CT_CTYPE_WB6	6	/* w/b, cp15,7, lockdown fmt A */
#define CPU_CT_CTYPE_WB7	7	/* w/b, cp15,7, lockdown fmt B */

#define CPU_CT_xSIZE_LEN(x)	((x) & 0x3)		/* line size */
#define CPU_CT_xSIZE_M		(1U << 2)		/* multiplier */
#define CPU_CT_xSIZE_ASSOC(x)	(((x) >> 3) & 0x7)	/* associativity */
#define CPU_CT_xSIZE_SIZE(x)	(((x) >> 6) & 0x7)	/* size */

/* MPIDR, Multiprocessor Affinity Register */
#define MPIDR_AFF2		(0xffU << 16)
#define MPIDR_AFF1		(0xffU << 8)
#define MPIDR_AFF0		(0xffU << 0)
#define MPIDR_AFF		(MPIDR_AFF2|MPIDR_AFF1|MPIDR_AFF0)

/* Fault status register definitions */

#define FAULT_USER      0x20

#define FAULT_WRTBUF_0  0x00 /* Vector Exception */
#define FAULT_WRTBUF_1  0x02 /* Terminal Exception */
#define FAULT_BUSERR_0  0x04 /* External Abort on Linefetch -- Section */
#define FAULT_BUSERR_1  0x06 /* External Abort on Linefetch -- Page */
#define FAULT_BUSERR_2  0x08 /* External Abort on Non-linefetch -- Section */
#define FAULT_BUSERR_3  0x0a /* External Abort on Non-linefetch -- Page */
#define FAULT_BUSTRNL1  0x0c /* External abort on Translation -- Level 1 */
#define FAULT_BUSTRNL2  0x0e /* External abort on Translation -- Level 2 */
#define FAULT_ALIGN_0   0x01 /* Alignment */
#define FAULT_ALIGN_1   0x03 /* Alignment */
#define FAULT_TRANS_S   0x05 /* Translation -- Section */
#define FAULT_TRANS_P   0x07 /* Translation -- Page */
#define FAULT_DOMAIN_S  0x09 /* Domain -- Section */
#define FAULT_DOMAIN_P  0x0b /* Domain -- Page */
#define FAULT_PERM_S    0x0d /* Permission -- Section */
#define FAULT_PERM_P    0x0f /* Permission -- Page */

/* Fault type definitions for ARM v7 */
#define FAULT_ACCESS_1	0x03 /* Access flag fault -- Level 1 */
#define FAULT_ACCESS_2	0x06 /* Access flag fault -- Level 2 */

#define FAULT_IMPRECISE	0x400	/* Imprecise exception (XSCALE) */

#define	FAULT_EXT	0x00001000	/* external abort */
#define	FAULT_WNR	0x00000800	/* write fault */

#define	FAULT_TYPE(fsr)		((fsr) & 0x0f)
#define	FAULT_TYPE_V7(fsr)	(((fsr) & 0x0f) | (((fsr) & 0x00000400) >> 6))

/*
 * Address of the vector page, low and high versions.
 */
#define ARM_VECTORS_LOW		0x00000000U
#define ARM_VECTORS_HIGH	0xffff0000U

/*
 * ARM Instructions
 *
 *       3 3 2 2 2                              
 *       1 0 9 8 7                                                     0
 *      +-------+-------------------------------------------------------+
 *      | cond  |              instruction dependant                    |
 *      |c c c c|                                                       |
 *      +-------+-------------------------------------------------------+
 */

#define INSN_SIZE		4		/* Always 4 bytes */
#define INSN_COND_MASK		0xf0000000	/* Condition mask */
#define INSN_COND_AL		0xe0000000	/* Always condition */

/* Translation Table Base Register */
#define TTBR_C			(1 << 0)	/* without MPE */
#define TTBR_S			(1 << 1)
#define TTBR_IMP		(1 << 2)
#define TTBR_RGN_MASK		(3 << 3)
#define  TTBR_RGN_NC		(0 << 3)
#define  TTBR_RGN_WBWA		(1 << 3)
#define  TTBR_RGN_WT		(2 << 3)
#define  TTBR_RGN_WBNWA		(3 << 3)
#define TTBR_NOS		(1 << 5)
#define TTBR_IRGN_MASK		((1 << 0) | (1 << 6))
#define  TTBR_IRGN_NC		((0 << 0) | (0 << 6))
#define  TTBR_IRGN_WBWA		((0 << 0) | (1 << 6))
#define  TTBR_IRGN_WT		((1 << 0) | (0 << 6))
#define  TTBR_IRGN_WBNWA	((1 << 0) | (1 << 6))

#endif
