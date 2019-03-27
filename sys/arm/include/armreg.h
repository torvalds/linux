/*	$NetBSD: armreg.h,v 1.37 2007/01/06 00:50:54 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *
 * $FreeBSD$
 */

#ifndef MACHINE_ARMREG_H
#define MACHINE_ARMREG_H

#ifndef _SYS_CDEFS_H_
#error Please include sys/cdefs.h before including machine/armreg.h
#endif

#define INSN_SIZE	4
#define INSN_COND_MASK	0xf0000000	/* Condition mask */
#define PSR_MODE        0x0000001f      /* mode mask */
#define PSR_USR32_MODE  0x00000010
#define PSR_FIQ32_MODE  0x00000011
#define PSR_IRQ32_MODE  0x00000012
#define PSR_SVC32_MODE  0x00000013
#define PSR_MON32_MODE	0x00000016
#define PSR_ABT32_MODE  0x00000017
#define PSR_HYP32_MODE	0x0000001a
#define PSR_UND32_MODE  0x0000001b
#define PSR_SYS32_MODE  0x0000001f
#define PSR_32_MODE     0x00000010
#define PSR_T		0x00000020	/* Instruction set bit */
#define PSR_F		0x00000040	/* FIQ disable bit */
#define PSR_I		0x00000080	/* IRQ disable bit */
#define PSR_A		0x00000100	/* Imprecise abort bit */
#define PSR_E		0x00000200	/* Data endianess bit */
#define PSR_GE		0x000f0000	/* Greater than or equal to bits */
#define PSR_J		0x01000000	/* Java bit */
#define PSR_Q		0x08000000	/* Sticky overflow bit */
#define PSR_V		0x10000000	/* Overflow bit */
#define PSR_C		0x20000000	/* Carry bit */
#define PSR_Z		0x40000000	/* Zero bit */
#define PSR_N		0x80000000	/* Negative bit */
#define PSR_FLAGS	0xf0000000	/* Flags mask. */

/* The high-order byte is always the implementor */
#define CPU_ID_IMPLEMENTOR_MASK	0xff000000
#define CPU_ID_ARM_LTD		0x41000000 /* 'A' */
#define CPU_ID_DEC		0x44000000 /* 'D' */
#define	CPU_ID_MOTOROLA		0x4D000000 /* 'M' */
#define	CPU_ID_QUALCOM		0x51000000 /* 'Q' */
#define	CPU_ID_TI		0x54000000 /* 'T' */
#define	CPU_ID_MARVELL		0x56000000 /* 'V' */
#define	CPU_ID_INTEL		0x69000000 /* 'i' */
#define	CPU_ID_FARADAY		0x66000000 /* 'f' */

#define	CPU_ID_VARIANT_SHIFT	20
#define	CPU_ID_VARIANT_MASK	0x00f00000

/* How to decide what format the CPUID is in. */
#define CPU_ID_ISOLD(x)		(((x) & 0x0000f000) == 0x00000000)
#define CPU_ID_IS7(x)		(((x) & 0x0000f000) == 0x00007000)
#define CPU_ID_ISNEW(x)		(!CPU_ID_ISOLD(x) && !CPU_ID_IS7(x))

/* On recent ARMs this byte holds the architecture and variant (sub-model) */
#define CPU_ID_ARCH_MASK	0x000f0000
#define CPU_ID_ARCH_V3		0x00000000
#define CPU_ID_ARCH_V4		0x00010000
#define CPU_ID_ARCH_V4T		0x00020000
#define CPU_ID_ARCH_V5		0x00030000
#define CPU_ID_ARCH_V5T		0x00040000
#define CPU_ID_ARCH_V5TE	0x00050000
#define CPU_ID_ARCH_V5TEJ	0x00060000
#define CPU_ID_ARCH_V6		0x00070000
#define CPU_ID_CPUID_SCHEME	0x000f0000

/* Next three nybbles are part number */
#define CPU_ID_PARTNO_MASK	0x0000fff0

