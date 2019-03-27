/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	from: @(#)specialreg.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _MACHINE_SPECIALREG_H_
#define	_MACHINE_SPECIALREG_H_

/*
 * Bits in 386 special registers:
 */
#define	CR0_PE	0x00000001	/* Protected mode Enable */
#define	CR0_MP	0x00000002	/* "Math" (fpu) Present */
#define	CR0_EM	0x00000004	/* EMulate FPU instructions. (trap ESC only) */
#define	CR0_TS	0x00000008	/* Task Switched (if MP, trap ESC and WAIT) */
#define	CR0_PG	0x80000000	/* PaGing enable */

/*
 * Bits in 486 special registers:
 */
#define	CR0_NE	0x00000020	/* Numeric Error enable (EX16 vs IRQ13) */
#define	CR0_WP	0x00010000	/* Write Protect (honor page protect in
							   all modes) */
#define	CR0_AM	0x00040000	/* Alignment Mask (set to enable AC flag) */
#define	CR0_NW  0x20000000	/* Not Write-through */
#define	CR0_CD  0x40000000	/* Cache Disable */

#define	CR3_PCID_SAVE 0x8000000000000000
#define	CR3_PCID_MASK 0xfff

/*
 * Bits in PPro special registers
 */
#define	CR4_VME	0x00000001	/* Virtual 8086 mode extensions */
#define	CR4_PVI	0x00000002	/* Protected-mode virtual interrupts */
#define	CR4_TSD	0x00000004	/* Time stamp disable */
#define	CR4_DE	0x00000008	/* Debugging extensions */
#define	CR4_PSE	0x00000010	/* Page size extensions */
#define	CR4_PAE	0x00000020	/* Physical address extension */
#define	CR4_MCE	0x00000040	/* Machine check enable */
#define	CR4_PGE	0x00000080	/* Page global enable */
#define	CR4_PCE	0x00000100	/* Performance monitoring counter enable */
#define	CR4_FXSR 0x00000200	/* Fast FPU save/restore used by OS */
#define	CR4_XMM	0x00000400	/* enable SIMD/MMX2 to use except 16 */
#define	CR4_VMXE 0x00002000	/* enable VMX operation (Intel-specific) */
#define	CR4_FSGSBASE 0x00010000	/* Enable FS/GS BASE accessing instructions */
#define	CR4_PCIDE 0x00020000	/* Enable Context ID */
#define	CR4_XSAVE 0x00040000	/* XSETBV/XGETBV */
#define	CR4_SMEP 0x00100000	/* Supervisor-Mode Execution Prevention */
#define	CR4_SMAP 0x00200000	/* Supervisor-Mode Access Prevention */
#define	CR4_PKE	0x00400000	/* Protection Keys Enable */

/*
 * Bits in AMD64 special registers.  EFER is 64 bits wide.
 */
#define	EFER_SCE 0x000000001	/* System Call Extensions (R/W) */
#define	EFER_LME 0x000000100	/* Long mode enable (R/W) */
#define	EFER_LMA 0x000000400	/* Long mode active (R) */
#define	EFER_NXE 0x000000800	/* PTE No-Execute bit enable (R/W) */
#define	EFER_SVM 0x000001000	/* SVM enable bit for AMD, reserved for Intel */
#define	EFER_LMSLE 0x000002000	/* Long Mode Segment Limit Enable */
#define	EFER_FFXSR 0x000004000	/* Fast FXSAVE/FSRSTOR */
#define	EFER_TCE   0x000008000	/* Translation Cache Extension */

/*
 * Intel Extended Features registers
 */
#define	XCR0	0		/* XFEATURE_ENABLED_MASK register */

#define	XFEATURE_ENABLED_X87		0x00000001
#define	XFEATURE_ENABLED_SSE		0x00000002
#define	XFEATURE_ENABLED_YMM_HI128	0x00000004
#define	XFEATURE_ENABLED_AVX		XFEATURE_ENABLED_YMM_HI128
#define	XFEATURE_ENABLED_BNDREGS	0x00000008
#define	XFEATURE_ENABLED_BNDCSR		0x00000010
#define	XFEATURE_ENABLED_OPMASK		0x00000020
#define	XFEATURE_ENABLED_ZMM_HI256	0x00000040
#define	XFEATURE_ENABLED_HI16_ZMM	0x00000080

#define	XFEATURE_AVX					\
    (XFEATURE_ENABLED_X87 | XFEATURE_ENABLED_SSE | XFEATURE_ENABLED_AVX)
#define	XFEATURE_AVX512						\
    (XFEATURE_ENABLED_OPMASK | XFEATURE_ENABLED_ZMM_HI256 |	\
    XFEATURE_ENABLED_HI16_ZMM)
#define	XFEATURE_MPX					\
    (XFEATURE_ENABLED_BNDREGS | XFEATURE_ENABLED_BNDCSR)

/*
 * CPUID instruction features register
 */
#define	CPUID_FPU	0x00000001
#define	CPUID_VME	0x00000002
#define	CPUID_DE	0x00000004
#define	CPUID_PSE	0x00000008
#define	CPUID_TSC	0x00000010
#define	CPUID_MSR	0x00000020
#define	CPUID_PAE	0x00000040
#define	CPUID_MCE	0x00000080
#define	CPUID_CX8	0x00000100
#define	CPUID_APIC	0x00000200
#define	CPUID_B10	0x00000400
#define	CPUID_SEP	0x00000800
#define	CPUID_MTRR	0x00001000
#define	CPUID_PGE	0x00002000
#define	CPUID_MCA	0x00004000
#define	CPUID_CMOV	0x00008000
#define	CPUID_PAT	0x00010000
#define	CPUID_PSE36	0x00020000
#define	CPUID_PSN	0x00040000
#define	CPUID_CLFSH	0x00080000
#define	CPUID_B20	0x00100000
#define	CPUID_DS	0x00200000
#define	CPUID_ACPI	0x00400000
#define	CPUID_MMX	0x00800000
#define	CPUID_FXSR	0x01000000
#define	CPUID_SSE	0x02000000
#define	CPUID_XMM	0x02000000
#define	CPUID_SSE2	0x04000000
#define	CPUID_SS	0x08000000
#define	CPUID_HTT	0x10000000
#define	CPUID_TM	0x20000000
#define	CPUID_IA64	0x40000000
#define	CPUID_PBE	0x80000000

#define	CPUID2_SSE3	0x00000001
#define	CPUID2_PCLMULQDQ 0x00000002
#define	CPUID2_DTES64	0x00000004
#define	CPUID2_MON	0x00000008
#define	CPUID2_DS_CPL	0x00000010
#define	CPUID2_VMX	0x00000020
#define	CPUID2_SMX	0x00000040
#define	CPUID2_EST	0x00000080
#define	CPUID2_TM2	0x00000100
#define	CPUID2_SSSE3	0x00000200
#define	CPUID2_CNXTID	0x00000400
#define	CPUID2_SDBG	0x00000800
#define	CPUID2_FMA	0x00001000
#define	CPUID2_CX16	0x00002000
#define	CPUID2_XTPR	0x00004000
#define	CPUID2_PDCM	0x00008000
#define	CPUID2_PCID	0x00020000
#define	CPUID2_DCA	0x00040000
#define	CPUID2_SSE41	0x00080000
#define	CPUID2_SSE42	0x00100000
#define	CPUID2_X2APIC	0x00200000
#define	CPUID2_MOVBE	0x00400000
#define	CPUID2_POPCNT	0x00800000
#define	CPUID2_TSCDLT	0x01000000
#define	CPUID2_AESNI	0x02000000
#define	CPUID2_XSAVE	0x04000000
#define	CPUID2_OSXSAVE	0x08000000
#define	CPUID2_AVX	0x10000000
#define	CPUID2_F16C	0x20000000
#define	CPUID2_RDRAND	0x40000000
#define	CPUID2_HV	0x80000000

