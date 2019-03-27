/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: spr.h,v 1.25 2002/08/14 15:38:40 matt Exp $
 * $FreeBSD$
 */
#ifndef _POWERPC_SPR_H_
#define	_POWERPC_SPR_H_

#ifndef _LOCORE
#define	mtspr(reg, val)							\
	__asm __volatile("mtspr %0,%1" : : "K"(reg), "r"(val))
#define	mfspr(reg)							\
	( { register_t val;						\
	  __asm __volatile("mfspr %0,%1" : "=r"(val) : "K"(reg));	\
	  val; } )


#ifndef __powerpc64__

/* The following routines allow manipulation of the full 64-bit width 
 * of SPRs on 64 bit CPUs in bridge mode */

#define mtspr64(reg,valhi,vallo,scratch)				\
	__asm __volatile("						\
		mfmsr %0; 						\
		insrdi %0,%5,1,0; 					\
		mtmsrd %0; 						\
		isync; 							\
									\
		sld %1,%1,%4;						\
		or %1,%1,%2;						\
		mtspr %3,%1;						\
		srd %1,%1,%4;						\
									\
		clrldi %0,%0,1; 					\
		mtmsrd %0; 						\
		isync;"							\
	: "=r"(scratch), "=r"(valhi) : "r"(vallo), "K"(reg), "r"(32), "r"(1))

#define mfspr64upper(reg,scratch)					\
	( { register_t val;						\
	    __asm __volatile("						\
		mfmsr %0; 						\
		insrdi %0,%4,1,0; 					\
		mtmsrd %0; 						\
		isync; 							\
									\
		mfspr %1,%2;						\
		srd %1,%1,%3;						\
									\
		clrldi %0,%0,1; 					\
		mtmsrd %0; 						\
		isync;" 						\
	    : "=r"(scratch), "=r"(val) : "K"(reg), "r"(32), "r"(1));	\
	    val; } )

#endif

#endif /* _LOCORE */

/*
 * Special Purpose Register declarations.
 *
 * The first column in the comments indicates which PowerPC
 * architectures the SPR is valid on - 4 for 4xx series,
 * 6 for 6xx/7xx series and 8 for 8xx and 8xxx series.
 */

#define	SPR_MQ			0x000	/* .6. 601 MQ register */
#define	SPR_XER			0x001	/* 468 Fixed Point Exception Register */
#define	SPR_RTCU_R		0x004	/* .6. 601 RTC Upper - Read */
#define	SPR_RTCL_R		0x005	/* .6. 601 RTC Lower - Read */
#define	SPR_LR			0x008	/* 468 Link Register */
#define	SPR_CTR			0x009	/* 468 Count Register */
#define	SPR_DSCR		0x011   /* Data Stream Control Register */
#define	SPR_DSISR		0x012	/* .68 DSI exception source */
#define	  DSISR_DIRECT		  0x80000000 /* Direct-store error exception */
#define	  DSISR_NOTFOUND	  0x40000000 /* Translation not found */
#define	  DSISR_PROTECT		  0x08000000 /* Memory access not permitted */
#define	  DSISR_INVRX		  0x04000000 /* Reserve-indexed insn direct-store access */
#define	  DSISR_STORE		  0x02000000 /* Store operation */
#define	  DSISR_DABR		  0x00400000 /* DABR match */
#define	  DSISR_SEGMENT		  0x00200000 /* XXX; not in 6xx PEM */
#define	  DSISR_EAR		  0x00100000 /* eciwx/ecowx && EAR[E] == 0 */
#define	SPR_DAR			0x013	/* .68 Data Address Register */
#define	SPR_RTCU_W		0x014	/* .6. 601 RTC Upper - Write */
#define	SPR_RTCL_W		0x015	/* .6. 601 RTC Lower - Write */
#define	SPR_DEC			0x016	/* .68 DECrementer register */
#define	SPR_SDR1		0x019	/* .68 Page table base address register */
#define	SPR_SRR0		0x01a	/* 468 Save/Restore Register 0 */
#define	SPR_SRR1		0x01b	/* 468 Save/Restore Register 1 */
#define	  SRR1_ISI_PFAULT	0x40000000 /* ISI page not found */
#define	  SRR1_ISI_NOEXECUTE	0x10000000 /* Memory marked no-execute */
#define	  SRR1_ISI_PP		0x08000000 /* PP bits forbid access */
#define	SPR_DECAR		0x036	/* ..8 Decrementer auto reload */
#define	SPR_EIE			0x050	/* ..8 Exception Interrupt ??? */
#define	SPR_EID			0x051	/* ..8 Exception Interrupt ??? */
#define	SPR_NRI			0x052	/* ..8 Exception Interrupt ??? */
#define	SPR_FSCR		0x099	/* Facility Status and Control Register */
#define FSCR_IC_MASK		  0xFF00000000000000ULL	/* FSCR[0:7] is Interrupt Cause */
#define FSCR_IC_FP		  0x0000000000000000ULL	/* FP unavailable */
#define FSCR_IC_VSX		  0x0100000000000000ULL	/* VSX unavailable */
#define FSCR_IC_DSCR		  0x0200000000000000ULL	/* Access to the DSCR at SPRs 3 or 17 */
#define FSCR_IC_PM		  0x0300000000000000ULL	/* Read or write access of a Performance Monitor SPR in group A */
#define FSCR_IC_BHRB		  0x0400000000000000ULL	/* Execution of a BHRB Instruction */
#define FSCR_IC_HTM		  0x0500000000000000ULL	/* Access to a Transactional Memory */
/* Reserved 0x0600000000000000ULL */
#define FSCR_IC_EBB		  0x0700000000000000ULL	/* Access to Event-Based Branch */
#define FSCR_IC_TAR		  0x0800000000000000ULL	/* Access to Target Address Register */
#define FSCR_IC_STOP		  0x0900000000000000ULL	/* Access to the 'stop' instruction in privileged non-hypervisor state */
#define FSCR_IC_MSG		  0x0A00000000000000ULL	/* Access to 'msgsndp' or 'msgclrp' instructions */
#define FSCR_IC_SCV		  0x0C00000000000000ULL	/* Execution of a 'scv' instruction */
#define	SPR_USPRG0		0x100	/* 4.. User SPR General 0 */
#define	SPR_VRSAVE		0x100	/* .6. AltiVec VRSAVE */
#define	SPR_SPRG0		0x110	/* 468 SPR General 0 */
#define	SPR_SPRG1		0x111	/* 468 SPR General 1 */
#define	SPR_SPRG2		0x112	/* 468 SPR General 2 */
#define	SPR_SPRG3		0x113	/* 468 SPR General 3 */
#define	SPR_SPRG4		0x114	/* 4.. SPR General 4 */
#define	SPR_SPRG5		0x115	/* 4.. SPR General 5 */
#define	SPR_SPRG6		0x116	/* 4.. SPR General 6 */
#define	SPR_SPRG7		0x117	/* 4.. SPR General 7 */
#define	SPR_SCOMC		0x114	/* ... SCOM Address Register (970) */
#define	SPR_SCOMD		0x115	/* ... SCOM Data Register (970) */
#define	SPR_ASR			0x118	/* ... Address Space Register (PPC64) */
#define	SPR_EAR			0x11a	/* .68 External Access Register */
#define	SPR_PVR			0x11f	/* 468 Processor Version Register */
#define	  MPC601		  0x0001
#define	  MPC603		  0x0003
#define	  MPC604		  0x0004
#define	  MPC602		  0x0005
#define	  MPC603e		  0x0006
#define	  MPC603ev		  0x0007
#define	  MPC750		  0x0008
#define	  MPC750CL		  0x7000	/* Nintendo Wii's Broadway */
#define	  MPC604ev		  0x0009
#define	  MPC7400		  0x000c
#define	  MPC620		  0x0014
#define	  IBM403		  0x0020
#define	  IBM401A1		  0x0021
#define	  IBM401B2		  0x0022
#define	  IBM401C2		  0x0023
#define	  IBM401D2		  0x0024
#define	  IBM401E2		  0x0025
#define	  IBM401F2		  0x0026
#define	  IBM401G2		  0x0027
#define	  IBMRS64II		  0x0033
#define	  IBMRS64III		  0x0034
#define	  IBMPOWER4		  0x0035
#define	  IBMRS64III_2		  0x0036
#define	  IBMRS64IV		  0x0037
#define	  IBMPOWER4PLUS		  0x0038
#define	  IBM970		  0x0039
#define	  IBMPOWER5		  0x003a
#define	  IBMPOWER5PLUS		  0x003b
#define	  IBM970FX		  0x003c
#define	  IBMPOWER6		  0x003e
#define	  IBMPOWER7		  0x003f
#define	  IBMPOWER3		  0x0040
#define	  IBMPOWER3PLUS		  0x0041
#define	  IBM970MP		  0x0044
#define	  IBM970GX		  0x0045
#define	  IBMPOWERPCA2		  0x0049
#define	  IBMPOWER7PLUS		  0x004a
#define	  IBMPOWER8E		  0x004b
#define	  IBMPOWER8		  0x004d
#define	  IBMPOWER9		  0x004e
#define	  MPC860		  0x0050
#define	  IBMCELLBE		  0x0070
#define	  MPC8240		  0x0081
#define	  PA6T			  0x0090
#define	  IBM405GP		  0x4011
#define	  IBM405L		  0x4161
#define	  IBM750FX		  0x7000
#define	MPC745X_P(v)	((v & 0xFFF8) == 0x8000)
#define	  MPC7450		  0x8000
#define	  MPC7455		  0x8001
#define	  MPC7457		  0x8002
#define	  MPC7447A		  0x8003
#define	  MPC7448		  0x8004
#define	  MPC7410		  0x800c
#define	  MPC8245		  0x8081
#define	  FSL_E500v1		  0x8020
#define	  FSL_E500v2		  0x8021
#define	  FSL_E500mc		  0x8023
#define	  FSL_E5500		  0x8024
#define	  FSL_E6500		  0x8040
#define	  FSL_E300C1		  0x8083
#define	  FSL_E300C2		  0x8084
#define	  FSL_E300C3		  0x8085
#define	  FSL_E300C4		  0x8086

