/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * Macros to make working with the System Control Registers simpler.
 *
 * Note that when register r0 is hard-coded in these definitions it means the
 * cp15 operation neither reads nor writes the register, and r0 is used only
 * because some syntatically-valid register name has to appear at that point to
 * keep the asm parser happy.
 */

#ifndef MACHINE_SYSREG_H
#define	MACHINE_SYSREG_H

/*
 * CP14 registers
 */
#if __ARM_ARCH >= 6

#define	CP14_DBGDIDR(rr)	p14, 0, rr, c0, c0, 0 /* Debug ID Register */
#define	CP14_DBGDSCRext_V6(rr)	p14, 0, rr, c0, c1, 0 /* Debug Status and Ctrl Register v6 */
#define	CP14_DBGDSCRext_V7(rr)	p14, 0, rr, c0, c2, 2 /* Debug Status and Ctrl Register v7 */
#define	CP14_DBGVCR(rr)		p14, 0, rr, c0, c7, 0 /* Vector Catch Register */
#define	CP14_DBGOSLAR(rr)	p14, 0, rr, c1, c0, 4 /* OS Lock Access Register */
#define	CP14_DBGOSLSR(rr)	p14, 0, rr, c1, c1, 4 /* OS Lock Status Register */
#define	CP14_DBGOSDLR(rr)	p14, 0, rr, c1, c3, 4 /* OS Double Lock Register */
#define	CP14_DBGPRSR(rr)	p14, 0, rr, c1, c5, 4 /* Device Powerdown and Reset Status */

#define	CP14_DBGDSCRint(rr)	CP14_DBGDSCRext_V6(rr) /* Debug Status and Ctrl internal view */

#endif

/*
 * CP15 C0 registers
 */
#define	CP15_MIDR(rr)		p15, 0, rr, c0, c0,  0 /* Main ID Register */
#define	CP15_CTR(rr)		p15, 0, rr, c0, c0,  1 /* Cache Type Register */
#define	CP15_TCMTR(rr)		p15, 0, rr, c0, c0,  2 /* TCM Type Register */
#define	CP15_TLBTR(rr)		p15, 0, rr, c0, c0,  3 /* TLB Type Register */
#define	CP15_MPIDR(rr)		p15, 0, rr, c0, c0,  5 /* Multiprocessor Affinity Register */
#define	CP15_REVIDR(rr)		p15, 0, rr, c0, c0,  6 /* Revision ID Register */

#define	CP15_ID_PFR0(rr)	p15, 0, rr, c0, c1,  0 /* Processor Feature Register 0 */
#define	CP15_ID_PFR1(rr)	p15, 0, rr, c0, c1,  1 /* Processor Feature Register 1 */
#define	CP15_ID_DFR0(rr)	p15, 0, rr, c0, c1,  2 /* Debug Feature Register 0 */
#define	CP15_ID_AFR0(rr)	p15, 0, rr, c0, c1,  3 /* Auxiliary Feature Register  0 */
#define	CP15_ID_MMFR0(rr)	p15, 0, rr, c0, c1,  4 /* Memory Model Feature Register 0 */
#define	CP15_ID_MMFR1(rr)	p15, 0, rr, c0, c1,  5 /* Memory Model Feature Register 1 */
#define	CP15_ID_MMFR2(rr)	p15, 0, rr, c0, c1,  6 /* Memory Model Feature Register 2 */
#define	CP15_ID_MMFR3(rr)	p15, 0, rr, c0, c1,  7 /* Memory Model Feature Register 3 */

#define	CP15_ID_ISAR0(rr)	p15, 0, rr, c0, c2,  0 /* Instruction Set Attribute Register 0 */
#define	CP15_ID_ISAR1(rr)	p15, 0, rr, c0, c2,  1 /* Instruction Set Attribute Register 1 */
#define	CP15_ID_ISAR2(rr)	p15, 0, rr, c0, c2,  2 /* Instruction Set Attribute Register 2 */
#define	CP15_ID_ISAR3(rr)	p15, 0, rr, c0, c2,  3 /* Instruction Set Attribute Register 3 */
#define	CP15_ID_ISAR4(rr)	p15, 0, rr, c0, c2,  4 /* Instruction Set Attribute Register 4 */
#define	CP15_ID_ISAR5(rr)	p15, 0, rr, c0, c2,  5 /* Instruction Set Attribute Register 5 */

#define	CP15_CCSIDR(rr)		p15, 1, rr, c0, c0,  0 /* Cache Size ID Registers */
#define	CP15_CLIDR(rr)		p15, 1, rr, c0, c0,  1 /* Cache Level ID Register */
#define	CP15_AIDR(rr)		p15, 1, rr, c0, c0,  7 /* Auxiliary ID Register */

