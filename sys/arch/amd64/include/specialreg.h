/*	$OpenBSD: specialreg.h,v 1.120 2025/07/16 07:15:41 jsg Exp $	*/
/*	$NetBSD: specialreg.h,v 1.1 2003/04/26 18:39:48 fvdl Exp $	*/
/*	$NetBSD: x86/specialreg.h,v 1.2 2003/04/25 21:54:30 fvdl Exp $	*/

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
#define CR3_PCID	0xfffULL
#define CR3_PWT		(1ULL << 3)
#define CR3_PCD		(1ULL << 4)
#define CR3_REUSE_PCID	(1ULL << 63)
#define CR3_PADDR	0x7ffffffffffff000ULL

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
#define	CR4_KL		0x00080000	/* enable AES Key Locker */
#define	CR4_SMEP	0x00100000	/* supervisor mode exec protection */
#define	CR4_SMAP	0x00200000	/* supervisor mode access prevention */
#define	CR4_PKE		0x00400000	/* user-mode protection keys */
#define	CR4_CET		0x00800000	/* control-flow enforcement tech */
#define	CR4_PKS		0x01000000	/* supervisor-mode protection keys */
#define	CR4_UINTR	0x02000000	/* user interrupts enable bit */

/*
 * Extended state components, for xsave/xrstor family of instructions.
 */
#define	XFEATURE_X87		0x00000001	/* x87 FPU/MMX state */
#define	XFEATURE_SSE		0x00000002	/* SSE state */
#define	XFEATURE_AVX		0x00000004	/* AVX state */
#define	XFEATURE_BNDREG		0x00000008	/* MPX state */
#define	XFEATURE_BNDCSR		0x00000010	/* MPX state */
#define	XFEATURE_MPX		(XFEATURE_BNDREG | XFEATURE_BNDCSR)
#define	XFEATURE_OPMASK		0x00000020	/* AVX-512 opmask */
#define	XFEATURE_ZMM_HI256	0x00000040	/* AVX-512 ZMM0-7 */
#define	XFEATURE_HI16_ZMM	0x00000080	/* AVX-512 ZMM16-31 */
#define	XFEATURE_AVX512		(XFEATURE_OPMASK | XFEATURE_ZMM_HI256 | \
				 XFEATURE_HI16_ZMM)
#define	XFEATURE_PT		0x00000100	/* processor trace */
#define	XFEATURE_PKRU		0x00000200	/* user page key */
#define	XFEATURE_PASID		0x00000400	/* Process ASIDs */
#define	XFEATURE_CET_U		0x00000800	/* ctrl-flow enforce user */
#define	XFEATURE_CET_S		0x00001000	/* ctrl-flow enforce system */
#define	XFEATURE_CET		(XFEATURE_CET_U | XFEATURE_CET_S)
#define	XFEATURE_HDC		0x00002000	/* HW duty cycling */
#define	XFEATURE_UINTR		0x00004000	/* user interrupts */
#define	XFEATURE_LBR		0x00008000	/* last-branch record */
#define	XFEATURE_HWP		0x00010000	/* HW P-states */
#define	XFEATURE_TILECFG	0x00020000	/* AMX state */
#define	XFEATURE_TILEDATA	0x00040000	/* AMX state */
#define	XFEATURE_AMX		(XFEATURE_TILECFG | XFEATURE_TILEDATA)

/* valid only in xcomp_bv field: */
#define XFEATURE_COMPRESSED	(1ULL << 63)	/* compressed format */

/* which bits are for XCR0 and which for the XSS MSR? */
#define XFEATURE_XCR0_MASK \
	(XFEATURE_X87 | XFEATURE_SSE | XFEATURE_AVX | XFEATURE_MPX | \
	 XFEATURE_AVX512 | XFEATURE_PKRU | XFEATURE_AMX)
#define	XFEATURE_XSS_MASK \
	(XFEATURE_PT | XFEATURE_PASID | XFEATURE_CET | XFEATURE_HDC | \
	 XFEATURE_UINTR | XFEATURE_LBR | XFEATURE_HWP)

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
#define CPUID_EDX_BITS \
    ("\20" "\01FPU" "\02VME" "\03DE" "\04PSE" "\05TSC" "\06MSR" "\07PAE" \
     "\010MCE" "\011CX8" "\012APIC" "\014SEP" "\015MTRR" "\016PGE" "\017MCA" \
     "\020CMOV" "\021PAT" "\022PSE36" "\023PSN" "\024CFLUSH" "\026DS" \
     "\027ACPI" "\030MMX" "\031FXSR" "\032SSE" "\033SSE2" "\034SS" "\035HTT" \
     "\036TM" "\040PBE" )

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
#define CPUID_ECX_BITS \
    ("\20" "\01SSE3" "\02PCLMUL" "\03DTES64" "\04MWAIT" "\05DS-CPL" "\06VMX" \
     "\07SMX" "\010EST" "\011TM2" "\012SSSE3" "\013CNXT-ID" "\014SDBG" \
     "\015FMA3" "\016CX16" "\017xTPR" "\020PDCM" "\022PCID" "\023DCA" \
     "\024SSE4.1" "\025SSE4.2" "\026x2APIC" "\027MOVBE" "\030POPCNT" \
     "\031DEADLINE" "\032AES" "\033XSAVE" "\034OSXSAVE" "\035AVX" "\036F16C" \
     "\037RDRAND" "\040HV" )

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
#define SEFF0_EBX_BITS \
    ("\20" "\01FSGSBASE" "\02TSC_ADJUST" "\03SGX" "\04BMI1" "\05HLE" \
     "\06AVX2" "\010SMEP" "\011BMI2" "\012ERMS" "\013INVPCID" "\014RTM" \
     "\015PQM" "\017MPX" "\021AVX512F" "\022AVX512DQ" "\023RDSEED" "\024ADX" \
     "\025SMAP" "\026AVX512IFMA" "\027PCOMMIT" "\030CLFLUSHOPT" "\031CLWB" \
     "\032PT" "\033AVX512PF" "\034AVX512ER" "\035AVX512CD" "\036SHA" \
     "\037AVX512BW" "\040AVX512VL" )

/* SEFF ECX bits */
#define SEFF0ECX_PREFETCHWT1	0x00000001 /* PREFETCHWT1 instruction */
#define SEFF0ECX_AVX512VBMI	0x00000002 /* AVX-512 vector bit inst */
#define SEFF0ECX_UMIP		0x00000004 /* UMIP support */
#define SEFF0ECX_PKU		0x00000008 /* Page prot keys for user mode */
#define SEFF0ECX_OSPKE		0x00000010 /* OS enabled RD/WRPKRU */
#define SEFF0ECX_WAITPKG	0x00000020 /* UMONITOR/UMWAIT/TPAUSE insns */
#define SEFF0ECX_PKS		0x80000000 /* Page prot keys for sup mode */
#define SEFF0_ECX_BITS \
    ("\20" "\01PREFETCHWT1" "\02AVX512VBMI" "\03UMIP" "\04PKU" "\06WAITPKG" \
     "\040PKS" )

/* SEFF EDX bits */
#define SEFF0EDX_AVX512_4FNNIW	0x00000004 /* AVX-512 neural network insns */
#define SEFF0EDX_AVX512_4FMAPS	0x00000008 /* AVX-512 mult accum single prec */
#define SEFF0EDX_SRBDS_CTRL	0x00000200 /* MCU_OPT_CTRL MSR */
#define SEFF0EDX_MD_CLEAR	0x00000400 /* Microarch Data Clear */
#define SEFF0EDX_TSXFA		0x00002000 /* TSX Forced Abort */
#define SEFF0EDX_IBT		0x00100000 /* Indirect Branch Tracking */
#define SEFF0EDX_IBRS		0x04000000 /* IBRS / IBPB Speculation Control */
#define SEFF0EDX_STIBP		0x08000000 /* STIBP Speculation Control */
#define SEFF0EDX_L1DF		0x10000000 /* L1D_FLUSH */
#define SEFF0EDX_ARCH_CAP	0x20000000 /* Has IA32_ARCH_CAPABILITIES MSR */
#define SEFF0EDX_SSBD		0x80000000 /* Spec Store Bypass Disable */
#define SEFF0_EDX_BITS \
    ("\20" "\03AVX512FNNIW" "\04AVX512FMAPS" "\012SRBDS_CTRL" "\013MD_CLEAR" \
     "\016TSXFA" "\025IBT" "\033IBRS,IBPB" "\034STIBP" "\035L1DF" "\040SSBD" )