#define   LPCR_PECE_WAKESET     (LPCR_PECE_EXT | LPCR_PECE_DECR | LPCR_PECE_ME)
 
#define	SPR_EPCR		0x133
#define	  EPCR_EXTGS		  0x80000000
#define	  EPCR_DTLBGS		  0x40000000
#define	  EPCR_ITLBGS		  0x20000000
#define	  EPCR_DSIGS		  0x10000000
#define	  EPCR_ISIGS		  0x08000000
#define	  EPCR_DUVGS		  0x04000000
#define	  EPCR_ICM		  0x02000000
#define	  EPCR_GICMGS		  0x01000000
#define	  EPCR_DGTMI		  0x00800000
#define	  EPCR_DMIUH		  0x00400000
#define	  EPCR_PMGS		  0x00200000

#define	SPR_HSRR0		0x13a
#define	SPR_HSRR1		0x13b
#define	SPR_LPCR		0x13e	/* Logical Partitioning Control */
#define	  LPCR_LPES		  0x008	/* Bit 60 */
#define	  LPCR_HVICE		  0x002	/* Hypervisor Virtualization Interrupt (Arch 3.0) */
#define	  LPCR_PECE_DRBL          (1ULL << 16) /* Directed Privileged Doorbell */
#define	  LPCR_PECE_HDRBL         (1ULL << 15) /* Directed Hypervisor Doorbell */
#define	  LPCR_PECE_EXT           (1ULL << 14) /* External exceptions */
#define	  LPCR_PECE_DECR          (1ULL << 13) /* Decrementer exceptions */
#define	  LPCR_PECE_ME            (1ULL << 12) /* Machine Check and Hypervisor */
                                               /* Maintenance exceptions */
#define	SPR_LPID		0x13f	/* Logical Partitioning Control */
#define	SPR_HMER		0x150	/* Hypervisor Maintenance Exception Register */
#define	SPR_HMEER		0x151	/* Hypervisor Maintenance Exception Enable Register */