/* Intel XScale has sub fields in part number */
#define CPU_ID_XSCALE_COREGEN_MASK	0x0000e000 /* core generation */
#define CPU_ID_XSCALE_COREREV_MASK	0x00001c00 /* core revision */
#define CPU_ID_XSCALE_PRODUCT_MASK	0x000003f0 /* product number */

/* And finally, the revision number. */
#define CPU_ID_REVISION_MASK	0x0000000f

/* Individual CPUs are probably best IDed by everything but the revision. */
#define CPU_ID_CPU_MASK		0xfffffff0

/* ARM9 and later CPUs */
#define CPU_ID_ARM920T		0x41129200
#define CPU_ID_ARM920T_ALT	0x41009200
#define CPU_ID_ARM922T		0x41029220
#define CPU_ID_ARM926EJS	0x41069260
#define CPU_ID_ARM940T		0x41029400 /* XXX no MMU */
#define CPU_ID_ARM946ES		0x41049460 /* XXX no MMU */
#define	CPU_ID_ARM966ES		0x41049660 /* XXX no MMU */
#define	CPU_ID_ARM966ESR1	0x41059660 /* XXX no MMU */
#define CPU_ID_ARM1020E		0x4115a200 /* (AKA arm10 rev 1) */
#define CPU_ID_ARM1022ES	0x4105a220
#define CPU_ID_ARM1026EJS	0x4106a260
#define CPU_ID_ARM1136JS	0x4107b360
#define CPU_ID_ARM1136JSR1	0x4117b360
#define CPU_ID_ARM1176JZS	0x410fb760

/* CPUs that follow the CPUID scheme */
#define	CPU_ID_SCHEME_MASK	\
    (CPU_ID_IMPLEMENTOR_MASK | CPU_ID_ARCH_MASK | CPU_ID_PARTNO_MASK)

#define	CPU_ID_CORTEXA5		(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc050)
#define	CPU_ID_CORTEXA7		(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc070)
#define	CPU_ID_CORTEXA8		(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc080)
#define	 CPU_ID_CORTEXA8R1	(CPU_ID_CORTEXA8 | (1 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA8R2	(CPU_ID_CORTEXA8 | (2 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA8R3	(CPU_ID_CORTEXA8 | (3 << CPU_ID_VARIANT_SHIFT))
#define	CPU_ID_CORTEXA9		(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc090)
#define	 CPU_ID_CORTEXA9R1	(CPU_ID_CORTEXA9 | (1 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA9R2	(CPU_ID_CORTEXA9 | (2 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA9R3	(CPU_ID_CORTEXA9 | (3 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA9R4	(CPU_ID_CORTEXA9 | (4 << CPU_ID_VARIANT_SHIFT))
/* XXX: Cortex-A12 is the old name for this part, it has been renamed the A17 */
#define	CPU_ID_CORTEXA12	(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc0d0)
#define	 CPU_ID_CORTEXA12R0	(CPU_ID_CORTEXA12 | (0 << CPU_ID_VARIANT_SHIFT))
#define	CPU_ID_CORTEXA15	(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xc0f0)
#define	 CPU_ID_CORTEXA15R0	(CPU_ID_CORTEXA15 | (0 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA15R1	(CPU_ID_CORTEXA15 | (1 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA15R2	(CPU_ID_CORTEXA15 | (2 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_CORTEXA15R3	(CPU_ID_CORTEXA15 | (3 << CPU_ID_VARIANT_SHIFT))
#define	CPU_ID_CORTEXA53	(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xd030)
#define	CPU_ID_CORTEXA57	(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xd070)
#define	CPU_ID_CORTEXA72	(CPU_ID_ARM_LTD | CPU_ID_CPUID_SCHEME | 0xd080)

#define	CPU_ID_KRAIT300		(CPU_ID_QUALCOM | CPU_ID_CPUID_SCHEME | 0x06f0)
/* Snapdragon S4 Pro/APQ8064 */
#define	 CPU_ID_KRAIT300R0	(CPU_ID_KRAIT300 | (0 << CPU_ID_VARIANT_SHIFT))
#define	 CPU_ID_KRAIT300R1	(CPU_ID_KRAIT300 | (1 << CPU_ID_VARIANT_SHIFT))

