/*	$OpenBSD: specialreg.h,v 1.85 2023/08/16 04:07:38 jsg Exp $	*/
/*	$NetBSD: specialreg.h,v 1.7 1994/10/27 04:16:26 cgd Exp $	*/

/*-
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
 *	@(#)specialreg.h	7.1 (Berkeley) 5/9/91
 */

/*
 * Bits in 386 special registers:
 */
#define	CR0_PE	0x00000001	/* Protected mode Enable */
#define	CR0_MP	0x00000002	/* "Math" Present (NPX or NPX emulator) */
#define	CR0_EM	0x00000004	/* EMulate non-NPX coproc. (trap ESC only) */
#define	CR0_TS	0x00000008	/* Task Switched (if MP, trap ESC and WAIT) */
#define	CR0_ET	0x00000010	/* Extension Type (387 (if set) vs 287) */
#define	CR0_PG	0x80000000	/* PaGing enable */

/*
 * Bits in 486 special registers:
 */
#define CR0_NE	0x00000020	/* Numeric Error enable (EX16 vs IRQ13) */
#define CR0_WP	0x00010000	/* Write Protect (honor PG_RW in all modes) */
#define CR0_AM	0x00040000	/* Alignment Mask (set to enable AC flag) */
#define	CR0_NW	0x20000000	/* Not Write-through */
#define	CR0_CD	0x40000000	/* Cache Disable */

/*
 * bits in CR3
 */
#define CR3_PWT		(1ULL << 3)
#define CR3_PCD		(1ULL << 4)

/*
 * bits in the pentiums %cr4 register:
 */

#define	CR4_VME	0x00000001	/* virtual 8086 mode extension enable */
#define	CR4_PVI 0x00000002	/* protected mode virtual interrupt enable */
#define	CR4_TSD 0x00000004	/* restrict RDTSC instruction to cpl 0 only */
#define	CR4_DE	0x00000008	/* debugging extension */
#define	CR4_PSE	0x00000010	/* large (4MB) page size enable */
#define	CR4_PAE 0x00000020	/* physical address extension enable */
#define	CR4_MCE	0x00000040	/* machine check enable */
#define	CR4_PGE	0x00000080	/* page global enable */
#define	CR4_PCE	0x00000100	/* enable RDPMC instruction for all cpls */
#define	CR4_OSFXSR	0x00000200	/* enable fxsave/fxrestor and SSE */
#define	CR4_OSXMMEXCPT	0x00000400	/* enable unmasked SSE exceptions */
#define	CR4_UMIP	0x00000800	/* user mode instruction prevention */
#define	CR4_VMXE	0x00002000	/* enable virtual machine operation */
#define	CR4_SMXE	0x00004000	/* enable safe mode operation */
#define	CR4_FSGSBASE	0x00010000	/* enable {RD,WR}{FS,GS}BASE ops */
#define	CR4_PCIDE	0x00020000	/* enable process-context IDs */
#define	CR4_OSXSAVE	0x00040000	/* enable XSAVE and extended states */
#define	CR4_SMEP	0x00100000	/* supervisor mode exec protection */
#define	CR4_SMAP	0x00200000	/* supervisor mode access prevention */
#define CR4_PKE		0x00400000	/* protection key enable */

/*
 * CPUID "features" bits (CPUID function 0x1):
 * EDX bits, then ECX bits
 */

#define	CPUID_FPU	0x00000001	/* processor has an FPU? */
#define	CPUID_VME	0x00000002	/* has virtual mode (%cr4's VME/PVI) */
#define	CPUID_DE	0x00000004	/* has debugging extension */
#define	CPUID_PSE	0x00000008	/* has 4MB page size extension */
#define	CPUID_TSC	0x00000010	/* has time stamp counter */
#define	CPUID_MSR	0x00000020	/* has model specific registers */
#define	CPUID_PAE	0x00000040	/* has phys address extension */
#define	CPUID_MCE	0x00000080	/* has machine check exception */
#define	CPUID_CX8	0x00000100	/* has CMPXCHG8B instruction */
#define	CPUID_APIC	0x00000200	/* has enabled APIC */
#define	CPUID_SYS1	0x00000400	/* has SYSCALL/SYSRET inst. (Cyrix) */
#define	CPUID_SEP	0x00000800	/* has SYSCALL/SYSRET inst. (AMD/Intel) */
#define	CPUID_MTRR	0x00001000	/* has memory type range register */
#define	CPUID_PGE	0x00002000	/* has page global extension */
#define	CPUID_MCA	0x00004000	/* has machine check architecture */
#define	CPUID_CMOV	0x00008000	/* has CMOVcc instruction */
#define	CPUID_PAT	0x00010000	/* has page attribute table */
#define	CPUID_PSE36	0x00020000	/* has 36bit page size extension */
#define	CPUID_PSN	0x00040000	/* has processor serial number */
#define	CPUID_CFLUSH	0x00080000	/* CFLUSH insn supported */
#define	CPUID_B20	0x00100000	/* reserved */
#define	CPUID_DS	0x00200000	/* Debug Store */
#define	CPUID_ACPI	0x00400000	/* ACPI performance modulation regs */
#define	CPUID_MMX	0x00800000	/* has MMX instructions */
#define	CPUID_FXSR	0x01000000	/* has FXRSTOR instruction */
#define	CPUID_SSE	0x02000000	/* has streaming SIMD extensions */
#define	CPUID_SSE2	0x04000000	/* has streaming SIMD extensions #2 */
#define	CPUID_SS	0x08000000	/* self-snoop */
#define	CPUID_HTT	0x10000000	/* Hyper-Threading Technology */
#define	CPUID_TM	0x20000000	/* thermal monitor (TCC) */
#define	CPUID_B30	0x40000000	/* reserved */
#define	CPUID_PBE	0x80000000	/* Pending Break Enabled restarts clock */