#define	SPR_PTCR		0x1d0	/* Partition Table Control Register */
#define	SPR_SPEFSCR		0x200	/* ..8 Signal Processing Engine FSCR. */
#define	  SPEFSCR_SOVH		  0x80000000
#define	  SPEFSCR_OVH		  0x40000000
#define	  SPEFSCR_FGH		  0x20000000
#define	  SPEFSCR_FXH		  0x10000000
#define	  SPEFSCR_FINVH		  0x08000000
#define	  SPEFSCR_FDBZH		  0x04000000
#define	  SPEFSCR_FUNFH		  0x02000000
#define	  SPEFSCR_FOVFH		  0x01000000
#define	  SPEFSCR_FINXS		  0x00200000
#define	  SPEFSCR_FINVS		  0x00100000
#define	  SPEFSCR_FDBZS		  0x00080000
#define	  SPEFSCR_FUNFS		  0x00040000
#define	  SPEFSCR_FOVFS		  0x00020000
#define	  SPEFSCR_SOV		  0x00008000
#define	  SPEFSCR_OV		  0x00004000
#define	  SPEFSCR_FG		  0x00002000
#define	  SPEFSCR_FX		  0x00001000
#define	  SPEFSCR_FINV		  0x00000800
#define	  SPEFSCR_FDBZ		  0x00000400
#define	  SPEFSCR_FUNF		  0x00000200
#define	  SPEFSCR_FOVF		  0x00000100
#define	  SPEFSCR_FINXE		  0x00000040
#define	  SPEFSCR_FINVE		  0x00000020
#define	  SPEFSCR_FDBZE		  0x00000010
#define	  SPEFSCR_FUNFE		  0x00000008
#define	  SPEFSCR_FOVFE		  0x00000004
#define	  SPEFSCR_FRMC_M	  0x00000003
#define	SPR_IBAT0U		0x210	/* .6. Instruction BAT Reg 0 Upper */
#define	SPR_IBAT0L		0x211	/* .6. Instruction BAT Reg 0 Lower */
#define	SPR_IBAT1U		0x212	/* .6. Instruction BAT Reg 1 Upper */
#define	SPR_IBAT1L		0x213	/* .6. Instruction BAT Reg 1 Lower */
#define	SPR_IBAT2U		0x214	/* .6. Instruction BAT Reg 2 Upper */
#define	SPR_IBAT2L		0x215	/* .6. Instruction BAT Reg 2 Lower */
#define	SPR_IBAT3U		0x216	/* .6. Instruction BAT Reg 3 Upper */
#define	SPR_IBAT3L		0x217	/* .6. Instruction BAT Reg 3 Lower */
#define	SPR_DBAT0U		0x218	/* .6. Data BAT Reg 0 Upper */
#define	SPR_DBAT0L		0x219	/* .6. Data BAT Reg 0 Lower */
#define	SPR_DBAT1U		0x21a	/* .6. Data BAT Reg 1 Upper */
#define	SPR_DBAT1L		0x21b	/* .6. Data BAT Reg 1 Lower */
#define	SPR_DBAT2U		0x21c	/* .6. Data BAT Reg 2 Upper */
#define	SPR_DBAT2L		0x21d	/* .6. Data BAT Reg 2 Lower */
#define	SPR_DBAT3U		0x21e	/* .6. Data BAT Reg 3 Upper */
#define	SPR_DBAT3L		0x21f	/* .6. Data BAT Reg 3 Lower */
#define	SPR_IC_CST		0x230	/* ..8 Instruction Cache CSR */
#define	  IC_CST_IEN		0x80000000 /* I cache is ENabled   (RO) */
#define	  IC_CST_CMD_INVALL	0x0c000000 /* I cache invalidate all */
#define	  IC_CST_CMD_UNLOCKALL	0x0a000000 /* I cache unlock all */
#define	  IC_CST_CMD_UNLOCK	0x08000000 /* I cache unlock block */
#define	  IC_CST_CMD_LOADLOCK	0x06000000 /* I cache load & lock block */
#define	  IC_CST_CMD_DISABLE	0x04000000 /* I cache disable */
#define	  IC_CST_CMD_ENABLE	0x02000000 /* I cache enable */
#define	  IC_CST_CCER1		0x00200000 /* I cache error type 1 (RO) */
#define	  IC_CST_CCER2		0x00100000 /* I cache error type 2 (RO) */
#define	  IC_CST_CCER3		0x00080000 /* I cache error type 3 (RO) */
#define	SPR_IBAT4U		0x230	/* .6. Instruction BAT Reg 4 Upper */
#define	SPR_IC_ADR		0x231	/* ..8 Instruction Cache Address */
#define	SPR_IBAT4L		0x231	/* .6. Instruction BAT Reg 4 Lower */
#define	SPR_IC_DAT		0x232	/* ..8 Instruction Cache Data */
#define	SPR_IBAT5U		0x232	/* .6. Instruction BAT Reg 5 Upper */
#define	SPR_IBAT5L		0x233	/* .6. Instruction BAT Reg 5 Lower */
#define	SPR_IBAT6U		0x234	/* .6. Instruction BAT Reg 6 Upper */
#define	SPR_IBAT6L		0x235	/* .6. Instruction BAT Reg 6 Lower */
#define	SPR_IBAT7U		0x236	/* .6. Instruction BAT Reg 7 Upper */
#define	SPR_IBAT7L		0x237	/* .6. Instruction BAT Reg 7 Lower */
#define	SPR_DC_CST		0x230	/* ..8 Data Cache CSR */
#define	  DC_CST_DEN		0x80000000 /* D cache ENabled (RO) */
#define	  DC_CST_DFWT		0x40000000 /* D cache Force Write-Thru (RO) */
#define	  DC_CST_LES		0x20000000 /* D cache Little Endian Swap (RO) */
#define	  DC_CST_CMD_FLUSH	0x0e000000 /* D cache invalidate all */
#define	  DC_CST_CMD_INVALL	0x0c000000 /* D cache invalidate all */
#define	  DC_CST_CMD_UNLOCKALL	0x0a000000 /* D cache unlock all */
#define	  DC_CST_CMD_UNLOCK	0x08000000 /* D cache unlock block */
#define	  DC_CST_CMD_CLRLESWAP	0x07000000 /* D cache clr little-endian swap */
#define	  DC_CST_CMD_LOADLOCK	0x06000000 /* D cache load & lock block */
#define	  DC_CST_CMD_SETLESWAP	0x05000000 /* D cache set little-endian swap */
#define	  DC_CST_CMD_DISABLE	0x04000000 /* D cache disable */
#define	  DC_CST_CMD_CLRFWT	0x03000000 /* D cache clear forced write-thru */
#define	  DC_CST_CMD_ENABLE	0x02000000 /* D cache enable */
#define	  DC_CST_CMD_SETFWT	0x01000000 /* D cache set forced write-thru */
#define	  DC_CST_CCER1		0x00200000 /* D cache error type 1 (RO) */
#define	  DC_CST_CCER2		0x00100000 /* D cache error type 2 (RO) */
#define	  DC_CST_CCER3		0x00080000 /* D cache error type 3 (RO) */
#define	SPR_DBAT4U		0x238	/* .6. Data BAT Reg 4 Upper */
#define	SPR_DC_ADR		0x231	/* ..8 Data Cache Address */
#define	SPR_DBAT4L		0x239	/* .6. Data BAT Reg 4 Lower */
#define	SPR_DC_DAT		0x232	/* ..8 Data Cache Data */
#define	SPR_DBAT5U		0x23a	/* .6. Data BAT Reg 5 Upper */
#define	SPR_DBAT5L		0x23b	/* .6. Data BAT Reg 5 Lower */
#define	SPR_DBAT6U		0x23c	/* .6. Data BAT Reg 6 Upper */
#define	SPR_DBAT6L		0x23d	/* .6. Data BAT Reg 6 Lower */
#define	SPR_DBAT7U		0x23e	/* .6. Data BAT Reg 7 Upper */
#define	SPR_DBAT7L		0x23f	/* .6. Data BAT Reg 7 Lower */
#define	SPR_SPRG8		0x25c	/* ..8 SPR General 8 */
#define	SPR_MI_CTR		0x310	/* ..8 IMMU control */
#define	  Mx_CTR_GPM		0x80000000 /* Group Protection Mode */
#define	  Mx_CTR_PPM		0x40000000 /* Page Protection Mode */
#define	  Mx_CTR_CIDEF		0x20000000 /* Cache-Inhibit DEFault */
#define	  MD_CTR_WTDEF		0x20000000 /* Write-Through DEFault */
#define	  Mx_CTR_RSV4		0x08000000 /* Reserve 4 TLB entries */
#define	  MD_CTR_TWAM		0x04000000 /* TableWalk Assist Mode */
#define	  Mx_CTR_PPCS		0x02000000 /* Priv/user state compare mode */
#define	  Mx_CTR_TLB_INDX	0x000001f0 /* TLB index mask */
#define	  Mx_CTR_TLB_INDX_BITPOS	8	  /* TLB index shift */
#define	SPR_MI_AP		0x312	/* ..8 IMMU access protection */
#define	  Mx_GP_SUPER(n)	(0 << (2*(15-(n)))) /* access is supervisor */
#define	  Mx_GP_PAGE		(1 << (2*(15-(n)))) /* access is page protect */
#define	  Mx_GP_SWAPPED		(2 << (2*(15-(n)))) /* access is swapped */
#define	  Mx_GP_USER		(3 << (2*(15-(n)))) /* access is user */
#define	SPR_MI_EPN		0x313	/* ..8 IMMU effective number */
#define	  Mx_EPN_EPN		0xfffff000 /* Effective Page Number mask */
#define	  Mx_EPN_EV		0x00000020 /* Entry Valid */
#define	  Mx_EPN_ASID		0x0000000f /* Address Space ID */
#define	SPR_MI_TWC		0x315	/* ..8 IMMU tablewalk control */
#define	  MD_TWC_L2TB		0xfffff000 /* Level-2 Tablewalk Base */
#define	  Mx_TWC_APG		0x000001e0 /* Access Protection Group */
#define	  Mx_TWC_G		0x00000010 /* Guarded memory */
#define	  Mx_TWC_PS		0x0000000c /* Page Size (L1) */
#define	  MD_TWC_WT		0x00000002 /* Write-Through */
#define	  Mx_TWC_V		0x00000001 /* Entry Valid */
#define	SPR_MI_RPN		0x316	/* ..8 IMMU real (phys) page number */
#define	  Mx_RPN_RPN		0xfffff000 /* Real Page Number */
#define	  Mx_RPN_PP		0x00000ff0 /* Page Protection */
#define	  Mx_RPN_SPS		0x00000008 /* Small Page Size */
#define	  Mx_RPN_SH		0x00000004 /* SHared page */
#define	  Mx_RPN_CI		0x00000002 /* Cache Inhibit */
#define	  Mx_RPN_V		0x00000001 /* Valid */
#define	SPR_MD_CTR		0x318	/* ..8 DMMU control */
#define	SPR_M_CASID		0x319	/* ..8 CASID */
#define	  M_CASID		0x0000000f /* Current AS Id */
#define	SPR_MD_AP		0x31a	/* ..8 DMMU access protection */
#define	SPR_MD_EPN		0x31b	/* ..8 DMMU effective number */

