/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUFEATURES_H
#define _ASM_X86_CPUFEATURES_H

#ifndef _ASM_X86_REQUIRED_FEATURES_H
#include <asm/required-features.h>
#endif

#ifndef _ASM_X86_DISABLED_FEATURES_H
#include <asm/disabled-features.h>
#endif

/*
 * Defines x86 CPU feature bits
 */
#define NCAPINTS			19	   /* N 32-bit words worth of info */
#define NBUGINTS			1	   /* N 32-bit bug flags */

/*
 * Note: If the comment begins with a quoted string, that string is used
 * in /proc/cpuinfo instead of the macro name.  If the string is "",
 * this feature bit is not displayed in /proc/cpuinfo at all.
 *
 * When adding new features here that depend on other features,
 * please update the table in kernel/cpu/cpuid-deps.c as well.
 */

/* Intel-defined CPU features, CPUID level 0x00000001 (EDX), word 0 */
#define X86_FEATURE_FPU			( 0*32+ 0) /* Onboard FPU */
#define X86_FEATURE_VME			( 0*32+ 1) /* Virtual Mode Extensions */
#define X86_FEATURE_DE			( 0*32+ 2) /* Debugging Extensions */
#define X86_FEATURE_PSE			( 0*32+ 3) /* Page Size Extensions */
#define X86_FEATURE_TSC			( 0*32+ 4) /* Time Stamp Counter */
#define X86_FEATURE_MSR			( 0*32+ 5) /* Model-Specific Registers */
#define X86_FEATURE_PAE			( 0*32+ 6) /* Physical Address Extensions */
#define X86_FEATURE_MCE			( 0*32+ 7) /* Machine Check Exception */
#define X86_FEATURE_CX8			( 0*32+ 8) /* CMPXCHG8 instruction */
#define X86_FEATURE_APIC		( 0*32+ 9) /* Onboard APIC */
#define X86_FEATURE_SEP			( 0*32+11) /* SYSENTER/SYSEXIT */
#define X86_FEATURE_MTRR		( 0*32+12) /* Memory Type Range Registers */
#define X86_FEATURE_PGE			( 0*32+13) /* Page Global Enable */
#define X86_FEATURE_MCA			( 0*32+14) /* Machine Check Architecture */
#define X86_FEATURE_CMOV		( 0*32+15) /* CMOV instructions (plus FCMOVcc, FCOMI with FPU) */
#define X86_FEATURE_PAT			( 0*32+16) /* Page Attribute Table */
#define X86_FEATURE_PSE36		( 0*32+17) /* 36-bit PSEs */
#define X86_FEATURE_PN			( 0*32+18) /* Processor serial number */
#define X86_FEATURE_CLFLUSH		( 0*32+19) /* CLFLUSH instruction */
#define X86_FEATURE_DS			( 0*32+21) /* "dts" Debug Store */
#define X86_FEATURE_ACPI		( 0*32+22) /* ACPI via MSR */
#define X86_FEATURE_MMX			( 0*32+23) /* Multimedia Extensions */
#define X86_FEATURE_FXSR		( 0*32+24) /* FXSAVE/FXRSTOR, CR4.OSFXSR */
#define X86_FEATURE_XMM			( 0*32+25) /* "sse" */
#define X86_FEATURE_XMM2		( 0*32+26) /* "sse2" */
#define X86_FEATURE_SELFSNOOP		( 0*32+27) /* "ss" CPU self snoop */
#define X86_FEATURE_HT			( 0*32+28) /* Hyper-Threading */
#define X86_FEATURE_ACC			( 0*32+29) /* "tm" Automatic clock control */
#define X86_FEATURE_IA64		( 0*32+30) /* IA-64 processor */
#define X86_FEATURE_PBE			( 0*32+31) /* Pending Break Enable */

/* AMD-defined CPU features, CPUID level 0x80000001, word 1 */
/* Don't duplicate feature flags which are redundant with Intel! */
#define X86_FEATURE_SYSCALL		( 1*32+11) /* SYSCALL/SYSRET */
#define X86_FEATURE_MP			( 1*32+19) /* MP Capable */
#define X86_FEATURE_NX			( 1*32+20) /* Execute Disable */
#define X86_FEATURE_MMXEXT		( 1*32+22) /* AMD MMX extensions */
#define X86_FEATURE_FXSR_OPT		( 1*32+25) /* FXSAVE/FXRSTOR optimizations */
#define X86_FEATURE_GBPAGES		( 1*32+26) /* "pdpe1gb" GB pages */
#define X86_FEATURE_RDTSCP		( 1*32+27) /* RDTSCP */
#define X86_FEATURE_LM			( 1*32+29) /* Long Mode (x86-64, 64-bit support) */
#define X86_FEATURE_3DNOWEXT		( 1*32+30) /* AMD 3DNow extensions */
#define X86_FEATURE_3DNOW		( 1*32+31) /* 3DNow */