/*
 * Important bits in the Thermal and Power Management flags
 * CPUID.6 EAX and ECX.
 */
#define	CPUTPM1_SENSOR	0x00000001
#define	CPUTPM1_TURBO	0x00000002
#define	CPUTPM1_ARAT	0x00000004
#define	CPUTPM1_HWP	0x00000080
#define	CPUTPM1_HWP_NOTIFICATION	0x00000100
#define	CPUTPM1_HWP_ACTIVITY_WINDOW	0x00000200
#define	CPUTPM1_HWP_PERF_PREF	0x00000400
#define	CPUTPM1_HWP_PKG	0x00000800
#define	CPUTPM1_HWP_FLEXIBLE	0x00020000
#define	CPUTPM2_EFFREQ	0x00000001

/* Intel Processor Trace CPUID. */

/* Leaf 0 ebx. */
#define	CPUPT_CR3		(1 << 0)	/* CR3 Filtering Support */
#define	CPUPT_PSB		(1 << 1)	/* Configurable PSB and Cycle-Accurate Mode Supported */
#define	CPUPT_IPF		(1 << 2)	/* IP Filtering and TraceStop supported */
#define	CPUPT_MTC		(1 << 3)	/* MTC Supported */
#define	CPUPT_PRW		(1 << 4)	/* PTWRITE Supported */
#define	CPUPT_PWR		(1 << 5)	/* Power Event Trace Supported */

/* Leaf 0 ecx. */
#define	CPUPT_TOPA		(1 << 0)	/* ToPA Output Supported */
#define	CPUPT_TOPA_MULTI	(1 << 1)	/* ToPA Tables Allow Multiple Output Entries */
#define	CPUPT_SINGLE		(1 << 2)	/* Single-Range Output Supported */
#define	CPUPT_TT_OUT		(1 << 3)	/* Output to Trace Transport Subsystem Supported */
#define	CPUPT_LINEAR_IP		(1 << 31)	/* IP Payloads are Linear IP, otherwise IP is effective */

/* Leaf 1 eax. */
#define	CPUPT_NADDR_S		0	/* Number of Address Ranges */
#define	CPUPT_NADDR_M		(0x7 << CPUPT_NADDR_S)
#define	CPUPT_MTC_BITMAP_S	16	/* Bitmap of supported MTC Period Encodings */
#define	CPUPT_MTC_BITMAP_M	(0xffff << CPUPT_MTC_BITMAP_S)

/* Leaf 1 ebx. */
#define	CPUPT_CT_BITMAP_S	0	/* Bitmap of supported Cycle Threshold values */
#define	CPUPT_CT_BITMAP_M	(0xffff << CPUPT_CT_BITMAP_S)
#define	CPUPT_PFE_BITMAP_S	16	/* Bitmap of supported Configurable PSB Frequency encoding */
#define	CPUPT_PFE_BITMAP_M	(0xffff << CPUPT_PFE_BITMAP_S)

/*
 * Important bits in the AMD extended cpuid flags
 */
#define	AMDID_SYSCALL	0x00000800
#define	AMDID_MP	0x00080000
#define	AMDID_NX	0x00100000
#define	AMDID_EXT_MMX	0x00400000
#define	AMDID_FFXSR	0x02000000
#define	AMDID_PAGE1GB	0x04000000
#define	AMDID_RDTSCP	0x08000000
#define	AMDID_LM	0x20000000
#define	AMDID_EXT_3DNOW	0x40000000
#define	AMDID_3DNOW	0x80000000

#define	AMDID2_LAHF	0x00000001
#define	AMDID2_CMP	0x00000002
#define	AMDID2_SVM	0x00000004
#define	AMDID2_EXT_APIC	0x00000008
#define	AMDID2_CR8	0x00000010
#define	AMDID2_ABM	0x00000020
#define	AMDID2_SSE4A	0x00000040
#define	AMDID2_MAS	0x00000080
#define	AMDID2_PREFETCH	0x00000100
#define	AMDID2_OSVW	0x00000200
#define	AMDID2_IBS	0x00000400
#define	AMDID2_XOP	0x00000800
#define	AMDID2_SKINIT	0x00001000
#define	AMDID2_WDT	0x00002000
#define	AMDID2_LWP	0x00008000
#define	AMDID2_FMA4	0x00010000
#define	AMDID2_TCE	0x00020000
#define	AMDID2_NODE_ID	0x00080000
#define	AMDID2_TBM	0x00200000
#define	AMDID2_TOPOLOGY	0x00400000
#define	AMDID2_PCXC	0x00800000
#define	AMDID2_PNXC	0x01000000
#define	AMDID2_DBE	0x04000000
#define	AMDID2_PTSC	0x08000000
#define	AMDID2_PTSCEL2I	0x10000000
#define	AMDID2_MWAITX	0x20000000

/*
 * CPUID instruction 1 eax info
 */
#define	CPUID_STEPPING		0x0000000f
#define	CPUID_MODEL		0x000000f0
#define	CPUID_FAMILY		0x00000f00
#define	CPUID_EXT_MODEL		0x000f0000
#define	CPUID_EXT_FAMILY	0x0ff00000
#ifdef __i386__
#define	CPUID_TO_MODEL(id) \
    ((((id) & CPUID_MODEL) >> 4) | \
    ((((id) & CPUID_FAMILY) >= 0x600) ? \
    (((id) & CPUID_EXT_MODEL) >> 12) : 0))
#define	CPUID_TO_FAMILY(id) \
    ((((id) & CPUID_FAMILY) >> 8) + \
    ((((id) & CPUID_FAMILY) == 0xf00) ? \
    (((id) & CPUID_EXT_FAMILY) >> 20) : 0))
#else
#define	CPUID_TO_MODEL(id) \
    ((((id) & CPUID_MODEL) >> 4) | \
    (((id) & CPUID_EXT_MODEL) >> 12))
#define	CPUID_TO_FAMILY(id) \
    ((((id) & CPUID_FAMILY) >> 8) + \
    (((id) & CPUID_EXT_FAMILY) >> 20))
#endif

/*
 * CPUID instruction 1 ebx info
 */
#define	CPUID_BRAND_INDEX	0x000000ff
#define	CPUID_CLFUSH_SIZE	0x0000ff00
#define	CPUID_HTT_CORES		0x00ff0000
#define	CPUID_LOCAL_APIC_ID	0xff000000

/*
 * CPUID instruction 5 info
 */
#define	CPUID5_MON_MIN_SIZE	0x0000ffff	/* eax */
#define	CPUID5_MON_MAX_SIZE	0x0000ffff	/* ebx */
#define	CPUID5_MON_MWAIT_EXT	0x00000001	/* ecx */
#define	CPUID5_MWAIT_INTRBREAK	0x00000002	/* ecx */

/*
 * MWAIT cpu power states.  Lower 4 bits are sub-states.
 */
#define	MWAIT_C0	0xf0
#define	MWAIT_C1	0x00
#define	MWAIT_C2	0x10
#define	MWAIT_C3	0x20
#define	MWAIT_C4	0x30

/*
 * MWAIT extensions.
 */
/* Interrupt breaks MWAIT even when masked. */
#define	MWAIT_INTRBREAK		0x00000001

/*
 * CPUID instruction 6 ecx info
 */
#define	CPUID_PERF_STAT		0x00000001
#define	CPUID_PERF_BIAS		0x00000008