#define	SPR_970MMCR0		0x31b	/* ... Monitor Mode Control Register 0 (PPC 970) */
#define	  SPR_970MMCR0_PMC1SEL(x) ((x) << 8) /* PMC1 selector (970) */
#define	  SPR_970MMCR0_PMC2SEL(x) ((x) << 1) /* PMC2 selector (970) */
#define	SPR_970MMCR1		0x31e	/* ... Monitor Mode Control Register 1 (PPC 970) */
#define	  SPR_970MMCR1_PMC3SEL(x)	  (((x) & 0x1f) << 27) /* PMC 3 selector */
#define	  SPR_970MMCR1_PMC4SEL(x)	  (((x) & 0x1f) << 22) /* PMC 4 selector */
#define	  SPR_970MMCR1_PMC5SEL(x)	  (((x) & 0x1f) << 17) /* PMC 5 selector */
#define	  SPR_970MMCR1_PMC6SEL(x)	  (((x) & 0x1f) << 12) /* PMC 6 selector */
#define	  SPR_970MMCR1_PMC7SEL(x)	  (((x) & 0x1f) << 7) /* PMC 7 selector */
#define	  SPR_970MMCR1_PMC8SEL(x)	  (((x) & 0x1f) << 2) /* PMC 8 selector */
#define	SPR_970MMCRA		0x312	/* ... Monitor Mode Control Register 2 (PPC 970) */
#define	SPR_970PMC1		0x313	/* ... PMC 1 */
#define	SPR_970PMC2		0x314	/* ... PMC 2 */
#define	SPR_970PMC3		0x315	/* ... PMC 3 */
#define	SPR_970PMC4		0x316	/* ... PMC 4 */
#define	SPR_970PMC5		0x317	/* ... PMC 5 */
#define	SPR_970PMC6		0x318	/* ... PMC 6 */
#define	SPR_970PMC7		0x319	/* ... PMC 7 */
#define	SPR_970PMC8		0x31a	/* ... PMC 8 */

#define	SPR_M_TWB		0x31c	/* ..8 MMU tablewalk base */
#define	  M_TWB_L1TB		0xfffff000 /* level-1 translation base */
#define	  M_TWB_L1INDX		0x00000ffc /* level-1 index */
#define	SPR_MD_TWC		0x31d	/* ..8 DMMU tablewalk control */
#define	SPR_MD_RPN		0x31e	/* ..8 DMMU real (phys) page number */
#define	SPR_MD_TW		0x31f	/* ..8 MMU tablewalk scratch */
#define	SPR_MI_CAM		0x330	/* ..8 IMMU CAM entry read */
#define	SPR_MI_RAM0		0x331	/* ..8 IMMU RAM entry read reg 0 */
#define	SPR_MI_RAM1		0x332	/* ..8 IMMU RAM entry read reg 1 */
#define	SPR_MD_CAM		0x338	/* ..8 IMMU CAM entry read */
#define	SPR_MD_RAM0		0x339	/* ..8 IMMU RAM entry read reg 0 */
#define	SPR_MD_RAM1		0x33a	/* ..8 IMMU RAM entry read reg 1 */
#define	SPR_PSSCR		0x357	/* Processor Stop Status and Control Register (ISA 3.0) */
#define	SPR_PMCR                0x374   /* Processor Management Control Register */
#define	SPR_UMMCR2		0x3a0	/* .6. User Monitor Mode Control Register 2 */
#define	SPR_UMMCR0		0x3a8	/* .6. User Monitor Mode Control Register 0 */
#define	SPR_USIA		0x3ab	/* .6. User Sampled Instruction Address */
#define	SPR_UMMCR1		0x3ac	/* .6. User Monitor Mode Control Register 1 */
#define	SPR_ZPR			0x3b0	/* 4.. Zone Protection Register */
#define	SPR_MMCR2		0x3b0	/* .6. Monitor Mode Control Register 2 */
#define	  SPR_MMCR2_THRESHMULT_32	  0x80000000 /* Multiply MMCR0 threshold by 32 */
#define	  SPR_MMCR2_THRESHMULT_2	  0x00000000 /* Multiply MMCR0 threshold by 2 */
#define	SPR_PID			0x3b1	/* 4.. Process ID */
#define	SPR_PMC5		0x3b1	/* .6. Performance Counter Register 5 */
#define	SPR_PMC6		0x3b2	/* .6. Performance Counter Register 6 */
#define	SPR_CCR0		0x3b3	/* 4.. Core Configuration Register 0 */
#define	SPR_IAC3		0x3b4	/* 4.. Instruction Address Compare 3 */
#define	SPR_IAC4		0x3b5	/* 4.. Instruction Address Compare 4 */
#define	SPR_DVC1		0x3b6	/* 4.. Data Value Compare 1 */
#define	SPR_DVC2		0x3b7	/* 4.. Data Value Compare 2 */
#define	SPR_MMCR0		0x3b8	/* .6. Monitor Mode Control Register 0 */
#define	  SPR_MMCR0_FC		  0x80000000 /* Freeze counters */
#define	  SPR_MMCR0_FCS		  0x40000000 /* Freeze counters in supervisor mode */
#define	  SPR_MMCR0_FCP		  0x20000000 /* Freeze counters in user mode */
#define	  SPR_MMCR0_FCM1	  0x10000000 /* Freeze counters when mark=1 */
#define	  SPR_MMCR0_FCM0	  0x08000000 /* Freeze counters when mark=0 */
#define	  SPR_MMCR0_PMXE	  0x04000000 /* Enable PM interrupt */
#define	  SPR_MMCR0_FCECE	  0x02000000 /* Freeze counters after event */
#define	  SPR_MMCR0_TBSEL_15	  0x01800000 /* Count bit 15 of TBL */
#define	  SPR_MMCR0_TBSEL_19	  0x01000000 /* Count bit 19 of TBL */
#define	  SPR_MMCR0_TBSEL_23	  0x00800000 /* Count bit 23 of TBL */
#define	  SPR_MMCR0_TBSEL_31	  0x00000000 /* Count bit 31 of TBL */
#define	  SPR_MMCR0_TBEE	  0x00400000 /* Time-base event enable */
#define	  SPR_MMCRO_THRESHOLD(x)  ((x) << 16) /* Threshold value */
#define	  SPR_MMCR0_PMC1CE	  0x00008000 /* PMC1 condition enable */
#define	  SPR_MMCR0_PMCNCE	  0x00004000 /* PMCn condition enable */
#define	  SPR_MMCR0_TRIGGER	  0x00002000 /* Trigger */
#define	  SPR_MMCR0_PMC1SEL(x)	  (((x) & 0x3f) << 6) /* PMC1 selector */
#define	  SPR_MMCR0_PMC2SEL(x)	  (((x) & 0x3f) << 0) /* PMC2 selector */
#define	SPR_SGR			0x3b9	/* 4.. Storage Guarded Register */
#define	SPR_PMC1		0x3b9	/* .6. Performance Counter Register 1 */
#define	SPR_DCWR		0x3ba	/* 4.. Data Cache Write-through Register */
#define	SPR_PMC2		0x3ba	/* .6. Performance Counter Register 2 */
#define	SPR_SLER		0x3bb	/* 4.. Storage Little Endian Register */
#define	SPR_SIA			0x3bb	/* .6. Sampled Instruction Address */
#define	SPR_MMCR1		0x3bc	/* .6. Monitor Mode Control Register 2 */
#define	  SPR_MMCR1_PMC3SEL(x)	  (((x) & 0x1f) << 27) /* PMC 3 selector */
#define	  SPR_MMCR1_PMC4SEL(x)	  (((x) & 0x1f) << 22) /* PMC 4 selector */
#define	  SPR_MMCR1_PMC5SEL(x)	  (((x) & 0x1f) << 17) /* PMC 5 selector */
#define	  SPR_MMCR1_PMC6SEL(x)	  (((x) & 0x3f) << 11) /* PMC 6 selector */

