/*	$OpenBSD: trap.h,v 1.17 2022/01/28 16:20:09 visa Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 *	from: Utah Hdr: trap.h 1.1 90/07/09
 *	from: @(#)trap.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _MIPS64_TRAP_H_
#define _MIPS64_TRAP_H_

/*
 * Trap codes (ExcCode in the cause register); also known in trap.c for
 * name strings.
 */

#define T_INT			0	/* Interrupt pending */
#define T_TLB_MOD		1	/* TLB modified fault */
#define T_TLB_LD_MISS		2	/* TLB miss on load or ifetch */
#define T_TLB_ST_MISS		3	/* TLB miss on a store */
#define T_ADDR_ERR_LD		4	/* Address error on a load or ifetch */
#define T_ADDR_ERR_ST		5	/* Address error on a store */
#define T_BUS_ERR_IFETCH	6	/* Bus error on an ifetch */
#define T_BUS_ERR_LD_ST		7	/* Bus error on a load or store */
#define T_SYSCALL		8	/* System call */
#define T_BREAK			9	/* Breakpoint */
#define T_RES_INST		10	/* Reserved instruction exception */
#define T_COP_UNUSABLE		11	/* Coprocessor unusable */
#define T_OVFLOW		12	/* Arithmetic overflow */
#define	T_TRAP			13	/* Trap instruction */
#define	T_VCEI			14	/* R4k Virtual coherency instruction */
#define	T_FPE			15	/* Floating point exception */
#define	T_IWATCH		16	/* R4k Inst. Watch address reference */
#define	T_C2E			18	/* R5k Coprocessor 2 exception */
#define	T_MDMX			22	/* R5k MDMX unusable */
#define	T_DWATCH		23	/* Data Watch address reference */
#define	T_MCHECK		24	/* Machine check */
#define	T_CACHEERR		30	/* Cache error */
#define T_VCED			31	/* R4k Virtual coherency data */

#define	T_USER			0x20	/* user-mode flag or'ed with type */

/*
 *  Defines for trap handler catching kernel accessing memory.
 */
#define	KT_COPYERR	1		/* User space copy error */
#define	KT_KCOPYERR	2		/* Kernel space copy error */
#define	KT_DDBERR	3		/* DDB access error */

#ifndef _LOCORE

#if defined(DDB) || defined(DEBUG)

struct trapdebug {			/* trap history buffer for debugging */
	register_t status;
	register_t cause;
	register_t vadr;
	register_t pc;
	register_t ra;
	register_t sp;
	u_int	code;
	u_int	ipl;
};

#define	trapdebug_enter(ci, frame, cd)					\
do {									\
	register_t sr = disableintr();					\
	u_long cpuid = ci->ci_cpuid;					\
	struct trapdebug *t;						\
									\
	t = trapdebug + TRAPSIZE * cpuid + trppos[cpuid];		\
	t->status = frame->sr;						\
	t->cause = frame->cause;					\
	t->vadr = frame->badvaddr;					\
	t->pc = frame->pc;						\
	t->sp = frame->sp;						\
	t->ra = frame->ra;						\
	t->ipl = frame->ipl;						\
	t->code = cd;							\
	if (++trppos[cpuid] == TRAPSIZE)				\
		trppos[cpuid] = 0;					\
	setsr(sr);							\
} while (0)

#define TRAPSIZE 10		/* Trap log buffer length */
extern struct trapdebug trapdebug[MAXCPUS * TRAPSIZE];
extern uint trppos[MAXCPUS];

void trapDump(const char *, int (*)(const char *, ...));

#else
#define	trapdebug_enter(ci, frame, code)
#endif
#endif /* _LOCORE */

#endif /* !_MIPS64_TRAP_H_ */