/* 
 * CPUID instruction 0xb ebx info.
 */
#define	CPUID_TYPE_INVAL	0
#define	CPUID_TYPE_SMT		1
#define	CPUID_TYPE_CORE		2

/*
 * CPUID instruction 0xd Processor Extended State Enumeration Sub-leaf 1
 */
#define	CPUID_EXTSTATE_XSAVEOPT	0x00000001
#define	CPUID_EXTSTATE_XSAVEC	0x00000002
#define	CPUID_EXTSTATE_XINUSE	0x00000004
#define	CPUID_EXTSTATE_XSAVES	0x00000008

/*
 * AMD extended function 8000_0007h ebx info
 */
#define	AMDRAS_MCA_OF_RECOV	0x00000001
#define	AMDRAS_SUCCOR		0x00000002
#define	AMDRAS_HW_ASSERT	0x00000004
#define	AMDRAS_SCALABLE_MCA	0x00000008
#define	AMDRAS_PFEH_SUPPORT	0x00000010

/*
 * AMD extended function 8000_0007h edx info
 */
#define	AMDPM_TS		0x00000001
#define	AMDPM_FID		0x00000002
#define	AMDPM_VID		0x00000004
#define	AMDPM_TTP		0x00000008
#define	AMDPM_TM		0x00000010
#define	AMDPM_STC		0x00000020
#define	AMDPM_100MHZ_STEPS	0x00000040
#define	AMDPM_HW_PSTATE		0x00000080
#define	AMDPM_TSC_INVARIANT	0x00000100
#define	AMDPM_CPB		0x00000200

/*
 * AMD extended function 8000_0008h ebx info (amd_extended_feature_extensions)
 */
#define	AMDFEID_CLZERO		0x00000001
#define	AMDFEID_IRPERF		0x00000002
#define	AMDFEID_XSAVEERPTR	0x00000004
#define	AMDFEID_IBPB		0x00001000
#define	AMDFEID_IBRS		0x00004000
#define	AMDFEID_STIBP		0x00008000
/* The below are only defined if the corresponding base feature above exists. */
#define	AMDFEID_IBRS_ALWAYSON	0x00010000
#define	AMDFEID_STIBP_ALWAYSON	0x00020000
#define	AMDFEID_PREFER_IBRS	0x00040000
#define	AMDFEID_SSBD		0x01000000
/* SSBD via MSRC001_011F instead of MSR 0x48: */
#define	AMDFEID_VIRT_SSBD	0x02000000
#define	AMDFEID_SSB_NO		0x04000000

/*
 * AMD extended function 8000_0008h ecx info
 */
#define	AMDID_CMP_CORES		0x000000ff
#define	AMDID_COREID_SIZE	0x0000f000
#define	AMDID_COREID_SIZE_SHIFT	12

/*
 * CPUID instruction 7 Structured Extended Features, leaf 0 ebx info
 */
#define	CPUID_STDEXT_FSGSBASE	0x00000001
#define	CPUID_STDEXT_TSC_ADJUST	0x00000002
#define	CPUID_STDEXT_SGX	0x00000004
#define	CPUID_STDEXT_BMI1	0x00000008
#define	CPUID_STDEXT_HLE	0x00000010
#define	CPUID_STDEXT_AVX2	0x00000020
#define	CPUID_STDEXT_FDP_EXC	0x00000040
#define	CPUID_STDEXT_SMEP	0x00000080
#define	CPUID_STDEXT_BMI2	0x00000100
#define	CPUID_STDEXT_ERMS	0x00000200
#define	CPUID_STDEXT_INVPCID	0x00000400
#define	CPUID_STDEXT_RTM	0x00000800
#define	CPUID_STDEXT_PQM	0x00001000
#define	CPUID_STDEXT_NFPUSG	0x00002000
#define	CPUID_STDEXT_MPX	0x00004000
#define	CPUID_STDEXT_PQE	0x00008000
#define	CPUID_STDEXT_AVX512F	0x00010000
#define	CPUID_STDEXT_AVX512DQ	0x00020000
#define	CPUID_STDEXT_RDSEED	0x00040000
#define	CPUID_STDEXT_ADX	0x00080000
#define	CPUID_STDEXT_SMAP	0x00100000
#define	CPUID_STDEXT_AVX512IFMA	0x00200000
#define	CPUID_STDEXT_PCOMMIT	0x00400000
#define	CPUID_STDEXT_CLFLUSHOPT	0x00800000
#define	CPUID_STDEXT_CLWB	0x01000000
#define	CPUID_STDEXT_PROCTRACE	0x02000000
#define	CPUID_STDEXT_AVX512PF	0x04000000
#define	CPUID_STDEXT_AVX512ER	0x08000000
#define	CPUID_STDEXT_AVX512CD	0x10000000
#define	CPUID_STDEXT_SHA	0x20000000
#define	CPUID_STDEXT_AVX512BW	0x40000000
#define	CPUID_STDEXT_AVX512VL	0x80000000

/*
 * CPUID instruction 7 Structured Extended Features, leaf 0 ecx info
 */
#define	CPUID_STDEXT2_PREFETCHWT1 0x00000001
#define	CPUID_STDEXT2_UMIP	0x00000004
#define	CPUID_STDEXT2_PKU	0x00000008
#define	CPUID_STDEXT2_OSPKE	0x00000010
#define	CPUID_STDEXT2_WAITPKG	0x00000020
#define	CPUID_STDEXT2_GFNI	0x00000100
#define	CPUID_STDEXT2_RDPID	0x00400000
#define	CPUID_STDEXT2_CLDEMOTE	0x02000000
#define	CPUID_STDEXT2_MOVDIRI	0x08000000
#define	CPUID_STDEXT2_MOVDIRI64B	0x10000000
#define	CPUID_STDEXT2_SGXLC	0x40000000

/*
 * CPUID instruction 7 Structured Extended Features, leaf 0 edx info
 */
#define	CPUID_STDEXT3_TSXFA	0x00002000
#define	CPUID_STDEXT3_IBPB	0x04000000
#define	CPUID_STDEXT3_STIBP	0x08000000
#define	CPUID_STDEXT3_L1D_FLUSH	0x10000000
#define	CPUID_STDEXT3_ARCH_CAP	0x20000000
#define	CPUID_STDEXT3_CORE_CAP	0x40000000
#define	CPUID_STDEXT3_SSBD	0x80000000

/* MSR IA32_ARCH_CAP(ABILITIES) bits */
#define	IA32_ARCH_CAP_RDCL_NO	0x00000001
#define	IA32_ARCH_CAP_IBRS_ALL	0x00000002
#define	IA32_ARCH_CAP_RSBA	0x00000004
#define	IA32_ARCH_CAP_SKIP_L1DFL_VMENTRY	0x00000008
#define	IA32_ARCH_CAP_SSB_NO	0x00000010

/*
 * CPUID manufacturers identifiers
 */
#define	AMD_VENDOR_ID		"AuthenticAMD"
#define	CENTAUR_VENDOR_ID	"CentaurHauls"
#define	CYRIX_VENDOR_ID		"CyrixInstead"
#define	INTEL_VENDOR_ID		"GenuineIntel"
#define	NEXGEN_VENDOR_ID	"NexGenDriven"
#define	NSC_VENDOR_ID		"Geode by NSC"
#define	RISE_VENDOR_ID		"RiseRiseRise"
#define	SIS_VENDOR_ID		"SiS SiS SiS "
#define	TRANSMETA_VENDOR_ID	"GenuineTMx86"
#define	UMC_VENDOR_ID		"UMC UMC UMC "

/*
 * Model-specific registers for the i386 family
 */