#define	SPR_SU0R		0x3bc	/* 4.. Storage User-defined 0 Register */
#define	SPR_PMC3		0x3bd	/* .6. Performance Counter Register 3 */
#define	SPR_PMC4		0x3be	/* .6. Performance Counter Register 4 */
#define	SPR_DMISS		0x3d0	/* .68 Data TLB Miss Address Register */
#define	SPR_DCMP		0x3d1	/* .68 Data TLB Compare Register */
#define	SPR_HASH1		0x3d2	/* .68 Primary Hash Address Register */
#define	SPR_ICDBDR		0x3d3	/* 4.. Instruction Cache Debug Data Register */
#define	SPR_HASH2		0x3d3	/* .68 Secondary Hash Address Register */
#define	SPR_IMISS		0x3d4	/* .68 Instruction TLB Miss Address Register */
#define	SPR_TLBMISS		0x3d4	/* .6. TLB Miss Address Register */
#define	SPR_DEAR		0x3d5	/* 4.. Data Error Address Register */
#define	SPR_ICMP		0x3d5	/* .68 Instruction TLB Compare Register */
#define	SPR_PTEHI		0x3d5	/* .6. Instruction TLB Compare Register */
#define	SPR_EVPR		0x3d6	/* 4.. Exception Vector Prefix Register */
#define	SPR_RPA			0x3d6	/* .68 Required Physical Address Register */
#define	SPR_PTELO		0x3d6	/* .6. Required Physical Address Register */

#define	SPR_TSR			0x150	/* ..8 Timer Status Register */
#define	SPR_TCR			0x154	/* ..8 Timer Control Register */

#define	  TSR_ENW		  0x80000000 /* Enable Next Watchdog */
#define	  TSR_WIS		  0x40000000 /* Watchdog Interrupt Status */
#define	  TSR_WRS_MASK		  0x30000000 /* Watchdog Reset Status */
#define	  TSR_WRS_NONE		  0x00000000 /* No watchdog reset has occurred */
#define	  TSR_WRS_CORE		  0x10000000 /* Core reset was forced by the watchdog */
#define	  TSR_WRS_CHIP		  0x20000000 /* Chip reset was forced by the watchdog */
#define	  TSR_WRS_SYSTEM	  0x30000000 /* System reset was forced by the watchdog */
#define	  TSR_PIS		  0x08000000 /* PIT Interrupt Status */
#define	  TSR_DIS		  0x08000000 /* Decrementer Interrupt Status */
#define	  TSR_FIS		  0x04000000 /* FIT Interrupt Status */

#define	  TCR_WP_MASK		  0xc0000000 /* Watchdog Period mask */
#define	  TCR_WP_2_17		  0x00000000 /* 2**17 clocks */
#define	  TCR_WP_2_21		  0x40000000 /* 2**21 clocks */
#define	  TCR_WP_2_25		  0x80000000 /* 2**25 clocks */
#define	  TCR_WP_2_29		  0xc0000000 /* 2**29 clocks */
#define	  TCR_WRC_MASK		  0x30000000 /* Watchdog Reset Control mask */
#define	  TCR_WRC_NONE		  0x00000000 /* No watchdog reset */
#define	  TCR_WRC_CORE		  0x10000000 /* Core reset */
#define	  TCR_WRC_CHIP		  0x20000000 /* Chip reset */
#define	  TCR_WRC_SYSTEM	  0x30000000 /* System reset */
#define	  TCR_WIE		  0x08000000 /* Watchdog Interrupt Enable */
#define	  TCR_PIE		  0x04000000 /* PIT Interrupt Enable */
#define	  TCR_DIE		  0x04000000 /* Pecrementer Interrupt Enable */
#define	  TCR_FP_MASK		  0x03000000 /* FIT Period */
#define	  TCR_FP_2_9		  0x00000000 /* 2**9 clocks */
#define	  TCR_FP_2_13		  0x01000000 /* 2**13 clocks */
#define	  TCR_FP_2_17		  0x02000000 /* 2**17 clocks */
#define	  TCR_FP_2_21		  0x03000000 /* 2**21 clocks */
#define	  TCR_FIE		  0x00800000 /* FIT Interrupt Enable */
#define	  TCR_ARE		  0x00400000 /* Auto Reload Enable */

#define	SPR_PIT			0x3db	/* 4.. Programmable Interval Timer */
#define	SPR_SRR2		0x3de	/* 4.. Save/Restore Register 2 */
#define	SPR_SRR3		0x3df	/* 4.. Save/Restore Register 3 */
#define	SPR_HID0		0x3f0	/* ..8 Hardware Implementation Register 0 */
#define	SPR_HID1		0x3f1	/* ..8 Hardware Implementation Register 1 */
#define	SPR_HID2		0x3f3	/* ..8 Hardware Implementation Register 2 */
#define	SPR_HID4		0x3f4	/* ..8 Hardware Implementation Register 4 */
#define	SPR_HID5		0x3f6	/* ..8 Hardware Implementation Register 5 */
#define	SPR_HID6		0x3f9	/* ..8 Hardware Implementation Register 6 */

#define	SPR_CELL_TSRL		0x380	/* ... Cell BE Thread Status Register */
#define	SPR_CELL_TSCR		0x399	/* ... Cell BE Thread Switch Register */

