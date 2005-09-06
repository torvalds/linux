/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#include <linux/config.h>

#include <asm/isadep.h>

/* 0 - 31 are integer registers, 32 - 63 are fp registers.  */
#define FPR_BASE	32
#define PC		64
#define CAUSE		65
#define BADVADDR	66
#define MMHI		67
#define MMLO		68
#define FPC_CSR		69
#define FPC_EIR		70

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
#ifdef CONFIG_32BIT
	/* Pad bytes for argument save space on the stack. */
	unsigned long pad0[6];
#endif

	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Saved special registers. */
	unsigned long cp0_status;
	unsigned long lo;
	unsigned long hi;
	unsigned long cp0_badvaddr;
	unsigned long cp0_cause;
	unsigned long cp0_epc;
};

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
/* #define PTRACE_GETREGS		12 */
/* #define PTRACE_SETREGS		13 */
/* #define PTRACE_GETFPREGS		14 */
/* #define PTRACE_SETFPREGS		15 */
/* #define PTRACE_GETFPXREGS		18 */
/* #define PTRACE_SETFPXREGS		19 */

#define PTRACE_OLDSETOPTIONS	21

#define PTRACE_GET_THREAD_AREA	25
#define PTRACE_SET_THREAD_AREA	26

#ifdef __KERNEL__

#include <linux/linkage.h>

/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) (((regs)->cp0_status & KU_MASK) == KU_USER)

#define instruction_pointer(regs) ((regs)->cp0_epc)
#define profile_pc(regs) instruction_pointer(regs)

extern void show_regs(struct pt_regs *);

extern asmlinkage void do_syscall_trace(struct pt_regs *regs, int entryexit);

#endif

#endif /* _ASM_PTRACE_H */
