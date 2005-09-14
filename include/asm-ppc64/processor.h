#ifndef __ASM_PPC64_PROCESSOR_H
#define __ASM_PPC64_PROCESSOR_H

/*
 * Copyright (C) 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/stringify.h>
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <asm/atomic.h>
#include <asm/ppcdebug.h>
#include <asm/a.out.h>
#endif
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/systemcfg.h>
#include <asm/cputable.h>

/* Machine State Register (MSR) Fields */
#define MSR_SF_LG	63              /* Enable 64 bit mode */
#define MSR_ISF_LG	61              /* Interrupt 64b mode valid on 630 */
#define MSR_HV_LG 	60              /* Hypervisor state */
#define MSR_VEC_LG	25	        /* Enable AltiVec */
#define MSR_POW_LG	18		/* Enable Power Management */
#define MSR_WE_LG	18		/* Wait State Enable */
#define MSR_TGPR_LG	17		/* TLB Update registers in use */
#define MSR_CE_LG	17		/* Critical Interrupt Enable */
#define MSR_ILE_LG	16		/* Interrupt Little Endian */
#define MSR_EE_LG	15		/* External Interrupt Enable */
#define MSR_PR_LG	14		/* Problem State / Privilege Level */
#define MSR_FP_LG	13		/* Floating Point enable */
#define MSR_ME_LG	12		/* Machine Check Enable */
#define MSR_FE0_LG	11		/* Floating Exception mode 0 */
#define MSR_SE_LG	10		/* Single Step */
#define MSR_BE_LG	9		/* Branch Trace */
#define MSR_DE_LG	9 		/* Debug Exception Enable */
#define MSR_FE1_LG	8		/* Floating Exception mode 1 */
#define MSR_IP_LG	6		/* Exception prefix 0x000/0xFFF */
#define MSR_IR_LG	5 		/* Instruction Relocate */
#define MSR_DR_LG	4 		/* Data Relocate */
#define MSR_PE_LG	3		/* Protection Enable */
#define MSR_PX_LG	2		/* Protection Exclusive Mode */
#define MSR_PMM_LG	2		/* Performance monitor */
#define MSR_RI_LG	1		/* Recoverable Exception */
#define MSR_LE_LG	0 		/* Little Endian */

#ifdef __ASSEMBLY__
#define __MASK(X)	(1<<(X))
#else
#define __MASK(X)	(1UL<<(X))
#endif

#define MSR_SF		__MASK(MSR_SF_LG)	/* Enable 64 bit mode */
#define MSR_ISF		__MASK(MSR_ISF_LG)	/* Interrupt 64b mode valid on 630 */
#define MSR_HV 		__MASK(MSR_HV_LG)	/* Hypervisor state */
#define MSR_VEC		__MASK(MSR_VEC_LG)	/* Enable AltiVec */
#define MSR_POW		__MASK(MSR_POW_LG)	/* Enable Power Management */
#define MSR_WE		__MASK(MSR_WE_LG)	/* Wait State Enable */
#define MSR_TGPR	__MASK(MSR_TGPR_LG)	/* TLB Update registers in use */
#define MSR_CE		__MASK(MSR_CE_LG)	/* Critical Interrupt Enable */
#define MSR_ILE		__MASK(MSR_ILE_LG)	/* Interrupt Little Endian */
#define MSR_EE		__MASK(MSR_EE_LG)	/* External Interrupt Enable */
#define MSR_PR		__MASK(MSR_PR_LG)	/* Problem State / Privilege Level */
#define MSR_FP		__MASK(MSR_FP_LG)	/* Floating Point enable */
#define MSR_ME		__MASK(MSR_ME_LG)	/* Machine Check Enable */
#define MSR_FE0		__MASK(MSR_FE0_LG)	/* Floating Exception mode 0 */
#define MSR_SE		__MASK(MSR_SE_LG)	/* Single Step */
#define MSR_BE		__MASK(MSR_BE_LG)	/* Branch Trace */
#define MSR_DE		__MASK(MSR_DE_LG)	/* Debug Exception Enable */
#define MSR_FE1		__MASK(MSR_FE1_LG)	/* Floating Exception mode 1 */
#define MSR_IP		__MASK(MSR_IP_LG)	/* Exception prefix 0x000/0xFFF */
#define MSR_IR		__MASK(MSR_IR_LG)	/* Instruction Relocate */
#define MSR_DR		__MASK(MSR_DR_LG)	/* Data Relocate */
#define MSR_PE		__MASK(MSR_PE_LG)	/* Protection Enable */
#define MSR_PX		__MASK(MSR_PX_LG)	/* Protection Exclusive Mode */
#define MSR_PMM		__MASK(MSR_PMM_LG)	/* Performance monitor */
#define MSR_RI		__MASK(MSR_RI_LG)	/* Recoverable Exception */
#define MSR_LE		__MASK(MSR_LE_LG)	/* Little Endian */