#if defined(AIM)
#define	SPR_DBSR		0x3f0	/* 4.. Debug Status Register */
#define	  DBSR_IC		  0x80000000 /* Instruction completion debug event */
#define	  DBSR_BT		  0x40000000 /* Branch Taken debug event */
#define	  DBSR_EDE		  0x20000000 /* Exception debug event */
#define	  DBSR_TIE		  0x10000000 /* Trap Instruction debug event */
#define	  DBSR_UDE		  0x08000000 /* Unconditional debug event */
#define	  DBSR_IA1		  0x04000000 /* IAC1 debug event */
#define	  DBSR_IA2		  0x02000000 /* IAC2 debug event */
#define	  DBSR_DR1		  0x01000000 /* DAC1 Read debug event */
#define	  DBSR_DW1		  0x00800000 /* DAC1 Write debug event */
#define	  DBSR_DR2		  0x00400000 /* DAC2 Read debug event */
#define	  DBSR_DW2		  0x00200000 /* DAC2 Write debug event */
#define	  DBSR_IDE		  0x00100000 /* Imprecise debug event */
#define	  DBSR_IA3		  0x00080000 /* IAC3 debug event */
#define	  DBSR_IA4		  0x00040000 /* IAC4 debug event */
#define	  DBSR_MRR		  0x00000300 /* Most recent reset */
#define	SPR_DBCR0		0x3f2	/* 4.. Debug Control Register 0 */
#define	SPR_DBCR1		0x3bd	/* 4.. Debug Control Register 1 */
#define	SPR_IAC1		0x3f4	/* 4.. Instruction Address Compare 1 */
#define	SPR_IAC2		0x3f5	/* 4.. Instruction Address Compare 2 */
#define	SPR_DAC1		0x3f6	/* 4.. Data Address Compare 1 */
#define	SPR_DAC2		0x3f7	/* 4.. Data Address Compare 2 */
#define	SPR_PIR			0x3ff	/* .6. Processor Identification Register */
#elif defined(BOOKE)
#define	SPR_PIR			0x11e	/* ..8 Processor Identification Register */
#define	SPR_DBSR		0x130	/* ..8 Debug Status Register */
#define	  DBSR_IDE		  0x80000000 /* Imprecise debug event. */
#define	  DBSR_UDE		  0x40000000 /* Unconditional debug event. */
#define	  DBSR_MRR		  0x30000000 /* Most recent Reset (mask). */
#define	  DBSR_ICMP		  0x08000000 /* Instr. complete debug event. */
#define	  DBSR_BRT		  0x04000000 /* Branch taken debug event. */
#define	  DBSR_IRPT		  0x02000000 /* Interrupt taken debug event. */
#define	  DBSR_TRAP		  0x01000000 /* Trap instr. debug event. */
#define	  DBSR_IAC1		  0x00800000 /* Instr. address compare #1. */
#define	  DBSR_IAC2		  0x00400000 /* Instr. address compare #2. */
#define	  DBSR_IAC3		  0x00200000 /* Instr. address compare #3. */
#define	  DBSR_IAC4		  0x00100000 /* Instr. address compare #4. */
#define	  DBSR_DAC1R		  0x00080000 /* Data addr. read compare #1. */
#define	  DBSR_DAC1W		  0x00040000 /* Data addr. write compare #1. */
#define	  DBSR_DAC2R		  0x00020000 /* Data addr. read compare #2. */
#define	  DBSR_DAC2W		  0x00010000 /* Data addr. write compare #2. */
#define	  DBSR_RET		  0x00008000 /* Return debug event. */
#define	SPR_DBCR0		0x134	/* ..8 Debug Control Register 0 */
#define	SPR_DBCR1		0x135	/* ..8 Debug Control Register 1 */
#define	SPR_IAC1		0x138	/* ..8 Instruction Address Compare 1 */
#define	SPR_IAC2		0x139	/* ..8 Instruction Address Compare 2 */
#define	SPR_DAC1		0x13c	/* ..8 Data Address Compare 1 */
#define	SPR_DAC2		0x13d	/* ..8 Data Address Compare 2 */
#endif

#define	  DBCR0_EDM		  0x80000000 /* External Debug Mode */
#define	  DBCR0_IDM		  0x40000000 /* Internal Debug Mode */
#define	  DBCR0_RST_MASK	  0x30000000 /* ReSeT */
#define	  DBCR0_RST_NONE	  0x00000000 /*   No action */
#define	  DBCR0_RST_CORE	  0x10000000 /*   Core reset */
#define	  DBCR0_RST_CHIP	  0x20000000 /*   Chip reset */
#define	  DBCR0_RST_SYSTEM	  0x30000000 /*   System reset */
#define	  DBCR0_IC		  0x08000000 /* Instruction Completion debug event */
#define	  DBCR0_BT		  0x04000000 /* Branch Taken debug event */
#define	  DBCR0_EDE		  0x02000000 /* Exception Debug Event */
#define	  DBCR0_TDE		  0x01000000 /* Trap Debug Event */
#define	  DBCR0_IA1		  0x00800000 /* IAC (Instruction Address Compare) 1 debug event */
#define	  DBCR0_IA2		  0x00400000 /* IAC 2 debug event */
#define	  DBCR0_IA12		  0x00200000 /* Instruction Address Range Compare 1-2 */
#define	  DBCR0_IA12X		  0x00100000 /* IA12 eXclusive */
#define	  DBCR0_IA3		  0x00080000 /* IAC 3 debug event */
#define	  DBCR0_IA4		  0x00040000 /* IAC 4 debug event */
#define	  DBCR0_IA34		  0x00020000 /* Instruction Address Range Compare 3-4 */
#define	  DBCR0_IA34X		  0x00010000 /* IA34 eXclusive */
#define	  DBCR0_IA12T		  0x00008000 /* Instruction Address Range Compare 1-2 range Toggle */
#define	  DBCR0_IA34T		  0x00004000 /* Instruction Address Range Compare 3-4 range Toggle */
#define	  DBCR0_FT		  0x00000001 /* Freeze Timers on debug event */

#define	SPR_IABR		0x3f2	/* ..8 Instruction Address Breakpoint Register 0 */
#define	SPR_DABR		0x3f5	/* .6. Data Address Breakpoint Register */
#define	SPR_MSSCR0		0x3f6	/* .6. Memory SubSystem Control Register */
#define	  MSSCR0_SHDEN		  0x80000000 /* 0: Shared-state enable */
#define	  MSSCR0_SHDPEN3	  0x40000000 /* 1: ~SHD[01] signal enable in MEI mode */
#define	  MSSCR0_L1INTVEN	  0x38000000 /* 2-4: L1 data cache ~HIT intervention enable */
#define	  MSSCR0_L2INTVEN	  0x07000000 /* 5-7: L2 data cache ~HIT intervention enable*/
#define	  MSSCR0_DL1HWF		  0x00800000 /* 8: L1 data cache hardware flush */
#define	  MSSCR0_MBO		  0x00400000 /* 9: must be one */
#define	  MSSCR0_EMODE		  0x00200000 /* 10: MPX bus mode (read-only) */
#define	  MSSCR0_ABD		  0x00100000 /* 11: address bus driven (read-only) */
#define	  MSSCR0_MBZ		  0x000fffff /* 12-31: must be zero */
#define	  MSSCR0_L2PFE		  0x00000003 /* 30-31: L2 prefetch enable */
#define	SPR_MSSSR0		0x3f7	/* .6. Memory Subsystem Status Register (MPC745x) */
#define	  MSSSR0_L2TAG		  0x00040000 /* 13: L2 tag parity error */
#define	  MSSSR0_L2DAT		  0x00020000 /* 14: L2 data parity error */
#define	  MSSSR0_L3TAG		  0x00010000 /* 15: L3 tag parity error */
#define	  MSSSR0_L3DAT		  0x00008000 /* 16: L3 data parity error */
#define	  MSSSR0_APE		  0x00004000 /* 17: Address parity error */
#define	  MSSSR0_DPE		  0x00002000 /* 18: Data parity error */
#define	  MSSSR0_TEA		  0x00001000 /* 19: Bus transfer error acknowledge */
#define	SPR_LDSTCR		0x3f8	/* .6. Load/Store Control Register */
#define	SPR_L2PM		0x3f8	/* .6. L2 Private Memory Control Register */
#define	SPR_L2CR		0x3f9	/* .6. L2 Control Register */
#define	  L2CR_L2E		  0x80000000 /* 0: L2 enable */
#define	  L2CR_L2PE		  0x40000000 /* 1: L2 data parity enable */
#define	  L2CR_L2SIZ		  0x30000000 /* 2-3: L2 size */
#define	   L2SIZ_2M		  0x00000000
#define	   L2SIZ_256K		  0x10000000
#define	   L2SIZ_512K		  0x20000000
#define	   L2SIZ_1M		  0x30000000
#define	  L2CR_L2CLK		  0x0e000000 /* 4-6: L2 clock ratio */
#define	   L2CLK_DIS		  0x00000000 /* disable L2 clock */
#define	   L2CLK_10		  0x02000000 /* core clock / 1   */
#define	   L2CLK_15		  0x04000000 /*            / 1.5 */
#define	   L2CLK_20		  0x08000000 /*            / 2   */
#define	   L2CLK_25		  0x0a000000 /*            / 2.5 */
#define	   L2CLK_30		  0x0c000000 /*            / 3   */
#define	  L2CR_L2RAM		  0x01800000 /* 7-8: L2 RAM type */
#define	   L2RAM_FLOWTHRU_BURST	  0x00000000
#define	   L2RAM_PIPELINE_BURST	  0x01000000
#define	   L2RAM_PIPELINE_LATE	  0x01800000
#define	  L2CR_L2DO		  0x00400000 /* 9: L2 data-only.
				      Setting this bit disables instruction
				      caching. */