#define	CP15_CSSELR(rr)		p15, 2, rr, c0, c0,  0 /* Cache Size Selection Register */

#define	CP15_VPIDR(rr)		p15, 4, rr, c0, c0,  0 /* Virtualization Processor ID Register */
#define	CP15_VMPIDR(rr)		p15, 4, rr, c0, c0,  5 /* Virtualization Multiprocessor ID Register */

/*
 * CP15 C1 registers
 */
#define	CP15_SCTLR(rr)		p15, 0, rr, c1, c0,  0 /* System Control Register */
#define	CP15_ACTLR(rr)		p15, 0, rr, c1, c0,  1 /* IMPLEMENTATION DEFINED Auxiliary Control Register */
#define	CP15_CPACR(rr)		p15, 0, rr, c1, c0,  2 /* Coprocessor Access Control Register */

#define	CP15_SCR(rr)		p15, 0, rr, c1, c1,  0 /* Secure Configuration Register */
#define	CP15_SDER(rr)		p15, 0, rr, c1, c1,  1 /* Secure Debug Enable Register */
#define	CP15_NSACR(rr)		p15, 0, rr, c1, c1,  2 /* Non-Secure Access Control Register */

#define	CP15_HSCTLR(rr)		p15, 4, rr, c1, c0,  0 /* Hyp System Control Register */

#define	CP15_HCR(rr)		p15, 4, rr, c1, c1,  0 /* Hyp Configuration Register */
#define	CP15_HDCR(rr)		p15, 4, rr, c1, c1,  1 /* Hyp Debug Configuration Register */
#define	CP15_HCPTR(rr)		p15, 4, rr, c1, c1,  2 /* Hyp Coprocessor Trap Register */
#define	CP15_HSTR(rr)		p15, 4, rr, c1, c1,  3 /* Hyp System Trap Register */

/*
 * CP15 C2 registers
 */
#define	CP15_TTBR0(rr)		p15, 0, rr, c2, c0,  0 /* Translation Table Base Register 0 */
#define	CP15_TTBR1(rr)		p15, 0, rr, c2, c0,  1 /* Translation Table Base Register 1 */
#define	CP15_TTBCR(rr)		p15, 0, rr, c2, c0,  2 /* Translation Table Base Control Register */

#define	CP15_HTCR(rr)		p15, 4, rr, c2, c0,  2 /* Hyp Translation Control Register */
#define	CP15_VTCR(rr)		p15, 4, rr, c2, c1,  2 /* Virtualization Translation Control Register */

/*
 * CP15 C3 registers
 */
#define	CP15_DACR(rr)		p15, 0, rr, c3, c0,  0 /* Domain Access Control Register */

/*
 * CP15 C5 registers
 */
#define	CP15_DFSR(rr)		p15, 0, rr, c5, c0,  0 /* Data Fault Status Register */
#define	CP15_HSR(rr)		p15, 4, rr, c5, c2,  0 /* Hyp Syndrome Register */

#if __ARM_ARCH >= 6
/* From ARMv6: */
#define	CP15_IFSR(rr)		p15, 0, rr, c5, c0,  1 /* Instruction Fault Status Register */
#endif
#if __ARM_ARCH >= 7
/* From ARMv7: */
#define	CP15_ADFSR(rr)		p15, 0, rr, c5, c1,  0 /* Auxiliary Data Fault Status Register */
#define	CP15_AIFSR(rr)		p15, 0, rr, c5, c1,  1 /* Auxiliary Instruction Fault Status Register */
#endif

/*
 * CP15 C6 registers
 */
#define	CP15_DFAR(rr)		p15, 0, rr, c6, c0,  0 /* Data Fault Address Register */
#define	CP15_HDFAR(rr)		p15, 4, rr, c6, c0,  0 /* Hyp Data Fault Address Register */
#define	CP15_HIFAR(rr)		p15, 4, rr, c6, c0,  2 /* Hyp Instruction Fault Address Register */
#define	CP15_HPFAR(rr)		p15, 4, rr, c6, c0,  4 /* Hyp IPA Fault Address Register */

#if __ARM_ARCH >= 6
/* From ARMv6k: */
#define	CP15_IFAR(rr)		p15, 0, rr, c6, c0,  2 /* Instruction Fault Address Register */
#endif

/*
 * CP15 C7 registers
 */
#if __ARM_ARCH >= 7 && defined(SMP)
/* From ARMv7: */
#define	CP15_ICIALLUIS		p15, 0, r0, c7, c1,  0 /* Instruction cache invalidate all PoU, IS */
#define	CP15_BPIALLIS		p15, 0, r0, c7, c1,  6 /* Branch predictor invalidate all IS */
#endif

