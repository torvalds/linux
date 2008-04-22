/* spr-regs.h: special-purpose registers on the FRV
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_SPR_REGS_H
#define _ASM_SPR_REGS_H

/*
 * PSR - Processor Status Register
 */
#define PSR_ET			0x00000001	/* enable interrupts/exceptions flag */
#define PSR_PS			0x00000002	/* previous supervisor mode flag */
#define PSR_S			0x00000004	/* supervisor mode flag */
#define PSR_PIL			0x00000078	/* processor external interrupt level */
#define PSR_PIL_0		0x00000000	/* - no interrupt in progress */
#define PSR_PIL_13		0x00000068	/* - debugging only */
#define PSR_PIL_14		0x00000070	/* - debugging in progress */
#define PSR_PIL_15		0x00000078	/* - NMI in progress */
#define PSR_EM			0x00000080	/* enable media operation */
#define PSR_EF			0x00000100	/* enable FPU operation */
#define PSR_BE			0x00001000	/* endianness mode */
#define PSR_BE_LE		0x00000000	/* - little endian mode */
#define PSR_BE_BE		0x00001000	/* - big endian mode */
#define PSR_CM			0x00002000	/* conditional mode */
#define PSR_NEM			0x00004000	/* non-excepting mode */
#define PSR_ICE			0x00010000	/* in-circuit emulation mode */
#define PSR_VERSION_SHIFT	24		/* CPU silicon ID */
#define PSR_IMPLE_SHIFT		28		/* CPU core ID */

#define PSR_VERSION(psr)	(((psr) >> PSR_VERSION_SHIFT) & 0xf)
#define PSR_IMPLE(psr)		(((psr) >> PSR_IMPLE_SHIFT) & 0xf)

#define PSR_IMPLE_FR401		0x2
#define PSR_VERSION_FR401_MB93401	0x0
#define PSR_VERSION_FR401_MB93401A	0x1
#define PSR_VERSION_FR401_MB93403	0x2

#define PSR_IMPLE_FR405		0x4
#define PSR_VERSION_FR405_MB93405	0x0

#define PSR_IMPLE_FR451		0x5
#define PSR_VERSION_FR451_MB93451	0x0

#define PSR_IMPLE_FR501		0x1
#define PSR_VERSION_FR501_MB93501	0x1
#define PSR_VERSION_FR501_MB93501A	0x2

#define PSR_IMPLE_FR551		0x3
#define PSR_VERSION_FR551_MB93555	0x1

#define __get_PSR()	({ unsigned long x; asm volatile("movsg psr,%0" : "=r"(x)); x; })
#define __set_PSR(V)	do { asm volatile("movgs %0,psr" : : "r"(V)); } while(0)

/*
 * TBR - Trap Base Register
 */