#define	CPU_ID_TI925T		0x54029250
#define CPU_ID_MV88FR131	0x56251310 /* Marvell Feroceon 88FR131 Core */
#define CPU_ID_MV88FR331	0x56153310 /* Marvell Feroceon 88FR331 Core */
#define CPU_ID_MV88FR571_VD	0x56155710 /* Marvell Feroceon 88FR571-VD Core (ID from datasheet) */

/*
 * LokiPlus core has also ID set to 0x41159260 and this define cause execution of unsupported
 * L2-cache instructions so need to disable it. 0x41159260 is a generic ARM926E-S ID.
 */
#ifdef SOC_MV_LOKIPLUS
#define CPU_ID_MV88FR571_41	0x00000000
#else
#define CPU_ID_MV88FR571_41	0x41159260 /* Marvell Feroceon 88FR571-VD Core (actual ID from CPU reg) */
#endif

#define CPU_ID_MV88SV581X_V7		0x561F5810 /* Marvell Sheeva 88SV581x v7 Core */
#define CPU_ID_MV88SV584X_V7		0x562F5840 /* Marvell Sheeva 88SV584x v7 Core */
/* Marvell's CPUIDs with ARM ID in implementor field */
#define CPU_ID_ARM_88SV581X_V7		0x413FC080 /* Marvell Sheeva 88SV581x v7 Core */

#define	CPU_ID_FA526		0x66015260
#define	CPU_ID_FA626TE		0x66056260
#define CPU_ID_80200		0x69052000
#define CPU_ID_PXA250    	0x69052100 /* sans core revision */
#define CPU_ID_PXA210    	0x69052120
#define CPU_ID_PXA250A		0x69052100 /* 1st version Core */
#define CPU_ID_PXA210A		0x69052120 /* 1st version Core */
#define CPU_ID_PXA250B		0x69052900 /* 3rd version Core */
#define CPU_ID_PXA210B		0x69052920 /* 3rd version Core */
#define CPU_ID_PXA250C		0x69052d00 /* 4th version Core */
#define CPU_ID_PXA210C		0x69052d20 /* 4th version Core */
#define	CPU_ID_PXA27X		0x69054110
#define	CPU_ID_80321_400	0x69052420
#define	CPU_ID_80321_600	0x69052430
#define	CPU_ID_80321_400_B0	0x69052c20
#define	CPU_ID_80321_600_B0	0x69052c30
#define	CPU_ID_80219_400	0x69052e20 /* A0 stepping/revision. */
#define	CPU_ID_80219_600	0x69052e30 /* A0 stepping/revision. */
#define	CPU_ID_81342		0x69056810
#define	CPU_ID_IXP425		0x690541c0
#define	CPU_ID_IXP425_533	0x690541c0
#define	CPU_ID_IXP425_400	0x690541d0
#define	CPU_ID_IXP425_266	0x690541f0
#define	CPU_ID_IXP435		0x69054040
#define	CPU_ID_IXP465		0x69054200

/* CPUID registers */
#define ARM_PFR0_ARM_ISA_MASK	0x0000000f

#define ARM_PFR0_THUMB_MASK	0x000000f0
#define ARM_PFR0_THUMB		0x10
#define ARM_PFR0_THUMB2		0x30

#define ARM_PFR0_JAZELLE_MASK	0x00000f00
#define ARM_PFR0_THUMBEE_MASK	0x0000f000