/*
 * Thermal and Power Management (CPUID function 0x6) EAX bits
 */
#define	TPM_SENSOR	0x00000001	 /* Digital temp sensor */
#define	TPM_ARAT	0x00000004	 /* APIC Timer Always Running */
#define	TPM_PTS		0x00000040	 /* Intel Package Thermal Status */ 
#define TPM_EAX_BITS \
    ("\20" "\01SENSOR" "\03ARAT" "\07PTS")
/* Thermal and Power Management (CPUID function 0x6) ECX bits */
#define	TPM_EFFFREQ	0x00000001	 /* APERF & MPERF MSR present */
#define TPM_ECX_BITS \
    ("\20" "\01EFFFREQ" )

 /*
  * "Architectural Performance Monitoring" bits (CPUID function 0x0a):
  * EAX bits, EBX bits, EDX bits.
  */

#define CPUIDEAX_VERID			0x000000ff /* Version ID */
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
#define CPUID_RDTSCP	0x08000000	/* RDTSCP / IA32_TSC_AUX available */
#define	CPUID_LONG	0x20000000	/* long mode */
#define	CPUID_3DNOW2	0x40000000	/* 3DNow! Instruction Extension */
#define	CPUID_3DNOW	0x80000000	/* 3DNow! Instructions */
#define CPUIDE_EDX_BITS \
    ("\20" "\024MPC" "\025NXE" "\027MMXX" "\032FFXSR" "\033PAGE1GB" \
     "\034RDTSCP" "\036LONG" "\0373DNOW2" "\0403DNOW" )

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
#define CPUIDE_ECX_BITS \
    ("\20" "\01LAHF" "\02CMPLEG" "\03SVM" "\04EAPICSP" "\05AMCR8" "\06ABM" \
     "\07SSE4A" "\010MASSE" "\0113DNOWP" "\012OSVW" "\013IBS" "\014XOP" \
     "\015SKINIT" "\020WDT" "\021FMA4" "\022TCE" "\024NODEID" "\026TBM" \
     "\027TOPEXT" "\030CPCTR" "\033DBKP" "\034PERFTSC" "\035PCTRL3" \
     "\036MWAITX" )

/*
 * "Advanced Power Management Information" bits (CPUID function 0x80000007):
 * EDX bits.
 */
#define CPUIDEDX_HWPSTATE	(1 << 7)	/* Hardware P State Control */
#define CPUIDEDX_ITSC		(1 << 8)	/* Invariant TSC */
#define CPUID_APMI_EDX_BITS \
    ("\20" "\010HWPSTATE" "\011ITSC" )

/*
 * AMD CPUID function 0x80000008 EBX bits
 */
#define CPUIDEBX_INVLPGB	(1ULL <<  3)	/* INVLPG w/broadcast */
#define CPUIDEBX_IBPB		(1ULL << 12)	/* Speculation Control IBPB */
#define CPUIDEBX_IBRS		(1ULL << 14)	/* Speculation Control IBRS */
#define CPUIDEBX_STIBP		(1ULL << 15)	/* Speculation Control STIBP */
#define CPUIDEBX_IBRS_ALWAYSON	(1ULL << 16)	/* IBRS always on mode */
#define CPUIDEBX_STIBP_ALWAYSON	(1ULL << 17)	/* STIBP always on mode */
#define CPUIDEBX_IBRS_PREF	(1ULL << 18)	/* IBRS preferred */
#define CPUIDEBX_IBRS_SAME_MODE	(1ULL << 19)	/* IBRS not mode-specific */
#define CPUIDEBX_SSBD		(1ULL << 24)	/* Speculation Control SSBD */
#define CPUIDEBX_VIRT_SSBD	(1ULL << 25)	/* Virt Spec Control SSBD */
#define CPUIDEBX_SSBD_NOTREQ	(1ULL << 26)	/* SSBD not required */
#define CPUID_AMDSPEC_EBX_BITS \
    ("\20" "\04INVLPGB" "\015IBPB" "\017IBRS" "\020STIBP" "\021IBRS_ALL" \
     "\022STIBP_ALL" "\023IBRS_PREF" "\024IBRS_SM" "\031SSBD" "\032VIRTSSBD" \
     "\033SSBDNR" )

/*
 * AMD CPUID function 0x8000001F EAX bits
 */
#define CPUIDEAX_SME		(1ULL << 0)  /* SME */
#define CPUIDEAX_SEV		(1ULL << 1)  /* SEV */
#define CPUIDEAX_PFLUSH_MSR	(1ULL << 2)  /* Page Flush MSR */
#define CPUIDEAX_SEVES		(1ULL << 3)  /* SEV-ES */
#define CPUIDEAX_SEVSNP		(1ULL << 4)  /* SEV-SNP */
#define CPUIDEAX_VMPL		(1ULL << 5)  /* VM Permission Levels */
#define CPUIDEAX_RMPQUERY	(1ULL << 6)  /* RMPQUERY */
#define CPUIDEAX_VMPLSSS	(1ULL << 7)  /* VMPL Supervisor Shadow Stack */
#define CPUIDEAX_SECTSC		(1ULL << 8)  /* Secure TSC */
#define CPUIDEAX_TSCAUXVIRT	(1ULL << 9)  /* TSC Aux Virtualization */
#define CPUIDEAX_HWECACHECOH	(1ULL << 10) /* Coherency Across Enc. Domains */
#define CPUIDEAX_64BITHOST	(1ULL << 11) /* SEV guest requires 64bit host */
#define CPUIDEAX_RESTINJ	(1ULL << 12) /* Restricted Injection */
#define CPUIDEAX_ALTINJ		(1ULL << 13) /* Alternate Injection */
#define CPUIDEAX_DBGSTSW	(1ULL << 14) /* Full debug state swap */
#define CPUIDEAX_IBSDISALLOW	(1ULL << 15) /* Disallowing IBS use by host */
#define CPUIDEAX_VTE		(1ULL << 16) /* Virt. Transparent Encryption */
#define CPUIDEAX_VMGEXITPARAM	(1ULL << 17) /* VMGEXIT Parameter */
#define CPUIDEAX_VTOMMSR	(1ULL << 18) /* Virtual TOM MSR */
#define CPUIDEAX_IBSVIRT	(1ULL << 19) /* IBS Virtualization for SEV-ES */
#define CPUIDEAX_VMSARPROT	(1ULL << 24) /* VMSA Register Protection */
#define CPUIDEAX_SMTPROT	(1ULL << 25) /* SMT Protection */
#define CPUIDEAX_SVSMPAGEMSR	(1ULL << 28) /* SVSM Communication Page MSR */
#define CPUIDEAX_NVSMSR		(1ULL << 29) /* NestedVirtSnpMsr */
#define CPUID_AMDSEV_EAX_BITS \
    ("\20" "\01SME" "\02SEV" "\03PFLUSH_MSR" "\04SEVES" "\05SEVSNP" "\06VMPL" \
     "\07RMPQUERY" "\010VMPLSSS" "\011SECTSC" "\012TSCAUXVIRT" \
     "\013HWECACHECOH" "\014REQ64BITHOST" "\015RESTINJ" "\016ALTINJ" \
     "\017DBGSTSW" "\020IBSDISALLOW" "\021VTE" "\022VMGEXITPARAM" \
     "\023VTOMMSR" "\024IBSVIRT" "\031VMSARPROT" "\032SMTPROT" \
     "\035SVSMPAGEMSR" "\036NVSMSR" )

/* Number of encrypted guests */
#define CPUID_AMDSEV_ECX_BITS ("\20")