#define	  L2CR_L2I		  0x00200000 /* 10: L2 global invalidate. */
#define	  L2CR_L2IO_7450	  0x00010000 /* 11: L2 instruction-only (MPC745x). */
#define	  L2CR_L2CTL		  0x00100000 /* 11: L2 RAM control (ZZ enable).
				      Enables automatic operation of the
				      L2ZZ (low-power mode) signal. */
#define	  L2CR_L2WT		  0x00080000 /* 12: L2 write-through. */
#define	  L2CR_L2TS		  0x00040000 /* 13: L2 test support. */
#define	  L2CR_L2OH		  0x00030000 /* 14-15: L2 output hold. */
#define	  L2CR_L2DO_7450	  0x00010000 /* 15: L2 data-only (MPC745x). */
#define	  L2CR_L2SL		  0x00008000 /* 16: L2 DLL slow. */
#define	  L2CR_L2DF		  0x00004000 /* 17: L2 differential clock. */
#define	  L2CR_L2BYP		  0x00002000 /* 18: L2 DLL bypass. */
#define	  L2CR_L2FA		  0x00001000 /* 19: L2 flush assist (for software flush). */
#define	  L2CR_L2HWF		  0x00000800 /* 20: L2 hardware flush. */
#define	  L2CR_L2IO		  0x00000400 /* 21: L2 instruction-only. */
#define	  L2CR_L2CLKSTP		  0x00000200 /* 22: L2 clock stop. */
#define	  L2CR_L2DRO		  0x00000100 /* 23: L2DLL rollover checkstop enable. */
#define	  L2CR_L2IP		  0x00000001 /* 31: L2 global invalidate in */
					     /*     progress (read only). */
#define	SPR_L3CR		0x3fa	/* .6. L3 Control Register */
#define	  L3CR_L3E		  0x80000000 /* 0: L3 enable */
#define	  L3CR_L3PE		  0x40000000 /* 1: L3 data parity enable */
#define	  L3CR_L3APE		  0x20000000
#define	  L3CR_L3SIZ		  0x10000000 /* 3: L3 size (0=1MB, 1=2MB) */
#define	  L3CR_L3CLKEN		  0x08000000 /* 4: Enables L3_CLK[0:1] */
#define	  L3CR_L3CLK		  0x03800000
#define	  L3CR_L3IO		  0x00400000
#define	  L3CR_L3CLKEXT		  0x00200000
#define	  L3CR_L3CKSPEXT	  0x00100000
#define	  L3CR_L3OH1		  0x00080000
#define	  L3CR_L3SPO		  0x00040000
#define	  L3CR_L3CKSP		  0x00030000
#define	  L3CR_L3PSP		  0x0000e000
#define	  L3CR_L3REP		  0x00001000
#define	  L3CR_L3HWF		  0x00000800
#define	  L3CR_L3I		  0x00000400 /* 21: L3 global invalidate */
#define	  L3CR_L3RT		  0x00000300
#define	  L3CR_L3NIRCA		  0x00000080
#define	  L3CR_L3DO		  0x00000040
#define	  L3CR_PMEN		  0x00000004
#define	  L3CR_PMSIZ		  0x00000003

#define	SPR_DCCR		0x3fa	/* 4.. Data Cache Cachability Register */
#define	SPR_ICCR		0x3fb	/* 4.. Instruction Cache Cachability Register */
#define	SPR_THRM1		0x3fc	/* .6. Thermal Management Register */
#define	SPR_THRM2		0x3fd	/* .6. Thermal Management Register */
#define	  SPR_THRM_TIN		  0x80000000 /* Thermal interrupt bit (RO) */
#define	  SPR_THRM_TIV		  0x40000000 /* Thermal interrupt valid (RO) */
#define	  SPR_THRM_THRESHOLD(x)	  ((x) << 23) /* Thermal sensor threshold */
#define	  SPR_THRM_TID		  0x00000004 /* Thermal interrupt direction */
#define	  SPR_THRM_TIE		  0x00000002 /* Thermal interrupt enable */
#define	  SPR_THRM_VALID		  0x00000001 /* Valid bit */
#define	SPR_THRM3		0x3fe	/* .6. Thermal Management Register */
#define	  SPR_THRM_TIMER(x)	  ((x) << 1) /* Sampling interval timer */
#define	  SPR_THRM_ENABLE	  0x00000001 /* TAU Enable */
#define	SPR_FPECR		0x3fe	/* .6. Floating-Point Exception Cause Register */

/* Time Base Register declarations */
#define	TBR_TBL			0x10c	/* 468 Time Base Lower - read */
#define	TBR_TBU			0x10d	/* 468 Time Base Upper - read */
#define	TBR_TBWL		0x11c	/* 468 Time Base Lower - supervisor, write */
#define	TBR_TBWU		0x11d	/* 468 Time Base Upper - supervisor, write */

/* Performance counter declarations */
#define	PMC_OVERFLOW		0x80000000 /* Counter has overflowed */

/* The first five countable [non-]events are common to many PMC's */
#define	PMCN_NONE		 0 /* Count nothing */
#define	PMCN_CYCLES		 1 /* Processor cycles */
#define	PMCN_ICOMP		 2 /* Instructions completed */
#define	PMCN_TBLTRANS		 3 /* TBL bit transitions */
#define	PCMN_IDISPATCH		 4 /* Instructions dispatched */

/* Similar things for the 970 PMC direct counters */
#define	PMC970N_NONE		0x8 /* Count nothing */
#define	PMC970N_CYCLES		0xf /* Processor cycles */
#define	PMC970N_ICOMP		0x9 /* Instructions completed */

#if defined(BOOKE)

#define	SPR_MCARU		0x239	/* ..8 Machine Check Address register upper bits */
#define	SPR_MCSR		0x23c	/* ..8 Machine Check Syndrome register */
#define	SPR_MCAR		0x23d	/* ..8 Machine Check Address register */

#define	SPR_ESR			0x003e	/* ..8 Exception Syndrome Register */
#define	  ESR_PIL		  0x08000000 /* Program interrupt - illegal */
#define	  ESR_PPR		  0x04000000 /* Program interrupt - privileged */
#define	  ESR_PTR		  0x02000000 /* Program interrupt - trap */
#define	  ESR_ST		  0x00800000 /* Store operation */
#define	  ESR_DLK		  0x00200000 /* Data storage, D cache locking */
#define	  ESR_ILK		  0x00100000 /* Data storage, I cache locking */
#define	  ESR_BO		  0x00020000 /* Data/instruction storage, byte ordering */
#define	  ESR_SPE		  0x00000080 /* SPE exception bit */

#define	SPR_CSRR0		0x03a	/* ..8 58 Critical SRR0 */
#define	SPR_CSRR1		0x03b	/* ..8 59 Critical SRR1 */
#define	SPR_MCSRR0		0x23a	/* ..8 570 Machine check SRR0 */
#define	SPR_MCSRR1		0x23b	/* ..8 571 Machine check SRR1 */
#define	SPR_DSRR0		0x23e	/* ..8 574 Debug SRR0<E.ED> */
#define	SPR_DSRR1		0x23f	/* ..8 575 Debug SRR1<E.ED> */

#define	SPR_MMUCR		0x3b2	/* 4.. MMU Control Register */
#define	  MMUCR_SWOA		(0x80000000 >> 7)
#define	  MMUCR_U1TE		(0x80000000 >> 9)
#define	  MMUCR_U2SWOAE		(0x80000000 >> 10)
#define	  MMUCR_DULXE		(0x80000000 >> 12)
#define	  MMUCR_IULXE		(0x80000000 >> 13)
#define	  MMUCR_STS		(0x80000000 >> 15)
#define	  MMUCR_STID_MASK	(0xFF000000 >> 24)

