/* registers.h: register frame declarations
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * notes:
 *
 * (1) that the members of all these structures are carefully aligned to permit
 *     usage of STD/STDF instructions
 *
 * (2) if you change these structures, you must change the code in
 *     arch/frvnommu/kernel/{break.S,entry.S,switch_to.S,gdb-stub.c}
 *
 *
 * the kernel stack space block looks like this:
 *
 *	+0x2000	+----------------------
 *		| union {
 *		|	struct user_context
 *		|	struct pt_regs [user exception]
 *		| }
 *		+---------------------- <-- __kernel_frame0_ptr (maybe GR28)
 *		|
 *		| kernel stack
 *		|
 *		|......................
 *		| struct pt_regs [kernel exception]
 *		|...................... <-- __kernel_frame0_ptr (maybe GR28)
 *		|
 *		| kernel stack
 *		|
 *		|...................... <-- stack pointer (GR1)
 *		|
 *		| unused stack space
 *		|
 *		+----------------------
 *		| struct thread_info
 *	+0x0000	+---------------------- <-- __current_thread_info (GR15);
 *
 * note that GR28 points to the current exception frame
 */

#ifndef _ASM_REGISTERS_H
#define _ASM_REGISTERS_H

#ifndef __ASSEMBLY__
#define __OFFSET(X)	(X)
#define __OFFSETC(X,N)	xxxxxxxxxxxxxxxxxxxxxxxx
#else
#define __OFFSET(X)	((X)*4)
#define __OFFSETC(X,N)	((X)*4+(N))
#endif

/*****************************************************************************/
/*
 * Exception/Interrupt frame
 * - held on kernel stack
 * - 8-byte aligned on stack (old SP is saved in frame)
 * - GR0 is fixed 0, so we don't save it
 */
#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long		psr;		/* Processor Status Register */
	unsigned long		isr;		/* Integer Status Register */
	unsigned long		ccr;		/* Condition Code Register */
	unsigned long		cccr;		/* Condition Code for Conditional Insns Register */
	unsigned long		lr;		/* Link Register */
	unsigned long		lcr;		/* Loop Count Register */
	unsigned long		pc;		/* Program Counter Register */
	unsigned long		__status;	/* exception status */
	unsigned long		syscallno;	/* syscall number or -1 */
	unsigned long		orig_gr8;	/* original syscall arg #1 */
	unsigned long		gner0;
	unsigned long		gner1;
	unsigned long long	iacc0;
	unsigned long		tbr;		/* GR0 is fixed zero, so we use this for TBR */
	unsigned long		sp;		/* GR1: USP/KSP */
	unsigned long		fp;		/* GR2: FP */
	unsigned long		gr3;
	unsigned long		gr4;
	unsigned long		gr5;
	unsigned long		gr6;
	unsigned long		gr7;		/* syscall number */
	unsigned long		gr8;		/* 1st syscall param; syscall return */
	unsigned long		gr9;		/* 2nd syscall param */
	unsigned long		gr10;		/* 3rd syscall param */
	unsigned long		gr11;		/* 4th syscall param */
	unsigned long		gr12;		/* 5th syscall param */
	unsigned long		gr13;		/* 6th syscall param */
	unsigned long		gr14;
	unsigned long		gr15;
	unsigned long		gr16;		/* GP pointer */
	unsigned long		gr17;		/* small data */
	unsigned long		gr18;		/* PIC/PID */
	unsigned long		gr19;
	unsigned long		gr20;
	unsigned long		gr21;
	unsigned long		gr22;
	unsigned long		gr23;
	unsigned long		gr24;
	unsigned long		gr25;
	unsigned long		gr26;
	unsigned long		gr27;
	struct pt_regs		*next_frame;	/* GR28 - next exception frame */
	unsigned long		gr29;		/* GR29 - OS reserved */
	unsigned long		gr30;		/* GR30 - OS reserved */
	unsigned long		gr31;		/* GR31 - OS reserved */
} __attribute__((aligned(8)));

#endif

#define REG_PSR		__OFFSET( 0)	/* Processor Status Register */
#define REG_ISR		__OFFSET( 1)	/* Integer Status Register */
#define REG_CCR		__OFFSET( 2)	/* Condition Code Register */
#define REG_CCCR	__OFFSET( 3)	/* Condition Code for Conditional Insns Register */
#define REG_LR		__OFFSET( 4)	/* Link Register */
#define REG_LCR		__OFFSET( 5)	/* Loop Count Register */
#define REG_PC		__OFFSET( 6)	/* Program Counter */

#define REG__STATUS	__OFFSET( 7)	/* exception status */
#define REG__STATUS_STEP	0x00000001	/* - reenable single stepping on return */
#define REG__STATUS_STEPPED	0x00000002	/* - single step caused exception */
#define REG__STATUS_BROKE	0x00000004	/* - BREAK insn caused exception */
#define REG__STATUS_SYSC_ENTRY	0x40000000	/* - T on syscall entry (ptrace.c only) */
#define REG__STATUS_SYSC_EXIT	0x80000000	/* - T on syscall exit (ptrace.c only) */

