/*
 * include/asm-v850/ptrace.h -- Access to CPU registers
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_PTRACE_H__
#define __V850_PTRACE_H__


/* v850 general purpose registers with special meanings.  */
#define GPR_ZERO	0	/* constant zero */
#define GPR_ASM		1	/* reserved for assembler */
#define GPR_SP		3	/* stack pointer */
#define GPR_GP		4	/* global data pointer */
#define GPR_TP		5	/* `text pointer' */
#define GPR_EP		30	/* `element pointer' */
#define GPR_LP		31	/* link pointer (current return address) */

/* These aren't official names, but they make some code more descriptive.  */
#define GPR_ARG0	6
#define GPR_ARG1	7
#define GPR_ARG2	8
#define GPR_ARG3	9
#define GPR_RVAL0	10
#define GPR_RVAL1	11
#define GPR_RVAL	GPR_RVAL0

#define NUM_GPRS	32

/* v850 `system' registers.  */
#define SR_EIPC		0
#define SR_EIPSW	1
#define SR_FEPC		2
#define SR_FEPSW	3
#define SR_ECR		4
#define SR_PSW		5
#define SR_CTPC		16
#define SR_CTPSW	17
#define SR_DBPC		18
#define SR_DBPSW	19
#define SR_CTBP		20
#define SR_DIR		21
#define SR_ASID		23


#ifndef __ASSEMBLY__

typedef unsigned long v850_reg_t;

/* How processor state is stored on the stack during a syscall/signal.
   If you change this structure, change the associated assembly-language
   macros below too (PT_*)!  */
struct pt_regs
{
	/* General purpose registers.  */
	v850_reg_t gpr[NUM_GPRS];

	v850_reg_t pc;		/* program counter */
	v850_reg_t psw;		/* program status word */

	/* Registers used by `callt' instruction:  */
	v850_reg_t ctpc;	/* saved program counter */
	v850_reg_t ctpsw;	/* saved psw */
	v850_reg_t ctbp;	/* base pointer for callt table */

	char kernel_mode;	/* 1 if in `kernel mode', 0 if user mode */
};


#define instruction_pointer(regs)	((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)
#define user_mode(regs)			(!(regs)->kernel_mode)

/* When a struct pt_regs is used to save user state for a system call in
   the kernel, the system call is stored in the space for R0 (since it's
   never used otherwise, R0 being a constant 0).  Non-system-calls
   simply store 0 there.  */
#define PT_REGS_SYSCALL(regs)		(regs)->gpr[0]
#define PT_REGS_SET_SYSCALL(regs, val)	((regs)->gpr[0] = (val))

#endif /* !__ASSEMBLY__ */


/* The number of bytes used to store each register.  */
#define _PT_REG_SIZE	4

/* Offset of a general purpose register in a stuct pt_regs.  */
#define PT_GPR(num)	((num) * _PT_REG_SIZE)

/* Offsets of various special registers & fields in a struct pt_regs.  */
#define PT_PC		((NUM_GPRS + 0) * _PT_REG_SIZE)
#define PT_PSW		((NUM_GPRS + 1) * _PT_REG_SIZE)
#define PT_CTPC		((NUM_GPRS + 2) * _PT_REG_SIZE)
#define PT_CTPSW	((NUM_GPRS + 3) * _PT_REG_SIZE)
#define PT_CTBP		((NUM_GPRS + 4) * _PT_REG_SIZE)
#define PT_KERNEL_MODE	((NUM_GPRS + 5) * _PT_REG_SIZE)

/* Where the current syscall number is stashed; obviously only valid in
   the kernel!  */
#define PT_CUR_SYSCALL	PT_GPR(0)

/* Size of struct pt_regs, including alignment.  */
#define PT_SIZE		((NUM_GPRS + 6) * _PT_REG_SIZE)


/* These are `magic' values for PTRACE_PEEKUSR that return info about where
   a process is located in memory.  */
#define PT_TEXT_ADDR	(PT_SIZE + 1)
#define PT_TEXT_LEN	(PT_SIZE + 2)
#define PT_DATA_ADDR	(PT_SIZE + 3)


#endif /* __V850_PTRACE_H__ */