#define ARM_PFR1_ARMV4_MASK	0x0000000f
#define ARM_PFR1_SEC_EXT_MASK	0x000000f0
#define ARM_PFR1_MICROCTRL_MASK	0x00000f00

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
#define CPU_CONTROL_SW_ENABLE	0x00000400 /* SW: SWP instruction enable */
#define CPU_CONTROL_BPRD_ENABLE 0x00000800 /* Z: Branch prediction enable */
#define CPU_CONTROL_IC_ENABLE   0x00001000 /* I: IC enable */
#define CPU_CONTROL_VECRELOC	0x00002000 /* V: Vector relocation */
#define CPU_CONTROL_ROUNDROBIN	0x00004000 /* RR: Predictable replacement */
#define CPU_CONTROL_V4COMPAT	0x00008000 /* L4: ARMv4 compat LDR R15 etc */
#define CPU_CONTROL_HAF_ENABLE	0x00020000 /* HA: Hardware Access Flag Enable */
#define CPU_CONTROL_FI_ENABLE	0x00200000 /* FI: Low interrupt latency */
#define CPU_CONTROL_UNAL_ENABLE 0x00400000 /* U: unaligned data access */
#define CPU_CONTROL_V6_EXTPAGE	0x00800000 /* XP: ARMv6 extended page tables */
#define CPU_CONTROL_V_ENABLE	0x01000000 /* VE: Interrupt vectors enable */
#define CPU_CONTROL_EX_BEND	0x02000000 /* EE: exception endianness */
#define CPU_CONTROL_L2_ENABLE	0x04000000 /* L2 Cache enabled */
#define CPU_CONTROL_NMFI	0x08000000 /* NMFI: Non maskable FIQ */
#define CPU_CONTROL_TR_ENABLE	0x10000000 /* TRE: TEX Remap*/
#define CPU_CONTROL_AF_ENABLE	0x20000000 /* AFE: Access Flag enable */
#define CPU_CONTROL_TE_ENABLE	0x40000000 /* TE: Thumb Exception enable */

#define CPU_CONTROL_IDC_ENABLE	CPU_CONTROL_DC_ENABLE

/* ARM11x6 Auxiliary Control Register (CP15 register 1, opcode2 1) */
#define	ARM11X6_AUXCTL_RS	0x00000001 /* return stack */
#define	ARM11X6_AUXCTL_DB	0x00000002 /* dynamic branch prediction */
#define	ARM11X6_AUXCTL_SB	0x00000004 /* static branch prediction */
#define	ARM11X6_AUXCTL_TR	0x00000008 /* MicroTLB replacement strat. */
#define	ARM11X6_AUXCTL_EX	0x00000010 /* exclusive L1/L2 cache */
#define	ARM11X6_AUXCTL_RA	0x00000020 /* clean entire cache disable */
#define	ARM11X6_AUXCTL_RV	0x00000040 /* block transfer cache disable */
#define	ARM11X6_AUXCTL_CZ	0x00000080 /* restrict cache size */

/* ARM1136 Auxiliary Control Register (CP15 register 1, opcode2 1) */
#define ARM1136_AUXCTL_PFI	0x80000000 /* PFI: partial FI mode. */
					   /* This is an undocumented flag
					    * used to work around a cache bug
					    * in r0 steppings. See errata
					    * 364296.
					    */
/* ARM1176 Auxiliary Control Register (CP15 register 1, opcode2 1) */
#define	ARM1176_AUXCTL_PHD	0x10000000 /* inst. prefetch halting disable */
#define	ARM1176_AUXCTL_BFD	0x20000000 /* branch folding disable */
#define	ARM1176_AUXCTL_FSD	0x40000000 /* force speculative ops disable */
#define	ARM1176_AUXCTL_FIO	0x80000000 /* low intr latency override */

/* XScale Auxillary Control Register (CP15 register 1, opcode2 1) */
#define	XSCALE_AUXCTL_K		0x00000001 /* dis. write buffer coalescing */
#define	XSCALE_AUXCTL_P		0x00000002 /* ECC protect page table access */
/* Note: XSCale core 3 uses those for LLR DCcahce attributes */
#define	XSCALE_AUXCTL_MD_WB_RA	0x00000000 /* mini-D$ wb, read-allocate */
#define	XSCALE_AUXCTL_MD_WB_RWA	0x00000010 /* mini-D$ wb, read/write-allocate */
#define	XSCALE_AUXCTL_MD_WT	0x00000020 /* mini-D$ wt, read-allocate */
#define	XSCALE_AUXCTL_MD_MASK	0x00000030

/* Xscale Core 3 only */
#define XSCALE_AUXCTL_LLR	0x00000400 /* Enable L2 for LLR Cache */