#define	CP15_PAR(rr)		p15, 0, rr, c7, c4,  0 /* Physical Address Register */

#define	CP15_ICIALLU		p15, 0, r0, c7, c5,  0 /* Instruction cache invalidate all PoU */
#define	CP15_ICIMVAU(rr)	p15, 0, rr, c7, c5,  1 /* Instruction cache invalidate */
#if __ARM_ARCH == 6
/* Deprecated in ARMv7 */
#define	CP15_CP15ISB		p15, 0, r0, c7, c5,  4 /* ISB */
#endif
#define	CP15_BPIALL		p15, 0, r0, c7, c5,  6 /* Branch predictor invalidate all */
#define	CP15_BPIMVA		p15, 0, rr, c7, c5,  7 /* Branch predictor invalidate by MVA */

#if __ARM_ARCH == 6
/* Only ARMv6: */
#define	CP15_DCIALL		p15, 0, r0, c7, c6,  0 /* Data cache invalidate all */
#endif
#define	CP15_DCIMVAC(rr)	p15, 0, rr, c7, c6,  1 /* Data cache invalidate by MVA PoC */
#define	CP15_DCISW(rr)		p15, 0, rr, c7, c6,  2 /* Data cache invalidate by set/way */

#define	CP15_ATS1CPR(rr)	p15, 0, rr, c7, c8,  0 /* Stage 1 Current state PL1 read */
#define	CP15_ATS1CPW(rr)	p15, 0, rr, c7, c8,  1 /* Stage 1 Current state PL1 write */
#define	CP15_ATS1CUR(rr)	p15, 0, rr, c7, c8,  2 /* Stage 1 Current state unprivileged read */
#define	CP15_ATS1CUW(rr)	p15, 0, rr, c7, c8,  3 /* Stage 1 Current state unprivileged write */

#if __ARM_ARCH >= 7
/* From ARMv7: */
#define	CP15_ATS12NSOPR(rr)	p15, 0, rr, c7, c8,  4 /* Stages 1 and 2 Non-secure only PL1 read */
#define	CP15_ATS12NSOPW(rr)	p15, 0, rr, c7, c8,  5 /* Stages 1 and 2 Non-secure only PL1 write */
#define	CP15_ATS12NSOUR(rr)	p15, 0, rr, c7, c8,  6 /* Stages 1 and 2 Non-secure only unprivileged read */
#define	CP15_ATS12NSOUW(rr)	p15, 0, rr, c7, c8,  7 /* Stages 1 and 2 Non-secure only unprivileged write */
#endif

#if __ARM_ARCH == 6
/* Only ARMv6: */
#define	CP15_DCCALL		p15, 0, r0, c7, c10, 0 /* Data cache clean all */
#endif
#define	CP15_DCCMVAC(rr)	p15, 0, rr, c7, c10, 1 /* Data cache clean by MVA PoC */
#define	CP15_DCCSW(rr)		p15, 0, rr, c7, c10, 2 /* Data cache clean by set/way */
#if __ARM_ARCH == 6
/* Only ARMv6: */
#define	CP15_CP15DSB		p15, 0, r0, c7, c10, 4 /* DSB */
#define	CP15_CP15DMB		p15, 0, r0, c7, c10, 5 /* DMB */
#define	CP15_CP15WFI		p15, 0, r0, c7, c0,  4 /* WFI */
#endif

#if __ARM_ARCH >= 7
/* From ARMv7: */
#define	CP15_DCCMVAU(rr)	p15, 0, rr, c7, c11, 1 /* Data cache clean by MVA PoU */
#endif

#if __ARM_ARCH == 6
/* Only ARMv6: */
#define	CP15_DCCIALL		p15, 0, r0, c7, c14, 0 /* Data cache clean and invalidate all */
#endif
#define	CP15_DCCIMVAC(rr)	p15, 0, rr, c7, c14, 1 /* Data cache clean and invalidate by MVA PoC */
#define	CP15_DCCISW(rr)		p15, 0, rr, c7, c14, 2 /* Data cache clean and invalidate by set/way */

/*
 * CP15 C8 registers
 */
#if __ARM_ARCH >= 7 && defined(SMP)
/* From ARMv7: */
#define	CP15_TLBIALLIS		p15, 0, r0, c8, c3, 0 /* Invalidate entire unified TLB IS */
#define	CP15_TLBIMVAIS(rr)	p15, 0, rr, c8, c3, 1 /* Invalidate unified TLB by MVA IS */
#define	CP15_TLBIASIDIS(rr)	p15, 0, rr, c8, c3, 2 /* Invalidate unified TLB by ASID IS */
#define	CP15_TLBIMVAAIS(rr)	p15, 0, rr, c8, c3, 3 /* Invalidate unified TLB by MVA, all ASID IS */
#endif