/* Transmeta-defined CPU features, CPUID level 0x80860001, word 2 */
#define X86_FEATURE_RECOVERY		( 2*32+ 0) /* CPU in recovery mode */
#define X86_FEATURE_LONGRUN		( 2*32+ 1) /* Longrun power control */
#define X86_FEATURE_LRTI		( 2*32+ 3) /* LongRun table interface */

/* Other features, Linux-defined mapping, word 3 */
/* This range is used for feature bits which conflict or are synthesized */
#define X86_FEATURE_CXMMX		( 3*32+ 0) /* Cyrix MMX extensions */
#define X86_FEATURE_K6_MTRR		( 3*32+ 1) /* AMD K6 nonstandard MTRRs */
#define X86_FEATURE_CYRIX_ARR		( 3*32+ 2) /* Cyrix ARRs (= MTRRs) */
#define X86_FEATURE_CENTAUR_MCR		( 3*32+ 3) /* Centaur MCRs (= MTRRs) */

/* CPU types for specific tunings: */
#define X86_FEATURE_K8			( 3*32+ 4) /* "" Opteron, Athlon64 */
#define X86_FEATURE_K7			( 3*32+ 5) /* "" Athlon */
#define X86_FEATURE_P3			( 3*32+ 6) /* "" P3 */
#define X86_FEATURE_P4			( 3*32+ 7) /* "" P4 */
#define X86_FEATURE_CONSTANT_TSC	( 3*32+ 8) /* TSC ticks at a constant rate */
#define X86_FEATURE_UP			( 3*32+ 9) /* SMP kernel running on UP */
#define X86_FEATURE_ART			( 3*32+10) /* Always running timer (ART) */
#define X86_FEATURE_ARCH_PERFMON	( 3*32+11) /* Intel Architectural PerfMon */
#define X86_FEATURE_PEBS		( 3*32+12) /* Precise-Event Based Sampling */
#define X86_FEATURE_BTS			( 3*32+13) /* Branch Trace Store */
#define X86_FEATURE_SYSCALL32		( 3*32+14) /* "" syscall in IA32 userspace */
#define X86_FEATURE_SYSENTER32		( 3*32+15) /* "" sysenter in IA32 userspace */
#define X86_FEATURE_REP_GOOD		( 3*32+16) /* REP microcode works well */
#define X86_FEATURE_SME_COHERENT	( 3*32+17) /* "" AMD hardware-enforced cache coherency */
#define X86_FEATURE_LFENCE_RDTSC	( 3*32+18) /* "" LFENCE synchronizes RDTSC */
#define X86_FEATURE_ACC_POWER		( 3*32+19) /* AMD Accumulated Power Mechanism */
#define X86_FEATURE_NOPL		( 3*32+20) /* The NOPL (0F 1F) instructions */
#define X86_FEATURE_ALWAYS		( 3*32+21) /* "" Always-present feature */
#define X86_FEATURE_XTOPOLOGY		( 3*32+22) /* CPU topology enum extensions */
#define X86_FEATURE_TSC_RELIABLE	( 3*32+23) /* TSC is known to be reliable */
#define X86_FEATURE_NONSTOP_TSC		( 3*32+24) /* TSC does not stop in C states */
#define X86_FEATURE_CPUID		( 3*32+25) /* CPU has CPUID instruction itself */
#define X86_FEATURE_EXTD_APICID		( 3*32+26) /* Extended APICID (8 bits) */
#define X86_FEATURE_AMD_DCM		( 3*32+27) /* AMD multi-node processor */
#define X86_FEATURE_APERFMPERF		( 3*32+28) /* P-State hardware coordination feedback capability (APERF/MPERF MSRs) */
/* free					( 3*32+29) */
#define X86_FEATURE_NONSTOP_TSC_S3	( 3*32+30) /* TSC doesn't stop in S3 state */
#define X86_FEATURE_TSC_KNOWN_FREQ	( 3*32+31) /* TSC has known frequency */