#define TBR_TT			0x00000ff0
#define TBR_TT_INSTR_MMU_MISS	(0x01 << 4)
#define TBR_TT_INSTR_ACC_ERROR	(0x02 << 4)
#define TBR_TT_INSTR_ACC_EXCEP	(0x03 << 4)
#define TBR_TT_PRIV_INSTR	(0x06 << 4)
#define TBR_TT_ILLEGAL_INSTR	(0x07 << 4)
#define TBR_TT_FP_EXCEPTION	(0x0d << 4)
#define TBR_TT_MP_EXCEPTION	(0x0e << 4)
#define TBR_TT_DATA_ACC_ERROR	(0x11 << 4)
#define TBR_TT_DATA_MMU_MISS	(0x12 << 4)
#define TBR_TT_DATA_ACC_EXCEP	(0x13 << 4)
#define TBR_TT_DATA_STR_ERROR	(0x14 << 4)
#define TBR_TT_DIVISION_EXCEP	(0x17 << 4)
#define TBR_TT_COMMIT_EXCEP	(0x19 << 4)
#define TBR_TT_INSTR_TLB_MISS	(0x1a << 4)
#define TBR_TT_DATA_TLB_MISS	(0x1b << 4)
#define TBR_TT_DATA_DAT_EXCEP	(0x1d << 4)
#define TBR_TT_DECREMENT_TIMER	(0x1f << 4)
#define TBR_TT_COMPOUND_EXCEP	(0x20 << 4)
#define TBR_TT_INTERRUPT_1	(0x21 << 4)
#define TBR_TT_INTERRUPT_2	(0x22 << 4)
#define TBR_TT_INTERRUPT_3	(0x23 << 4)
#define TBR_TT_INTERRUPT_4	(0x24 << 4)
#define TBR_TT_INTERRUPT_5	(0x25 << 4)
#define TBR_TT_INTERRUPT_6	(0x26 << 4)
#define TBR_TT_INTERRUPT_7	(0x27 << 4)
#define TBR_TT_INTERRUPT_8	(0x28 << 4)
#define TBR_TT_INTERRUPT_9	(0x29 << 4)
#define TBR_TT_INTERRUPT_10	(0x2a << 4)
#define TBR_TT_INTERRUPT_11	(0x2b << 4)
#define TBR_TT_INTERRUPT_12	(0x2c << 4)
#define TBR_TT_INTERRUPT_13	(0x2d << 4)
#define TBR_TT_INTERRUPT_14	(0x2e << 4)
#define TBR_TT_INTERRUPT_15	(0x2f << 4)
#define TBR_TT_TRAP0		(0x80 << 4)
#define TBR_TT_TRAP1		(0x81 << 4)
#define TBR_TT_TRAP2		(0x82 << 4)
#define TBR_TT_TRAP3		(0x83 << 4)
#define TBR_TT_TRAP120		(0xf8 << 4)
#define TBR_TT_TRAP121		(0xf9 << 4)
#define TBR_TT_TRAP122		(0xfa << 4)
#define TBR_TT_TRAP123		(0xfb << 4)
#define TBR_TT_TRAP124		(0xfc << 4)
#define TBR_TT_TRAP125		(0xfd << 4)
#define TBR_TT_TRAP126		(0xfe << 4)
#define TBR_TT_BREAK		(0xff << 4)

#define TBR_TT_ATOMIC_CMPXCHG32	TBR_TT_TRAP120
#define TBR_TT_ATOMIC_XCHG32	TBR_TT_TRAP121
#define TBR_TT_ATOMIC_XOR	TBR_TT_TRAP122
#define TBR_TT_ATOMIC_OR	TBR_TT_TRAP123
#define TBR_TT_ATOMIC_AND	TBR_TT_TRAP124
#define TBR_TT_ATOMIC_SUB	TBR_TT_TRAP125
#define TBR_TT_ATOMIC_ADD	TBR_TT_TRAP126

#define __get_TBR()	({ unsigned long x; asm volatile("movsg tbr,%0" : "=r"(x)); x; })

/*
 * HSR0 - Hardware Status Register 0
 */
#define HSR0_PDM		0x00000007	/* power down mode */
#define HSR0_PDM_NORMAL		0x00000000	/* - normal mode */
#define HSR0_PDM_CORE_SLEEP	0x00000001	/* - CPU core sleep mode */
#define HSR0_PDM_BUS_SLEEP	0x00000003	/* - bus sleep mode */
#define HSR0_PDM_PLL_RUN	0x00000005	/* - PLL run */
#define HSR0_PDM_PLL_STOP	0x00000007	/* - PLL stop */
#define HSR0_GRLE		0x00000040	/* GR lower register set enable */
#define HSR0_GRHE		0x00000080	/* GR higher register set enable */
#define HSR0_FRLE		0x00000100	/* FR lower register set enable */
#define HSR0_FRHE		0x00000200	/* FR higher register set enable */
#define HSR0_GRN		0x00000400	/* GR quantity */
#define HSR0_GRN_64		0x00000000	/* - 64 GR registers */
#define HSR0_GRN_32		0x00000400	/* - 32 GR registers */
#define HSR0_FRN		0x00000800	/* FR quantity */
#define HSR0_FRN_64		0x00000000	/* - 64 FR registers */
#define HSR0_FRN_32		0x00000800	/* - 32 FR registers */
#define HSR0_SA			0x00001000	/* start address (RAMBOOT#) */
#define HSR0_ETMI		0x00008000	/* enable TIMERI (64-bit up timer) */
#define HSR0_ETMD		0x00004000	/* enable TIMERD (32-bit down timer) */
#define HSR0_PEDAT		0x00010000	/* previous DAT mode */
#define HSR0_XEDAT		0x00020000	/* exception DAT mode */
#define HSR0_EDAT		0x00080000	/* enable DAT mode */
#define HSR0_RME		0x00400000	/* enable RAM mode */
#define HSR0_EMEM		0x00800000	/* enable MMU_Miss mask */
#define HSR0_EXMMU		0x01000000	/* enable extended MMU mode */
#define HSR0_EDMMU		0x02000000	/* enable data MMU */
#define HSR0_EIMMU		0x04000000	/* enable instruction MMU */
#define HSR0_CBM		0x08000000	/* copy back mode */
#define HSR0_CBM_WRITE_THRU	0x00000000	/* - write through */
#define HSR0_CBM_COPY_BACK	0x08000000	/* - copy back */
#define HSR0_NWA		0x10000000	/* no write allocate */
#define HSR0_DCE		0x40000000	/* data cache enable */
#define HSR0_ICE		0x80000000	/* instruction cache enable */