#define	CP15_TLBIALL		p15, 0, r0, c8, c7, 0 /* Invalidate entire unified TLB */
#define	CP15_TLBIMVA(rr)	p15, 0, rr, c8, c7, 1 /* Invalidate unified TLB by MVA */
#define	CP15_TLBIASID(rr)	p15, 0, rr, c8, c7, 2 /* Invalidate unified TLB by ASID */

#define	CP15_TLBIALLH(rr)	p15, 4, rr, c8, c7, 0 /* Invalidate Entire Hyp Unified TLB */

#if __ARM_ARCH >= 6
/* From ARMv6: */
#define	CP15_TLBIMVAA(rr)	p15, 0, rr, c8, c7, 3 /* Invalidate unified TLB by MVA, all ASID */
#endif

/*
 * CP15 C9 registers
 */
#if __ARM_ARCH == 6 && defined(CPU_ARM1176)
#define	CP15_PMUSERENR(rr)	p15, 0, rr, c15,  c9, 0 /* Access Validation Control Register */
#define	CP15_PMCR(rr)		p15, 0, rr, c15, c12, 0 /* Performance Monitor Control Register */
#define	CP15_PMCCNTR(rr)	p15, 0, rr, c15, c12, 1 /* PM Cycle Count Register */
#elif __ARM_ARCH > 6
#define	CP15_L2CTLR(rr)		p15, 1, rr,  c9, c0,  2 /* L2 Control Register */
#define	CP15_PMCR(rr)		p15, 0, rr,  c9, c12, 0 /* Performance Monitor Control Register */
#define	CP15_PMCNTENSET(rr)	p15, 0, rr,  c9, c12, 1 /* PM Count Enable Set Register */
#define	CP15_PMCNTENCLR(rr)	p15, 0, rr,  c9, c12, 2 /* PM Count Enable Clear Register */
#define	CP15_PMOVSR(rr)		p15, 0, rr,  c9, c12, 3 /* PM Overflow Flag Status Register */
#define	CP15_PMSWINC(rr)	p15, 0, rr,  c9, c12, 4 /* PM Software Increment Register */
#define	CP15_PMSELR(rr)		p15, 0, rr,  c9, c12, 5 /* PM Event Counter Selection Register */
#define	CP15_PMCCNTR(rr)	p15, 0, rr,  c9, c13, 0 /* PM Cycle Count Register */
#define	CP15_PMXEVTYPER(rr)	p15, 0, rr,  c9, c13, 1 /* PM Event Type Select Register */
#define	CP15_PMXEVCNTRR(rr)	p15, 0, rr,  c9, c13, 2 /* PM Event Count Register */
#define	CP15_PMUSERENR(rr)	p15, 0, rr,  c9, c14, 0 /* PM User Enable Register */
#define	CP15_PMINTENSET(rr)	p15, 0, rr,  c9, c14, 1 /* PM Interrupt Enable Set Register */
#define	CP15_PMINTENCLR(rr)	p15, 0, rr,  c9, c14, 2 /* PM Interrupt Enable Clear Register */
#endif

/*
 * CP15 C10 registers
 */
/* Without LPAE this is PRRR, with LPAE it's MAIR0 */
#define	CP15_PRRR(rr)		p15, 0, rr, c10, c2, 0 /* Primary Region Remap Register */
#define	CP15_MAIR0(rr)		p15, 0, rr, c10, c2, 0 /* Memory Attribute Indirection Register 0 */
/* Without LPAE this is NMRR, with LPAE it's MAIR1 */
#define	CP15_NMRR(rr)		p15, 0, rr, c10, c2, 1 /* Normal Memory Remap Register */
#define	CP15_MAIR1(rr)		p15, 0, rr, c10, c2, 1 /* Memory Attribute Indirection Register 1 */

#define	CP15_AMAIR0(rr)		p15, 0, rr, c10, c3, 0 /* Auxiliary Memory Attribute Indirection Register 0 */
#define	CP15_AMAIR1(rr)		p15, 0, rr, c10, c3, 1 /* Auxiliary Memory Attribute Indirection Register 1 */

#define	CP15_HMAIR0(rr)		p15, 4, rr, c10, c2, 0 /* Hyp Memory Attribute Indirection Register 0 */
#define	CP15_HMAIR1(rr)		p15, 4, rr, c10, c2, 1 /* Hyp Memory Attribute Indirection Register 1 */

