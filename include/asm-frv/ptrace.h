/* ptrace.h: ptrace() relevant definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#include <asm/registers.h>

#define in_syscall(regs) (((regs)->tbr & TBR_TT) == TBR_TT_TRAP0)


#define PT_PSR		0
#define	PT_ISR		1
#define PT_CCR		2
#define PT_CCCR		3
#define PT_LR		4
#define PT_LCR		5
#define PT_PC		6

#define PT__STATUS	7	/* exception status */
#define PT_SYSCALLNO	8	/* syscall number or -1 */
#define PT_ORIG_GR8	9	/* saved GR8 for signal handling */
#define PT_GNER0	10
#define PT_GNER1	11
#define PT_IACC0H	12
#define PT_IACC0L	13

#define PT_GR(j)	( 14 + (j))	/* GRj for 0<=j<=63 */
#define PT_FR(j)	( 78 + (j))	/* FRj for 0<=j<=63 */
#define PT_FNER(j)	(142 + (j))	/* FNERj for 0<=j<=1 */
#define PT_MSR(j)	(144 + (j))	/* MSRj for 0<=j<=2 */
#define PT_ACC(j)	(146 + (j))	/* ACCj for 0<=j<=7 */
#define PT_ACCG(jklm)	(154 + (jklm))	/* ACCGjklm for 0<=jklm<=1 (reads four regs per slot) */
#define PT_FSR(j)	(156 + (j))	/* FSRj for 0<=j<=0 */
#define PT__GPEND	78
#define PT__END		157

#define PT_TBR		PT_GR(0)
#define PT_SP		PT_GR(1)
#define PT_FP		PT_GR(2)
#define PT_PREV_FRAME	PT_GR(28)	/* previous exception frame pointer (old gr28 value) */
#define PT_CURR_TASK	PT_GR(29)	/* current task */


/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15
#define PTRACE_GETFDPIC		31	/* get the ELF fdpic loadmap address */

#define PTRACE_GETFDPIC_EXEC	0	/* [addr] request the executable loadmap */
#define PTRACE_GETFDPIC_INTERP	1	/* [addr] request the interpreter loadmap */

#ifndef __ASSEMBLY__

/*
 * dedicate GR28; to keeping the a pointer to the current exception frame
 */
register struct pt_regs *__frame asm("gr28");
register struct pt_regs *__debug_frame asm("gr31");

#ifndef container_of
#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
        (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define __debug_regs container_of(__debug_frame, struct pt_debug_regs, normal_regs)

#define user_mode(regs)			(!((regs)->psr & PSR_S))
#define instruction_pointer(regs)	((regs)->pc)

extern unsigned long user_stack(const struct pt_regs *);
extern void show_regs(struct pt_regs *);
#define profile_pc(regs) ((regs)->pc)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_PTRACE_H */