#define	CPUIDECX_SSE3	0x00000001	/* streaming SIMD extensions #3 */
#define	CPUIDECX_PCLMUL	0x00000002	/* Carryless Multiplication */
#define	CPUIDECX_DTES64	0x00000004	/* 64bit debug store */
#define	CPUIDECX_MWAIT	0x00000008	/* Monitor/Mwait */
#define	CPUIDECX_DSCPL	0x00000010	/* CPL Qualified Debug Store */
#define	CPUIDECX_VMX	0x00000020	/* Virtual Machine Extensions */
#define	CPUIDECX_SMX	0x00000040	/* Safer Mode Extensions */
#define	CPUIDECX_EST	0x00000080	/* enhanced SpeedStep */
#define	CPUIDECX_TM2	0x00000100	/* thermal monitor 2 */
#define	CPUIDECX_SSSE3	0x00000200	/* Supplemental Streaming SIMD Ext. 3 */
#define	CPUIDECX_CNXTID	0x00000400	/* Context ID */
#define CPUIDECX_SDBG	0x00000800	/* Silicon debug capability */
#define	CPUIDECX_FMA3	0x00001000	/* Fused Multiply Add */
#define	CPUIDECX_CX16	0x00002000	/* has CMPXCHG16B instruction */
#define	CPUIDECX_XTPR	0x00004000	/* xTPR Update Control */
#define	CPUIDECX_PDCM	0x00008000	/* Perfmon and Debug Capability */
#define	CPUIDECX_PCID	0x00020000	/* Process-context ID Capability */
#define	CPUIDECX_DCA	0x00040000	/* Direct Cache Access */
#define	CPUIDECX_SSE41	0x00080000	/* Streaming SIMD Extensions 4.1 */
#define	CPUIDECX_SSE42	0x00100000	/* Streaming SIMD Extensions 4.2 */
#define	CPUIDECX_X2APIC	0x00200000	/* Extended xAPIC Support */
#define	CPUIDECX_MOVBE	0x00400000	/* MOVBE Instruction */
#define	CPUIDECX_POPCNT	0x00800000	/* POPCNT Instruction */
#define	CPUIDECX_DEADLINE	0x01000000	/* APIC one-shot via deadline */
#define	CPUIDECX_AES	0x02000000	/* AES Instruction */
#define	CPUIDECX_XSAVE	0x04000000	/* XSAVE/XSTOR States */
#define	CPUIDECX_OSXSAVE	0x08000000	/* OSXSAVE */
#define	CPUIDECX_AVX	0x10000000	/* Advanced Vector Extensions */
#define	CPUIDECX_F16C	0x20000000	/* 16bit fp conversion  */
#define	CPUIDECX_RDRAND	0x40000000	/* RDRAND instruction  */
#define	CPUIDECX_HV	0x80000000	/* Running on hypervisor */

/*
 * "Structured Extended Feature Flags Parameters" (CPUID function 0x7, leaf 0)
 * EBX bits
 */
#define	SEFF0EBX_FSGSBASE	0x00000001 /* {RD,WR}[FG]SBASE instructions */
#define	SEFF0EBX_TSC_ADJUST	0x00000002 /* Has IA32_TSC_ADJUST MSR */
#define	SEFF0EBX_SGX		0x00000004 /* Software Guard Extensions */
#define	SEFF0EBX_BMI1		0x00000008 /* advanced bit manipulation */
#define	SEFF0EBX_HLE		0x00000010 /* Hardware Lock Elision */
#define	SEFF0EBX_AVX2		0x00000020 /* Advanced Vector Extensions 2 */
#define	SEFF0EBX_SMEP		0x00000080 /* Supervisor mode exec protection */
#define	SEFF0EBX_BMI2		0x00000100 /* advanced bit manipulation */
#define	SEFF0EBX_ERMS		0x00000200 /* Enhanced REP MOVSB/STOSB */
#define	SEFF0EBX_INVPCID	0x00000400 /* INVPCID instruction */
#define	SEFF0EBX_RTM		0x00000800 /* Restricted Transactional Memory */
#define	SEFF0EBX_PQM		0x00001000 /* Quality of Service Monitoring */
#define	SEFF0EBX_MPX		0x00004000 /* Memory Protection Extensions */
#define	SEFF0EBX_AVX512F	0x00010000 /* AVX-512 foundation inst */
#define	SEFF0EBX_AVX512DQ	0x00020000 /* AVX-512 double/quadword */
#define	SEFF0EBX_RDSEED		0x00040000 /* RDSEED instruction */
#define	SEFF0EBX_ADX		0x00080000 /* ADCX/ADOX instructions */
#define	SEFF0EBX_SMAP		0x00100000 /* Supervisor mode access prevent */
#define	SEFF0EBX_AVX512IFMA	0x00200000 /* AVX-512 integer mult-add */
#define	SEFF0EBX_PCOMMIT	0x00400000 /* Persistent commit inst */
#define	SEFF0EBX_CLFLUSHOPT	0x00800000 /* cache line flush */
#define	SEFF0EBX_CLWB		0x01000000 /* cache line write back */
#define	SEFF0EBX_PT		0x02000000 /* Processor Trace */
#define	SEFF0EBX_AVX512PF	0x04000000 /* AVX-512 prefetch */
#define	SEFF0EBX_AVX512ER	0x08000000 /* AVX-512 exp/reciprocal */
#define	SEFF0EBX_AVX512CD	0x10000000 /* AVX-512 conflict detection */
#define	SEFF0EBX_SHA		0x20000000 /* SHA Extensions */
#define	SEFF0EBX_AVX512BW	0x40000000 /* AVX-512 byte/word inst */
#define	SEFF0EBX_AVX512VL	0x80000000 /* AVX-512 vector len inst */
/* SEFF ECX bits */
#define SEFF0ECX_PREFETCHWT1	0x00000001 /* PREFETCHWT1 instruction */
#define SEFF0ECX_AVX512VBMI	0x00000002 /* AVX-512 vector bit inst */
#define SEFF0ECX_UMIP		0x00000004 /* UMIP support */
#define SEFF0ECX_PKU		0x00000008 /* Page prot keys for user mode */
#define SEFF0ECX_WAITPKG	0x00000020 /* UMONITOR/UMWAIT/TPAUSE insns */
/* SEFF EDX bits */
#define SEFF0EDX_AVX512_4FNNIW	0x00000004 /* AVX-512 neural network insns */
#define SEFF0EDX_AVX512_4FMAPS	0x00000008 /* AVX-512 mult accum single prec */
#define SEFF0EDX_SRBDS_CTRL	0x00000200 /* MCU_OPT_CTRL MSR */
#define SEFF0EDX_MD_CLEAR	0x00000400 /* Microarch Data Clear */
#define SEFF0EDX_TSXFA		0x00002000 /* TSX Forced Abort */
#define SEFF0EDX_IBRS		0x04000000 /* IBRS / IBPB Speculation Control */
#define SEFF0EDX_STIBP		0x08000000 /* STIBP Speculation Control */
#define SEFF0EDX_L1DF		0x10000000 /* L1D_FLUSH */
#define SEFF0EDX_ARCH_CAP	0x20000000 /* Has IA32_ARCH_CAPABILITIES MSR */
#define SEFF0EDX_SSBD		0x80000000 /* Spec Store Bypass Disable */