#define	MSR_P5_MC_ADDR		0x000
#define	MSR_P5_MC_TYPE		0x001
#define	MSR_TSC			0x010
#define	MSR_P5_CESR		0x011
#define	MSR_P5_CTR0		0x012
#define	MSR_P5_CTR1		0x013
#define	MSR_IA32_PLATFORM_ID	0x017
#define	MSR_APICBASE		0x01b
#define	MSR_EBL_CR_POWERON	0x02a
#define	MSR_TEST_CTL		0x033
#define	MSR_IA32_FEATURE_CONTROL 0x03a
#define	MSR_IA32_SPEC_CTRL	0x048
#define	MSR_IA32_PRED_CMD	0x049
#define	MSR_BIOS_UPDT_TRIG	0x079
#define	MSR_BBL_CR_D0		0x088
#define	MSR_BBL_CR_D1		0x089
#define	MSR_BBL_CR_D2		0x08a
#define	MSR_BIOS_SIGN		0x08b
#define	MSR_PERFCTR0		0x0c1
#define	MSR_PERFCTR1		0x0c2
#define	MSR_PLATFORM_INFO	0x0ce
#define	MSR_MPERF		0x0e7
#define	MSR_APERF		0x0e8
#define	MSR_IA32_EXT_CONFIG	0x0ee	/* Undocumented. Core Solo/Duo only */
#define	MSR_MTRRcap		0x0fe
#define	MSR_IA32_ARCH_CAP	0x10a
#define	MSR_IA32_FLUSH_CMD	0x10b
#define	MSR_TSX_FORCE_ABORT	0x10f
#define	MSR_BBL_CR_ADDR		0x116
#define	MSR_BBL_CR_DECC		0x118
#define	MSR_BBL_CR_CTL		0x119
#define	MSR_BBL_CR_TRIG		0x11a
#define	MSR_BBL_CR_BUSY		0x11b
#define	MSR_BBL_CR_CTL3		0x11e
#define	MSR_SYSENTER_CS_MSR	0x174
#define	MSR_SYSENTER_ESP_MSR	0x175
#define	MSR_SYSENTER_EIP_MSR	0x176
#define	MSR_MCG_CAP		0x179
#define	MSR_MCG_STATUS		0x17a
#define	MSR_MCG_CTL		0x17b
#define	MSR_EVNTSEL0		0x186
#define	MSR_EVNTSEL1		0x187
#define	MSR_THERM_CONTROL	0x19a
#define	MSR_THERM_INTERRUPT	0x19b
#define	MSR_THERM_STATUS	0x19c
#define	MSR_IA32_MISC_ENABLE	0x1a0
#define	MSR_IA32_TEMPERATURE_TARGET	0x1a2
#define	MSR_TURBO_RATIO_LIMIT	0x1ad
#define	MSR_TURBO_RATIO_LIMIT1	0x1ae
#define	MSR_DEBUGCTLMSR		0x1d9
#define	MSR_LASTBRANCHFROMIP	0x1db
#define	MSR_LASTBRANCHTOIP	0x1dc
#define	MSR_LASTINTFROMIP	0x1dd
#define	MSR_LASTINTTOIP		0x1de
#define	MSR_ROB_CR_BKUPTMPDR6	0x1e0
#define	MSR_MTRRVarBase		0x200
#define	MSR_MTRR64kBase		0x250
#define	MSR_MTRR16kBase		0x258
#define	MSR_MTRR4kBase		0x268
#define	MSR_PAT			0x277
#define	MSR_MC0_CTL2		0x280
#define	MSR_MTRRdefType		0x2ff
#define	MSR_MC0_CTL		0x400
#define	MSR_MC0_STATUS		0x401
#define	MSR_MC0_ADDR		0x402
#define	MSR_MC0_MISC		0x403
#define	MSR_MC1_CTL		0x404
#define	MSR_MC1_STATUS		0x405
#define	MSR_MC1_ADDR		0x406
#define	MSR_MC1_MISC		0x407
#define	MSR_MC2_CTL		0x408
#define	MSR_MC2_STATUS		0x409
#define	MSR_MC2_ADDR		0x40a
#define	MSR_MC2_MISC		0x40b
#define	MSR_MC3_CTL		0x40c
#define	MSR_MC3_STATUS		0x40d
#define	MSR_MC3_ADDR		0x40e
#define	MSR_MC3_MISC		0x40f
#define	MSR_MC4_CTL		0x410
#define	MSR_MC4_STATUS		0x411
#define	MSR_MC4_ADDR		0x412
#define	MSR_MC4_MISC		0x413
#define	MSR_RAPL_POWER_UNIT	0x606
#define	MSR_PKG_ENERGY_STATUS	0x611
#define	MSR_DRAM_ENERGY_STATUS	0x619
#define	MSR_PP0_ENERGY_STATUS	0x639
#define	MSR_PP1_ENERGY_STATUS	0x641
#define	MSR_PPERF		0x64e
#define	MSR_TSC_DEADLINE	0x6e0	/* Writes are not serializing */
#define	MSR_IA32_PM_ENABLE	0x770
#define	MSR_IA32_HWP_CAPABILITIES	0x771
#define	MSR_IA32_HWP_REQUEST_PKG	0x772
#define	MSR_IA32_HWP_INTERRUPT		0x773
#define	MSR_IA32_HWP_REQUEST	0x774
#define	MSR_IA32_HWP_STATUS	0x777

/*
 * VMX MSRs
 */
#define	MSR_VMX_BASIC		0x480
#define	MSR_VMX_PINBASED_CTLS	0x481
#define	MSR_VMX_PROCBASED_CTLS	0x482
#define	MSR_VMX_EXIT_CTLS	0x483
#define	MSR_VMX_ENTRY_CTLS	0x484
#define	MSR_VMX_CR0_FIXED0	0x486
#define	MSR_VMX_CR0_FIXED1	0x487
#define	MSR_VMX_CR4_FIXED0	0x488
#define	MSR_VMX_CR4_FIXED1	0x489
#define	MSR_VMX_PROCBASED_CTLS2	0x48b
#define	MSR_VMX_EPT_VPID_CAP	0x48c
#define	MSR_VMX_TRUE_PINBASED_CTLS	0x48d
#define	MSR_VMX_TRUE_PROCBASED_CTLS	0x48e
#define	MSR_VMX_TRUE_EXIT_CTLS	0x48f
#define	MSR_VMX_TRUE_ENTRY_CTLS	0x490

/*
 * X2APIC MSRs.
 * Writes are not serializing.
 */
#define	MSR_APIC_000		0x800
#define	MSR_APIC_ID		0x802
#define	MSR_APIC_VERSION	0x803
#define	MSR_APIC_TPR		0x808
#define	MSR_APIC_EOI		0x80b
#define	MSR_APIC_LDR		0x80d
#define	MSR_APIC_SVR		0x80f
#define	MSR_APIC_ISR0		0x810
#define	MSR_APIC_ISR1		0x811
#define	MSR_APIC_ISR2		0x812
#define	MSR_APIC_ISR3		0x813
#define	MSR_APIC_ISR4		0x814
#define	MSR_APIC_ISR5		0x815
#define	MSR_APIC_ISR6		0x816
#define	MSR_APIC_ISR7		0x817
#define	MSR_APIC_TMR0		0x818
#define	MSR_APIC_IRR0		0x820
#define	MSR_APIC_ESR		0x828
#define	MSR_APIC_LVT_CMCI	0x82F
#define	MSR_APIC_ICR		0x830
#define	MSR_APIC_LVT_TIMER	0x832
#define	MSR_APIC_LVT_THERMAL	0x833
#define	MSR_APIC_LVT_PCINT	0x834
#define	MSR_APIC_LVT_LINT0	0x835
#define	MSR_APIC_LVT_LINT1	0x836
#define	MSR_APIC_LVT_ERROR	0x837
#define	MSR_APIC_ICR_TIMER	0x838
#define	MSR_APIC_CCR_TIMER	0x839
#define	MSR_APIC_DCR_TIMER	0x83e
#define	MSR_APIC_SELF_IPI	0x83f