/* Intel-defined CPU features, CPUID level 0x00000001 (ECX), word 4 */
#define X86_FEATURE_XMM3		( 4*32+ 0) /* "pni" SSE-3 */
#define X86_FEATURE_PCLMULQDQ		( 4*32+ 1) /* PCLMULQDQ instruction */
#define X86_FEATURE_DTES64		( 4*32+ 2) /* 64-bit Debug Store */
#define X86_FEATURE_MWAIT		( 4*32+ 3) /* "monitor" MONITOR/MWAIT support */
#define X86_FEATURE_DSCPL		( 4*32+ 4) /* "ds_cpl" CPL-qualified (filtered) Debug Store */
#define X86_FEATURE_VMX			( 4*32+ 5) /* Hardware virtualization */
#define X86_FEATURE_SMX			( 4*32+ 6) /* Safer Mode eXtensions */
#define X86_FEATURE_EST			( 4*32+ 7) /* Enhanced SpeedStep */
#define X86_FEATURE_TM2			( 4*32+ 8) /* Thermal Monitor 2 */
#define X86_FEATURE_SSSE3		( 4*32+ 9) /* Supplemental SSE-3 */
#define X86_FEATURE_CID			( 4*32+10) /* Context ID */
#define X86_FEATURE_SDBG		( 4*32+11) /* Silicon Debug */
#define X86_FEATURE_FMA			( 4*32+12) /* Fused multiply-add */
#define X86_FEATURE_CX16		( 4*32+13) /* CMPXCHG16B instruction */
#define X86_FEATURE_XTPR		( 4*32+14) /* Send Task Priority Messages */
#define X86_FEATURE_PDCM		( 4*32+15) /* Perf/Debug Capabilities MSR */
#define X86_FEATURE_PCID		( 4*32+17) /* Process Context Identifiers */
#define X86_FEATURE_DCA			( 4*32+18) /* Direct Cache Access */
#define X86_FEATURE_XMM4_1		( 4*32+19) /* "sse4_1" SSE-4.1 */
#define X86_FEATURE_XMM4_2		( 4*32+20) /* "sse4_2" SSE-4.2 */
#define X86_FEATURE_X2APIC		( 4*32+21) /* X2APIC */
#define X86_FEATURE_MOVBE		( 4*32+22) /* MOVBE instruction */
#define X86_FEATURE_POPCNT		( 4*32+23) /* POPCNT instruction */
#define X86_FEATURE_TSC_DEADLINE_TIMER	( 4*32+24) /* TSC deadline timer */
#define X86_FEATURE_AES			( 4*32+25) /* AES instructions */
#define X86_FEATURE_XSAVE		( 4*32+26) /* XSAVE/XRSTOR/XSETBV/XGETBV instructions */
#define X86_FEATURE_OSXSAVE		( 4*32+27) /* "" XSAVE instruction enabled in the OS */
#define X86_FEATURE_AVX			( 4*32+28) /* Advanced Vector Extensions */
#define X86_FEATURE_F16C		( 4*32+29) /* 16-bit FP conversions */
#define X86_FEATURE_RDRAND		( 4*32+30) /* RDRAND instruction */
#define X86_FEATURE_HYPERVISOR		( 4*32+31) /* Running on a hypervisor */

/* VIA/Cyrix/Centaur-defined CPU features, CPUID level 0xC0000001, word 5 */
#define X86_FEATURE_XSTORE		( 5*32+ 2) /* "rng" RNG present (xstore) */
#define X86_FEATURE_XSTORE_EN		( 5*32+ 3) /* "rng_en" RNG enabled */
#define X86_FEATURE_XCRYPT		( 5*32+ 6) /* "ace" on-CPU crypto (xcrypt) */
#define X86_FEATURE_XCRYPT_EN		( 5*32+ 7) /* "ace_en" on-CPU crypto enabled */
#define X86_FEATURE_ACE2		( 5*32+ 8) /* Advanced Cryptography Engine v2 */
#define X86_FEATURE_ACE2_EN		( 5*32+ 9) /* ACE v2 enabled */
#define X86_FEATURE_PHE			( 5*32+10) /* PadLock Hash Engine */
#define X86_FEATURE_PHE_EN		( 5*32+11) /* PHE enabled */
#define X86_FEATURE_PMM			( 5*32+12) /* PadLock Montgomery Multiplier */
#define X86_FEATURE_PMM_EN		( 5*32+13) /* PMM enabled */