/* Marvell Extra Features Register (CP15 register 1, opcode2 0) */
#define MV_DC_REPLACE_LOCK	0x80000000 /* Replace DCache Lock */
#define MV_DC_STREAM_ENABLE	0x20000000 /* DCache Streaming Switch */
#define MV_WA_ENABLE		0x10000000 /* Enable Write Allocate */
#define MV_L2_PREFETCH_DISABLE	0x01000000 /* L2 Cache Prefetch Disable */
#define MV_L2_INV_EVICT_ERR	0x00800000 /* L2 Invalidates Uncorrectable Error Line Eviction */
#define MV_L2_ENABLE		0x00400000 /* L2 Cache enable */
#define MV_IC_REPLACE_LOCK	0x00080000 /* Replace ICache Lock */
#define MV_BGH_ENABLE		0x00040000 /* Branch Global History Register Enable */
#define MV_BTB_DISABLE		0x00020000 /* Branch Target Buffer Disable */
#define MV_L1_PARERR_ENABLE	0x00010000 /* L1 Parity Error Enable */

/* Cache type register definitions */
#define	CPU_CT_ISIZE(x)		((x) & 0xfff)		/* I$ info */
#define	CPU_CT_DSIZE(x)		(((x) >> 12) & 0xfff)	/* D$ info */
#define	CPU_CT_S		(1U << 24)		/* split cache */
#define	CPU_CT_CTYPE(x)		(((x) >> 25) & 0xf)	/* cache type */
#define	CPU_CT_FORMAT(x)	((x) >> 29)
/* Cache type register definitions for ARM v7 */
#define	CPU_CT_IMINLINE(x)	((x) & 0xf)		/* I$ min line size */
#define	CPU_CT_DMINLINE(x)	(((x) >> 16) & 0xf)	/* D$ min line size */

#define	CPU_CT_CTYPE_WT		0	/* write-through */
#define	CPU_CT_CTYPE_WB1	1	/* write-back, clean w/ read */
#define	CPU_CT_CTYPE_WB2	2	/* w/b, clean w/ cp15,7 */
#define	CPU_CT_CTYPE_WB6	6	/* w/b, cp15,7, lockdown fmt A */
#define	CPU_CT_CTYPE_WB7	7	/* w/b, cp15,7, lockdown fmt B */

#define	CPU_CT_xSIZE_LEN(x)	((x) & 0x3)		/* line size */
#define	CPU_CT_xSIZE_M		(1U << 2)		/* multiplier */
#define	CPU_CT_xSIZE_ASSOC(x)	(((x) >> 3) & 0x7)	/* associativity */
#define	CPU_CT_xSIZE_SIZE(x)	(((x) >> 6) & 0x7)	/* size */

#define	CPU_CT_ARMV7		0x4
/* ARM v7 Cache type definitions */
#define	CPUV7_CT_CTYPE_WT	(1U << 31)
#define	CPUV7_CT_CTYPE_WB	(1 << 30)
#define	CPUV7_CT_CTYPE_RA	(1 << 29)
#define	CPUV7_CT_CTYPE_WA	(1 << 28)

#define	CPUV7_CT_xSIZE_LEN(x)	((x) & 0x7)		/* line size */
#define	CPUV7_CT_xSIZE_ASSOC(x)	(((x) >> 3) & 0x3ff)	/* associativity */
#define	CPUV7_CT_xSIZE_SET(x)	(((x) >> 13) & 0x7fff)	/* num sets */

#define	CPUV7_L2CTLR_NPROC_SHIFT	24
#define	CPUV7_L2CTLR_NPROC(r)	((((r) >> CPUV7_L2CTLR_NPROC_SHIFT) & 3) + 1)

#define	CPU_CLIDR_CTYPE(reg,x)	(((reg) >> ((x) * 3)) & 0x7)
#define	CPU_CLIDR_LOUIS(reg)	(((reg) >> 21) & 0x7)
#define	CPU_CLIDR_LOC(reg)	(((reg) >> 24) & 0x7)
#define	CPU_CLIDR_LOUU(reg)	(((reg) >> 27) & 0x7)