/* Minimum ASID for SEV enabled, SEV-ES disabled guest. */
#define CPUID_AMDSEV_EDX_BITS ("\20")

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
#define MSR_P5_MC_ADDR		0x000	/* P5 only */
#define MSR_P5_MC_TYPE		0x001	/* P5 only */
#define MSR_TSC			0x010
#define	MSR_CESR		0x011	/* P5 only (trap on P6) */
#define	MSR_CTR0		0x012	/* P5 only (trap on P6) */
#define	MSR_CTR1		0x013	/* P5 only (trap on P6) */
#define	MSR_PLATFORM_ID		0x017	/* Platform ID for microcode */
#define MSR_APICBASE		0x01b
#define APICBASE_BSP		0x100
#define APICBASE_ENABLE_X2APIC	0x400
#define APICBASE_GLOBAL_ENABLE	0x800
#define MSR_EBL_CR_POWERON	0x02a
#define MSR_EBC_FREQUENCY_ID    0x02c   /* Pentium 4 only */
#define	MSR_TEST_CTL		0x033
#define MSR_IA32_FEATURE_CONTROL 0x03a
#define MSR_TSC_ADJUST		0x03b
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
#define MSR_SMM_MONITOR_CTL	0x09b
#define MSR_SMBASE		0x09e
#define MSR_PERFCTR0		0x0c1
#define MSR_PERFCTR1		0x0c2
#define MSR_FSB_FREQ		0x0cd	/* Core Duo/Solo only */
#define MSR_MPERF		0x0e7
#define MSR_APERF		0x0e8
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
#define ARCH_CAP_RFDS_NO		(1 << 27) /* RFDS safe */
#define ARCH_CAP_RFDS_CLEAR		(1 << 28) /* use VERW for RFDS */
#define ARCH_CAP_MSR_BITS \
    ("\20" "\02IBRS_ALL" "\03RSBA" "\04SKIP_L1DFL" "\05SSB_NO" "\06MDS_NO" \
     "\07IF_PSCHANGE" "\010TSX_CTRL" "\011TAA_NO" "\012MCU_CONTROL" \
     "\013MISC_PKG_CT" "\014ENERGY_FILT" "\015DOITM" "\016SBDR_SSDP_N" \
     "\017FBSDP_NO" "\020PSDP_NO" "\022FB_CLEAR" "\023FB_CLEAR_CT" \
     "\024RRSBA" "\025BHI_NO" "\026XAPIC_DIS" "\030OVERCLOCK" "\031PBRSB_NO" \
     "\032GDS_CTRL" "\033GDS_NO" "\034RFDS_NO" "\035RFDS_CLEAR" )

#define MSR_FLUSH_CMD		0x10b
#define FLUSH_CMD_L1D_FLUSH	0x1	/* (1ULL << 0) */
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
#define	MSR_SYSENTER_CS		0x174 	/* PII+ only */
#define	MSR_SYSENTER_ESP	0x175 	/* PII+ only */
#define	MSR_SYSENTER_EIP	0x176   /* PII+ only */
#define MSR_MCG_CAP		0x179
#define MSR_MCG_STATUS		0x17a
#define MSR_MCG_CTL		0x17b
#define MSR_EVNTSEL0		0x186
#define MSR_EVNTSEL1		0x187
#define MSR_PERF_STATUS		0x198	/* Pentium M */
#define MSR_PERF_CTL		0x199	/* Pentium M */
#define PERF_CTL_TURBO		0x100000000ULL /* bit 32 - turbo mode */
#define MSR_THERM_CONTROL	0x19a
#define MSR_THERM_INTERRUPT	0x19b
#define MSR_THERM_STATUS	0x19c
#define MSR_THERM_STATUS_VALID_BIT	0x80000000
#define	MSR_THERM_STATUS_TEMP(msr)	((msr >> 16) & 0x7f)
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
#define MSR_TEMPERATURE_TARGET_TJMAX(msr) (((msr) >> 16) & 0xff)
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
#define	MSR_MTRRvarBase		0x200
#define	MSR_MTRRfix64K_00000	0x250
#define	MSR_MTRRfix16K_80000	0x258
#define	MSR_MTRRfix4K_C0000	0x268
#define MSR_CR_PAT		0x277
#define MSR_MTRRdefType		0x2ff
#define MTRRdefType_FIXED_ENABLE	0x400 /* bit 10 - fixed MTRR enabled */
#define MTRRdefType_ENABLE	0x800 /* bit 11 - MTRRs enabled */
#define MSR_PERF_FIXED_CTR1	0x30a	/* CPU_CLK_Unhalted.Core */
#define MSR_PERF_FIXED_CTR2	0x30b	/* CPU_CLK.Unhalted.Ref */
#define MSR_PERF_FIXED_CTR_CTRL 0x38d
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
#define MSR_PKG_C3_RESIDENCY	0x3f8
#define MSR_PKG_C6_RESIDENCY	0x3f9
#define MSR_PKG_C7_RESIDENCY	0x3fa
#define MSR_CORE_C3_RESIDENCY	0x3fc
#define MSR_CORE_C6_RESIDENCY	0x3fd
#define MSR_CORE_C7_RESIDENCY	0x3fe
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
#define MSR_PKG_C2_RESIDENCY	0x60d
#define MSR_PKG_C8_RESIDENCY	0x630
#define MSR_PKG_C9_RESIDENCY	0x631
#define MSR_PKG_C10_RESIDENCY	0x632
#define MSR_U_CET		0x6a0
#define MSR_CET_ENDBR_EN		(1 << 2)
#define MSR_CET_NO_TRACK_EN		(1 << 4)
#define MSR_S_CET		0x6a2
#define MSR_PKRS		0x6e1
#define MSR_XSS			0xda0

/* VIA MSR */
#define MSR_CENT_TMTEMPERATURE	0x1423	/* Thermal monitor temperature */

/*
 * AMD K6/K7 MSRs.
 */
#define	MSR_K6_UWCCR		0xc0000085
#define	MSR_K7_EVNTSEL0		0xc0010000
#define	MSR_K7_EVNTSEL1		0xc0010001
#define	MSR_K7_EVNTSEL2		0xc0010002
#define	MSR_K7_EVNTSEL3		0xc0010003
#define	MSR_K7_PERFCTR0		0xc0010004
#define	MSR_K7_PERFCTR1		0xc0010005
#define	MSR_K7_PERFCTR2		0xc0010006
#define	MSR_K7_PERFCTR3		0xc0010007

/*
 * AMD K8 (Opteron) MSRs.
 */
#define	MSR_PATCH_LEVEL	0x0000008b
#define	MSR_SYSCFG	0xc0000010

#define MSR_EFER	0xc0000080	/* Extended feature enable */
#define EFER_SCE	0x00000001	/* SYSCALL extension */
#define EFER_LME	0x00000100	/* Long Mode Enabled */
#define	EFER_LMA	0x00000400	/* Long Mode Active */
#define EFER_NXE	0x00000800	/* No-Execute Enabled */
#define EFER_SVME	0x00001000	/* SVM Enabled */

#define MSR_STAR	0xc0000081	/* 32 bit syscall gate addr */
#define MSR_LSTAR	0xc0000082	/* 64 bit syscall gate addr */
#define MSR_CSTAR	0xc0000083	/* compat syscall gate addr */
#define MSR_SFMASK	0xc0000084	/* flags to clear on syscall */

#define MSR_FSBASE	0xc0000100	/* 64bit offset for fs: */
#define MSR_GSBASE	0xc0000101	/* 64bit offset for gs: */
#define MSR_KERNELGSBASE 0xc0000102	/* storage for swapgs ins */
#define MSR_PATCH_LOADER	0xc0010020
#define MSR_INT_PEN_MSG	0xc0010055	/* Interrupt pending message */

#define MSR_DE_CFG	0xc0011029	/* Decode Configuration */
#define	DE_CFG_721	0x00000001	/* errata 721 */
#define DE_CFG_SERIALIZE_LFENCE	(1 << 1)	/* Enable serializing lfence */
#define DE_CFG_SERIALIZE_9 (1 << 9)	/* Zenbleed chickenbit */

#define IPM_C1E_CMP_HLT	0x10000000
#define IPM_SMI_CMP_HLT	0x08000000

/*
 * These require a 'passcode' for access.  See cpufunc.h.
 */
#define	MSR_HWCR	0xc0010015
#define		HWCR_FFDIS		0x00000040
#define		HWCR_TSCFREQSEL		0x01000000

#define	MSR_PSTATEDEF(_n)	(0xc0010064 + (_n))
#define		PSTATEDEF_EN		0x8000000000000000ULL

#define	MSR_NB_CFG	0xc001001f
#define		NB_CFG_DISIOREQLOCK	0x0000000000000004ULL
#define		NB_CFG_DISDATMSK	0x0000001000000000ULL

#define MSR_SEV_GHCB	0xc0010130
#define		SEV_CPUID_REQ		0x00000004
#define		SEV_CPUID_RESP		0x00000005

#define MSR_SEV_STATUS	0xc0010131
#define		SEV_STAT_ENABLED	0x00000001
#define		SEV_STAT_ES_ENABLED	0x00000002

#define	MSR_LS_CFG	0xc0011020
#define		LS_CFG_DIS_LS2_SQUISH	0x02000000

#define	MSR_IC_CFG	0xc0011021
#define		IC_CFG_DIS_SEQ_PREFETCH	0x00000800