/* More extended AMD flags: CPUID level 0x80000001, ECX, word 6 */
#define X86_FEATURE_LAHF_LM		( 6*32+ 0) /* LAHF/SAHF in long mode */
#define X86_FEATURE_CMP_LEGACY		( 6*32+ 1) /* If yes HyperThreading not valid */
#define X86_FEATURE_SVM			( 6*32+ 2) /* Secure Virtual Machine */
#define X86_FEATURE_EXTAPIC		( 6*32+ 3) /* Extended APIC space */
#define X86_FEATURE_CR8_LEGACY		( 6*32+ 4) /* CR8 in 32-bit mode */
#define X86_FEATURE_ABM			( 6*32+ 5) /* Advanced bit manipulation */
#define X86_FEATURE_SSE4A		( 6*32+ 6) /* SSE-4A */
#define X86_FEATURE_MISALIGNSSE		( 6*32+ 7) /* Misaligned SSE mode */
#define X86_FEATURE_3DNOWPREFETCH	( 6*32+ 8) /* 3DNow prefetch instructions */
#define X86_FEATURE_OSVW		( 6*32+ 9) /* OS Visible Workaround */
#define X86_FEATURE_IBS			( 6*32+10) /* Instruction Based Sampling */
#define X86_FEATURE_XOP			( 6*32+11) /* extended AVX instructions */
#define X86_FEATURE_SKINIT		( 6*32+12) /* SKINIT/STGI instructions */
#define X86_FEATURE_WDT			( 6*32+13) /* Watchdog timer */
#define X86_FEATURE_LWP			( 6*32+15) /* Light Weight Profiling */
#define X86_FEATURE_FMA4		( 6*32+16) /* 4 operands MAC instructions */
#define X86_FEATURE_TCE			( 6*32+17) /* Translation Cache Extension */
#define X86_FEATURE_NODEID_MSR		( 6*32+19) /* NodeId MSR */
#define X86_FEATURE_TBM			( 6*32+21) /* Trailing Bit Manipulations */
#define X86_FEATURE_TOPOEXT		( 6*32+22) /* Topology extensions CPUID leafs */
#define X86_FEATURE_PERFCTR_CORE	( 6*32+23) /* Core performance counter extensions */
#define X86_FEATURE_PERFCTR_NB		( 6*32+24) /* NB performance counter extensions */
#define X86_FEATURE_BPEXT		( 6*32+26) /* Data breakpoint extension */
#define X86_FEATURE_PTSC		( 6*32+27) /* Performance time-stamp counter */
#define X86_FEATURE_PERFCTR_LLC		( 6*32+28) /* Last Level Cache performance counter extensions */
#define X86_FEATURE_MWAITX		( 6*32+29) /* MWAIT extension (MONITORX/MWAITX instructions) */

/*
 * Auxiliary flags: Linux defined - For features scattered in various
 * CPUID levels like 0x6, 0xA etc, word 7.
 *
 * Reuse free bits when adding new feature flags!
 */
#define X86_FEATURE_RING3MWAIT		( 7*32+ 0) /* Ring 3 MONITOR/MWAIT instructions */
#define X86_FEATURE_CPUID_FAULT		( 7*32+ 1) /* Intel CPUID faulting */
#define X86_FEATURE_CPB			( 7*32+ 2) /* AMD Core Performance Boost */
#define X86_FEATURE_EPB			( 7*32+ 3) /* IA32_ENERGY_PERF_BIAS support */
#define X86_FEATURE_CAT_L3		( 7*32+ 4) /* Cache Allocation Technology L3 */
#define X86_FEATURE_CAT_L2		( 7*32+ 5) /* Cache Allocation Technology L2 */
#define X86_FEATURE_CDP_L3		( 7*32+ 6) /* Code and Data Prioritization L3 */
#define X86_FEATURE_INVPCID_SINGLE	( 7*32+ 7) /* Effectively INVPCID && CR4.PCIDE=1 */
#define X86_FEATURE_HW_PSTATE		( 7*32+ 8) /* AMD HW-PState */
#define X86_FEATURE_PROC_FEEDBACK	( 7*32+ 9) /* AMD ProcFeedbackInterface */
#define X86_FEATURE_SME			( 7*32+10) /* AMD Secure Memory Encryption */
#define X86_FEATURE_PTI			( 7*32+11) /* Kernel Page Table Isolation enabled */
#define X86_FEATURE_RETPOLINE		( 7*32+12) /* "" Generic Retpoline mitigation for Spectre variant 2 */
#define X86_FEATURE_RETPOLINE_AMD	( 7*32+13) /* "" AMD Retpoline mitigation for Spectre variant 2 */
#define X86_FEATURE_INTEL_PPIN		( 7*32+14) /* Intel Processor Inventory Number */
#define X86_FEATURE_CDP_L2		( 7*32+15) /* Code and Data Prioritization L2 */
#define X86_FEATURE_MSR_SPEC_CTRL	( 7*32+16) /* "" MSR SPEC_CTRL is implemented */
#define X86_FEATURE_SSBD		( 7*32+17) /* Speculative Store Bypass Disable */
#define X86_FEATURE_MBA			( 7*32+18) /* Memory Bandwidth Allocation */
#define X86_FEATURE_RSB_CTXSW		( 7*32+19) /* "" Fill RSB on context switches */
#define X86_FEATURE_SEV			( 7*32+20) /* AMD Secure Encrypted Virtualization */
#define X86_FEATURE_USE_IBPB		( 7*32+21) /* "" Indirect Branch Prediction Barrier enabled */
#define X86_FEATURE_USE_IBRS_FW		( 7*32+22) /* "" Use IBRS during runtime firmware calls */
#define X86_FEATURE_SPEC_STORE_BYPASS_DISABLE	( 7*32+23) /* "" Disable Speculative Store Bypass. */
#define X86_FEATURE_LS_CFG_SSBD		( 7*32+24)  /* "" AMD SSBD implementation via LS_CFG MSR */
#define X86_FEATURE_IBRS		( 7*32+25) /* Indirect Branch Restricted Speculation */
#define X86_FEATURE_IBPB		( 7*32+26) /* Indirect Branch Prediction Barrier */
#define X86_FEATURE_STIBP		( 7*32+27) /* Single Thread Indirect Branch Predictors */
#define X86_FEATURE_ZEN			( 7*32+28) /* "" CPU is AMD family 0x17 or above (Zen) */
#define X86_FEATURE_L1TF_PTEINV		( 7*32+29) /* "" L1TF workaround PTE inversion */
#define X86_FEATURE_IBRS_ENHANCED	( 7*32+30) /* Enhanced IBRS */
#define X86_FEATURE_MSR_IA32_FEAT_CTL	( 7*32+31) /* "" MSR IA32_FEAT_CTL configured */