#define	MSR_IA32_XSS		0xda0

/*
 * Intel Processor Trace (PT) MSRs.
 */
#define	MSR_IA32_RTIT_OUTPUT_BASE	0x560	/* Trace Output Base Register (R/W) */
#define	MSR_IA32_RTIT_OUTPUT_MASK_PTRS	0x561	/* Trace Output Mask Pointers Register (R/W) */
#define	MSR_IA32_RTIT_CTL		0x570	/* Trace Control Register (R/W) */
#define	 RTIT_CTL_TRACEEN	(1 << 0)
#define	 RTIT_CTL_CYCEN		(1 << 1)
#define	 RTIT_CTL_OS		(1 << 2)
#define	 RTIT_CTL_USER		(1 << 3)
#define	 RTIT_CTL_PWREVTEN	(1 << 4)
#define	 RTIT_CTL_FUPONPTW	(1 << 5)
#define	 RTIT_CTL_FABRICEN	(1 << 6)
#define	 RTIT_CTL_CR3FILTER	(1 << 7)
#define	 RTIT_CTL_TOPA		(1 << 8)
#define	 RTIT_CTL_MTCEN		(1 << 9)
#define	 RTIT_CTL_TSCEN		(1 << 10)
#define	 RTIT_CTL_DISRETC	(1 << 11)
#define	 RTIT_CTL_PTWEN		(1 << 12)
#define	 RTIT_CTL_BRANCHEN	(1 << 13)
#define	 RTIT_CTL_MTC_FREQ_S	14
#define	 RTIT_CTL_MTC_FREQ(n)	((n) << RTIT_CTL_MTC_FREQ_S)
#define	 RTIT_CTL_MTC_FREQ_M	(0xf << RTIT_CTL_MTC_FREQ_S)
#define	 RTIT_CTL_CYC_THRESH_S	19
#define	 RTIT_CTL_CYC_THRESH_M	(0xf << RTIT_CTL_CYC_THRESH_S)
#define	 RTIT_CTL_PSB_FREQ_S	24
#define	 RTIT_CTL_PSB_FREQ_M	(0xf << RTIT_CTL_PSB_FREQ_S)
#define	 RTIT_CTL_ADDR_CFG_S(n) (32 + (n) * 4)
#define	 RTIT_CTL_ADDR0_CFG_S	32
#define	 RTIT_CTL_ADDR0_CFG_M	(0xfULL << RTIT_CTL_ADDR0_CFG_S)
#define	 RTIT_CTL_ADDR1_CFG_S	36
#define	 RTIT_CTL_ADDR1_CFG_M	(0xfULL << RTIT_CTL_ADDR1_CFG_S)
#define	 RTIT_CTL_ADDR2_CFG_S	40
#define	 RTIT_CTL_ADDR2_CFG_M	(0xfULL << RTIT_CTL_ADDR2_CFG_S)
#define	 RTIT_CTL_ADDR3_CFG_S	44
#define	 RTIT_CTL_ADDR3_CFG_M	(0xfULL << RTIT_CTL_ADDR3_CFG_S)
#define	MSR_IA32_RTIT_STATUS		0x571	/* Tracing Status Register (R/W) */
#define	 RTIT_STATUS_FILTEREN	(1 << 0)
#define	 RTIT_STATUS_CONTEXTEN	(1 << 1)
#define	 RTIT_STATUS_TRIGGEREN	(1 << 2)
#define	 RTIT_STATUS_ERROR	(1 << 4)
#define	 RTIT_STATUS_STOPPED	(1 << 5)
#define	 RTIT_STATUS_PACKETBYTECNT_S	32
#define	 RTIT_STATUS_PACKETBYTECNT_M	(0x1ffffULL << RTIT_STATUS_PACKETBYTECNT_S)
#define	MSR_IA32_RTIT_CR3_MATCH		0x572	/* Trace Filter CR3 Match Register (R/W) */
#define	MSR_IA32_RTIT_ADDR_A(n)		(0x580 + (n) * 2)
#define	MSR_IA32_RTIT_ADDR_B(n)		(0x581 + (n) * 2)
#define	MSR_IA32_RTIT_ADDR0_A		0x580	/* Region 0 Start Address (R/W) */
#define	MSR_IA32_RTIT_ADDR0_B		0x581	/* Region 0 End Address (R/W) */
#define	MSR_IA32_RTIT_ADDR1_A		0x582	/* Region 1 Start Address (R/W) */
#define	MSR_IA32_RTIT_ADDR1_B		0x583	/* Region 1 End Address (R/W) */
#define	MSR_IA32_RTIT_ADDR2_A		0x584	/* Region 2 Start Address (R/W) */
#define	MSR_IA32_RTIT_ADDR2_B		0x585	/* Region 2 End Address (R/W) */
#define	MSR_IA32_RTIT_ADDR3_A		0x586	/* Region 3 Start Address (R/W) */
#define	MSR_IA32_RTIT_ADDR3_B		0x587	/* Region 3 End Address (R/W) */

/* Intel Processor Trace Table of Physical Addresses (ToPA). */
#define	TOPA_SIZE_S	6
#define	TOPA_SIZE_M	(0xf << TOPA_SIZE_S)
#define	TOPA_SIZE_4K	(0 << TOPA_SIZE_S)
#define	TOPA_SIZE_8K	(1 << TOPA_SIZE_S)
#define	TOPA_SIZE_16K	(2 << TOPA_SIZE_S)
#define	TOPA_SIZE_32K	(3 << TOPA_SIZE_S)
#define	TOPA_SIZE_64K	(4 << TOPA_SIZE_S)
#define	TOPA_SIZE_128K	(5 << TOPA_SIZE_S)
#define	TOPA_SIZE_256K	(6 << TOPA_SIZE_S)
#define	TOPA_SIZE_512K	(7 << TOPA_SIZE_S)
#define	TOPA_SIZE_1M	(8 << TOPA_SIZE_S)
#define	TOPA_SIZE_2M	(9 << TOPA_SIZE_S)
#define	TOPA_SIZE_4M	(10 << TOPA_SIZE_S)
#define	TOPA_SIZE_8M	(11 << TOPA_SIZE_S)
#define	TOPA_SIZE_16M	(12 << TOPA_SIZE_S)
#define	TOPA_SIZE_32M	(13 << TOPA_SIZE_S)
#define	TOPA_SIZE_64M	(14 << TOPA_SIZE_S)
#define	TOPA_SIZE_128M	(15 << TOPA_SIZE_S)
#define	TOPA_STOP	(1 << 4)
#define	TOPA_INT	(1 << 2)
#define	TOPA_END	(1 << 0)

/*
 * Constants related to MSR's.
 */
#define	APICBASE_RESERVED	0x000002ff
#define	APICBASE_BSP		0x00000100
#define	APICBASE_X2APIC		0x00000400
#define	APICBASE_ENABLED	0x00000800
#define	APICBASE_ADDRESS	0xfffff000

/* MSR_IA32_FEATURE_CONTROL related */
#define	IA32_FEATURE_CONTROL_LOCK	0x01	/* lock bit */
#define	IA32_FEATURE_CONTROL_SMX_EN	0x02	/* enable VMX inside SMX */
#define	IA32_FEATURE_CONTROL_VMX_EN	0x04	/* enable VMX outside SMX */