#define	MSR_DC_CFG	0xc0011022
#define		DC_CFG_DIS_CNV_WC_SSO	0x00000004
#define		DC_CFG_DIS_SMC_CHK_BUF	0x00000400

#define	MSR_BU_CFG	0xc0011023
#define		BU_CFG_THRL2IDXCMPDIS	0x0000080000000000ULL
#define		BU_CFG_WBPFSMCCHKDIS	0x0000200000000000ULL
#define		BU_CFG_WBENHWSBDIS	0x0001000000000000ULL

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

#define PMC5_DATA_READ			0x00
#define PMC5_DATA_WRITE			0x01
#define PMC5_DATA_TLB_MISS		0x02
#define PMC5_DATA_READ_MISS		0x03
#define PMC5_DATA_WRITE_MISS		0x04
#define PMC5_WRITE_M_E			0x05
#define PMC5_DATA_LINES_WBACK		0x06
#define PMC5_DATA_CACHE_SNOOP		0x07
#define PMC5_DATA_CACHE_SNOOP_HIT	0x08
#define PMC5_MEM_ACCESS_BOTH_PIPES	0x09
#define PMC5_BANK_CONFLICTS		0x0a
#define PMC5_MISALIGNED_DATA		0x0b
#define PMC5_INST_READ			0x0c
#define PMC5_INST_TLB_MISS		0x0d
#define PMC5_INST_CACHE_MISS		0x0e
#define PMC5_SEGMENT_REG_LOAD		0x0f
#define PMC5_BRANCHES		 	0x12
#define PMC5_BTB_HITS		 	0x13
#define PMC5_BRANCH_TAKEN		0x14
#define PMC5_PIPELINE_FLUSH		0x15
#define PMC5_INST_EXECUTED		0x16
#define PMC5_INST_EXECUTED_V_PIPE	0x17
#define PMC5_BUS_UTILIZATION		0x18
#define PMC5_WRITE_BACKUP_STALL		0x19
#define PMC5_DATA_READ_STALL		0x1a
#define PMC5_WRITE_E_M_STALL		0x1b
#define PMC5_LOCKED_BUS			0x1c
#define PMC5_IO_CYCLE			0x1d
#define PMC5_NONCACHE_MEM_READ		0x1e
#define PMC5_AGI_STALL			0x1f
#define PMC5_FLOPS			0x22
#define PMC5_BP0_MATCH			0x23
#define PMC5_BP1_MATCH			0x24
#define PMC5_BP2_MATCH			0x25
#define PMC5_BP3_MATCH			0x26
#define PMC5_HARDWARE_INTR		0x27
#define PMC5_DATA_RW			0x28
#define PMC5_DATA_RW_MISS		0x29

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

/*
 * AMD K7 Event Selector MSR format.
 */

#define	K7_EVTSEL_EVENT			0x000000ff
#define	K7_EVTSEL_UNIT			0x0000ff00
#define	K7_EVTSEL_UNIT_SHIFT		8
#define	K7_EVTSEL_USR			(1 << 16)
#define	K7_EVTSEL_OS			(1 << 17)
#define	K7_EVTSEL_E			(1 << 18)
#define	K7_EVTSEL_PC			(1 << 19)
#define	K7_EVTSEL_INT			(1 << 20)
#define	K7_EVTSEL_EN			(1 << 22)
#define	K7_EVTSEL_INV			(1 << 23)
#define	K7_EVTSEL_COUNTER_MASK		0xff000000
#define	K7_EVTSEL_COUNTER_MASK_SHIFT	24

/* Segment Register Loads */
#define	K7_SEGMENT_REG_LOADS		0x20

#define	K7_STORES_TO_ACTIVE_INST_STREAM	0x21

/* Data Cache Unit */
#define	K7_DATA_CACHE_ACCESS		0x40
#define	K7_DATA_CACHE_MISS		0x41
#define	K7_DATA_CACHE_REFILL		0x42
#define	K7_DATA_CACHE_REFILL_SYSTEM	0x43
#define	K7_DATA_CACHE_WBACK		0x44
#define	K7_L2_DTLB_HIT			0x45
#define	K7_L2_DTLB_MISS			0x46
#define	K7_MISALIGNED_DATA_REF		0x47
#define	K7_SYSTEM_REQUEST		0x64
#define	K7_SYSTEM_REQUEST_TYPE		0x65

#define	K7_SNOOP_HIT			0x73
#define	K7_SINGLE_BIT_ECC_ERROR		0x74
#define	K7_CACHE_LINE_INVAL		0x75
#define	K7_CYCLES_PROCESSOR_IS_RUNNING	0x76
#define	K7_L2_REQUEST			0x79
#define	K7_L2_REQUEST_BUSY		0x7a

/* Instruction Fetch Unit */
#define	K7_IFU_IFETCH			0x80
#define	K7_IFU_IFETCH_MISS		0x81
#define	K7_IFU_REFILL_FROM_L2		0x82
#define	K7_IFU_REFILL_FROM_SYSTEM	0x83
#define	K7_ITLB_L1_MISS			0x84
#define	K7_ITLB_L2_MISS			0x85
#define	K7_SNOOP_RESYNC			0x86
#define	K7_IFU_STALL			0x87

#define	K7_RETURN_STACK_HITS		0x88
#define	K7_RETURN_STACK_OVERFLOW	0x89

/* Retired */
#define	K7_RETIRED_INST			0xc0
#define	K7_RETIRED_OPS			0xc1
#define	K7_RETIRED_BRANCHES		0xc2
#define	K7_RETIRED_BRANCH_MISPREDICTED	0xc3
#define	K7_RETIRED_TAKEN_BRANCH		0xc4
#define	K7_RETIRED_TAKEN_BRANCH_MISPREDICTED	0xc5
#define	K7_RETIRED_FAR_CONTROL_TRANSFER	0xc6
#define	K7_RETIRED_RESYNC_BRANCH	0xc7
#define	K7_RETIRED_NEAR_RETURNS		0xc8
#define	K7_RETIRED_NEAR_RETURNS_MISPREDICTED	0xc9
#define	K7_RETIRED_INDIRECT_MISPREDICTED	0xca

/* Interrupts */
#define	K7_CYCLES_INT_MASKED		0xcd
#define	K7_CYCLES_INT_PENDING_AND_MASKED	0xce
#define	K7_HW_INTR_RECV			0xcf

#define	K7_INSTRUCTION_DECODER_EMPTY	0xd0
#define	K7_DISPATCH_STALLS		0xd1
#define	K7_BRANCH_ABORTS_TO_RETIRE	0xd2
#define	K7_SERIALIZE			0xd3
#define	K7_SEGMENT_LOAD_STALL		0xd4
#define	K7_ICU_FULL			0xd5
#define	K7_RESERVATION_STATIONS_FULL	0xd6
#define	K7_FPU_FULL			0xd7
#define	K7_LS_FULL			0xd8
#define	K7_ALL_QUIET_STALL		0xd9
#define	K7_FAR_TRANSFER_OR_RESYNC_BRANCH_PENDING	0xda

#define	K7_BP0_MATCH			0xdc
#define	K7_BP1_MATCH			0xdd
#define	K7_BP2_MATCH			0xde
#define	K7_BP3_MATCH			0xdf

/* VIA C3 crypto featureset: for amd64_has_xcrypt */
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
 * VMX
 */
#define IA32_FEATURE_CONTROL_LOCK	0x01
#define IA32_FEATURE_CONTROL_SMX_EN	0x02
#define IA32_FEATURE_CONTROL_VMX_EN	0x04
#define IA32_FEATURE_CONTROL_SENTER_EN (1ULL << 15)
#define IA32_FEATURE_CONTROL_SENTER_PARAM_MASK 0x7f00
#define IA32_VMX_BASIC			0x480
#define IA32_VMX_PINBASED_CTLS		0x481
#define IA32_VMX_PROCBASED_CTLS		0x482
#define IA32_VMX_EXIT_CTLS		0x483
#define IA32_VMX_ENTRY_CTLS		0x484
#define IA32_VMX_MISC			0x485
#define IA32_VMX_CR0_FIXED0		0x486
#define IA32_VMX_CR0_FIXED1		0x487
#define IA32_VMX_CR4_FIXED0		0x488
#define IA32_VMX_CR4_FIXED1		0x489
#define IA32_VMX_PROCBASED2_CTLS	0x48B
#define IA32_VMX_EPT_VPID_CAP		0x48C
#define IA32_VMX_TRUE_PINBASED_CTLS	0x48D
#define IA32_VMX_TRUE_PROCBASED_CTLS	0x48E
#define IA32_VMX_TRUE_EXIT_CTLS		0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS	0x490
#define IA32_VMX_VMFUNC			0x491