/*
 * Thermal and Power Management (CPUID function 0x6) EAX bits
 */
#define	TPM_SENSOR	0x00000001	 /* Digital temp sensor */
#define	TPM_ARAT	0x00000004	 /* APIC Timer Always Running */

/*
 * "Architectural Performance Monitoring" bits (CPUID function 0x0a):
 * EAX bits
 */

#define CPUIDEAX_VERID			0x000000ff
#define CPUIDEAX_NUM_GC(cpuid)		(((cpuid) >>  8) & 0x000000ff)
#define CPUIDEAX_BIT_GC(cpuid)		(((cpuid) >> 16) & 0x000000ff)
#define CPUIDEAX_LEN_EBX(cpuid)		(((cpuid) >> 24) & 0x000000ff)

#define CPUIDEBX_EVT_CORE		(1 << 0) /* Core cycle */
#define CPUIDEBX_EVT_INST		(1 << 1) /* Instruction retired */
#define CPUIDEBX_EVT_REFR		(1 << 2) /* Reference cycles */
#define CPUIDEBX_EVT_CACHE_REF		(1 << 3) /* Last-level cache ref. */
#define CPUIDEBX_EVT_CACHE_MIS		(1 << 4) /* Last-level cache miss. */
#define CPUIDEBX_EVT_BRANCH_INST	(1 << 5) /* Branch instruction ret. */
#define CPUIDEBX_EVT_BRANCH_MISP	(1 << 6) /* Branch mispredict ret. */

#define CPUIDEDX_NUM_FC(cpuid)		(((cpuid) >> 0) & 0x0000001f)
#define CPUIDEDX_BIT_FC(cpuid)		(((cpuid) >> 5) & 0x000000ff)

/*
 * CPUID "extended features" bits (CPUID function 0x80000001):
 * EDX bits, then ECX bits
 */

#define	CPUID_MPC	0x00080000	/* Multiprocessing Capable */
#define	CPUID_NXE	0x00100000	/* No-Execute Extension */
#define	CPUID_MMXX	0x00400000	/* AMD MMX Extensions */
#define	CPUID_FFXSR	0x02000000	/* fast FP/MMX save/restore */
#define	CPUID_PAGE1GB	0x04000000	/* 1-GByte pages */
#define	CPUID_RDTSCP	0x08000000	/* RDTSCP / IA32_TSC_AUX available */
#define	CPUID_LONG	0x20000000	/* long mode */
#define	CPUID_3DNOW2	0x40000000	/* 3DNow! Instruction Extension */
#define	CPUID_3DNOW	0x80000000	/* 3DNow! Instructions */

#define	CPUIDECX_LAHF		0x00000001 /* LAHF and SAHF instructions */
#define	CPUIDECX_CMPLEG		0x00000002 /* Core MP legacy mode */
#define	CPUIDECX_SVM		0x00000004 /* Secure Virtual Machine */
#define	CPUIDECX_EAPICSP	0x00000008 /* Extended APIC space */
#define	CPUIDECX_AMCR8		0x00000010 /* LOCK MOV CR0 means MOV CR8 */
#define	CPUIDECX_ABM		0x00000020 /* LZCNT instruction */
#define	CPUIDECX_SSE4A		0x00000040 /* SSE4-A instruction set */
#define	CPUIDECX_MASSE		0x00000080 /* Misaligned SSE mode */
#define	CPUIDECX_3DNOWP		0x00000100 /* 3DNowPrefetch */
#define	CPUIDECX_OSVW		0x00000200 /* OS visible workaround */
#define	CPUIDECX_IBS		0x00000400 /* Instruction based sampling */
#define	CPUIDECX_XOP		0x00000800 /* Extended operating support */
#define	CPUIDECX_SKINIT		0x00001000 /* SKINIT and STGI are supported */
#define	CPUIDECX_WDT		0x00002000 /* Watchdog timer */
/* Reserved			0x00004000 */
#define	CPUIDECX_LWP		0x00008000 /* Lightweight profiling support */
#define	CPUIDECX_FMA4		0x00010000 /* 4-operand FMA instructions */
#define	CPUIDECX_TCE		0x00020000 /* Translation Cache Extension */
/* Reserved			0x00040000 */
#define	CPUIDECX_NODEID		0x00080000 /* Support for MSRC001C */
/* Reserved			0x00100000 */
#define	CPUIDECX_TBM		0x00200000 /* Trailing bit manipulation instruction */
#define	CPUIDECX_TOPEXT		0x00400000 /* Topology extensions support */
#define	CPUIDECX_CPCTR		0x00800000 /* core performance counter ext */
#define	CPUIDECX_DBKP		0x04000000 /* DataBreakpointExtension */
#define	CPUIDECX_PERFTSC	0x08000000 /* performance time-stamp counter */
#define	CPUIDECX_PCTRL3		0x10000000 /* L3 performance counter ext */
#define	CPUIDECX_MWAITX		0x20000000 /* MWAITX/MONITORX */