#define __get_HSR(R)	({ unsigned long x; asm volatile("movsg hsr"#R",%0" : "=r"(x)); x; })
#define __set_HSR(R,V)	do { asm volatile("movgs %0,hsr"#R : : "r"(V)); } while(0)

/*
 * CCR - Condition Codes Register
 */
#define CCR_FCC0		0x0000000f	/* FP/Media condition 0 (fcc0 reg) */
#define CCR_FCC1		0x000000f0	/* FP/Media condition 1 (fcc1 reg) */
#define CCR_FCC2		0x00000f00	/* FP/Media condition 2 (fcc2 reg) */
#define CCR_FCC3		0x0000f000	/* FP/Media condition 3 (fcc3 reg) */
#define CCR_ICC0		0x000f0000	/* Integer condition 0 (icc0 reg) */
#define CCR_ICC0_C		0x00010000	/* - Carry flag */
#define CCR_ICC0_V		0x00020000	/* - Overflow flag */
#define CCR_ICC0_Z		0x00040000	/* - Zero flag */
#define CCR_ICC0_N		0x00080000	/* - Negative flag */
#define CCR_ICC1		0x00f00000	/* Integer condition 1 (icc1 reg) */
#define CCR_ICC2		0x0f000000	/* Integer condition 2 (icc2 reg) */
#define CCR_ICC3		0xf0000000	/* Integer condition 3 (icc3 reg) */

/*
 * CCCR - Condition Codes for Conditional Instructions Register
 */
#define CCCR_CC0		0x00000003	/* condition 0 (cc0 reg) */
#define CCCR_CC0_FALSE		0x00000002	/* - condition is false */
#define CCCR_CC0_TRUE		0x00000003	/* - condition is true */
#define CCCR_CC1		0x0000000c	/* condition 1 (cc1 reg) */
#define CCCR_CC2		0x00000030	/* condition 2 (cc2 reg) */
#define CCCR_CC3		0x000000c0	/* condition 3 (cc3 reg) */
#define CCCR_CC4		0x00000300	/* condition 4 (cc4 reg) */
#define CCCR_CC5		0x00000c00	/* condition 5 (cc5 reg) */
#define CCCR_CC6		0x00003000	/* condition 6 (cc6 reg) */
#define CCCR_CC7		0x0000c000	/* condition 7 (cc7 reg) */

/*
 * ISR - Integer Status Register
 */
#define ISR_EMAM		0x00000001	/* memory misaligned access handling */
#define ISR_EMAM_EXCEPTION	0x00000000	/* - generate exception */
#define ISR_EMAM_FUDGE		0x00000001	/* - mask out invalid address bits */
#define ISR_AEXC		0x00000004	/* accrued [overflow] exception */
#define ISR_DTT			0x00000018	/* division type trap */
#define ISR_DTT_IGNORE		0x00000000	/* - ignore division error */
#define ISR_DTT_DIVBYZERO	0x00000008	/* - generate exception */
#define ISR_DTT_OVERFLOW	0x00000010	/* - record overflow */
#define ISR_EDE			0x00000020	/* enable division exception */
#define ISR_PLI			0x20000000	/* pre-load instruction information */
#define ISR_QI			0x80000000	/* quad data implementation information */

/*
 * EPCR0 - Exception PC Register
 */
#define EPCR0_V			0x00000001	/* register content validity indicator */
#define EPCR0_PC		0xfffffffc	/* faulting instruction address */