#define IA32_EPT_VPID_CAP_XO_TRANSLATIONS	(1ULL << 0)
#define IA32_EPT_VPID_CAP_PAGE_WALK_4		(1ULL << 6)
#define IA32_EPT_VPID_CAP_WB			(1ULL << 14)
#define IA32_EPT_VPID_CAP_AD_BITS		(1ULL << 21)
#define IA32_EPT_VPID_CAP_INVEPT_CONTEXT	(1ULL << 25)
#define IA32_EPT_VPID_CAP_INVEPT_ALL		(1ULL << 26)

#define IA32_EPT_PAGING_CACHE_TYPE_UC	0x0
#define IA32_EPT_PAGING_CACHE_TYPE_WB	0x6
#define IA32_EPT_AD_BITS_ENABLE		(1ULL << 6)
#define IA32_EPT_PAGE_WALK_LENGTH	0x4

/* VMX : IA32_VMX_BASIC bits */
#define IA32_VMX_TRUE_CTLS_AVAIL			(1ULL << 55)

/* VMX : IA32_VMX_PINBASED_CTLS bits */
#define IA32_VMX_EXTERNAL_INT_EXITING			(1ULL << 0)
#define IA32_VMX_NMI_EXITING				(1ULL << 3)
#define IA32_VMX_VIRTUAL_NMIS				(1ULL << 5)
#define IA32_VMX_ACTIVATE_VMX_PREEMPTION_TIMER		(1ULL << 6)
#define IA32_VMX_PROCESS_POSTED_INTERRUPTS		(1ULL << 7)

/* VMX : IA32_VMX_PROCBASED_CTLS bits */
#define IA32_VMX_INTERRUPT_WINDOW_EXITING		(1ULL << 2)
#define IA32_VMX_USE_TSC_OFFSETTING			(1ULL << 3)
#define IA32_VMX_HLT_EXITING				(1ULL << 7)
#define IA32_VMX_INVLPG_EXITING				(1ULL << 9)
#define IA32_VMX_MWAIT_EXITING				(1ULL << 10)
#define IA32_VMX_RDPMC_EXITING				(1ULL << 11)
#define IA32_VMX_RDTSC_EXITING				(1ULL << 12)
#define IA32_VMX_CR3_LOAD_EXITING			(1ULL << 15)
#define IA32_VMX_CR3_STORE_EXITING			(1ULL << 16)
#define IA32_VMX_CR8_LOAD_EXITING			(1ULL << 19)
#define IA32_VMX_CR8_STORE_EXITING			(1ULL << 20)
#define IA32_VMX_USE_TPR_SHADOW				(1ULL << 21)
#define IA32_VMX_NMI_WINDOW_EXITING			(1ULL << 22)
#define IA32_VMX_MOV_DR_EXITING				(1ULL << 23)
#define IA32_VMX_UNCONDITIONAL_IO_EXITING		(1ULL << 24)
#define IA32_VMX_USE_IO_BITMAPS				(1ULL << 25)
#define IA32_VMX_MONITOR_TRAP_FLAG			(1ULL << 27)
#define IA32_VMX_USE_MSR_BITMAPS			(1ULL << 28)
#define IA32_VMX_MONITOR_EXITING			(1ULL << 29)
#define IA32_VMX_PAUSE_EXITING				(1ULL << 30)
#define IA32_VMX_ACTIVATE_SECONDARY_CONTROLS		(1ULL << 31)

/* VMX : IA32_VMX_PROCBASED2_CTLS bits */
#define IA32_VMX_VIRTUALIZE_APIC			(1ULL << 0)
#define IA32_VMX_ENABLE_EPT				(1ULL << 1)
#define IA32_VMX_DESCRIPTOR_TABLE_EXITING		(1ULL << 2)
#define IA32_VMX_ENABLE_RDTSCP				(1ULL << 3)
#define IA32_VMX_VIRTUALIZE_X2APIC_MODE			(1ULL << 4)
#define IA32_VMX_ENABLE_VPID				(1ULL << 5)
#define IA32_VMX_WBINVD_EXITING				(1ULL << 6)
#define IA32_VMX_UNRESTRICTED_GUEST			(1ULL << 7)
#define IA32_VMX_APIC_REGISTER_VIRTUALIZATION		(1ULL << 8)
#define IA32_VMX_VIRTUAL_INTERRUPT_DELIVERY		(1ULL << 9)
#define IA32_VMX_PAUSE_LOOP_EXITING			(1ULL << 10)
#define IA32_VMX_RDRAND_EXITING				(1ULL << 11)
#define IA32_VMX_ENABLE_INVPCID				(1ULL << 12)
#define IA32_VMX_ENABLE_VM_FUNCTIONS			(1ULL << 13)
#define IA32_VMX_VMCS_SHADOWING				(1ULL << 14)
#define IA32_VMX_ENABLE_ENCLS_EXITING			(1ULL << 15)
#define IA32_VMX_RDSEED_EXITING				(1ULL << 16)
#define IA32_VMX_ENABLE_PML				(1ULL << 17)
#define IA32_VMX_EPT_VIOLATION_VE			(1ULL << 18)
#define IA32_VMX_CONCEAL_VMX_FROM_PT			(1ULL << 19)
#define IA32_VMX_ENABLE_XSAVES_XRSTORS			(1ULL << 20)
#define IA32_VMX_ENABLE_TSC_SCALING			(1ULL << 25)

/* VMX : IA32_VMX_EXIT_CTLS bits */
#define IA32_VMX_SAVE_DEBUG_CONTROLS			(1ULL << 2)
#define IA32_VMX_HOST_SPACE_ADDRESS_SIZE		(1ULL << 9)
#define IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_EXIT	(1ULL << 12)
#define IA32_VMX_ACKNOWLEDGE_INTERRUPT_ON_EXIT		(1ULL << 15)
#define IA32_VMX_SAVE_IA32_PAT_ON_EXIT			(1ULL << 18)
#define IA32_VMX_LOAD_IA32_PAT_ON_EXIT			(1ULL << 19)
#define IA32_VMX_SAVE_IA32_EFER_ON_EXIT			(1ULL << 20)
#define IA32_VMX_LOAD_IA32_EFER_ON_EXIT			(1ULL << 21)
#define IA32_VMX_SAVE_VMX_PREEMPTION_TIMER		(1ULL << 22)
#define IA32_VMX_CLEAR_IA32_BNDCFGS_ON_EXIT		(1ULL << 23)
#define IA32_VMX_CONCEAL_VM_EXITS_FROM_PT		(1ULL << 24)
#define IA32_VMX_LOAD_HOST_CET_STATE			(1ULL << 28)

/* VMX: IA32_VMX_ENTRY_CTLS bits */
#define IA32_VMX_LOAD_DEBUG_CONTROLS			(1ULL << 2)
#define IA32_VMX_IA32E_MODE_GUEST			(1ULL << 9)
#define IA32_VMX_ENTRY_TO_SMM				(1ULL << 10)
#define IA32_VMX_DEACTIVATE_DUAL_MONITOR_TREATMENT	(1ULL << 11)
#define IA32_VMX_LOAD_IA32_PERF_GLOBAL_CTRL_ON_ENTRY	(1ULL << 13)
#define IA32_VMX_LOAD_IA32_PAT_ON_ENTRY			(1ULL << 14)
#define IA32_VMX_LOAD_IA32_EFER_ON_ENTRY		(1ULL << 15)
#define IA32_VMX_LOAD_IA32_BNDCFGS_ON_ENTRY		(1ULL << 16)
#define IA32_VMX_CONCEAL_VM_ENTRIES_FROM_PT		(1ULL << 17)
#define IA32_VMX_LOAD_GUEST_CET_STATE			(1ULL << 20)

/*
 * VMX : VMCS Fields
 */

/* 16-bit control fields */
#define VMCS_GUEST_VPID			0x0000
#define VMCS_POSTED_INT_NOTIF_VECTOR	0x0002
#define VMCS_EPTP_INDEX			0x0004