/* Virtualization flags: Linux defined, word 8 */
#define X86_FEATURE_TPR_SHADOW		( 8*32+ 0) /* Intel TPR Shadow */
#define X86_FEATURE_VNMI		( 8*32+ 1) /* Intel Virtual NMI */
#define X86_FEATURE_FLEXPRIORITY	( 8*32+ 2) /* Intel FlexPriority */
#define X86_FEATURE_EPT			( 8*32+ 3) /* Intel Extended Page Table */
#define X86_FEATURE_VPID		( 8*32+ 4) /* Intel Virtual Processor ID */

#define X86_FEATURE_VMMCALL		( 8*32+15) /* Prefer VMMCALL to VMCALL */
#define X86_FEATURE_XENPV		( 8*32+16) /* "" Xen paravirtual guest */
#define X86_FEATURE_EPT_AD		( 8*32+17) /* Intel Extended Page Table access-dirty bit */
#define X86_FEATURE_VMCALL		( 8*32+18) /* "" Hypervisor supports the VMCALL instruction */
#define X86_FEATURE_VMW_VMMCALL		( 8*32+19) /* "" VMware prefers VMMCALL hypercall instruction */
#define X86_FEATURE_SEV_ES		( 8*32+20) /* AMD Secure Encrypted Virtualization - Encrypted State */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (EBX), word 9 */
#define X86_FEATURE_FSGSBASE		( 9*32+ 0) /* RDFSBASE, WRFSBASE, RDGSBASE, WRGSBASE instructions*/
#define X86_FEATURE_TSC_ADJUST		( 9*32+ 1) /* TSC adjustment MSR 0x3B */
#define X86_FEATURE_BMI1		( 9*32+ 3) /* 1st group bit manipulation extensions */
#define X86_FEATURE_HLE			( 9*32+ 4) /* Hardware Lock Elision */
#define X86_FEATURE_AVX2		( 9*32+ 5) /* AVX2 instructions */
#define X86_FEATURE_FDP_EXCPTN_ONLY	( 9*32+ 6) /* "" FPU data pointer updated only on x87 exceptions */
#define X86_FEATURE_SMEP		( 9*32+ 7) /* Supervisor Mode Execution Protection */
#define X86_FEATURE_BMI2		( 9*32+ 8) /* 2nd group bit manipulation extensions */
#define X86_FEATURE_ERMS		( 9*32+ 9) /* Enhanced REP MOVSB/STOSB instructions */
#define X86_FEATURE_INVPCID		( 9*32+10) /* Invalidate Processor Context ID */
#define X86_FEATURE_RTM			( 9*32+11) /* Restricted Transactional Memory */
#define X86_FEATURE_CQM			( 9*32+12) /* Cache QoS Monitoring */
#define X86_FEATURE_ZERO_FCS_FDS	( 9*32+13) /* "" Zero out FPU CS and FPU DS */
#define X86_FEATURE_MPX			( 9*32+14) /* Memory Protection Extension */
#define X86_FEATURE_RDT_A		( 9*32+15) /* Resource Director Technology Allocation */
#define X86_FEATURE_AVX512F		( 9*32+16) /* AVX-512 Foundation */
#define X86_FEATURE_AVX512DQ		( 9*32+17) /* AVX-512 DQ (Double/Quad granular) Instructions */
#define X86_FEATURE_RDSEED		( 9*32+18) /* RDSEED instruction */
#define X86_FEATURE_ADX			( 9*32+19) /* ADCX and ADOX instructions */
#define X86_FEATURE_SMAP		( 9*32+20) /* Supervisor Mode Access Prevention */
#define X86_FEATURE_AVX512IFMA		( 9*32+21) /* AVX-512 Integer Fused Multiply-Add instructions */
#define X86_FEATURE_CLFLUSHOPT		( 9*32+23) /* CLFLUSHOPT instruction */
#define X86_FEATURE_CLWB		( 9*32+24) /* CLWB instruction */
#define X86_FEATURE_INTEL_PT		( 9*32+25) /* Intel Processor Trace */
#define X86_FEATURE_AVX512PF		( 9*32+26) /* AVX-512 Prefetch */
#define X86_FEATURE_AVX512ER		( 9*32+27) /* AVX-512 Exponential and Reciprocal */
#define X86_FEATURE_AVX512CD		( 9*32+28) /* AVX-512 Conflict Detection */
#define X86_FEATURE_SHA_NI		( 9*32+29) /* SHA1/SHA256 Instruction Extensions */
#define X86_FEATURE_AVX512BW		( 9*32+30) /* AVX-512 BW (Byte/Word granular) Instructions */
#define X86_FEATURE_AVX512VL		( 9*32+31) /* AVX-512 VL (128/256 Vector Length) Extensions */