/*
 * ESR0/14/15 - Exception Status Register
 */
#define ESRx_VALID		0x00000001	/* register content validity indicator */
#define ESRx_EC			0x0000003e	/* exception type */
#define ESRx_EC_DATA_STORE	0x00000000	/* - data_store_error */
#define ESRx_EC_INSN_ACCESS	0x00000006	/* - instruction_access_error */
#define ESRx_EC_PRIV_INSN	0x00000008	/* - privileged_instruction */
#define ESRx_EC_ILL_INSN	0x0000000a	/* - illegal_instruction */
#define ESRx_EC_MP_EXCEP	0x0000001c	/* - mp_exception */
#define ESRx_EC_DATA_ACCESS	0x00000020	/* - data_access_error */
#define ESRx_EC_DIVISION	0x00000026	/* - division_exception */
#define ESRx_EC_ITLB_MISS	0x00000034	/* - instruction_access_TLB_miss */
#define ESRx_EC_DTLB_MISS	0x00000036	/* - data_access_TLB_miss */
#define ESRx_EC_DATA_ACCESS_DAT	0x0000003a	/* - data_access_DAT_exception */

#define ESR0_IAEC		0x00000100	/* info for instruction-access-exception */
#define ESR0_IAEC_RESV		0x00000000	/* - reserved */
#define ESR0_IAEC_PROT_VIOL	0x00000100	/* - protection violation */

#define ESR0_ATXC		0x00f00000	/* address translation exception code */
#define ESR0_ATXC_MMU_MISS	0x00000000	/* - MMU miss exception and more (?) */
#define ESR0_ATXC_MULTI_DAT	0x00800000	/* - multiple DAT entry hit */
#define ESR0_ATXC_MULTI_SAT	0x00900000	/* - multiple SAT entry hit */
#define ESR0_ATXC_AMRTLB_MISS	0x00a00000	/* - MMU/TLB miss exception */
#define ESR0_ATXC_PRIV_EXCEP	0x00c00000	/* - privilege protection fault */
#define ESR0_ATXC_WP_EXCEP	0x00d00000	/* - write protection fault */

#define ESR0_EAV		0x00000800	/* true if EAR0 register valid */
#define ESR15_EAV		0x00000800	/* true if EAR15 register valid */

/*
 * ESFR1 - Exception Status Valid Flag Register
 */
#define ESFR1_ESR0		0x00000001	/* true if ESR0 is valid */
#define ESFR1_ESR14		0x00004000	/* true if ESR14 is valid */
#define ESFR1_ESR15		0x00008000	/* true if ESR15 is valid */

/*
 * MSR - Media Status Register
 */
#define MSR0_AOVF		0x00000001	/* overflow exception accrued */
#define MSRx_OVF		0x00000002	/* overflow exception detected */
#define MSRx_SIE		0x0000003c	/* last SIMD instruction exception detected */
#define MSRx_SIE_NONE		0x00000000	/* - none detected */
#define MSRx_SIE_FRkHI_ACCk	0x00000020	/* - exception at FRkHI or ACCk */
#define MSRx_SIE_FRkLO_ACCk1	0x00000010	/* - exception at FRkLO or ACCk+1 */
#define MSRx_SIE_FRk1HI_ACCk2	0x00000008	/* - exception at FRk+1HI or ACCk+2 */
#define MSRx_SIE_FRk1LO_ACCk3	0x00000004	/* - exception at FRk+1LO or ACCk+3 */
#define MSR0_MTT		0x00007000	/* type of last media trap detected */
#define MSR0_MTT_NONE		0x00000000	/* - none detected */
#define MSR0_MTT_OVERFLOW	0x00001000	/* - overflow detected */
#define MSR0_HI			0x00c00000	/* hardware implementation */
#define MSR0_HI_ROUNDING	0x00000000	/* - rounding mode */
#define MSR0_HI_NONROUNDING	0x00c00000	/* - non-rounding mode */
#define MSR0_EMCI		0x01000000	/* enable media custom instructions */
#define MSR0_SRDAV		0x10000000	/* select rounding mode of MAVEH */
#define MSR0_SRDAV_RDAV		0x00000000	/* - controlled by MSR.RDAV */
#define MSR0_SRDAV_RD		0x10000000	/* - controlled by MSR.RD */
#define MSR0_RDAV		0x20000000	/* rounding mode of MAVEH */
#define MSR0_RDAV_NEAREST_MI	0x00000000	/* - round to nearest minus */
#define MSR0_RDAV_NEAREST_PL	0x20000000	/* - round to nearest plus */
#define MSR0_RD			0xc0000000	/* rounding mode */
#define MSR0_RD_NEAREST		0x00000000	/* - nearest */
#define MSR0_RD_ZERO		0x40000000	/* - zero */
#define MSR0_RD_POS_INF		0x80000000	/* - postive infinity */
#define MSR0_RD_NEG_INF		0xc0000000	/* - negative infinity */