#define REG_SYSCALLNO	__OFFSET( 8)	/* syscall number or -1 */
#define REG_ORIG_GR8	__OFFSET( 9)	/* saved GR8 for signal handling */
#define REG_GNER0	__OFFSET(10)
#define REG_GNER1	__OFFSET(11)
#define REG_IACC0	__OFFSET(12)

#define REG_TBR		__OFFSET(14)	/* Trap Vector Register */
#define REG_GR(R)	__OFFSET((14+(R)))
#define REG__END	REG_GR(32)

#define REG_SP		REG_GR(1)
#define REG_FP		REG_GR(2)
#define REG_PREV_FRAME	REG_GR(28)	/* previous exception frame pointer (old gr28 value) */
#define REG_CURR_TASK	REG_GR(29)	/* current task */

/*****************************************************************************/
/*
 * extension tacked in front of the exception frame in debug mode
 */
#ifndef __ASSEMBLY__

struct pt_debug_regs
{
	unsigned long		bpsr;
	unsigned long		dcr;
	unsigned long		brr;
	unsigned long		nmar;
	struct pt_regs		normal_regs;
} __attribute__((aligned(8)));

#endif

#define REG_NMAR		__OFFSET(-1)
#define REG_BRR			__OFFSET(-2)
#define REG_DCR			__OFFSET(-3)
#define REG_BPSR		__OFFSET(-4)
#define REG__DEBUG_XTRA		__OFFSET(4)

/*****************************************************************************/
/*
 * userspace registers
 */
#ifndef __ASSEMBLY__

struct user_int_regs
{
	/* integer registers
	 * - up to gr[31] mirror pt_regs
	 * - total size must be multiple of 8 bytes
	 */
	unsigned long		psr;		/* Processor Status Register */
	unsigned long		isr;		/* Integer Status Register */
	unsigned long		ccr;		/* Condition Code Register */
	unsigned long		cccr;		/* Condition Code for Conditional Insns Register */
	unsigned long		lr;		/* Link Register */
	unsigned long		lcr;		/* Loop Count Register */
	unsigned long		pc;		/* Program Counter Register */
	unsigned long		__status;	/* exception status */
	unsigned long		syscallno;	/* syscall number or -1 */
	unsigned long		orig_gr8;	/* original syscall arg #1 */
	unsigned long		gner[2];
	unsigned long long	iacc[1];

	union {
		unsigned long	tbr;
		unsigned long	gr[64];
	};
};

struct user_fpmedia_regs
{
	/* FP/Media registers */
	unsigned long	fr[64];
	unsigned long	fner[2];
	unsigned long	msr[2];
	unsigned long	acc[8];
	unsigned char	accg[8];
	unsigned long	fsr[1];
};

struct user_context
{
	struct user_int_regs		i;
	struct user_fpmedia_regs	f;

	/* we provide a context extension so that we can save the regs for CPUs that
	 * implement many more of Fujitsu's lavish register spec
	 */
	void *extension;
} __attribute__((aligned(8)));

#endif

#define NR_USER_INT_REGS	(14 + 64)
#define NR_USER_FPMEDIA_REGS	(64 + 2 + 2 + 8 + 8/4 + 1)
#define NR_USER_CONTEXT		(NR_USER_INT_REGS + NR_USER_FPMEDIA_REGS + 1)

#define USER_CONTEXT_SIZE	(((NR_USER_CONTEXT + 1) & ~1) * 4)

#define __THREAD_FRAME		__OFFSET(0)
#define __THREAD_CURR		__OFFSET(1)
#define __THREAD_SP		__OFFSET(2)
#define __THREAD_FP		__OFFSET(3)
#define __THREAD_LR		__OFFSET(4)
#define __THREAD_PC		__OFFSET(5)
#define __THREAD_GR(R)		__OFFSET(6 + (R) - 16)
#define __THREAD_FRAME0		__OFFSET(19)
#define __THREAD_USER		__OFFSET(19)

#define __USER_INT		__OFFSET(0)
#define __INT_GR(R)		__OFFSET(14 + (R))

#define __USER_FPMEDIA		__OFFSET(NR_USER_INT_REGS)
#define __FPMEDIA_FR(R)		__OFFSET(NR_USER_INT_REGS + (R))
#define __FPMEDIA_FNER(R)	__OFFSET(NR_USER_INT_REGS + 64 + (R))
#define __FPMEDIA_MSR(R)	__OFFSET(NR_USER_INT_REGS + 66 + (R))
#define __FPMEDIA_ACC(R)	__OFFSET(NR_USER_INT_REGS + 68 + (R))
#define __FPMEDIA_ACCG(R)	__OFFSETC(NR_USER_INT_REGS + 76, (R))
#define __FPMEDIA_FSR(R)	__OFFSET(NR_USER_INT_REGS + 78 + (R))

#endif /* _ASM_REGISTERS_H */