#define MSR_		MSR_ME | MSR_RI | MSR_IR | MSR_DR | MSR_ISF
#define MSR_KERNEL      MSR_ | MSR_SF | MSR_HV

#define MSR_USER32	MSR_ | MSR_PR | MSR_EE
#define MSR_USER64	MSR_USER32 | MSR_SF

/* Floating Point Status and Control Register (FPSCR) Fields */

#define FPSCR_FX	0x80000000	/* FPU exception summary */
#define FPSCR_FEX	0x40000000	/* FPU enabled exception summary */
#define FPSCR_VX	0x20000000	/* Invalid operation summary */
#define FPSCR_OX	0x10000000	/* Overflow exception summary */
#define FPSCR_UX	0x08000000	/* Underflow exception summary */
#define FPSCR_ZX	0x04000000	/* Zero-divide exception summary */
#define FPSCR_XX	0x02000000	/* Inexact exception summary */
#define FPSCR_VXSNAN	0x01000000	/* Invalid op for SNaN */
#define FPSCR_VXISI	0x00800000	/* Invalid op for Inv - Inv */
#define FPSCR_VXIDI	0x00400000	/* Invalid op for Inv / Inv */
#define FPSCR_VXZDZ	0x00200000	/* Invalid op for Zero / Zero */
#define FPSCR_VXIMZ	0x00100000	/* Invalid op for Inv * Zero */
#define FPSCR_VXVC	0x00080000	/* Invalid op for Compare */
#define FPSCR_FR	0x00040000	/* Fraction rounded */
#define FPSCR_FI	0x00020000	/* Fraction inexact */
#define FPSCR_FPRF	0x0001f000	/* FPU Result Flags */
#define FPSCR_FPCC	0x0000f000	/* FPU Condition Codes */
#define FPSCR_VXSOFT	0x00000400	/* Invalid op for software request */
#define FPSCR_VXSQRT	0x00000200	/* Invalid op for square root */
#define FPSCR_VXCVI	0x00000100	/* Invalid op for integer convert */
#define FPSCR_VE	0x00000080	/* Invalid op exception enable */
#define FPSCR_OE	0x00000040	/* IEEE overflow exception enable */
#define FPSCR_UE	0x00000020	/* IEEE underflow exception enable */
#define FPSCR_ZE	0x00000010	/* IEEE zero divide exception enable */
#define FPSCR_XE	0x00000008	/* FP inexact exception enable */
#define FPSCR_NI	0x00000004	/* FPU non IEEE-Mode */
#define FPSCR_RN	0x00000003	/* FPU rounding control */

/* Special Purpose Registers (SPRNs)*/