/* Extended state features, CPUID level 0x0000000d:1 (EAX), word 10 */
#define X86_FEATURE_XSAVEOPT		(10*32+ 0) /* XSAVEOPT instruction */
#define X86_FEATURE_XSAVEC		(10*32+ 1) /* XSAVEC instruction */
#define X86_FEATURE_XGETBV1		(10*32+ 2) /* XGETBV with ECX = 1 instruction */
#define X86_FEATURE_XSAVES		(10*32+ 3) /* XSAVES/XRSTORS instructions */

/*
 * Extended auxiliary flags: Linux defined - for features scattered in various
 * CPUID levels like 0xf, etc.
 *
 * Reuse free bits when adding new feature flags!
 */
#define X86_FEATURE_CQM_LLC		(11*32+ 0) /* LLC QoS if 1 */
#define X86_FEATURE_CQM_OCCUP_LLC	(11*32+ 1) /* LLC occupancy monitoring */
#define X86_FEATURE_CQM_MBM_TOTAL	(11*32+ 2) /* LLC Total MBM monitoring */
#define X86_FEATURE_CQM_MBM_LOCAL	(11*32+ 3) /* LLC Local MBM monitoring */
#define X86_FEATURE_FENCE_SWAPGS_USER	(11*32+ 4) /* "" LFENCE in user entry SWAPGS path */
#define X86_FEATURE_FENCE_SWAPGS_KERNEL	(11*32+ 5) /* "" LFENCE in kernel entry SWAPGS path */
#define X86_FEATURE_SPLIT_LOCK_DETECT	(11*32+ 6) /* #AC for split lock */
#define X86_FEATURE_PER_THREAD_MBA	(11*32+ 7) /* "" Per-thread Memory Bandwidth Allocation */

/* Intel-defined CPU features, CPUID level 0x00000007:1 (EAX), word 12 */
#define X86_FEATURE_AVX512_BF16		(12*32+ 5) /* AVX512 BFLOAT16 instructions */

/* AMD-defined CPU features, CPUID level 0x80000008 (EBX), word 13 */
#define X86_FEATURE_CLZERO		(13*32+ 0) /* CLZERO instruction */
#define X86_FEATURE_IRPERF		(13*32+ 1) /* Instructions Retired Count */
#define X86_FEATURE_XSAVEERPTR		(13*32+ 2) /* Always save/restore FP error pointers */
#define X86_FEATURE_RDPRU		(13*32+ 4) /* Read processor register at user level */
#define X86_FEATURE_WBNOINVD		(13*32+ 9) /* WBNOINVD instruction */
#define X86_FEATURE_AMD_IBPB		(13*32+12) /* "" Indirect Branch Prediction Barrier */
#define X86_FEATURE_AMD_IBRS		(13*32+14) /* "" Indirect Branch Restricted Speculation */
#define X86_FEATURE_AMD_STIBP		(13*32+15) /* "" Single Thread Indirect Branch Predictors */
#define X86_FEATURE_AMD_STIBP_ALWAYS_ON	(13*32+17) /* "" Single Thread Indirect Branch Predictors always-on preferred */
#define X86_FEATURE_AMD_PPIN		(13*32+23) /* Protected Processor Inventory Number */
#define X86_FEATURE_AMD_SSBD		(13*32+24) /* "" Speculative Store Bypass Disable */
#define X86_FEATURE_VIRT_SSBD		(13*32+25) /* Virtualized Speculative Store Bypass Disable */
#define X86_FEATURE_AMD_SSB_NO		(13*32+26) /* "" Speculative Store Bypass is fixed in hardware. */

/* Thermal and Power Management Leaf, CPUID level 0x00000006 (EAX), word 14 */
#define X86_FEATURE_DTHERM		(14*32+ 0) /* Digital Thermal Sensor */
#define X86_FEATURE_IDA			(14*32+ 1) /* Intel Dynamic Acceleration */
#define X86_FEATURE_ARAT		(14*32+ 2) /* Always Running APIC Timer */
#define X86_FEATURE_PLN			(14*32+ 4) /* Intel Power Limit Notification */
#define X86_FEATURE_PTS			(14*32+ 6) /* Intel Package Thermal Status */
#define X86_FEATURE_HWP			(14*32+ 7) /* Intel Hardware P-states */
#define X86_FEATURE_HWP_NOTIFY		(14*32+ 8) /* HWP Notification */
#define X86_FEATURE_HWP_ACT_WINDOW	(14*32+ 9) /* HWP Activity Window */
#define X86_FEATURE_HWP_EPP		(14*32+10) /* HWP Energy Perf. Preference */
#define X86_FEATURE_HWP_PKG_REQ		(14*32+11) /* HWP Package Level Request */