/* MSR IA32_MISC_ENABLE */
#define	IA32_MISC_EN_FASTSTR	0x0000000000000001ULL
#define	IA32_MISC_EN_ATCCE	0x0000000000000008ULL
#define	IA32_MISC_EN_PERFMON	0x0000000000000080ULL
#define	IA32_MISC_EN_PEBSU	0x0000000000001000ULL
#define	IA32_MISC_EN_ESSTE	0x0000000000010000ULL
#define	IA32_MISC_EN_MONE	0x0000000000040000ULL
#define	IA32_MISC_EN_LIMCPUID	0x0000000000400000ULL
#define	IA32_MISC_EN_xTPRD	0x0000000000800000ULL
#define	IA32_MISC_EN_XDD	0x0000000400000000ULL

/*
 * IA32_SPEC_CTRL and IA32_PRED_CMD MSRs are described in the Intel'
 * document 336996-001 Speculative Execution Side Channel Mitigations.
 *
 * AMD uses the same MSRs and bit definitions, as described in 111006-B
 * "Indirect Branch Control Extension" and 124441 "Speculative Store Bypass
 * Disable."
 */
/* MSR IA32_SPEC_CTRL */
#define	IA32_SPEC_CTRL_IBRS	0x00000001
#define	IA32_SPEC_CTRL_STIBP	0x00000002
#define	IA32_SPEC_CTRL_SSBD	0x00000004

/* MSR IA32_PRED_CMD */
#define	IA32_PRED_CMD_IBPB_BARRIER	0x0000000000000001ULL

/* MSR IA32_FLUSH_CMD */
#define	IA32_FLUSH_CMD_L1D	0x00000001

/* MSR IA32_HWP_CAPABILITIES */
#define	IA32_HWP_CAPABILITIES_HIGHEST_PERFORMANCE(x)	(((x) >> 0) & 0xff)
#define	IA32_HWP_CAPABILITIES_GUARANTEED_PERFORMANCE(x)	(((x) >> 8) & 0xff)
#define	IA32_HWP_CAPABILITIES_EFFICIENT_PERFORMANCE(x)	(((x) >> 16) & 0xff)
#define	IA32_HWP_CAPABILITIES_LOWEST_PERFORMANCE(x)	(((x) >> 24) & 0xff)

/* MSR IA32_HWP_REQUEST */
#define	IA32_HWP_REQUEST_MINIMUM_VALID			(1ULL << 63)
#define	IA32_HWP_REQUEST_MAXIMUM_VALID			(1ULL << 62)
#define	IA32_HWP_REQUEST_DESIRED_VALID			(1ULL << 61)
#define	IA32_HWP_REQUEST_EPP_VALID 			(1ULL << 60)
#define	IA32_HWP_REQUEST_ACTIVITY_WINDOW_VALID		(1ULL << 59)
#define	IA32_HWP_REQUEST_PACKAGE_CONTROL		(1ULL << 42)
#define	IA32_HWP_ACTIVITY_WINDOW			(0x3ffULL << 32)
#define	IA32_HWP_REQUEST_ENERGY_PERFORMANCE_PREFERENCE	(0xffULL << 24)
#define	IA32_HWP_DESIRED_PERFORMANCE			(0xffULL << 16)
#define	IA32_HWP_REQUEST_MAXIMUM_PERFORMANCE		(0xffULL << 8)
#define	IA32_HWP_MINIMUM_PERFORMANCE			(0xffULL << 0)

/*
 * PAT modes.
 */
#define	PAT_UNCACHEABLE		0x00
#define	PAT_WRITE_COMBINING	0x01
#define	PAT_WRITE_THROUGH	0x04
#define	PAT_WRITE_PROTECTED	0x05
#define	PAT_WRITE_BACK		0x06
#define	PAT_UNCACHED		0x07
#define	PAT_VALUE(i, m)		((long long)(m) << (8 * (i)))
#define	PAT_MASK(i)		PAT_VALUE(i, 0xff)

/*
 * Constants related to MTRRs
 */
#define	MTRR_UNCACHEABLE	0x00
#define	MTRR_WRITE_COMBINING	0x01
#define	MTRR_WRITE_THROUGH	0x04
#define	MTRR_WRITE_PROTECTED	0x05
#define	MTRR_WRITE_BACK		0x06
#define	MTRR_N64K		8	/* numbers of fixed-size entries */
#define	MTRR_N16K		16
#define	MTRR_N4K		64
#define	MTRR_CAP_WC		0x0000000000000400
#define	MTRR_CAP_FIXED		0x0000000000000100
#define	MTRR_CAP_VCNT		0x00000000000000ff
#define	MTRR_DEF_ENABLE		0x0000000000000800
#define	MTRR_DEF_FIXED_ENABLE	0x0000000000000400
#define	MTRR_DEF_TYPE		0x00000000000000ff
#define	MTRR_PHYSBASE_PHYSBASE	0x000ffffffffff000
#define	MTRR_PHYSBASE_TYPE	0x00000000000000ff
#define	MTRR_PHYSMASK_PHYSMASK	0x000ffffffffff000
#define	MTRR_PHYSMASK_VALID	0x0000000000000800

/*
 * Cyrix configuration registers, accessible as IO ports.
 */
#define	CCR0			0xc0	/* Configuration control register 0 */
#define	CCR0_NC0		0x01	/* First 64K of each 1M memory region is
								   non-cacheable */
#define	CCR0_NC1		0x02	/* 640K-1M region is non-cacheable */
#define	CCR0_A20M		0x04	/* Enables A20M# input pin */
#define	CCR0_KEN		0x08	/* Enables KEN# input pin */
#define	CCR0_FLUSH		0x10	/* Enables FLUSH# input pin */
#define	CCR0_BARB		0x20	/* Flushes internal cache when entering hold
								   state */
#define	CCR0_CO			0x40	/* Cache org: 1=direct mapped, 0=2x set
								   assoc */
#define	CCR0_SUSPEND	0x80	/* Enables SUSP# and SUSPA# pins */

#define	CCR1			0xc1	/* Configuration control register 1 */
#define	CCR1_RPL		0x01	/* Enables RPLSET and RPLVAL# pins */
#define	CCR1_SMI		0x02	/* Enables SMM pins */
#define	CCR1_SMAC		0x04	/* System management memory access */
#define	CCR1_MMAC		0x08	/* Main memory access */
#define	CCR1_NO_LOCK	0x10	/* Negate LOCK# */
#define	CCR1_SM3		0x80	/* SMM address space address region 3 */

#define	CCR2			0xc2
#define	CCR2_WB			0x02	/* Enables WB cache interface pins */
#define	CCR2_SADS		0x02	/* Slow ADS */
#define	CCR2_LOCK_NW	0x04	/* LOCK NW Bit */
#define	CCR2_SUSP_HLT	0x08	/* Suspend on HALT */
#define	CCR2_WT1		0x10	/* WT region 1 */
#define	CCR2_WPR1		0x10	/* Write-protect region 1 */
#define	CCR2_BARB		0x20	/* Flushes write-back cache when entering
								   hold state. */
#define	CCR2_BWRT		0x40	/* Enables burst write cycles */
#define	CCR2_USE_SUSP	0x80	/* Enables suspend pins */