/*
 * "Advanced Power Management Information" bits (CPUID function 0x80000007):
 * EDX bits.
 */

#define CPUIDEDX_ITSC		(1 << 8)	/* Invariant TSC */

/*
 * AMD CPUID function 0x80000008 EBX bits
 */
#define CPUIDEBX_IBPB		(1ULL << 12)	/* Speculation Control IBPB */
#define CPUIDEBX_IBRS		(1ULL << 14)	/* Speculation Control IBRS */
#define CPUIDEBX_STIBP		(1ULL << 15)	/* Speculation Control STIBP */
#define CPUIDEBX_IBRS_ALWAYSON	(1ULL << 16)	/* IBRS always on mode */
#define CPUIDEBX_STIBP_ALWAYSON	(1ULL << 17)	/* STIBP always on mode */
#define CPUIDEBX_IBRS_PREF	(1ULL << 18)	/* IBRS preferred */
#define CPUIDEBX_SSBD		(1ULL << 24)	/* Speculation Control SSBD */
#define CPUIDEBX_VIRT_SSBD	(1ULL << 25)	/* Virt Spec Control SSBD */
#define CPUIDEBX_SSBD_NOTREQ	(1ULL << 26)	/* SSBD not required */

#define	CPUID2FAMILY(cpuid)	(((cpuid) >> 8) & 15)
#define	CPUID2MODEL(cpuid)	(((cpuid) >> 4) & 15)
#define	CPUID2STEPPING(cpuid)	((cpuid) & 15)

#define	CPUID(code, eax, ebx, ecx, edx)                         \
	__asm volatile("cpuid"                                  \
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)    \
	    : "a" (code))
#define	CPUID_LEAF(code, leaf, eax, ebx, ecx, edx)		\
	__asm volatile("cpuid"                                  \
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)    \
	    : "a" (code), "c" (leaf))


/*
 * Model-specific registers for the i386 family
 */
#define MSR_P5_MC_ADDR		0x000
#define MSR_P5_MC_TYPE		0x001
#define MSR_TSC			0x010
#define	P5MSR_CTRSEL		0x011	/* P5 only (trap on P6) */
#define	P5MSR_CTR0		0x012	/* P5 only (trap on P6) */
#define	P5MSR_CTR1		0x013	/* P5 only (trap on P6) */
#define MSR_PLATFORM_ID		0x017	/* Platform ID for microcode */
#define MSR_APICBASE		0x01b
#define	APICBASE_BSP		0x100
#define APICBASE_ENABLE_X2APIC	0x400
#define APICBASE_GLOBAL_ENABLE	0x800
#define MSR_EBL_CR_POWERON	0x02a
#define MSR_EBC_FREQUENCY_ID	0x02c	/* Pentium 4 only */
#define	MSR_TEST_CTL		0x033
#define MSR_IA32_FEATURE_CONTROL 0x03a
#define MSR_SPEC_CTRL		0x048	/* Speculation Control IBRS / STIBP */
#define SPEC_CTRL_IBRS		(1ULL << 0)
#define SPEC_CTRL_STIBP		(1ULL << 1)
#define SPEC_CTRL_SSBD		(1ULL << 2)
#define MSR_PRED_CMD		0x049	/* Speculation Control IBPB */
#define PRED_CMD_IBPB		(1ULL << 0)
#define MSR_BIOS_UPDT_TRIG	0x079
#define	MSR_BBL_CR_D0		0x088	/* PII+ only */
#define	MSR_BBL_CR_D1		0x089	/* PII+ only */
#define	MSR_BBL_CR_D2		0x08a	/* PII+ only */
#define MSR_BIOS_SIGN		0x08b
#define MSR_PERFCTR0		0x0c1
#define MSR_PERFCTR1		0x0c2
#define P6MSR_CTR0		0x0c1
#define P6MSR_CTR1		0x0c2
#define MSR_FSB_FREQ		0x0cd	/* Core Duo/Solo only */
#define MSR_MTRRcap		0x0fe
#define MTRRcap_FIXED		0x100	/* bit 8 - fixed MTRRs supported */
#define MTRRcap_WC		0x400	/* bit 10 - WC type supported */
#define MTRRcap_SMRR		0x800	/* bit 11 - SMM range reg supported */
#define MSR_ARCH_CAPABILITIES	0x10a
#define ARCH_CAP_RDCL_NO		(1 <<  0) /* Meltdown safe */
#define ARCH_CAP_IBRS_ALL		(1 <<  1) /* enhanced IBRS */
#define ARCH_CAP_RSBA			(1 <<  2) /* RSB Alternate */
#define ARCH_CAP_SKIP_L1DFL_VMENTRY	(1 <<  3)
#define ARCH_CAP_SSB_NO			(1 <<  4) /* Spec St Byp safe */
#define ARCH_CAP_MDS_NO			(1 <<  5) /* microarch data-sampling */
#define ARCH_CAP_IF_PSCHANGE_MC_NO	(1 <<  6) /* PS MCE safe */
#define ARCH_CAP_TSX_CTRL		(1 <<  7) /* has TSX_CTRL MSR */
#define ARCH_CAP_TAA_NO			(1 <<  8) /* TSX AA safe */
#define ARCH_CAP_MCU_CONTROL		(1 <<  9) /* has MCU_CTRL MSR */
#define ARCH_CAP_MISC_PACKAGE_CTLS	(1 << 10) /* has MISC_PKG_CTLS MSR */
#define ARCH_CAP_ENERGY_FILTERING_CTL	(1 << 11) /* r/w energy fltring bit */
#define ARCH_CAP_DOITM			(1 << 12) /* Data oprnd indpdnt tmng */
#define ARCH_CAP_SBDR_SSDP_NO		(1 << 13) /* SBDR/SSDP safe */
#define ARCH_CAP_FBSDP_NO		(1 << 14) /* FBSDP safe */
#define ARCH_CAP_PSDP_NO		(1 << 15) /* PSDP safe */
#define ARCH_CAP_FB_CLEAR		(1 << 17) /* MD_CLEAR covers FB */
#define ARCH_CAP_FB_CLEAR_CTRL		(1 << 18)
#define ARCH_CAP_RRSBA			(1 << 19) /* has RRSBA if not dis */
#define ARCH_CAP_BHI_NO			(1 << 20) /* BHI safe */
#define ARCH_CAP_XAPIC_DISABLE_STATUS	(1 << 21) /* can disable xAPIC */
#define ARCH_CAP_OVERCLOCKING_STATUS	(1 << 23) /* has OVRCLCKNG_STAT MSR */
#define ARCH_CAP_PBRSB_NO		(1 << 24) /* PBSR safe */
#define ARCH_CAP_GDS_CTRL		(1 << 25) /* has GDS_MITG_DIS/LOCK */
#define ARCH_CAP_GDS_NO			(1 << 26) /* GDS safe */
#define MSR_FLUSH_CMD		0x10b
#define FLUSH_CMD_L1D_FLUSH	(1ULL << 0)
#define	MSR_BBL_CR_ADDR		0x116	/* PII+ only */
#define	MSR_BBL_CR_DECC		0x118	/* PII+ only */
#define	MSR_BBL_CR_CTL		0x119	/* PII+ only */
#define	MSR_BBL_CR_TRIG		0x11a	/* PII+ only */
#define	MSR_BBL_CR_BUSY		0x11b	/* PII+ only */
#define	MSR_BBL_CR_CTR3		0x11e	/* PII+ only */
#define	MSR_TSX_CTRL		0x122
#define TSX_CTRL_RTM_DISABLE		(1ULL << 0)
#define TSX_CTRL_TSX_CPUID_CLEAR	(1ULL << 1)
#define	MSR_MCU_OPT_CTRL	0x123
#define RNGDS_MITG_DIS			(1ULL << 0)
#define MSR_SYSENTER_CS		0x174
#define MSR_SYSENTER_ESP	0x175
#define MSR_SYSENTER_EIP	0x176
#define MSR_MCG_CAP		0x179
#define MSR_MCG_STATUS		0x17a
#define MSR_MCG_CTL		0x17b
#define P6MSR_CTRSEL0		0x186
#define P6MSR_CTRSEL1		0x187
#define MSR_PERF_STATUS		0x198	/* Pentium M */
#define MSR_PERF_CTL		0x199	/* Pentium M */
#define PERF_CTL_TURBO		0x100000000ULL /* bit 32 - turbo mode */
#define MSR_THERM_CONTROL	0x19a
#define MSR_THERM_INTERRUPT	0x19b
#define MSR_THERM_STATUS	0x19c
#define MSR_THERM_STATUS_VALID_BIT	0x80000000
#define MSR_THERM_STATUS_TEMP(msr)	((msr >> 16) & 0x7f)
#define MSR_THERM2_CTL		0x19d	/* Pentium M */
#define MSR_MISC_ENABLE		0x1a0
/*
 * MSR_MISC_ENABLE (0x1a0)
 *
 * Enable Fast Strings: enables fast REP MOVS/REP STORS (R/W)
 * Enable TCC: Enable automatic thermal control circuit (R/W)
 * Performance monitoring available: 1 if enabled (R/O)
 * Branch trace storage unavailable: 1 if unsupported (R/O)
 * Processor event based sampling unavailable: 1 if unsupported (R/O)
 * Enhanced Intel SpeedStep technology enable: 1 to enable (R/W)
 * Enable monitor FSM: 1 to enable MONITOR/MWAIT (R/W)
 * Limit CPUID maxval: 1 to limit CPUID leaf nodes to 0x2 and lower (R/W)
 * Enable xTPR message disable: 1 to disable xTPR messages
 * XD bit disable: 1 to disable NX capability (bit 34, or bit 2 of %edx/%rdx)
 */