/* AMD SVM Feature Identification, CPUID level 0x8000000a (EDX), word 15 */
#define X86_FEATURE_NPT			(15*32+ 0) /* Nested Page Table support */
#define X86_FEATURE_LBRV		(15*32+ 1) /* LBR Virtualization support */
#define X86_FEATURE_SVML		(15*32+ 2) /* "svm_lock" SVM locking MSR */
#define X86_FEATURE_NRIPS		(15*32+ 3) /* "nrip_save" SVM next_rip save */
#define X86_FEATURE_TSCRATEMSR		(15*32+ 4) /* "tsc_scale" TSC scaling support */
#define X86_FEATURE_VMCBCLEAN		(15*32+ 5) /* "vmcb_clean" VMCB clean bits support */
#define X86_FEATURE_FLUSHBYASID		(15*32+ 6) /* flush-by-ASID support */
#define X86_FEATURE_DECODEASSISTS	(15*32+ 7) /* Decode Assists support */
#define X86_FEATURE_PAUSEFILTER		(15*32+10) /* filtered pause intercept */
#define X86_FEATURE_PFTHRESHOLD		(15*32+12) /* pause filter threshold */
#define X86_FEATURE_AVIC		(15*32+13) /* Virtual Interrupt Controller */
#define X86_FEATURE_V_VMSAVE_VMLOAD	(15*32+15) /* Virtual VMSAVE VMLOAD */
#define X86_FEATURE_VGIF		(15*32+16) /* Virtual GIF */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ECX), word 16 */
#define X86_FEATURE_AVX512VBMI		(16*32+ 1) /* AVX512 Vector Bit Manipulation instructions*/
#define X86_FEATURE_UMIP		(16*32+ 2) /* User Mode Instruction Protection */
#define X86_FEATURE_PKU			(16*32+ 3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE		(16*32+ 4) /* OS Protection Keys Enable */
#define X86_FEATURE_WAITPKG		(16*32+ 5) /* UMONITOR/UMWAIT/TPAUSE Instructions */
#define X86_FEATURE_AVX512_VBMI2	(16*32+ 6) /* Additional AVX512 Vector Bit Manipulation Instructions */
#define X86_FEATURE_GFNI		(16*32+ 8) /* Galois Field New Instructions */
#define X86_FEATURE_VAES		(16*32+ 9) /* Vector AES */
#define X86_FEATURE_VPCLMULQDQ		(16*32+10) /* Carry-Less Multiplication Double Quadword */
#define X86_FEATURE_AVX512_VNNI		(16*32+11) /* Vector Neural Network Instructions */
#define X86_FEATURE_AVX512_BITALG	(16*32+12) /* Support for VPOPCNT[B,W] and VPSHUF-BITQMB instructions */
#define X86_FEATURE_TME			(16*32+13) /* Intel Total Memory Encryption */
#define X86_FEATURE_AVX512_VPOPCNTDQ	(16*32+14) /* POPCNT for vectors of DW/QW */
#define X86_FEATURE_LA57		(16*32+16) /* 5-level page tables */
#define X86_FEATURE_RDPID		(16*32+22) /* RDPID instruction */
#define X86_FEATURE_CLDEMOTE		(16*32+25) /* CLDEMOTE instruction */
#define X86_FEATURE_MOVDIRI		(16*32+27) /* MOVDIRI instruction */
#define X86_FEATURE_MOVDIR64B		(16*32+28) /* MOVDIR64B instruction */
#define X86_FEATURE_ENQCMD		(16*32+29) /* ENQCMD and ENQCMDS instructions */