#define	SPRN_CTR	0x009	/* Count Register */
#define	SPRN_DABR	0x3F5	/* Data Address Breakpoint Register */
#define   DABR_TRANSLATION	(1UL << 2)
#define	SPRN_DAR	0x013	/* Data Address Register */
#define	SPRN_DEC	0x016	/* Decrement Register */
#define	SPRN_DSISR	0x012	/* Data Storage Interrupt Status Register */
#define   DSISR_NOHPTE		0x40000000	/* no translation found */
#define   DSISR_PROTFAULT	0x08000000	/* protection fault */
#define   DSISR_ISSTORE		0x02000000	/* access was a store */
#define   DSISR_DABRMATCH	0x00400000	/* hit data breakpoint */
#define   DSISR_NOSEGMENT	0x00200000	/* STAB/SLB miss */
#define	SPRN_HID0	0x3F0	/* Hardware Implementation Register 0 */
#define	SPRN_MSRDORM	0x3F1	/* Hardware Implementation Register 1 */
#define SPRN_HID1	0x3F1	/* Hardware Implementation Register 1 */
#define	SPRN_IABR	0x3F2	/* Instruction Address Breakpoint Register */
#define	SPRN_NIADORM	0x3F3	/* Hardware Implementation Register 2 */
#define SPRN_HID4	0x3F4	/* 970 HID4 */
#define SPRN_HID5	0x3F6	/* 970 HID5 */
#define	SPRN_HID6	0x3F9	/* BE HID 6 */
#define	  HID6_LB	(0x0F<<12) /* Concurrent Large Page Modes */
#define	  HID6_DLP	(1<<20)	/* Disable all large page modes (4K only) */
#define	SPRN_TSCR	0x399   /* Thread switch control on BE */
#define	SPRN_TTR	0x39A   /* Thread switch timeout on BE */
#define	  TSCR_DEC_ENABLE	0x200000 /* Decrementer Interrupt */
#define	  TSCR_EE_ENABLE	0x100000 /* External Interrupt */
#define	  TSCR_EE_BOOST		0x080000 /* External Interrupt Boost */
#define	SPRN_TSC 	0x3FD	/* Thread switch control on others */
#define	SPRN_TST 	0x3FC	/* Thread switch timeout on others */
#define	SPRN_L2CR	0x3F9	/* Level 2 Cache Control Regsiter */
#define	SPRN_LR		0x008	/* Link Register */
#define	SPRN_PIR	0x3FF	/* Processor Identification Register */
#define	SPRN_PIT	0x3DB	/* Programmable Interval Timer */
#define	SPRN_PURR	0x135	/* Processor Utilization of Resources Register */
#define	SPRN_PVR	0x11F	/* Processor Version Register */
#define	SPRN_RPA	0x3D6	/* Required Physical Address Register */
#define	SPRN_SDA	0x3BF	/* Sampled Data Address Register */
#define	SPRN_SDR1	0x019	/* MMU Hash Base Register */
#define	SPRN_SIA	0x3BB	/* Sampled Instruction Address Register */
#define	SPRN_SPRG0	0x110	/* Special Purpose Register General 0 */
#define	SPRN_SPRG1	0x111	/* Special Purpose Register General 1 */
#define	SPRN_SPRG2	0x112	/* Special Purpose Register General 2 */
#define	SPRN_SPRG3	0x113	/* Special Purpose Register General 3 */
#define	SPRN_SRR0	0x01A	/* Save/Restore Register 0 */
#define	SPRN_SRR1	0x01B	/* Save/Restore Register 1 */
#define	SPRN_TBRL	0x10C	/* Time Base Read Lower Register (user, R/O) */
#define	SPRN_TBRU	0x10D	/* Time Base Read Upper Register (user, R/O) */
#define	SPRN_TBWL	0x11C	/* Time Base Lower Register (super, W/O) */
#define	SPRN_TBWU	0x11D	/* Time Base Write Upper Register (super, W/O) */
#define SPRN_HIOR	0x137	/* 970 Hypervisor interrupt offset */
#define	SPRN_USIA	0x3AB	/* User Sampled Instruction Address Register */
#define	SPRN_XER	0x001	/* Fixed Point Exception Register */
#define SPRN_VRSAVE     0x100   /* Vector save */
#define SPRN_CTRLF	0x088
#define SPRN_CTRLT	0x098
#define   CTRL_RUNLATCH	0x1

/* Performance monitor SPRs */
#define SPRN_SIAR	780
#define SPRN_SDAR	781
#define SPRN_MMCRA	786
#define   MMCRA_SIHV	0x10000000UL /* state of MSR HV when SIAR set */
#define   MMCRA_SIPR	0x08000000UL /* state of MSR PR when SIAR set */
#define   MMCRA_SAMPLE_ENABLE 0x00000001UL /* enable sampling */
#define SPRN_PMC1	787
#define SPRN_PMC2	788
#define SPRN_PMC3	789
#define SPRN_PMC4	790
#define SPRN_PMC5	791
#define SPRN_PMC6	792
#define SPRN_PMC7	793
#define SPRN_PMC8	794
#define SPRN_MMCR0	795
#define   MMCR0_FC	0x80000000UL /* freeze counters. set to 1 on a perfmon exception */
#define   MMCR0_FCS	0x40000000UL /* freeze in supervisor state */
#define   MMCR0_KERNEL_DISABLE MMCR0_FCS
#define   MMCR0_FCP	0x20000000UL /* freeze in problem state */
#define   MMCR0_PROBLEM_DISABLE MMCR0_FCP
#define   MMCR0_FCM1	0x10000000UL /* freeze counters while MSR mark = 1 */
#define   MMCR0_FCM0	0x08000000UL /* freeze counters while MSR mark = 0 */
#define   MMCR0_PMXE	0x04000000UL /* performance monitor exception enable */
#define   MMCR0_FCECE	0x02000000UL /* freeze counters on enabled condition or event */
/* time base exception enable */
#define   MMCR0_TBEE	0x00400000UL /* time base exception enable */
#define   MMCR0_PMC1CE	0x00008000UL /* PMC1 count enable*/
#define   MMCR0_PMCjCE	0x00004000UL /* PMCj count enable*/
#define   MMCR0_TRIGGER	0x00002000UL /* TRIGGER enable */
#define   MMCR0_PMAO	0x00000080UL /* performance monitor alert has occurred, set to 0 after handling exception */
#define   MMCR0_SHRFC	0x00000040UL /* SHRre freeze conditions between threads */
#define   MMCR0_FCTI	0x00000008UL /* freeze counters in tags inactive mode */
#define   MMCR0_FCTA	0x00000004UL /* freeze counters in tags active mode */
#define   MMCR0_FCWAIT	0x00000002UL /* freeze counter in WAIT state */
#define   MMCR0_FCHV	0x00000001UL /* freeze conditions in hypervisor mode */
#define SPRN_MMCR1	798