/* 16-bit guest state fields */
#define VMCS_GUEST_IA32_ES_SEL		0x0800
#define VMCS_GUEST_IA32_CS_SEL		0x0802
#define VMCS_GUEST_IA32_SS_SEL		0x0804
#define VMCS_GUEST_IA32_DS_SEL		0x0806
#define VMCS_GUEST_IA32_FS_SEL		0x0808
#define VMCS_GUEST_IA32_GS_SEL		0x080A
#define VMCS_GUEST_IA32_LDTR_SEL	0x080C
#define VMCS_GUEST_IA32_TR_SEL		0x080E
#define VMCS_GUEST_INTERRUPT_STATUS	0x0810
#define VMCS_GUEST_PML_INDEX		0x0812

/* 16-bit host state fields */
#define VMCS_HOST_IA32_ES_SEL		0x0C00
#define VMCS_HOST_IA32_CS_SEL		0x0C02
#define VMCS_HOST_IA32_SS_SEL		0x0C04
#define VMCS_HOST_IA32_DS_SEL		0x0C06
#define VMCS_HOST_IA32_FS_SEL		0x0C08
#define VMCS_HOST_IA32_GS_SEL		0x0C0A
#define VMCS_HOST_IA32_TR_SEL		0x0C0C

/* 64-bit control fields */
#define VMCS_IO_BITMAP_A		0x2000
#define VMCS_IO_BITMAP_B		0x2002
#define VMCS_MSR_BITMAP_ADDRESS		0x2004
#define VMCS_EXIT_STORE_MSR_ADDRESS	0x2006
#define VMCS_EXIT_LOAD_MSR_ADDRESS	0x2008
#define VMCS_ENTRY_LOAD_MSR_ADDRESS	0x200A
#define VMCS_EXECUTIVE_VMCS_POINTER	0x200C
#define VMCS_PML_ADDRESS		0x200E
#define VMCS_TSC_OFFSET			0x2010
#define VMCS_VIRTUAL_APIC_ADDRESS	0x2012
#define VMCS_APIC_ACCESS_ADDRESS	0x2014
#define VMCS_POSTED_INTERRUPT_DESC	0x2016
#define VMCS_VM_FUNCTION_CONTROLS	0x2018
#define VMCS_GUEST_IA32_EPTP		0x201A
#define VMCS_EOI_EXIT_BITMAP_0		0x201C
#define VMCS_EOI_EXIT_BITMAP_1		0x201E
#define VMCS_EOI_EXIT_BITMAP_2		0x2020
#define VMCS_EOI_EXIT_BITMAP_3		0x2022
#define VMCS_EPTP_LIST_ADDRESS		0x2024
#define VMCS_VMREAD_BITMAP_ADDRESS	0x2026
#define VMCS_VMWRITE_BITMAP_ADDRESS	0x2028
#define VMCS_VIRTUALIZATION_EXC_ADDRESS	0x202A
#define VMCS_XSS_EXITING_BITMAP		0x202C
#define VMCS_ENCLS_EXITING_BITMAP	0x202E
#define VMCS_TSC_MULTIPLIER		0x2032

/* 64-bit RO data field */
#define VMCS_GUEST_PHYSICAL_ADDRESS	0x2400

/* 64-bit guest state fields */
#define VMCS_LINK_POINTER		0x2800
#define VMCS_GUEST_IA32_DEBUGCTL	0x2802
#define VMCS_GUEST_IA32_PAT		0x2804
#define VMCS_GUEST_IA32_EFER		0x2806
#define VMCS_GUEST_IA32_PERF_GBL_CTRL	0x2808
#define VMCS_GUEST_PDPTE0		0x280A
#define VMCS_GUEST_PDPTE1		0x280C
#define VMCS_GUEST_PDPTE2		0x280E
#define VMCS_GUEST_PDPTE3		0x2810
#define VMCS_GUEST_IA32_BNDCFGS		0x2812

/* 64-bit host state fields */
#define VMCS_HOST_IA32_PAT		0x2C00
#define VMCS_HOST_IA32_EFER		0x2C02
#define VMCS_HOST_IA32_PERF_GBL_CTRL	0x2C04

/* 32-bit control fields */
#define VMCS_PINBASED_CTLS		0x4000
#define VMCS_PROCBASED_CTLS		0x4002
#define VMCS_EXCEPTION_BITMAP		0x4004
#define VMCS_PF_ERROR_CODE_MASK		0x4006
#define VMCS_PF_ERROR_CODE_MATCH	0x4008
#define VMCS_CR3_TARGET_COUNT		0x400A
#define VMCS_EXIT_CTLS			0x400C
#define VMCS_EXIT_MSR_STORE_COUNT	0x400E
#define VMCS_EXIT_MSR_LOAD_COUNT	0x4010
#define VMCS_ENTRY_CTLS			0x4012
#define VMCS_ENTRY_MSR_LOAD_COUNT	0x4014
#define VMCS_ENTRY_INTERRUPTION_INFO	0x4016
#define VMCS_ENTRY_EXCEPTION_ERROR_CODE	0x4018
#define VMCS_ENTRY_INSTRUCTION_LENGTH	0x401A
#define VMCS_TPR_THRESHOLD		0x401C
#define VMCS_PROCBASED2_CTLS		0x401E
#define VMCS_PLE_GAP			0x4020
#define VMCS_PLE_WINDOW			0x4022

/* 32-bit RO data fields */
#define VMCS_INSTRUCTION_ERROR		0x4400
#define VMCS_EXIT_REASON		0x4402
#define VMCS_EXIT_INTERRUPTION_INFO	0x4404
#define VMCS_EXIT_INTERRUPTION_ERR_CODE	0x4406
#define VMCS_IDT_VECTORING_INFO		0x4408
#define VMCS_IDT_VECTORING_ERROR_CODE	0x440A
#define VMCS_INSTRUCTION_LENGTH		0x440C
#define VMCS_EXIT_INSTRUCTION_INFO	0x440E

/* 32-bit guest state fields */
#define VMCS_GUEST_IA32_ES_LIMIT	0x4800
#define VMCS_GUEST_IA32_CS_LIMIT	0x4802
#define VMCS_GUEST_IA32_SS_LIMIT	0x4804
#define VMCS_GUEST_IA32_DS_LIMIT	0x4806
#define VMCS_GUEST_IA32_FS_LIMIT	0x4808
#define VMCS_GUEST_IA32_GS_LIMIT	0x480A
#define VMCS_GUEST_IA32_LDTR_LIMIT	0x480C
#define VMCS_GUEST_IA32_TR_LIMIT	0x480E
#define VMCS_GUEST_IA32_GDTR_LIMIT	0x4810
#define VMCS_GUEST_IA32_IDTR_LIMIT	0x4812
#define VMCS_GUEST_IA32_ES_AR		0x4814
#define VMCS_GUEST_IA32_CS_AR		0x4816
#define VMCS_GUEST_IA32_SS_AR		0x4818
#define VMCS_GUEST_IA32_DS_AR		0x481A
#define VMCS_GUEST_IA32_FS_AR		0x481C
#define VMCS_GUEST_IA32_GS_AR		0x481E
#define VMCS_GUEST_IA32_LDTR_AR		0x4820
#define VMCS_GUEST_IA32_TR_AR		0x4822
#define VMCS_GUEST_INTERRUPTIBILITY_ST	0x4824
#define VMCS_GUEST_ACTIVITY_STATE	0x4826
#define VMCS_GUEST_SMBASE		0x4828
#define VMCS_GUEST_IA32_SYSENTER_CS	0x482A
#define VMCS_VMX_PREEMPTION_TIMER_VAL	0x482E

/* 32-bit host state field */
#define VMCS_HOST_IA32_SYSENTER_CS	0x4C00

/* Natural-width control fields */
#define VMCS_CR0_MASK			0x6000
#define VMCS_CR4_MASK			0x6002
#define VMCS_CR0_READ_SHADOW		0x6004
#define VMCS_CR4_READ_SHADOW		0x6006
#define VMCS_CR3_TARGET_0		0x6008
#define VMCS_CR3_TARGET_1		0x600A
#define VMCS_CR3_TARGET_2		0x600C
#define VMCS_CR3_TARGET_3		0x600E

/* Natural-width RO fields */
#define VMCS_GUEST_EXIT_QUALIFICATION	0x6400
#define VMCS_IO_RCX			0x6402
#define VMCS_IO_RSI			0x6404
#define VMCS_IO_RDI			0x6406
#define VMCS_IO_RIP			0x6408
#define VMCS_GUEST_LINEAR_ADDRESS	0x640A