#define MISC_ENABLE_FAST_STRINGS		(1 << 0)
#define MISC_ENABLE_TCC				(1 << 3)
#define MISC_ENABLE_PERF_MON_AVAILABLE		(1 << 7)
#define MISC_ENABLE_BTS_UNAVAILABLE		(1 << 11)
#define MISC_ENABLE_PEBS_UNAVAILABLE		(1 << 12)
#define MISC_ENABLE_EIST_ENABLED		(1 << 16)
#define MISC_ENABLE_ENABLE_MONITOR_FSM		(1 << 18)
#define MISC_ENABLE_LIMIT_CPUID_MAXVAL		(1 << 22)
#define MISC_ENABLE_xTPR_MESSAGE_DISABLE	(1 << 23)
#define MISC_ENABLE_XD_BIT_DISABLE		(1 << 2)
/*
 * for Core i Series and newer Xeons, see
 * http://www.intel.com/content/dam/www/public/us/en/
 * documents/white-papers/cpu-monitoring-dts-peci-paper.pdf
 */
#define MSR_TEMPERATURE_TARGET	0x1a2	/* Core i Series, Newer Xeons */
#define MSR_TEMPERATURE_TARGET_TJMAX(r) (((r) >> 16) & 0xff)
/*
 * not documented anywhere, see intelcore_update_sensor()
 * only available Core Duo and Core Solo Processors
 */