/* AMD-defined CPU features, CPUID level 0x80000007 (EBX), word 17 */
#define X86_FEATURE_OVERFLOW_RECOV	(17*32+ 0) /* MCA overflow recovery support */
#define X86_FEATURE_SUCCOR		(17*32+ 1) /* Uncorrectable error containment and recovery */
#define X86_FEATURE_SMCA		(17*32+ 3) /* Scalable MCA */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (EDX), word 18 */
#define X86_FEATURE_AVX512_4VNNIW	(18*32+ 2) /* AVX-512 Neural Network Instructions */
#define X86_FEATURE_AVX512_4FMAPS	(18*32+ 3) /* AVX-512 Multiply Accumulation Single precision */
#define X86_FEATURE_FSRM		(18*32+ 4) /* Fast Short Rep Mov */
#define X86_FEATURE_AVX512_VP2INTERSECT (18*32+ 8) /* AVX-512 Intersect for D/Q */
#define X86_FEATURE_SRBDS_CTRL		(18*32+ 9) /* "" SRBDS mitigation MSR available */
#define X86_FEATURE_MD_CLEAR		(18*32+10) /* VERW clears CPU buffers */
#define X86_FEATURE_TSX_FORCE_ABORT	(18*32+13) /* "" TSX_FORCE_ABORT */
#define X86_FEATURE_SERIALIZE		(18*32+14) /* SERIALIZE instruction */
#define X86_FEATURE_TSXLDTRK		(18*32+16) /* TSX Suspend Load Address Tracking */
#define X86_FEATURE_PCONFIG		(18*32+18) /* Intel PCONFIG */
#define X86_FEATURE_ARCH_LBR		(18*32+19) /* Intel ARCH LBR */
#define X86_FEATURE_SPEC_CTRL		(18*32+26) /* "" Speculation Control (IBRS + IBPB) */
#define X86_FEATURE_INTEL_STIBP		(18*32+27) /* "" Single Thread Indirect Branch Predictors */
#define X86_FEATURE_FLUSH_L1D		(18*32+28) /* Flush L1D cache */
#define X86_FEATURE_ARCH_CAPABILITIES	(18*32+29) /* IA32_ARCH_CAPABILITIES MSR (Intel) */
#define X86_FEATURE_CORE_CAPABILITIES	(18*32+30) /* "" IA32_CORE_CAPABILITIES MSR */
#define X86_FEATURE_SPEC_CTRL_SSBD	(18*32+31) /* "" Speculative Store Bypass Disable */

/*
 * BUG word(s)
 */
#define X86_BUG(x)			(NCAPINTS*32 + (x))

#define X86_BUG_F00F			X86_BUG(0) /* Intel F00F */
#define X86_BUG_FDIV			X86_BUG(1) /* FPU FDIV */
#define X86_BUG_COMA			X86_BUG(2) /* Cyrix 6x86 coma */
#define X86_BUG_AMD_TLB_MMATCH		X86_BUG(3) /* "tlb_mmatch" AMD Erratum 383 */
#define X86_BUG_AMD_APIC_C1E		X86_BUG(4) /* "apic_c1e" AMD Erratum 400 */
#define X86_BUG_11AP			X86_BUG(5) /* Bad local APIC aka 11AP */
#define X86_BUG_FXSAVE_LEAK		X86_BUG(6) /* FXSAVE leaks FOP/FIP/FOP */
#define X86_BUG_CLFLUSH_MONITOR		X86_BUG(7) /* AAI65, CLFLUSH required before MONITOR */
#define X86_BUG_SYSRET_SS_ATTRS		X86_BUG(8) /* SYSRET doesn't fix up SS attrs */
#ifdef CONFIG_X86_32
/*
 * 64-bit kernels don't use X86_BUG_ESPFIX.  Make the define conditional
 * to avoid confusion.
 */
#define X86_BUG_ESPFIX			X86_BUG(9) /* "" IRET to 16-bit SS corrupts ESP/RSP high bits */
#endif
#define X86_BUG_NULL_SEG		X86_BUG(10) /* Nulling a selector preserves the base */
#define X86_BUG_SWAPGS_FENCE		X86_BUG(11) /* SWAPGS without input dep on GS */
#define X86_BUG_MONITOR			X86_BUG(12) /* IPI required to wake up remote CPU */
#define X86_BUG_AMD_E400		X86_BUG(13) /* CPU is among the affected by Erratum 400 */
#define X86_BUG_CPU_MELTDOWN		X86_BUG(14) /* CPU is affected by meltdown attack and needs kernel page table isolation */
#define X86_BUG_SPECTRE_V1		X86_BUG(15) /* CPU is affected by Spectre variant 1 attack with conditional branches */
#define X86_BUG_SPECTRE_V2		X86_BUG(16) /* CPU is affected by Spectre variant 2 attack with indirect branches */
#define X86_BUG_SPEC_STORE_BYPASS	X86_BUG(17) /* CPU is affected by speculative store bypass attack */
#define X86_BUG_L1TF			X86_BUG(18) /* CPU is affected by L1 Terminal Fault */
#define X86_BUG_MDS			X86_BUG(19) /* CPU is affected by Microarchitectural data sampling */
#define X86_BUG_MSBDS_ONLY		X86_BUG(20) /* CPU is only affected by the  MSDBS variant of BUG_MDS */
#define X86_BUG_SWAPGS			X86_BUG(21) /* CPU is affected by speculation through SWAPGS */
#define X86_BUG_TAA			X86_BUG(22) /* CPU is affected by TSX Async Abort(TAA) */
#define X86_BUG_ITLB_MULTIHIT		X86_BUG(23) /* CPU may incur MCE during certain page attribute changes */
#define X86_BUG_SRBDS			X86_BUG(24) /* CPU may leak RNG bits if not mitigated */

#endif /* _ASM_X86_CPUFEATURES_H */