#define	SPR_MMUCSR0		0x3f4	/* ..8 1012 MMU Control and Status Register 0 */
#define	  MMUCSR0_L2TLB0_FI	0x04	/*  TLB0 flash invalidate */
#define	  MMUCSR0_L2TLB1_FI	0x02	/*  TLB1 flash invalidate */

#define	SPR_SVR			0x3ff	/* ..8 1023 System Version Register */
#define	  SVR_MPC8533		  0x8034
#define	  SVR_MPC8533E		  0x803c
#define	  SVR_MPC8541		  0x8072
#define	  SVR_MPC8541E		  0x807a
#define	  SVR_MPC8548		  0x8031
#define	  SVR_MPC8548E		  0x8039
#define	  SVR_MPC8555		  0x8071
#define	  SVR_MPC8555E		  0x8079
#define	  SVR_MPC8572		  0x80e0
#define	  SVR_MPC8572E		  0x80e8
#define	  SVR_P1011		  0x80e5
#define	  SVR_P1011E		  0x80ed
#define	  SVR_P1013		  0x80e7
#define	  SVR_P1013E		  0x80ef
#define	  SVR_P1020		  0x80e4
#define	  SVR_P1020E		  0x80ec
#define	  SVR_P1022		  0x80e6
#define	  SVR_P1022E		  0x80ee
#define	  SVR_P2010		  0x80e3
#define	  SVR_P2010E		  0x80eb
#define	  SVR_P2020		  0x80e2
#define	  SVR_P2020E		  0x80ea
#define	  SVR_P2041		  0x8210
#define	  SVR_P2041E		  0x8218
#define	  SVR_P3041		  0x8211
#define	  SVR_P3041E		  0x8219
#define	  SVR_P4040		  0x8200
#define	  SVR_P4040E		  0x8208
#define	  SVR_P4080		  0x8201
#define	  SVR_P4080E		  0x8209
#define	  SVR_P5010		  0x8221
#define	  SVR_P5010E		  0x8229
#define	  SVR_P5020		  0x8220
#define	  SVR_P5020E		  0x8228
#define	  SVR_P5021		  0x8205
#define	  SVR_P5021E		  0x820d
#define	  SVR_P5040		  0x8204
#define	  SVR_P5040E		  0x820c
#define	SVR_VER(svr)		(((svr) >> 16) & 0xffff)

#define	SPR_PID0		0x030	/* ..8 Process ID Register 0 */
#define	SPR_PID1		0x279	/* ..8 Process ID Register 1 */
#define	SPR_PID2		0x27a	/* ..8 Process ID Register 2 */

#define	SPR_TLB0CFG		0x2B0	/* ..8 TLB 0 Config Register */
#define	SPR_TLB1CFG		0x2B1	/* ..8 TLB 1 Config Register */
#define	  TLBCFG_ASSOC_MASK	0xff000000 /* Associativity of TLB */
#define	  TLBCFG_ASSOC_SHIFT	24
#define	  TLBCFG_NENTRY_MASK	0x00000fff /* Number of entries in TLB */

#define	SPR_IVPR		0x03f	/* ..8 Interrupt Vector Prefix Register */
#define	SPR_IVOR0		0x190	/* ..8 Critical input */
#define	SPR_IVOR1		0x191	/* ..8 Machine check */
#define	SPR_IVOR2		0x192
#define	SPR_IVOR3		0x193
#define	SPR_IVOR4		0x194
#define	SPR_IVOR5		0x195
#define	SPR_IVOR6		0x196
#define	SPR_IVOR7		0x197
#define	SPR_IVOR8		0x198
#define	SPR_IVOR9		0x199
#define	SPR_IVOR10		0x19a
#define	SPR_IVOR11		0x19b
#define	SPR_IVOR12		0x19c
#define	SPR_IVOR13		0x19d
#define	SPR_IVOR14		0x19e
#define	SPR_IVOR15		0x19f
#define	SPR_IVOR32		0x210
#define	SPR_IVOR33		0x211
#define	SPR_IVOR34		0x212
#define	SPR_IVOR35		0x213

#define	SPR_MAS0		0x270	/* ..8 MMU Assist Register 0 Book-E/e500 */
#define	SPR_MAS1		0x271	/* ..8 MMU Assist Register 1 Book-E/e500 */
#define	SPR_MAS2		0x272	/* ..8 MMU Assist Register 2 Book-E/e500 */
#define	SPR_MAS3		0x273	/* ..8 MMU Assist Register 3 Book-E/e500 */
#define	SPR_MAS4		0x274	/* ..8 MMU Assist Register 4 Book-E/e500 */
#define	SPR_MAS5		0x275	/* ..8 MMU Assist Register 5 Book-E */
#define	SPR_MAS6		0x276	/* ..8 MMU Assist Register 6 Book-E/e500 */
#define	SPR_MAS7		0x3B0	/* ..8 MMU Assist Register 7 Book-E/e500 */
#define	SPR_MAS8		0x155	/* ..8 MMU Assist Register 8 Book-E/e500 */

#define	SPR_L1CFG0		0x203	/* ..8 L1 cache configuration register 0 */
#define	SPR_L1CFG1		0x204	/* ..8 L1 cache configuration register 1 */

#define	SPR_CCR1		0x378
#define	  CCR1_L2COBE		0x00000040

#define	DCR_L2DCDCRAI		0x0000	/* L2 D-Cache DCR Address Pointer */
#define	DCR_L2DCDCRDI		0x0001	/* L2 D-Cache DCR Data Indirect */
#define	DCR_L2CR0		0x00	/* L2 Cache Configuration Register 0 */
#define	  L2CR0_AS		0x30000000

#define	SPR_L1CSR0		0x3F2	/* ..8 L1 Cache Control and Status Register 0 */
#define	  L1CSR0_DCPE		0x00010000	/* Data Cache Parity Enable */
#define	  L1CSR0_DCLFR		0x00000100	/* Data Cache Lock Bits Flash Reset */
#define	  L1CSR0_DCFI		0x00000002	/* Data Cache Flash Invalidate */
#define	  L1CSR0_DCE		0x00000001	/* Data Cache Enable */
#define	SPR_L1CSR1		0x3F3	/* ..8 L1 Cache Control and Status Register 1 */
#define	  L1CSR1_ICPE		0x00010000	/* Instruction Cache Parity Enable */
#define	  L1CSR1_ICUL		0x00000400      /* Instr Cache Unable to Lock */
#define	  L1CSR1_ICLFR		0x00000100	/* Instruction Cache Lock Bits Flash Reset */
#define	  L1CSR1_ICFI		0x00000002	/* Instruction Cache Flash Invalidate */
#define	  L1CSR1_ICE		0x00000001	/* Instruction Cache Enable */

#define	SPR_L2CSR0		0x3F9	/* ..8 L2 Cache Control and Status Register 0 */
#define	  L2CSR0_L2E		0x80000000	/* L2 Cache Enable */
#define	  L2CSR0_L2PE		0x40000000	/* L2 Cache Parity Enable */
#define	  L2CSR0_L2FI		0x00200000	/* L2 Cache Flash Invalidate */
#define	  L2CSR0_L2LFC		0x00000400	/* L2 Cache Lock Flags Clear */

#define	SPR_BUCSR		0x3F5	/* ..8 Branch Unit Control and Status Register */
#define	  BUCSR_BPEN		0x00000001	/* Branch Prediction Enable */
#define	  BUCSR_BBFI		0x00000200	/* Branch Buffer Flash Invalidate */

#endif /* BOOKE */
#endif /* !_POWERPC_SPR_H_ */