/* Natural-width guest state fields */
#define VMCS_GUEST_IA32_CR0		0x6800
#define VMCS_GUEST_IA32_CR3		0x6802
#define VMCS_GUEST_IA32_CR4		0x6804
#define VMCS_GUEST_IA32_ES_BASE		0x6806
#define VMCS_GUEST_IA32_CS_BASE		0x6808
#define VMCS_GUEST_IA32_SS_BASE		0x680A
#define VMCS_GUEST_IA32_DS_BASE		0x680C
#define VMCS_GUEST_IA32_FS_BASE		0x680E
#define VMCS_GUEST_IA32_GS_BASE		0x6810
#define VMCS_GUEST_IA32_LDTR_BASE	0x6812
#define VMCS_GUEST_IA32_TR_BASE		0x6814
#define VMCS_GUEST_IA32_GDTR_BASE	0x6816
#define VMCS_GUEST_IA32_IDTR_BASE	0x6818
#define VMCS_GUEST_IA32_DR7		0x681A
#define VMCS_GUEST_IA32_RSP		0x681C
#define VMCS_GUEST_IA32_RIP		0x681E
#define VMCS_GUEST_IA32_RFLAGS		0x6820
#define VMCS_GUEST_PENDING_DBG_EXC	0x6822
#define VMCS_GUEST_IA32_SYSENTER_ESP	0x6824
#define VMCS_GUEST_IA32_SYSENTER_EIP	0x6826
#define VMCS_GUEST_IA32_S_CET		0x6828

/* Natural-width host state fields */
#define VMCS_HOST_IA32_CR0		0x6C00
#define VMCS_HOST_IA32_CR3		0x6C02
#define VMCS_HOST_IA32_CR4		0x6C04
#define VMCS_HOST_IA32_FS_BASE		0x6C06
#define VMCS_HOST_IA32_GS_BASE		0x6C08
#define VMCS_HOST_IA32_TR_BASE		0x6C0A
#define VMCS_HOST_IA32_GDTR_BASE	0x6C0C
#define VMCS_HOST_IA32_IDTR_BASE	0x6C0E
#define VMCS_HOST_IA32_SYSENTER_ESP	0x6C10
#define VMCS_HOST_IA32_SYSENTER_EIP	0x6C12
#define VMCS_HOST_IA32_RSP		0x6C14
#define VMCS_HOST_IA32_RIP		0x6C16
#define VMCS_HOST_IA32_S_CET		0x6C18

#define IA32_VMX_INVVPID_INDIV_ADDR_CTX	0x0
#define IA32_VMX_INVVPID_SINGLE_CTX	0x1
#define IA32_VMX_INVVPID_ALL_CTX	0x2
#define IA32_VMX_INVVPID_SINGLE_CTX_GLB	0x3

#define IA32_VMX_INVEPT_SINGLE_CTX	0x1
#define IA32_VMX_INVEPT_GLOBAL_CTX	0x2

#define IA32_VMX_EPT_FAULT_READ		(1ULL << 0)
#define IA32_VMX_EPT_FAULT_WRITE	(1ULL << 1)
#define IA32_VMX_EPT_FAULT_EXEC		(1ULL << 2)

#define IA32_VMX_EPT_FAULT_WAS_READABLE (1ULL << 3)
#define IA32_VMX_EPT_FAULT_WAS_WRITABLE	(1ULL << 4)
#define IA32_VMX_EPT_FAULT_WAS_EXECABLE (1ULL << 5)

#define IA32_VMX_MSR_LIST_SIZE_MASK	(7ULL << 25)
#define IA32_VMX_CR3_TGT_SIZE_MASK	(0x1FFULL << 16)

#define VMX_SKIP_L1D_FLUSH		2
#define VMX_L1D_FLUSH_SIZE		(64 * 1024)

/*
 * SVM
 */
#define MSR_AMD_VM_CR			0xc0010114
#define MSR_AMD_VM_HSAVE_PA		0xc0010117
#define CPUID_AMD_SVM_CAP		0x8000000A
#define AMD_SVM_NESTED_PAGING_CAP	(1 << 0)
#define AMD_SVM_VMCB_CLEAN_CAP		(1 << 5)
#define AMD_SVM_FLUSH_BY_ASID_CAP	(1 << 6)
#define AMD_SVM_DECODE_ASSIST_CAP	(1 << 7)
#define AMD_SVMDIS			0x10

#define SVM_TLB_CONTROL_FLUSH_NONE	0
#define SVM_TLB_CONTROL_FLUSH_ALL	1
#define SVM_TLB_CONTROL_FLUSH_ASID	3
#define SVM_TLB_CONTROL_FLUSH_ASID_GLB	7

#define SVM_CLEANBITS_I			(1 << 0)
#define SVM_CLEANBITS_IOPM		(1 << 1)
#define SVM_CLEANBITS_ASID		(1 << 2)
#define SVM_CLEANBITS_TPR		(1 << 3)
#define SVM_CLEANBITS_NP		(1 << 4)
#define SVM_CLEANBITS_CR		(1 << 5)
#define SVM_CLEANBITS_DR		(1 << 6)
#define SVM_CLEANBITS_DT		(1 << 7)
#define SVM_CLEANBITS_SEG		(1 << 8)
#define SVM_CLEANBITS_CR2		(1 << 9)
#define SVM_CLEANBITS_LBR		(1 << 10)
#define SVM_CLEANBITS_AVIC		(1 << 11)

#define SVM_CLEANBITS_ALL \
	(SVM_CLEANBITS_I | SVM_CLEANBITS_IOPM | SVM_CLEANBITS_ASID | \
	 SVM_CLEANBITS_TPR | SVM_CLEANBITS_NP | SVM_CLEANBITS_CR | \
	 SVM_CLEANBITS_DR | SVM_CLEANBITS_DT | SVM_CLEANBITS_SEG | \
	 SVM_CLEANBITS_CR2 | SVM_CLEANBITS_LBR | SVM_CLEANBITS_AVIC )

#define SVM_INTR_MISC_V_IGN_TPR 0x10

/*
 * SVM : VMCB intercepts
 */