#define	CCR3			0xc3
#define	CCR3_SMILOCK	0x01	/* SMM register lock */
#define	CCR3_NMI		0x02	/* Enables NMI during SMM */
#define	CCR3_LINBRST	0x04	/* Linear address burst cycles */
#define	CCR3_SMMMODE	0x08	/* SMM Mode */
#define	CCR3_MAPEN0		0x10	/* Enables Map0 */
#define	CCR3_MAPEN1		0x20	/* Enables Map1 */
#define	CCR3_MAPEN2		0x40	/* Enables Map2 */
#define	CCR3_MAPEN3		0x80	/* Enables Map3 */

#define	CCR4			0xe8
#define	CCR4_IOMASK		0x07
#define	CCR4_MEM		0x08	/* Enables momory bypassing */
#define	CCR4_DTE		0x10	/* Enables directory table entry cache */
#define	CCR4_FASTFPE	0x20	/* Fast FPU exception */
#define	CCR4_CPUID		0x80	/* Enables CPUID instruction */

#define	CCR5			0xe9
#define	CCR5_WT_ALLOC	0x01	/* Write-through allocate */
#define	CCR5_SLOP		0x02	/* LOOP instruction slowed down */
#define	CCR5_LBR1		0x10	/* Local bus region 1 */
#define	CCR5_ARREN		0x20	/* Enables ARR region */

#define	CCR6			0xea

#define	CCR7			0xeb

/* Performance Control Register (5x86 only). */
#define	PCR0			0x20
#define	PCR0_RSTK		0x01	/* Enables return stack */
#define	PCR0_BTB		0x02	/* Enables branch target buffer */
#define	PCR0_LOOP		0x04	/* Enables loop */
#define	PCR0_AIS		0x08	/* Enables all instrcutions stalled to
								   serialize pipe. */
#define	PCR0_MLR		0x10	/* Enables reordering of misaligned loads */
#define	PCR0_BTBRT		0x40	/* Enables BTB test register. */
#define	PCR0_LSSER		0x80	/* Disable reorder */

/* Device Identification Registers */
#define	DIR0			0xfe
#define	DIR1			0xff

/*
 * Machine Check register constants.
 */
#define	MCG_CAP_COUNT		0x000000ff
#define	MCG_CAP_CTL_P		0x00000100
#define	MCG_CAP_EXT_P		0x00000200
#define	MCG_CAP_CMCI_P		0x00000400
#define	MCG_CAP_TES_P		0x00000800
#define	MCG_CAP_EXT_CNT		0x00ff0000
#define	MCG_CAP_SER_P		0x01000000
#define	MCG_STATUS_RIPV		0x00000001
#define	MCG_STATUS_EIPV		0x00000002
#define	MCG_STATUS_MCIP		0x00000004
#define	MCG_CTL_ENABLE		0xffffffffffffffff
#define	MCG_CTL_DISABLE		0x0000000000000000
#define	MSR_MC_CTL(x)		(MSR_MC0_CTL + (x) * 4)
#define	MSR_MC_STATUS(x)	(MSR_MC0_STATUS + (x) * 4)
#define	MSR_MC_ADDR(x)		(MSR_MC0_ADDR + (x) * 4)
#define	MSR_MC_MISC(x)		(MSR_MC0_MISC + (x) * 4)
#define	MSR_MC_CTL2(x)		(MSR_MC0_CTL2 + (x))	/* If MCG_CAP_CMCI_P */
#define	MC_STATUS_MCA_ERROR	0x000000000000ffff
#define	MC_STATUS_MODEL_ERROR	0x00000000ffff0000
#define	MC_STATUS_OTHER_INFO	0x01ffffff00000000
#define	MC_STATUS_COR_COUNT	0x001fffc000000000	/* If MCG_CAP_CMCI_P */
#define	MC_STATUS_TES_STATUS	0x0060000000000000	/* If MCG_CAP_TES_P */
#define	MC_STATUS_AR		0x0080000000000000	/* If MCG_CAP_TES_P */
#define	MC_STATUS_S		0x0100000000000000	/* If MCG_CAP_TES_P */
#define	MC_STATUS_PCC		0x0200000000000000
#define	MC_STATUS_ADDRV		0x0400000000000000
#define	MC_STATUS_MISCV		0x0800000000000000
#define	MC_STATUS_EN		0x1000000000000000
#define	MC_STATUS_UC		0x2000000000000000
#define	MC_STATUS_OVER		0x4000000000000000
#define	MC_STATUS_VAL		0x8000000000000000
#define	MC_MISC_RA_LSB		0x000000000000003f	/* If MCG_CAP_SER_P */
#define	MC_MISC_ADDRESS_MODE	0x00000000000001c0	/* If MCG_CAP_SER_P */
#define	MC_CTL2_THRESHOLD	0x0000000000007fff
#define	MC_CTL2_CMCI_EN		0x0000000040000000
#define	MC_AMDNB_BANK		4
#define	MC_MISC_AMD_VAL		0x8000000000000000	/* Counter presence valid */
#define	MC_MISC_AMD_CNTP	0x4000000000000000	/* Counter present */
#define	MC_MISC_AMD_LOCK	0x2000000000000000	/* Register locked */
#define	MC_MISC_AMD_INTP	0x1000000000000000	/* Int. type can generate interrupts */
#define	MC_MISC_AMD_LVT_MASK	0x00f0000000000000	/* Extended LVT offset */
#define	MC_MISC_AMD_LVT_SHIFT	52
#define	MC_MISC_AMD_CNTEN	0x0008000000000000	/* Counter enabled */
#define	MC_MISC_AMD_INT_MASK	0x0006000000000000	/* Interrupt type */
#define	MC_MISC_AMD_INT_LVT	0x0002000000000000	/* Interrupt via Extended LVT */
#define	MC_MISC_AMD_INT_SMI	0x0004000000000000	/* SMI */
#define	MC_MISC_AMD_OVERFLOW	0x0001000000000000	/* Counter overflow */
#define	MC_MISC_AMD_CNT_MASK	0x00000fff00000000	/* Counter value */
#define	MC_MISC_AMD_CNT_SHIFT	32
#define	MC_MISC_AMD_CNT_MAX	0xfff
#define	MC_MISC_AMD_PTR_MASK	0x00000000ff000000	/* Pointer to additional registers */
#define	MC_MISC_AMD_PTR_SHIFT	24

/*
 * The following four 3-byte registers control the non-cacheable regions.
 * These registers must be written as three separate bytes.
 *
 * NCRx+0: A31-A24 of starting address
 * NCRx+1: A23-A16 of starting address
 * NCRx+2: A15-A12 of starting address | NCR_SIZE_xx.
 *
 * The non-cacheable region's starting address must be aligned to the
 * size indicated by the NCR_SIZE_xx field.
 */
#define	NCR1	0xc4
#define	NCR2	0xc7
#define	NCR3	0xca
#define	NCR4	0xcd

#define	NCR_SIZE_0K	0
#define	NCR_SIZE_4K	1
#define	NCR_SIZE_8K	2
#define	NCR_SIZE_16K	3
#define	NCR_SIZE_32K	4
#define	NCR_SIZE_64K	5
#define	NCR_SIZE_128K	6
#define	NCR_SIZE_256K	7
#define	NCR_SIZE_512K	8
#define	NCR_SIZE_1M	9
#define	NCR_SIZE_2M	10
#define	NCR_SIZE_4M	11
#define	NCR_SIZE_8M	12
#define	NCR_SIZE_16M	13
#define	NCR_SIZE_32M	14
#define	NCR_SIZE_4G	15

/*
 * The address region registers are used to specify the location and
 * size for the eight address regions.
 *
 * ARRx + 0: A31-A24 of start address
 * ARRx + 1: A23-A16 of start address
 * ARRx + 2: A15-A12 of start address | ARR_SIZE_xx
 */