/*
 * IAMPR0-7 - Instruction Address Mapping Register
 * DAMPR0-7 - Data Address Mapping Register
 */
#define xAMPRx_V		0x00000001	/* register content validity indicator */
#define DAMPRx_WP		0x00000002	/* write protect */
#define DAMPRx_WP_RW		0x00000000	/* - read/write */
#define DAMPRx_WP_RO		0x00000002	/* - read-only */
#define xAMPRx_C		0x00000004	/* cached/uncached */
#define xAMPRx_C_CACHED		0x00000000	/* - cached */
#define xAMPRx_C_UNCACHED	0x00000004	/* - uncached */
#define xAMPRx_S		0x00000008	/* supervisor only */
#define xAMPRx_S_USER		0x00000000	/* - userspace can access */
#define xAMPRx_S_KERNEL		0x00000008	/* - kernel only */
#define xAMPRx_SS		0x000000f0	/* segment size */
#define xAMPRx_SS_16Kb		0x00000000	/* - 16 kilobytes */
#define xAMPRx_SS_64Kb		0x00000010	/* - 64 kilobytes */
#define xAMPRx_SS_256Kb		0x00000020	/* - 256 kilobytes */
#define xAMPRx_SS_1Mb		0x00000030	/* - 1 megabyte */
#define xAMPRx_SS_2Mb		0x00000040	/* - 2 megabytes */
#define xAMPRx_SS_4Mb		0x00000050	/* - 4 megabytes */
#define xAMPRx_SS_8Mb		0x00000060	/* - 8 megabytes */
#define xAMPRx_SS_16Mb		0x00000070	/* - 16 megabytes */
#define xAMPRx_SS_32Mb		0x00000080	/* - 32 megabytes */
#define xAMPRx_SS_64Mb		0x00000090	/* - 64 megabytes */
#define xAMPRx_SS_128Mb		0x000000a0	/* - 128 megabytes */
#define xAMPRx_SS_256Mb		0x000000b0	/* - 256 megabytes */
#define xAMPRx_SS_512Mb		0x000000c0	/* - 512 megabytes */
#define xAMPRx_RESERVED8	0x00000100	/* reserved bit */
#define xAMPRx_NG		0x00000200	/* non-global */
#define xAMPRx_L		0x00000400	/* locked */
#define xAMPRx_M		0x00000800	/* modified */
#define xAMPRx_D		0x00001000	/* DAT entry */
#define xAMPRx_RESERVED13	0x00002000	/* reserved bit */
#define xAMPRx_PPFN		0xfff00000	/* physical page frame number */

#define xAMPRx_V_BIT		0
#define DAMPRx_WP_BIT		1
#define xAMPRx_C_BIT		2
#define xAMPRx_S_BIT		3
#define xAMPRx_RESERVED8_BIT	8
#define xAMPRx_NG_BIT		9
#define xAMPRx_L_BIT		10
#define xAMPRx_M_BIT		11
#define xAMPRx_D_BIT		12
#define xAMPRx_RESERVED13_BIT	13

#define __get_IAMPR(R) ({ unsigned long x; asm volatile("movsg iampr"#R",%0" : "=r"(x)); x; })
#define __get_DAMPR(R) ({ unsigned long x; asm volatile("movsg dampr"#R",%0" : "=r"(x)); x; })

#define __get_IAMLR(R) ({ unsigned long x; asm volatile("movsg iamlr"#R",%0" : "=r"(x)); x; })
#define __get_DAMLR(R) ({ unsigned long x; asm volatile("movsg damlr"#R",%0" : "=r"(x)); x; })