#define MSR_TEMPERATURE_TARGET_UNDOCUMENTED	0x0ee
#define MSR_TEMPERATURE_TARGET_LOW_BIT_UNDOCUMENTED	0x40000000
#define MSR_DEBUGCTLMSR		0x1d9
#define MSR_LASTBRANCHFROMIP	0x1db
#define MSR_LASTBRANCHTOIP	0x1dc
#define MSR_LASTINTFROMIP	0x1dd
#define MSR_LASTINTTOIP		0x1de
#define MSR_ROB_CR_BKUPTMPDR6	0x1e0
#define MSR_MTRRvarBase		0x200
#define MSR_MTRRfix64K_00000	0x250
#define MSR_MTRRfix16K_80000	0x258
#define MSR_MTRRfix4K_C0000	0x268
#define MSR_CR_PAT		0x277
#define MSR_MTRRdefType		0x2ff
#define MTRRdefType_FIXED_ENABLE	0x400 /* bit 10 - fixed MTRR enabled */
#define MTRRdefType_ENABLE	0x800 /* bit 11 - MTRRs enabled */
#define MSR_PERF_FIXED_CTR1	0x30a	/* CPU_CLK_Unhalted.Core */
#define MSR_PERF_FIXED_CTR2	0x30b	/* CPU_CLK.Unhalted.Ref */
#define MSR_PERF_FIXED_CTR_CTRL	0x38d
#define MSR_PERF_FIXED_CTR_FC_DIS	0x0 /* disable counter */
#define MSR_PERF_FIXED_CTR_FC_1	0x1 /* count ring 1 */
#define MSR_PERF_FIXED_CTR_FC_123	0x2 /* count rings 1,2,3 */
#define MSR_PERF_FIXED_CTR_FC_ANY	0x3 /* count everything */
#define MSR_PERF_FIXED_CTR_FC_MASK	0x3
#define MSR_PERF_FIXED_CTR_FC(_i, _v)	((_v) << (4 * (_i)))
#define MSR_PERF_FIXED_CTR_ANYTHR(_i)	(0x4 << (4 * (_i)))
#define MSR_PERF_FIXED_CTR_INT(_i)	(0x8 << (4 * (_i)))
#define MSR_PERF_GLOBAL_CTRL	0x38f
#define MSR_PERF_GLOBAL_CTR1_EN	(1ULL << 33)
#define MSR_PERF_GLOBAL_CTR2_EN	(1ULL << 34)
#define MSR_MC0_CTL		0x400
#define MSR_MC0_STATUS		0x401
#define MSR_MC0_ADDR		0x402
#define MSR_MC0_MISC		0x403
#define MSR_MC1_CTL		0x404
#define MSR_MC1_STATUS		0x405
#define MSR_MC1_ADDR		0x406
#define MSR_MC1_MISC		0x407
#define MSR_MC2_CTL		0x408
#define MSR_MC2_STATUS		0x409
#define MSR_MC2_ADDR		0x40a
#define MSR_MC2_MISC		0x40b
#define MSR_MC4_CTL		0x40c
#define MSR_MC4_STATUS		0x40d
#define MSR_MC4_ADDR		0x40e
#define MSR_MC4_MISC		0x40f
#define MSR_MC3_CTL		0x410
#define MSR_MC3_STATUS		0x411
#define MSR_MC3_ADDR		0x412
#define MSR_MC3_MISC		0x413

/* VIA MSRs */
#define MSR_CENT_TMTEMPERATURE	0x1423	/* Thermal monitor temperature */
#define MSR_C7M_TMTEMPERATURE	0x1169

/* AMD MSRs */
#define MSR_K6_EPMR		0xc0000086
#define MSR_K7_EVNTSEL0		0xc0010000
#define MSR_K7_EVNTSEL1		0xc0010001
#define MSR_K7_EVNTSEL2		0xc0010002
#define MSR_K7_EVNTSEL3		0xc0010003
#define MSR_K7_PERFCTR0		0xc0010004
#define MSR_K7_PERFCTR1		0xc0010005
#define MSR_K7_PERFCTR2		0xc0010006
#define MSR_K7_PERFCTR3		0xc0010007

/*
 * AMD K8 (Opteron) MSRs.
 */
#define	MSR_PATCH_LEVEL	0x0000008b
#define	MSR_SYSCFG	0xc0000010

#define MSR_EFER	0xc0000080		/* Extended feature enable */
#define	EFER_SCE	0x00000001	/* SYSCALL extension */
#define	EFER_LME	0x00000100	/* Long Mode Active */
#define	EFER_LMA	0x00000400	/* Long Mode Enabled */
#define	EFER_NXE	0x00000800	/* No-Execute Enabled */
#define EFER_SVME	0x00001000	/* SVM Enabled */

#define MSR_STAR	0xc0000081		/* 32 bit syscall gate addr */
#define MSR_LSTAR	0xc0000082		/* 64 bit syscall gate addr */
#define MSR_CSTAR	0xc0000083		/* compat syscall gate addr */
#define MSR_SFMASK	0xc0000084		/* flags to clear on syscall */

#define MSR_FSBASE	0xc0000100		/* 64bit offset for fs: */
#define MSR_GSBASE	0xc0000101		/* 64bit offset for gs: */
#define MSR_KERNELGSBASE 0xc0000102		/* storage for swapgs ins */
#define MSR_PATCH_LOADER 0xc0010020
#define MSR_INT_PEN_MSG	0xc0010055		/* Interrupt pending message */

#define MSR_DE_CFG	0xc0011029		/* Decode Configuration */
#define	DE_CFG_721	0x00000001	/* errata 721 */
#define	DE_CFG_SERIALIZE_LFENCE	(1 << 1)	/* Enable serializing lfence */
#define DE_CFG_SERIALIZE_9 (1 << 9)		/* Zenbleed chickenbit */

#define IPM_C1E_CMP_HLT	0x10000000
#define IPM_SMI_CMP_HLT	0x08000000

/*
 * These require a 'passcode' for access.  See cpufunc.h.
 */
#define	MSR_HWCR	0xc0010015
#define	HWCR_FFDIS	0x00000040

#define	MSR_NB_CFG	0xc001001f
#define	NB_CFG_DISIOREQLOCK	0x0000000000000004ULL
#define	NB_CFG_DISDATMSK	0x0000001000000000ULL

#define	MSR_LS_CFG	0xc0011020
#define	LS_CFG_DIS_LS2_SQUISH	0x02000000

#define	MSR_IC_CFG	0xc0011021
#define	IC_CFG_DIS_SEQ_PREFETCH	0x00000800

#define	MSR_DC_CFG	0xc0011022
#define	DC_CFG_DIS_CNV_WC_SSO	0x00000004
#define	DC_CFG_DIS_SMC_CHK_BUF	0x00000400

#define	MSR_BU_CFG	0xc0011023
#define	BU_CFG_THRL2IDXCMPDIS	0x0000080000000000ULL
#define	BU_CFG_WBPFSMCCHKDIS	0x0000200000000000ULL
#define	BU_CFG_WBENHWSBDIS	0x0001000000000000ULL