#define	ARR0	0xc4
#define	ARR1	0xc7
#define	ARR2	0xca
#define	ARR3	0xcd
#define	ARR4	0xd0
#define	ARR5	0xd3
#define	ARR6	0xd6
#define	ARR7	0xd9

#define	ARR_SIZE_0K		0
#define	ARR_SIZE_4K		1
#define	ARR_SIZE_8K		2
#define	ARR_SIZE_16K	3
#define	ARR_SIZE_32K	4
#define	ARR_SIZE_64K	5
#define	ARR_SIZE_128K	6
#define	ARR_SIZE_256K	7
#define	ARR_SIZE_512K	8
#define	ARR_SIZE_1M		9
#define	ARR_SIZE_2M		10
#define	ARR_SIZE_4M		11
#define	ARR_SIZE_8M		12
#define	ARR_SIZE_16M	13
#define	ARR_SIZE_32M	14
#define	ARR_SIZE_4G		15

/*
 * The region control registers specify the attributes associated with
 * the ARRx addres regions.
 */
#define	RCR0	0xdc
#define	RCR1	0xdd
#define	RCR2	0xde
#define	RCR3	0xdf
#define	RCR4	0xe0
#define	RCR5	0xe1
#define	RCR6	0xe2
#define	RCR7	0xe3

#define	RCR_RCD	0x01	/* Disables caching for ARRx (x = 0-6). */
#define	RCR_RCE	0x01	/* Enables caching for ARR7. */
#define	RCR_WWO	0x02	/* Weak write ordering. */
#define	RCR_WL	0x04	/* Weak locking. */
#define	RCR_WG	0x08	/* Write gathering. */
#define	RCR_WT	0x10	/* Write-through. */
#define	RCR_NLB	0x20	/* LBA# pin is not asserted. */

/* AMD Write Allocate Top-Of-Memory and Control Register */
#define	AMD_WT_ALLOC_TME	0x40000	/* top-of-memory enable */
#define	AMD_WT_ALLOC_PRE	0x20000	/* programmable range enable */
#define	AMD_WT_ALLOC_FRE	0x10000	/* fixed (A0000-FFFFF) range enable */

/* AMD64 MSR's */
#define	MSR_EFER	0xc0000080	/* extended features */
#define	MSR_STAR	0xc0000081	/* legacy mode SYSCALL target/cs/ss */
#define	MSR_LSTAR	0xc0000082	/* long mode SYSCALL target rip */
#define	MSR_CSTAR	0xc0000083	/* compat mode SYSCALL target rip */
#define	MSR_SF_MASK	0xc0000084	/* syscall flags mask */
#define	MSR_FSBASE	0xc0000100	/* base address of the %fs "segment" */
#define	MSR_GSBASE	0xc0000101	/* base address of the %gs "segment" */
#define	MSR_KGSBASE	0xc0000102	/* base address of the kernel %gs */
#define	MSR_TSC_AUX	0xc0000103
#define	MSR_PERFEVSEL0	0xc0010000
#define	MSR_PERFEVSEL1	0xc0010001
#define	MSR_PERFEVSEL2	0xc0010002
#define	MSR_PERFEVSEL3	0xc0010003
#define	MSR_K7_PERFCTR0	0xc0010004
#define	MSR_K7_PERFCTR1	0xc0010005
#define	MSR_K7_PERFCTR2	0xc0010006
#define	MSR_K7_PERFCTR3	0xc0010007
#define	MSR_SYSCFG	0xc0010010
#define	MSR_HWCR	0xc0010015
#define	MSR_IORRBASE0	0xc0010016
#define	MSR_IORRMASK0	0xc0010017
#define	MSR_IORRBASE1	0xc0010018
#define	MSR_IORRMASK1	0xc0010019
#define	MSR_TOP_MEM	0xc001001a	/* boundary for ram below 4G */
#define	MSR_TOP_MEM2	0xc001001d	/* boundary for ram above 4G */
#define	MSR_NB_CFG1	0xc001001f	/* NB configuration 1 */
#define	MSR_K8_UCODE_UPDATE 0xc0010020	/* update microcode */
#define	MSR_MC0_CTL_MASK 0xc0010044
#define	MSR_P_STATE_LIMIT 0xc0010061	/* P-state Current Limit Register */
#define	MSR_P_STATE_CONTROL 0xc0010062	/* P-state Control Register */
#define	MSR_P_STATE_STATUS 0xc0010063	/* P-state Status Register */
#define	MSR_P_STATE_CONFIG(n) (0xc0010064 + (n)) /* P-state Config */
#define	MSR_SMM_ADDR	0xc0010112	/* SMM TSEG base address */
#define	MSR_SMM_MASK	0xc0010113	/* SMM TSEG address mask */
#define	MSR_VM_CR	0xc0010114	/* SVM: feature control */
#define	MSR_VM_HSAVE_PA 0xc0010117	/* SVM: host save area address */
#define	MSR_AMD_CPUID07	0xc0011002	/* CPUID 07 %ebx override */
#define	MSR_EXTFEATURES	0xc0011005	/* Extended CPUID Features override */
#define	MSR_IC_CFG	0xc0011021	/* Instruction Cache Configuration */

/* MSR_VM_CR related */
#define	VM_CR_SVMDIS		0x10	/* SVM: disabled by BIOS */

/* VIA ACE crypto featureset: for via_feature_rng */
#define	VIA_HAS_RNG		1	/* cpu has RNG */

/* VIA ACE crypto featureset: for via_feature_xcrypt */
#define	VIA_HAS_AES		1	/* cpu has AES */
#define	VIA_HAS_SHA		2	/* cpu has SHA1 & SHA256 */
#define	VIA_HAS_MM		4	/* cpu has RSA instructions */
#define	VIA_HAS_AESCTR		8	/* cpu has AES-CTR instructions */

/* Centaur Extended Feature flags */
#define	VIA_CPUID_HAS_RNG	0x000004
#define	VIA_CPUID_DO_RNG	0x000008
#define	VIA_CPUID_HAS_ACE	0x000040
#define	VIA_CPUID_DO_ACE	0x000080
#define	VIA_CPUID_HAS_ACE2	0x000100
#define	VIA_CPUID_DO_ACE2	0x000200
#define	VIA_CPUID_HAS_PHE	0x000400
#define	VIA_CPUID_DO_PHE	0x000800
#define	VIA_CPUID_HAS_PMM	0x001000
#define	VIA_CPUID_DO_PMM	0x002000

/* VIA ACE xcrypt-* instruction context control options */
#define	VIA_CRYPT_CWLO_ROUND_M		0x0000000f
#define	VIA_CRYPT_CWLO_ALG_M		0x00000070
#define	VIA_CRYPT_CWLO_ALG_AES		0x00000000
#define	VIA_CRYPT_CWLO_KEYGEN_M		0x00000080
#define	VIA_CRYPT_CWLO_KEYGEN_HW	0x00000000
#define	VIA_CRYPT_CWLO_KEYGEN_SW	0x00000080
#define	VIA_CRYPT_CWLO_NORMAL		0x00000000
#define	VIA_CRYPT_CWLO_INTERMEDIATE	0x00000100
#define	VIA_CRYPT_CWLO_ENCRYPT		0x00000000
#define	VIA_CRYPT_CWLO_DECRYPT		0x00000200
#define	VIA_CRYPT_CWLO_KEY128		0x0000000a	/* 128bit, 10 rds */
#define	VIA_CRYPT_CWLO_KEY192		0x0000040c	/* 192bit, 12 rds */
#define	VIA_CRYPT_CWLO_KEY256		0x0000080e	/* 256bit, 15 rds */

#endif /* !_MACHINE_SPECIALREG_H_ */