/* Short-hand versions for a number of the above SPRNs */

#define	CTR	SPRN_CTR	/* Counter Register */
#define	DAR	SPRN_DAR	/* Data Address Register */
#define	DABR	SPRN_DABR	/* Data Address Breakpoint Register */
#define	DEC	SPRN_DEC       	/* Decrement Register */
#define	DSISR	SPRN_DSISR	/* Data Storage Interrupt Status Register */
#define	HID0	SPRN_HID0	/* Hardware Implementation Register 0 */
#define	MSRDORM	SPRN_MSRDORM	/* MSR Dormant Register */
#define	NIADORM	SPRN_NIADORM	/* NIA Dormant Register */
#define	TSC    	SPRN_TSC 	/* Thread switch control */
#define	TST    	SPRN_TST 	/* Thread switch timeout */
#define	IABR	SPRN_IABR      	/* Instruction Address Breakpoint Register */
#define	L2CR	SPRN_L2CR    	/* PPC 750 L2 control register */
#define	__LR	SPRN_LR
#define	PVR	SPRN_PVR	/* Processor Version */
#define	PIR	SPRN_PIR	/* Processor ID */
#define	PURR	SPRN_PURR	/* Processor Utilization of Resource Register */
#define	SDR1	SPRN_SDR1      	/* MMU hash base register */
#define	SPR0	SPRN_SPRG0	/* Supervisor Private Registers */
#define	SPR1	SPRN_SPRG1
#define	SPR2	SPRN_SPRG2
#define	SPR3	SPRN_SPRG3
#define	SPRG0   SPRN_SPRG0
#define	SPRG1   SPRN_SPRG1
#define	SPRG2   SPRN_SPRG2
#define	SPRG3   SPRN_SPRG3
#define	SRR0	SPRN_SRR0	/* Save and Restore Register 0 */
#define	SRR1	SPRN_SRR1	/* Save and Restore Register 1 */
#define	TBRL	SPRN_TBRL	/* Time Base Read Lower Register */
#define	TBRU	SPRN_TBRU	/* Time Base Read Upper Register */
#define	TBWL	SPRN_TBWL	/* Time Base Write Lower Register */
#define	TBWU	SPRN_TBWU	/* Time Base Write Upper Register */
#define	XER	SPRN_XER

/* Processor Version Register (PVR) field extraction */

#define	PVR_VER(pvr)  (((pvr) >>  16) & 0xFFFF)	/* Version field */
#define	PVR_REV(pvr)  (((pvr) >>   0) & 0xFFFF)	/* Revison field */

/* Processor Version Numbers */
#define	PV_NORTHSTAR	0x0033
#define	PV_PULSAR	0x0034
#define	PV_POWER4	0x0035
#define	PV_ICESTAR	0x0036
#define	PV_SSTAR	0x0037
#define	PV_POWER4p	0x0038
#define PV_970		0x0039
#define	PV_POWER5	0x003A
#define PV_POWER5p	0x003B
#define PV_970FX	0x003C
#define	PV_630        	0x0040
#define	PV_630p	        0x0041
#define	PV_970MP	0x0044
#define	PV_BE		0x0070