/*
 * Constants related to MTRRs
 */
#define MTRR_N64K		8	/* numbers of fixed-size entries */
#define MTRR_N16K		16
#define MTRR_N4K		64

/*
 * the following four 3-byte registers control the non-cacheable regions.
 * These registers must be written as three separate bytes.
 *
 * NCRx+0: A31-A24 of starting address
 * NCRx+1: A23-A16 of starting address
 * NCRx+2: A15-A12 of starting address | NCR_SIZE_xx.
 * 
 * The non-cacheable region's starting address must be aligned to the
 * size indicated by the NCR_SIZE_xx field.
 */
#define NCR1	0xc4
#define NCR2	0xc7
#define NCR3	0xca
#define NCR4	0xcd

#define NCR_SIZE_0K	0
#define NCR_SIZE_4K	1
#define NCR_SIZE_8K	2
#define NCR_SIZE_16K	3
#define NCR_SIZE_32K	4
#define NCR_SIZE_64K	5
#define NCR_SIZE_128K	6
#define NCR_SIZE_256K	7
#define NCR_SIZE_512K	8
#define NCR_SIZE_1M	9
#define NCR_SIZE_2M	10
#define NCR_SIZE_4M	11
#define NCR_SIZE_8M	12
#define NCR_SIZE_16M	13
#define NCR_SIZE_32M	14
#define NCR_SIZE_4G	15

/*
 * Performance monitor events.
 *
 * Note that 586-class and 686-class CPUs have different performance
 * monitors available, and they are accessed differently:
 *
 *	686-class: `rdpmc' instruction
 *	586-class: `rdmsr' instruction, CESR MSR
 *
 * The descriptions of these events are too lengthy to include here.
 * See Appendix A of "Intel Architecture Software Developer's
 * Manual, Volume 3: System Programming" for more information.
 */

/*
 * 586-class CESR MSR format.  Lower 16 bits is CTR0, upper 16 bits
 * is CTR1.
 */

#define	PMC5_CESR_EVENT			0x003f
#define	PMC5_CESR_OS			0x0040
#define	PMC5_CESR_USR			0x0080
#define	PMC5_CESR_E			0x0100
#define	PMC5_CESR_P			0x0200

/*
 * 686-class Event Selector MSR format.
 */

#define	PMC6_EVTSEL_EVENT		0x000000ff
#define	PMC6_EVTSEL_UNIT		0x0000ff00
#define	PMC6_EVTSEL_UNIT_SHIFT		8
#define	PMC6_EVTSEL_USR			(1 << 16)
#define	PMC6_EVTSEL_OS			(1 << 17)
#define	PMC6_EVTSEL_E			(1 << 18)
#define	PMC6_EVTSEL_PC			(1 << 19)
#define	PMC6_EVTSEL_INT			(1 << 20)
#define	PMC6_EVTSEL_EN			(1 << 22)	/* PerfEvtSel0 only */
#define	PMC6_EVTSEL_INV			(1 << 23)
#define	PMC6_EVTSEL_COUNTER_MASK	0xff000000
#define	PMC6_EVTSEL_COUNTER_MASK_SHIFT	24

/* Data Cache Unit */
#define	PMC6_DATA_MEM_REFS		0x43
#define	PMC6_DCU_LINES_IN		0x45
#define	PMC6_DCU_M_LINES_IN		0x46
#define	PMC6_DCU_M_LINES_OUT		0x47
#define	PMC6_DCU_MISS_OUTSTANDING	0x48

/* Instruction Fetch Unit */
#define	PMC6_IFU_IFETCH			0x80
#define	PMC6_IFU_IFETCH_MISS		0x81
#define	PMC6_ITLB_MISS			0x85
#define	PMC6_IFU_MEM_STALL		0x86
#define	PMC6_ILD_STALL			0x87

/* L2 Cache */
#define	PMC6_L2_IFETCH			0x28
#define	PMC6_L2_LD			0x29
#define	PMC6_L2_ST			0x2a
#define	PMC6_L2_LINES_IN		0x24
#define	PMC6_L2_LINES_OUT		0x26
#define	PMC6_L2_M_LINES_INM		0x25
#define	PMC6_L2_M_LINES_OUTM		0x27
#define	PMC6_L2_RQSTS			0x2e
#define	PMC6_L2_ADS			0x21
#define	PMC6_L2_DBUS_BUSY		0x22
#define	PMC6_L2_DBUS_BUSY_RD		0x23

/* External Bus Logic */
#define	PMC6_BUS_DRDY_CLOCKS		0x62
#define	PMC6_BUS_LOCK_CLOCKS		0x63
#define	PMC6_BUS_REQ_OUTSTANDING	0x60
#define	PMC6_BUS_TRAN_BRD		0x65
#define	PMC6_BUS_TRAN_RFO		0x66
#define	PMC6_BUS_TRANS_WB		0x67
#define	PMC6_BUS_TRAN_IFETCH		0x68
#define	PMC6_BUS_TRAN_INVAL		0x69
#define	PMC6_BUS_TRAN_PWR		0x6a
#define	PMC6_BUS_TRANS_P		0x6b
#define	PMC6_BUS_TRANS_IO		0x6c
#define	PMC6_BUS_TRAN_DEF		0x6d
#define	PMC6_BUS_TRAN_BURST		0x6e
#define	PMC6_BUS_TRAN_ANY		0x70
#define	PMC6_BUS_TRAN_MEM		0x6f
#define	PMC6_BUS_DATA_RCV		0x64
#define	PMC6_BUS_BNR_DRV		0x61
#define	PMC6_BUS_HIT_DRV		0x7a
#define	PMC6_BUS_HITM_DRDV		0x7b
#define	PMC6_BUS_SNOOP_STALL		0x7e

/* Floating Point Unit */
#define	PMC6_FLOPS			0xc1
#define	PMC6_FP_COMP_OPS_EXE		0x10
#define	PMC6_FP_ASSIST			0x11
#define	PMC6_MUL			0x12
#define	PMC6_DIV			0x12
#define	PMC6_CYCLES_DIV_BUSY		0x14