#define SVM_INTERCEPT_CR0_READ		(1UL << 0)
#define SVM_INTERCEPT_CR1_READ		(1UL << 1)
#define SVM_INTERCEPT_CR2_READ		(1UL << 2)
#define SVM_INTERCEPT_CR3_READ		(1UL << 2)
#define SVM_INTERCEPT_CR4_READ		(1UL << 4)
#define SVM_INTERCEPT_CR5_READ		(1UL << 5)
#define SVM_INTERCEPT_CR6_READ		(1UL << 6)
#define SVM_INTERCEPT_CR7_READ		(1UL << 7)
#define SVM_INTERCEPT_CR8_READ		(1UL << 8)
#define SVM_INTERCEPT_CR9_READ		(1UL << 9)
#define SVM_INTERCEPT_CR10_READ		(1UL << 10)
#define SVM_INTERCEPT_CR11_READ		(1UL << 11)
#define SVM_INTERCEPT_CR12_READ		(1UL << 12)
#define SVM_INTERCEPT_CR13_READ		(1UL << 13)
#define SVM_INTERCEPT_CR14_READ		(1UL << 14)
#define SVM_INTERCEPT_CR15_READ		(1UL << 15)
#define SVM_INTERCEPT_CR0_WRITE		(1UL << 16)
#define SVM_INTERCEPT_CR1_WRITE		(1UL << 17)
#define SVM_INTERCEPT_CR2_WRITE		(1UL << 18)
#define SVM_INTERCEPT_CR3_WRITE		(1UL << 19)
#define SVM_INTERCEPT_CR4_WRITE		(1UL << 20)
#define SVM_INTERCEPT_CR5_WRITE		(1UL << 21)
#define SVM_INTERCEPT_CR6_WRITE		(1UL << 22)
#define SVM_INTERCEPT_CR7_WRITE		(1UL << 23)
#define SVM_INTERCEPT_CR8_WRITE		(1UL << 24)
#define SVM_INTERCEPT_CR9_WRITE		(1UL << 25)
#define SVM_INTERCEPT_CR10_WRITE	(1UL << 26)
#define SVM_INTERCEPT_CR11_WRITE	(1UL << 27)
#define SVM_INTERCEPT_CR12_WRITE	(1UL << 28)
#define SVM_INTERCEPT_CR13_WRITE	(1UL << 29)
#define SVM_INTERCEPT_CR14_WRITE	(1UL << 30)
#define SVM_INTERCEPT_CR15_WRITE	(1UL << 31)
#define SVM_INTERCEPT_DR0_READ		(1UL << 0)
#define SVM_INTERCEPT_DR1_READ		(1UL << 1)
#define SVM_INTERCEPT_DR2_READ		(1UL << 2)
#define SVM_INTERCEPT_DR3_READ		(1UL << 2)
#define SVM_INTERCEPT_DR4_READ		(1UL << 4)
#define SVM_INTERCEPT_DR5_READ		(1UL << 5)
#define SVM_INTERCEPT_DR6_READ		(1UL << 6)
#define SVM_INTERCEPT_DR7_READ		(1UL << 7)
#define SVM_INTERCEPT_DR8_READ		(1UL << 8)
#define SVM_INTERCEPT_DR9_READ		(1UL << 9)
#define SVM_INTERCEPT_DR10_READ		(1UL << 10)
#define SVM_INTERCEPT_DR11_READ		(1UL << 11)
#define SVM_INTERCEPT_DR12_READ		(1UL << 12)
#define SVM_INTERCEPT_DR13_READ		(1UL << 13)
#define SVM_INTERCEPT_DR14_READ		(1UL << 14)
#define SVM_INTERCEPT_DR15_READ		(1UL << 15)
#define SVM_INTERCEPT_DR0_WRITE		(1UL << 16)
#define SVM_INTERCEPT_DR1_WRITE		(1UL << 17)
#define SVM_INTERCEPT_DR2_WRITE		(1UL << 18)
#define SVM_INTERCEPT_DR3_WRITE		(1UL << 19)
#define SVM_INTERCEPT_DR4_WRITE		(1UL << 20)
#define SVM_INTERCEPT_DR5_WRITE		(1UL << 21)
#define SVM_INTERCEPT_DR6_WRITE		(1UL << 22)
#define SVM_INTERCEPT_DR7_WRITE		(1UL << 23)
#define SVM_INTERCEPT_DR8_WRITE		(1UL << 24)
#define SVM_INTERCEPT_DR9_WRITE		(1UL << 25)
#define SVM_INTERCEPT_DR10_WRITE	(1UL << 26)
#define SVM_INTERCEPT_DR11_WRITE	(1UL << 27)
#define SVM_INTERCEPT_DR12_WRITE	(1UL << 28)
#define SVM_INTERCEPT_DR13_WRITE	(1UL << 29)
#define SVM_INTERCEPT_DR14_WRITE	(1UL << 30)
#define SVM_INTERCEPT_DR15_WRITE	(1UL << 31)
#define SVM_INTERCEPT_INTR		(1UL << 0)
#define SVM_INTERCEPT_NMI		(1UL << 1)
#define SVM_INTERCEPT_SMI		(1UL << 2)
#define SVM_INTERCEPT_INIT		(1UL << 3)
#define SVM_INTERCEPT_VINTR		(1UL << 4)
#define SVM_INTERCEPT_CR0_SEL_WRITE	(1UL << 5)
#define SVM_INTERCEPT_IDTR_READ		(1UL << 6)
#define SVM_INTERCEPT_GDTR_READ		(1UL << 7)
#define SVM_INTERCEPT_LDTR_READ		(1UL << 8)
#define SVM_INTERCEPT_TR_READ		(1UL << 9)
#define SVM_INTERCEPT_IDTR_WRITE	(1UL << 10)
#define SVM_INTERCEPT_GDTR_WRITE	(1UL << 11)
#define SVM_INTERCEPT_LDTR_WRITE	(1UL << 12)
#define SVM_INTERCEPT_TR_WRITE		(1UL << 13)
#define SVM_INTERCEPT_RDTSC		(1UL << 14)
#define SVM_INTERCEPT_RDPMC		(1UL << 15)
#define SVM_INTERCEPT_PUSHF		(1UL << 16)
#define SVM_INTERCEPT_POPF		(1UL << 17)
#define SVM_INTERCEPT_CPUID		(1UL << 18)
#define SVM_INTERCEPT_RSM		(1UL << 19)
#define SVM_INTERCEPT_IRET		(1UL << 20)
#define SVM_INTERCEPT_INTN		(1UL << 21)
#define SVM_INTERCEPT_INVD		(1UL << 22)
#define SVM_INTERCEPT_PAUSE		(1UL << 23)
#define SVM_INTERCEPT_HLT		(1UL << 24)
#define SVM_INTERCEPT_INVLPG		(1UL << 25)
#define SVM_INTERCEPT_INVLPGA		(1UL << 26)
#define SVM_INTERCEPT_INOUT		(1UL << 27)
#define SVM_INTERCEPT_MSR		(1UL << 28)
#define SVM_INTERCEPT_TASK_SWITCH	(1UL << 29)
#define SVM_INTERCEPT_FERR_FREEZE	(1UL << 30)
#define SVM_INTERCEPT_SHUTDOWN		(1UL << 31)
#define SVM_INTERCEPT_VMRUN		(1UL << 0)
#define SVM_INTERCEPT_VMMCALL		(1UL << 1)
#define SVM_INTERCEPT_VMLOAD		(1UL << 2)
#define SVM_INTERCEPT_VMSAVE		(1UL << 3)
#define SVM_INTERCEPT_STGI		(1UL << 4)
#define SVM_INTERCEPT_CLGI		(1UL << 5)
#define SVM_INTERCEPT_SKINIT		(1UL << 6)
#define SVM_INTERCEPT_RDTSCP		(1UL << 7)
#define SVM_INTERCEPT_ICEBP		(1UL << 8)
#define SVM_INTERCEPT_WBINVD		(1UL << 9)
#define SVM_INTERCEPT_MONITOR		(1UL << 10)
#define SVM_INTERCEPT_MWAIT_UNCOND	(1UL << 11)
#define SVM_INTERCEPT_MWAIT_COND	(1UL << 12)
#define SVM_INTERCEPT_XSETBV		(1UL << 13)
#define SVM_INTERCEPT_EFER_WRITE	(1UL << 15)
#define SVM_INTERCEPT_CR0_WRITE_POST	(1UL << 16)
#define SVM_INTERCEPT_CR1_WRITE_POST	(1UL << 17)
#define SVM_INTERCEPT_CR2_WRITE_POST	(1UL << 18)
#define SVM_INTERCEPT_CR3_WRITE_POST	(1UL << 19)
#define SVM_INTERCEPT_CR4_WRITE_POST	(1UL << 20)
#define SVM_INTERCEPT_CR5_WRITE_POST	(1UL << 21)
#define SVM_INTERCEPT_CR6_WRITE_POST	(1UL << 22)
#define SVM_INTERCEPT_CR7_WRITE_POST	(1UL << 23)
#define SVM_INTERCEPT_CR8_WRITE_POST	(1UL << 24)
#define SVM_INTERCEPT_CR9_WRITE_POST	(1UL << 25)
#define SVM_INTERCEPT_CR10_WRITE_POST	(1UL << 26)
#define SVM_INTERCEPT_CR11_WRITE_POST	(1UL << 27)
#define SVM_INTERCEPT_CR12_WRITE_POST	(1UL << 28)
#define SVM_INTERCEPT_CR13_WRITE_POST	(1UL << 29)
#define SVM_INTERCEPT_CR14_WRITE_POST	(1UL << 30)
#define SVM_INTERCEPT_CR15_WRITE_POST	(1UL << 31)

/*
 * SME and SEV
 */
#define CPUID_AMD_SEV_CAP		0x8000001F
#define AMD_SME_CAP			(1UL << 0)
#define AMD_SEV_CAP			(1UL << 1)

/*
 * PAT
 */
#define PATENTRY(n, type)       (type << ((n) * 8))
#define PAT_UC          0x0UL
#define PAT_WC          0x1UL
#define PAT_WT          0x4UL
#define PAT_WP          0x5UL
#define PAT_WB          0x6UL
#define PAT_UCMINUS     0x7UL

/*
 * XSAVE subfeatures (cpuid 0xd, leaf 1)
 */
#define XSAVE_XSAVEOPT		0x01UL
#define XSAVE_XSAVEC		0x02UL
#define XSAVE_XGETBV1		0x04UL
#define XSAVE_XSAVES		0x08UL
#define XSAVE_XFD		0x10UL
#define XSAVE_BITS \
    ("\20" "\01XSAVEOPT" "\02XSAVEC" "\03XGETBV1" "\04XSAVES" "\05XFD" )

/*
 * Default cr0 and cr4 flags.
 */
#define CR0_DEFAULT	(CR0_PE|CR0_PG|CR0_NE|CR0_WP)
#define CR4_DEFAULT	(CR4_PAE|CR4_PGE|CR4_PSE|CR4_OSFXSR|CR4_OSXMMEXCPT)