/* Platforms supported by PPC64 */
#define PLATFORM_PSERIES      0x0100
#define PLATFORM_PSERIES_LPAR 0x0101
#define PLATFORM_ISERIES_LPAR 0x0201
#define PLATFORM_LPAR         0x0001
#define PLATFORM_POWERMAC     0x0400
#define PLATFORM_MAPLE        0x0500
#define PLATFORM_BPA          0x1000

/* Compatibility with drivers coming from PPC32 world */
#define _machine	(systemcfg->platform)
#define _MACH_Pmac	PLATFORM_POWERMAC

/*
 * List of interrupt controllers.
 */
#define IC_INVALID    0
#define IC_OPEN_PIC   1
#define IC_PPC_XIC    2
#define IC_BPA_IIC    3

#define XGLUE(a,b) a##b
#define GLUE(a,b) XGLUE(a,b)

#ifdef __ASSEMBLY__

#define _GLOBAL(name) \
	.section ".text"; \
	.align 2 ; \
	.globl name; \
	.globl GLUE(.,name); \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _KPROBE(name) \
	.section ".kprobes.text","a"; \
	.align 2 ; \
	.globl name; \
	.globl GLUE(.,name); \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#define _STATIC(name) \
	.section ".text"; \
	.align 2 ; \
	.section ".opd","aw"; \
name: \
	.quad GLUE(.,name); \
	.quad .TOC.@tocbase; \
	.quad 0; \
	.previous; \
	.type GLUE(.,name),@function; \
GLUE(.,name):

#else /* __ASSEMBLY__ */

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

/* Macros for setting and retrieving special purpose registers */

#define mfmsr()		({unsigned long rval; \
			asm volatile("mfmsr %0" : "=r" (rval)); rval;})

#define __mtmsrd(v, l)	asm volatile("mtmsrd %0," __stringify(l) \
				     : : "r" (v))
#define mtmsrd(v)	__mtmsrd((v), 0)

#define mfspr(rn)	({unsigned long rval; \
			asm volatile("mfspr %0," __stringify(rn) \
				     : "=r" (rval)); rval;})
#define mtspr(rn, v)	asm volatile("mtspr " __stringify(rn) ",%0" : : "r" (v))

#define mftb()		({unsigned long rval;	\
			asm volatile("mftb %0" : "=r" (rval)); rval;})

#define mttbl(v)	asm volatile("mttbl %0":: "r"(v))
#define mttbu(v)	asm volatile("mttbu %0":: "r"(v))

#define mfasr()		({unsigned long rval; \
			asm volatile("mfasr %0" : "=r" (rval)); rval;})

static inline void set_tb(unsigned int upper, unsigned int lower)
{
	mttbl(0);
	mttbu(upper);
	mttbl(lower);
}

#define __get_SP()	({unsigned long sp; \
			asm volatile("mr %0,1": "=r" (sp)); sp;})

#ifdef __KERNEL__

extern int have_of;
extern u64 ppc64_interrupt_controller;

struct task_struct;
void start_thread(struct pt_regs *regs, unsigned long fdptr, unsigned long sp);
void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
extern void prepare_to_copy(struct task_struct *tsk);

/* Create a new kernel thread. */
extern long kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;
extern struct task_struct *last_task_used_altivec;

/* 64-bit user address space is 44-bits (16TB user VM) */
#define TASK_SIZE_USER64 (0x0000100000000000UL)

/* 
 * 32-bit user address space is 4GB - 1 page 
 * (this 1 page is needed so referencing of 0xFFFFFFFF generates EFAULT
 */
#define TASK_SIZE_USER32 (0x0000000100000000UL - (1*PAGE_SIZE))

#define TASK_SIZE (test_thread_flag(TIF_32BIT) ? \
		TASK_SIZE_USER32 : TASK_SIZE_USER64)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE_USER32 (PAGE_ALIGN(TASK_SIZE_USER32 / 4))
#define TASK_UNMAPPED_BASE_USER64 (PAGE_ALIGN(TASK_SIZE_USER64 / 4))