#define __set_IAMPR(R,V) 	do { asm volatile("movgs %0,iampr"#R : : "r"(V)); } while(0)
#define __set_DAMPR(R,V)  	do { asm volatile("movgs %0,dampr"#R : : "r"(V)); } while(0)

#define __set_IAMLR(R,V) 	do { asm volatile("movgs %0,iamlr"#R : : "r"(V)); } while(0)
#define __set_DAMLR(R,V)  	do { asm volatile("movgs %0,damlr"#R : : "r"(V)); } while(0)

#define save_dampr(R, _dampr)					\
do {								\
	asm volatile("movsg dampr"R",%0" : "=r"(_dampr));	\
} while(0)

#define restore_dampr(R, _dampr)			\
do {							\
	asm volatile("movgs %0,dampr"R :: "r"(_dampr));	\
} while(0)

/*
 * AMCR - Address Mapping Control Register
 */
#define AMCR_IAMRN		0x000000ff	/* quantity of IAMPR registers */
#define AMCR_DAMRN		0x0000ff00	/* quantity of DAMPR registers */

/*
 * TTBR - Address Translation Table Base Register
 */
#define __get_TTBR()		({ unsigned long x; asm volatile("movsg ttbr,%0" : "=r"(x)); x; })

/*
 * TPXR - TLB Probe Extend Register
 */
#define TPXR_E			0x00000001
#define TPXR_LMAX_SHIFT		20
#define TPXR_LMAX_SMASK		0xf
#define TPXR_WMAX_SHIFT		24
#define TPXR_WMAX_SMASK		0xf
#define TPXR_WAY_SHIFT		28
#define TPXR_WAY_SMASK		0xf

/*
 * DCR - Debug Control Register
 */
#define DCR_IBCE3		0x00000001	/* break on conditional insn pointed to by IBAR3 */
#define DCR_IBE3		0x00000002	/* break on insn pointed to by IBAR3 */
#define DCR_IBCE1		0x00000004	/* break on conditional insn pointed to by IBAR2 */
#define DCR_IBE1		0x00000008	/* break on insn pointed to by IBAR2 */
#define DCR_IBCE2		0x00000010	/* break on conditional insn pointed to by IBAR1 */
#define DCR_IBE2		0x00000020	/* break on insn pointed to by IBAR1 */
#define DCR_IBCE0		0x00000040	/* break on conditional insn pointed to by IBAR0 */
#define DCR_IBE0		0x00000080	/* break on insn pointed to by IBAR0 */

#define DCR_DDBE1		0x00004000	/* use DBDR1x when checking DBAR1 */
#define DCR_DWBE1		0x00008000	/* break on store to address in DBAR1/DBMR1x */
#define DCR_DRBE1		0x00010000	/* break on load from address in DBAR1/DBMR1x */
#define DCR_DDBE0		0x00020000	/* use DBDR0x when checking DBAR0 */
#define DCR_DWBE0		0x00040000	/* break on store to address in DBAR0/DBMR0x */
#define DCR_DRBE0		0x00080000	/* break on load from address in DBAR0/DBMR0x */

#define DCR_EIM			0x0c000000	/* external interrupt disable */
#define DCR_IBM			0x10000000	/* instruction break disable */
#define DCR_SE			0x20000000	/* single step enable */
#define DCR_EBE			0x40000000	/* exception break enable */

/*
 * BRR - Break Interrupt Request Register
 */
#define BRR_ST			0x00000001	/* single-step detected */
#define BRR_SB			0x00000002	/* break instruction detected */
#define BRR_BB			0x00000004	/* branch with hint detected */
#define BRR_CBB			0x00000008	/* branch to LR detected */
#define BRR_IBx			0x000000f0	/* hardware breakpoint detected */
#define BRR_DBx			0x00000f00	/* hardware watchpoint detected */
#define BRR_DBNEx		0x0000f000	/* ? */
#define BRR_EBTT		0x00ff0000	/* trap type of exception break */
#define BRR_TB			0x10000000	/* external break request detected */
#define BRR_CB			0x20000000	/* ICE break command detected */
#define BRR_EB			0x40000000	/* exception break detected */

/*
 * BPSR - Break PSR Save Register
 */
#define BPSR_BET		0x00000001	/* former PSR.ET */
#define BPSR_BS			0x00001000	/* former PSR.S */

#endif /* _ASM_SPR_REGS_H */
