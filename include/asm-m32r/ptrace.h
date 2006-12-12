#ifndef _ASM_M32R_PTRACE_H
#define _ASM_M32R_PTRACE_H

/*
 * linux/include/asm-m32r/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * M32R version:
 *   Copyright (C) 2001-2002, 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

/* 0 - 13 are integer registers (general purpose registers).  */
#define PT_R4		0
#define PT_R5		1
#define PT_R6		2
#define PT_REGS 	3
#define PT_R0		4
#define PT_R1		5
#define PT_R2		6
#define PT_R3		7
#define PT_R7		8
#define PT_R8		9
#define PT_R9		10
#define PT_R10		11
#define PT_R11		12
#define PT_R12		13
#define PT_SYSCNR	14
#define PT_R13		PT_FP
#define PT_R14		PT_LR
#define PT_R15		PT_SP

/* processor status and miscellaneous context registers.  */
#define PT_ACC0H	15
#define PT_ACC0L	16
#define PT_ACC1H	17	/* ISA_DSP_LEVEL2 only */
#define PT_ACC1L	18	/* ISA_DSP_LEVEL2 only */
#define PT_PSW		19
#define PT_BPC		20
#define PT_BBPSW	21
#define PT_BBPC		22
#define PT_SPU		23
#define PT_FP		24
#define PT_LR		25
#define PT_SPI		26
#define PT_ORIGR0	27

/* virtual pt_reg entry for gdb */
#define PT_PC		30
#define PT_CBR		31
#define PT_EVB		32


/* Control registers.  */
#define SPR_CR0 PT_PSW
#define SPR_CR1 PT_CBR		/* read only */
#define SPR_CR2 PT_SPI
#define SPR_CR3 PT_SPU
#define SPR_CR4
#define SPR_CR5 PT_EVB		/* part of M32R/E, M32R/I core only */
#define SPR_CR6 PT_BPC
#define SPR_CR7
#define SPR_CR8 PT_BBPSW
#define SPR_CR9
#define SPR_CR10
#define SPR_CR11
#define SPR_CR12
#define SPR_CR13 PT_WR
#define SPR_CR14 PT_BBPC
#define SPR_CR15

/* this struct defines the way the registers are stored on the
   stack during a system call. */
struct pt_regs {
	/* Saved main processor registers. */
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	struct pt_regs *pt_regs;
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	long syscall_nr;

	/* Saved main processor status and miscellaneous context registers. */
	unsigned long acc0h;
	unsigned long acc0l;
	unsigned long acc1h;	/* ISA_DSP_LEVEL2 only */
	unsigned long acc1l;	/* ISA_DSP_LEVEL2 only */
	unsigned long psw;
	unsigned long bpc;		/* saved PC for TRAP syscalls */
	unsigned long bbpsw;
	unsigned long bbpc;
	unsigned long spu;		/* saved user stack */
	unsigned long fp;
	unsigned long lr;		/* saved PC for JL syscalls */
	unsigned long spi;		/* saved kernel stack */
	unsigned long orig_r0;
};

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13

#define PTRACE_OLDSETOPTIONS	21

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD	0x00000001

#ifdef __KERNEL__

#include <asm/m32r.h>		/* M32R_PSW_BSM, M32R_PSW_BPM */

#define __ARCH_SYS_PTRACE	1

#if defined(CONFIG_ISA_M32R2) || defined(CONFIG_CHIP_VDEC2)
#define user_mode(regs) ((M32R_PSW_BPM & (regs)->psw) != 0)
#elif defined(CONFIG_ISA_M32R)
#define user_mode(regs) ((M32R_PSW_BSM & (regs)->psw) != 0)
#else
#error unknown isa configuration
#endif

#define instruction_pointer(regs) ((regs)->bpc)
#define profile_pc(regs) instruction_pointer(regs)

extern void show_regs(struct pt_regs *);

extern void withdraw_debug_trap(struct pt_regs *regs);

#define task_pt_regs(task) \
        ((struct pt_regs *)(task_stack_page(task) + THREAD_SIZE) - 1)

#endif /* __KERNEL */

#endif /* _ASM_M32R_PTRACE_H */