#define TASK_UNMAPPED_BASE ((test_thread_flag(TIF_32BIT)||(ppcdebugset(PPCDBG_BINFMT_32ADDR))) ? \
		TASK_UNMAPPED_BASE_USER32 : TASK_UNMAPPED_BASE_USER64 )

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct thread_struct {
	unsigned long	ksp;		/* Kernel stack pointer */
	unsigned long	ksp_vsid;
	struct pt_regs	*regs;		/* Pointer to saved register state */
	mm_segment_t	fs;		/* for get_fs() validation */
	double		fpr[32];	/* Complete floating point set */
	unsigned long	fpscr;		/* Floating point status (plus pad) */
	unsigned long	fpexc_mode;	/* Floating-point exception mode */
	unsigned long	start_tb;	/* Start purr when proc switched in */
	unsigned long	accum_tb;	/* Total accumilated purr for process */
	unsigned long	vdso_base;	/* base of the vDSO library */
	unsigned long	dabr;		/* Data address breakpoint register */
#ifdef CONFIG_ALTIVEC
	/* Complete AltiVec register set */
	vector128	vr[32] __attribute((aligned(16)));
	/* AltiVec status */
	vector128	vscr __attribute((aligned(16)));
	unsigned long	vrsave;
	int		used_vr;	/* set if process has used altivec */
#endif /* CONFIG_ALTIVEC */
};

#define ARCH_MIN_TASKALIGN 16

#define INIT_SP		(sizeof(init_stack) + (unsigned long) &init_stack)

#define INIT_THREAD  { \
	.ksp = INIT_SP, \
	.regs = (struct pt_regs *)INIT_SP - 1, \
	.fs = KERNEL_DS, \
	.fpr = {0}, \
	.fpscr = 0, \
	.fpexc_mode = MSR_FE0|MSR_FE1, \
}

/*
 * Return saved PC of a blocked thread. For now, this is the "user" PC
 */
#define thread_saved_pc(tsk)    \
        ((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)
#define KSTK_ESP(tsk)  ((tsk)->thread.regs? (tsk)->thread.regs->gpr[1]: 0)

/* Get/set floating-point exception mode */
#define GET_FPEXC_CTL(tsk, adr) get_fpexc_mode((tsk), (adr))
#define SET_FPEXC_CTL(tsk, val) set_fpexc_mode((tsk), (val))

extern int get_fpexc_mode(struct task_struct *tsk, unsigned long adr);
extern int set_fpexc_mode(struct task_struct *tsk, unsigned int val);

static inline unsigned int __unpack_fe01(unsigned long msr_bits)
{
	return ((msr_bits & MSR_FE0) >> 10) | ((msr_bits & MSR_FE1) >> 8);
}

static inline unsigned long __pack_fe01(unsigned int fpmode)
{
	return ((fpmode << 10) & MSR_FE0) | ((fpmode << 8) & MSR_FE1);
}

#define cpu_relax()	do { HMT_low(); HMT_medium(); barrier(); } while (0)

/*
 * Prefetch macros.
 */
#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

static inline void prefetch(const void *x)
{
	if (unlikely(!x))
		return;

	__asm__ __volatile__ ("dcbt 0,%0" : : "r" (x));
}

static inline void prefetchw(const void *x)
{
	if (unlikely(!x))
		return;

	__asm__ __volatile__ ("dcbtst 0,%0" : : "r" (x));
}

#define spin_lock_prefetch(x)	prefetchw(x)

#define HAVE_ARCH_PICK_MMAP_LAYOUT

static inline void ppc64_runlatch_on(void)
{
	unsigned long ctrl;

	if (cpu_has_feature(CPU_FTR_CTRL)) {
		ctrl = mfspr(SPRN_CTRLF);
		ctrl |= CTRL_RUNLATCH;
		mtspr(SPRN_CTRLT, ctrl);
	}
}

static inline void ppc64_runlatch_off(void)
{
	unsigned long ctrl;

	if (cpu_has_feature(CPU_FTR_CTRL)) {
		ctrl = mfspr(SPRN_CTRLF);
		ctrl &= ~CTRL_RUNLATCH;
		mtspr(SPRN_CTRLT, ctrl);
	}
}

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
#define RUNLATCH_ON(REG)			\
BEGIN_FTR_SECTION				\
	mfspr	(REG),SPRN_CTRLF;		\
	ori	(REG),(REG),CTRL_RUNLATCH;	\
	mtspr	SPRN_CTRLT,(REG);		\
END_FTR_SECTION_IFSET(CPU_FTR_CTRL)
#endif

/*
 * Number of entries in the SLB. If this ever changes we should handle
 * it with a use a cpu feature fixup.
 */
#define SLB_NUM_ENTRIES 64

#endif /* __ASM_PPC64_PROCESSOR_H */