#define	CACHE_ICACHE		1
#define	CACHE_DCACHE		2
#define	CACHE_SEP_CACHE		3
#define	CACHE_UNI_CACHE		4

/* Fault status register definitions */
#define FAULT_USER      0x10

#if __ARM_ARCH < 6
#define FAULT_TYPE_MASK 0x0f
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
#define FAULT_TRANS_F   0x06 /* Translation -- Flag */
#define FAULT_TRANS_P   0x07 /* Translation -- Page */
#define FAULT_DOMAIN_S  0x09 /* Domain -- Section */
#define FAULT_DOMAIN_P  0x0b /* Domain -- Page */
#define FAULT_PERM_S    0x0d /* Permission -- Section */
#define FAULT_PERM_P    0x0f /* Permission -- Page */

#define	FAULT_IMPRECISE	0x400	/* Imprecise exception (XSCALE) */
#define	FAULT_EXTERNAL	0x400	/* External abort (armv6+) */
#define	FAULT_WNR	0x800	/* Write-not-Read access (armv6+) */

#else /* __ARM_ARCH < 6 */

#define FAULT_ALIGN		0x001	/* Alignment Fault */
#define FAULT_DEBUG		0x002	/* Debug Event */
#define FAULT_ACCESS_L1		0x003	/* Access Bit (L1) */
#define FAULT_ICACHE		0x004	/* Instruction cache maintenance */
#define FAULT_TRAN_L1		0x005	/* Translation Fault (L1) */
#define FAULT_ACCESS_L2		0x006	/* Access Bit (L2) */
#define FAULT_TRAN_L2		0x007	/* Translation Fault (L2) */
#define FAULT_EA_PREC		0x008	/* External Abort */
#define FAULT_DOMAIN_L1		0x009	/* Domain Fault (L1) */
#define FAULT_DOMAIN_L2		0x00B	/* Domain Fault (L2) */
#define FAULT_EA_TRAN_L1	0x00C	/* External Translation Abort (L1) */
#define FAULT_PERM_L1		0x00D	/* Permission Fault (L1) */
#define FAULT_EA_TRAN_L2	0x00E	/* External Translation Abort (L2) */
#define FAULT_PERM_L2		0x00F	/* Permission Fault (L2) */
#define FAULT_TLB_CONFLICT	0x010	/* TLB Conflict Abort */
#define FAULT_EA_IMPREC		0x016	/* Asynchronous External Abort */
#define FAULT_PE_IMPREC		0x018	/* Asynchronous Parity Error */
#define FAULT_PARITY		0x019	/* Parity Error */
#define FAULT_PE_TRAN_L1	0x01C	/* Parity Error on Translation (L1) */
#define FAULT_PE_TRAN_L2	0x01E	/* Parity Error on Translation (L2) */

#define FSR_TO_FAULT(fsr)	(((fsr) & 0xF) | 			\
				 ((((fsr) & (1 << 10)) >> (10 - 4))))
#define FSR_LPAE		(1 <<  9) /* LPAE indicator */
#define FSR_WNR			(1 << 11) /* Write-not-Read access */
#define FSR_EXT			(1 << 12) /* DECERR/SLVERR for external*/
#define FSR_CM			(1 << 13) /* Cache maintenance fault */
#endif /* !__ARM_ARCH < 6 */

/*
 * Address of the vector page, low and high versions.
 */
#ifndef __ASSEMBLER__
#define	ARM_VECTORS_LOW		0x00000000U
#define	ARM_VECTORS_HIGH	0xffff0000U
#else
#define	ARM_VECTORS_LOW		0
#define	ARM_VECTORS_HIGH	0xffff0000
#endif

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

/* ARM register defines */
#define	ARM_REG_SIZE		4
#define	ARM_REG_NUM_PC		15
#define	ARM_REG_NUM_LR		14
#define	ARM_REG_NUM_SP		13

#define THUMB_INSN_SIZE		2		/* Some are 4 bytes.  */

/* ARM Hypervisor Related Defines */
#define	ARM_CP15_HDCR_HPMN	0x0000001f

#endif /* !MACHINE_ARMREG_H */