/* Memory Ordering */
#define	PMC6_LD_BLOCKS			0x03
#define	PMC6_SB_DRAINS			0x04
#define	PMC6_MISALIGN_MEM_REF		0x05
#define	PMC6_EMON_KNI_PREF_DISPATCHED	0x07	/* P-III only */
#define	PMC6_EMON_KNI_PREF_MISS		0x4b	/* P-III only */

/* Instruction Decoding and Retirement */
#define	PMC6_INST_RETIRED		0xc0
#define	PMC6_UOPS_RETIRED		0xc2
#define	PMC6_INST_DECODED		0xd0
#define	PMC6_EMON_KNI_INST_RETIRED	0xd8
#define	PMC6_EMON_KNI_COMP_INST_RET	0xd9

/* Interrupts */
#define	PMC6_HW_INT_RX			0xc8
#define	PMC6_CYCLES_INT_MASKED		0xc6
#define	PMC6_CYCLES_INT_PENDING_AND_MASKED 0xc7

/* Branches */
#define	PMC6_BR_INST_RETIRED		0xc4
#define	PMC6_BR_MISS_PRED_RETIRED	0xc5
#define	PMC6_BR_TAKEN_RETIRED		0xc9
#define	PMC6_BR_MISS_PRED_TAKEN_RET	0xca
#define	PMC6_BR_INST_DECODED		0xe0
#define	PMC6_BTB_MISSES			0xe2
#define	PMC6_BR_BOGUS			0xe4
#define	PMC6_BACLEARS			0xe6

/* Stalls */
#define	PMC6_RESOURCE_STALLS		0xa2
#define	PMC6_PARTIAL_RAT_STALLS		0xd2

/* Segment Register Loads */
#define	PMC6_SEGMENT_REG_LOADS		0x06

/* Clocks */
#define	PMC6_CPU_CLK_UNHALTED		0x79

/* MMX Unit */
#define	PMC6_MMX_INSTR_EXEC		0xb0	/* Celeron, P-II, P-IIX only */
#define	PMC6_MMX_SAT_INSTR_EXEC		0xb1	/* P-II and P-III only */
#define	PMC6_MMX_UOPS_EXEC		0xb2	/* P-II and P-III only */
#define	PMC6_MMX_INSTR_TYPE_EXEC	0xb3	/* P-II and P-III only */
#define	PMC6_FP_MMX_TRANS		0xcc	/* P-II and P-III only */
#define	PMC6_MMX_ASSIST			0xcd	/* P-II and P-III only */
#define	PMC6_MMX_INSTR_RET		0xc3	/* P-II only */

/* Segment Register Renaming */
#define	PMC6_SEG_RENAME_STALLS		0xd4	/* P-II and P-III only */
#define	PMC6_SEG_REG_RENAMES		0xd5	/* P-II and P-III only */
#define	PMC6_RET_SEG_RENAMES		0xd6	/* P-II and P-III only */

/* VIA C3 crypto featureset: for i386_has_xcrypt */
#define C3_HAS_AES			1	/* cpu has AES */
#define C3_HAS_SHA			2	/* cpu has SHA1 & SHA256 */
#define C3_HAS_MM			4	/* cpu has RSA instructions */
#define C3_HAS_AESCTR			8	/* cpu has AES-CTR instructions */

/* Centaur Extended Feature flags */
#define C3_CPUID_HAS_RNG		0x000004
#define C3_CPUID_DO_RNG			0x000008
#define C3_CPUID_HAS_ACE		0x000040
#define C3_CPUID_DO_ACE			0x000080
#define C3_CPUID_HAS_ACE2		0x000100
#define C3_CPUID_DO_ACE2		0x000200
#define C3_CPUID_HAS_PHE		0x000400
#define C3_CPUID_DO_PHE			0x000800
#define C3_CPUID_HAS_PMM		0x001000
#define C3_CPUID_DO_PMM			0x002000

/* VIA C3 xcrypt-* instruction context control options */
#define	C3_CRYPT_CWLO_ROUND_M		0x0000000f
#define	C3_CRYPT_CWLO_ALG_M		0x00000070
#define	C3_CRYPT_CWLO_ALG_AES		0x00000000
#define	C3_CRYPT_CWLO_KEYGEN_M		0x00000080
#define	C3_CRYPT_CWLO_KEYGEN_HW		0x00000000
#define	C3_CRYPT_CWLO_KEYGEN_SW		0x00000080
#define	C3_CRYPT_CWLO_NORMAL		0x00000000
#define	C3_CRYPT_CWLO_INTERMEDIATE	0x00000100
#define	C3_CRYPT_CWLO_ENCRYPT		0x00000000
#define	C3_CRYPT_CWLO_DECRYPT		0x00000200
#define	C3_CRYPT_CWLO_KEY128		0x0000000a	/* 128bit, 10 rds */
#define	C3_CRYPT_CWLO_KEY192		0x0000040c	/* 192bit, 12 rds */
#define	C3_CRYPT_CWLO_KEY256		0x0000080e	/* 256bit, 15 rds */

/* Intel Silicon Debug */
#define IA32_DEBUG_INTERFACE		0xc80
#define IA32_DEBUG_INTERFACE_ENABLE	0x00000001
#define IA32_DEBUG_INTERFACE_LOCK	0x40000000
#define IA32_DEBUG_INTERFACE_MASK	0x80000000

/*
 * PAT
 */
#define PATENTRY(n, type)       ((uint64_t)type << ((n) * 8))
#define PAT_UC          0x0UL
#define PAT_WC          0x1UL
#define PAT_WT          0x4UL
#define PAT_WP          0x5UL
#define PAT_WB          0x6UL
#define PAT_UCMINUS     0x7UL

/*
 * XSAVE subfeatures (cpuid 0xd, leaf 1)
 */
#define XSAVE_XSAVEOPT		0x1UL
#define XSAVE_XSAVEC		0x2UL
#define XSAVE_XGETBV1		0x4UL
#define XSAVE_XSAVES		0x8UL