/*
 * CP15 C12 registers
 */
#define	CP15_VBAR(rr)		p15, 0, rr, c12, c0, 0 /* Vector Base Address Register */
#define	CP15_MVBAR(rr)		p15, 0, rr, c12, c0, 1 /* Monitor Vector Base Address Register */

#define	CP15_ISR(rr)		p15, 0, rr, c12, c1, 0 /* Interrupt Status Register */
#define	CP15_HVBAR(rr)		p15, 4, rr, c12, c0, 0 /* Hyp Vector Base Address Register*/

/*
 * CP15 C13 registers
 */
#define	CP15_FCSEIDR(rr)	p15, 0, rr, c13, c0, 0 /* FCSE Process ID Register */
#define	CP15_CONTEXTIDR(rr)	p15, 0, rr, c13, c0, 1 /* Context ID Register */
#define	CP15_TPIDRURW(rr)	p15, 0, rr, c13, c0, 2 /* User Read/Write Thread ID Register */
#define	CP15_TPIDRURO(rr)	p15, 0, rr, c13, c0, 3 /* User Read-Only Thread ID Register */
#define	CP15_TPIDRPRW(rr)	p15, 0, rr, c13, c0, 4 /* PL1 only Thread ID Register */
#define	CP15_HTPIDR(rr)		p15, 4, rr, c13, c0, 2 /* Hyp Software Thread ID Register */

/*
 * CP15 C14 registers
 * These are the Generic Timer registers and may be unallocated on some SoCs.
 * Only use these when you know the Generic Timer is available.
 */
#define	CP15_CNTFRQ(rr)		p15, 0, rr, c14, c0, 0 /* Counter Frequency Register */
#define	CP15_CNTKCTL(rr)	p15, 0, rr, c14, c1, 0 /* Timer PL1 Control Register */
#define	CP15_CNTP_TVAL(rr)	p15, 0, rr, c14, c2, 0 /* PL1 Physical Timer Value Register */
#define	CP15_CNTP_CTL(rr)	p15, 0, rr, c14, c2, 1 /* PL1 Physical Timer Control Register */
#define	CP15_CNTV_TVAL(rr)	p15, 0, rr, c14, c3, 0 /* Virtual Timer Value Register */
#define	CP15_CNTV_CTL(rr)	p15, 0, rr, c14, c3, 1 /* Virtual Timer Control Register */
#define	CP15_CNTHCTL(rr)	p15, 4, rr, c14, c1, 0 /* Timer PL2 Control Register */
#define	CP15_CNTHP_TVAL(rr)	p15, 4, rr, c14, c2, 0 /* PL2 Physical Timer Value Register */
#define	CP15_CNTHP_CTL(rr)	p15, 4, rr, c14, c2, 1 /* PL2 Physical Timer Control Register */
/* 64-bit registers for use with mcrr/mrrc */
#define	CP15_CNTPCT(rq, rr)	p15, 0, rq, rr, c14	/* Physical Count Register */
#define	CP15_CNTVCT(rq, rr)	p15, 1, rq, rr, c14	/* Virtual Count Register */
#define	CP15_CNTP_CVAL(rq, rr)	p15, 2, rq, rr, c14	/* PL1 Physical Timer Compare Value Register */
#define	CP15_CNTV_CVAL(rq, rr)	p15, 3, rq, rr, c14	/* Virtual Timer Compare Value Register */
#define	CP15_CNTVOFF(rq, rr)	p15, 4, rq, rr, c14	/* Virtual Offset Register */
#define	CP15_CNTHP_CVAL(rq, rr)	p15, 6, rq, rr, c14	/* PL2 Physical Timer Compare Value Register */

#define	CP15_VTTBR(rq, rr)	p15, 6, rq, rr, c2	/* Virtualization Translation Table Base Register */
#define	CP15_HTTBR(rq, rr)	p15, 4, rq, rr, c2	/* Hyp Translation Table Base Register */
#define	CP15_TTBR0_2(rq, rr)	p15, 0, rq, rr, c2	/* Translation Table Base Register 0 */
#define	CP15_TTBR1_2(rq, rr)	p15, 1, rq, rr, c2	/* Translation Table Base Register 1 */
#define	CP15_PAR_2(rq, rr)	p15, 0, rq, rr, c7	/* Physical Address Register */

/*
 * CP15 C15 registers
 */
#define CP15_CBAR(rr)		p15, 4, rr, c15, c0, 0 /* Configuration Base Address Register */

#endif /* !MACHINE_SYSREG_H */
