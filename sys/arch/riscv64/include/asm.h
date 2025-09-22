/*	$OpenBSD: asm.h,v 1.8 2022/12/06 00:08:38 jca Exp $	*/

/*
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	from: @(#)asm.h	5.5 (Berkeley) 5/7/91
 */

#ifndef _MACHINE_ASM_H_
#define	_MACHINE_ASM_H_

#define _C_LABEL(x)	x
#define _ASM_LABEL(x)	x

#ifdef __STDC__
# define __CONCAT(x,y)	x ## y
# define __STRING(x)	#x
#else
# define __CONCAT(x,y)	x/**/y
# define __STRING(x)	"x"
#endif

#ifndef _ALIGN_TEXT
# define _ALIGN_TEXT .align 0
#endif

/* NB == No Binding: use .globl or .weak as necessary */
#define _ENTRY_NB(x) \
	.text; .p2align 1; .type x,@function; x:
#define _ENTRY(x)	.globl x; _ENTRY_NB(x)

#if defined(PROF) || defined(GPROF)
// XXX Profiler Support
#define _PROF_PROLOGUE			\
	addi	sp, sp, -16;		\
	sd	ra, 8(sp);		\
	sd	fp, 0(sp);		\
	mv	fp, sp;			\
	call	__mcount;		\
	ld	ra, 8(sp);		\
	ld	fp, 0(sp);		\
	add	sp, sp, 16;
#else
#define _PROF_PROLOGUE
#endif

#if defined(_RET_PROTECTOR)
// XXX Retguard Support
#error RETGUARD not yet supported for riscv64
#else
#define RETGUARD_CALC_COOKIE(reg)
#define RETGUARD_LOAD_RANDOM(x, reg)
#define RETGUARD_SETUP(x, reg)
#define RETGUARD_CHECK(x, reg)
#define RETGUARD_PUSH(reg)
#define RETGUARD_POP(reg)
#define RETGUARD_SYMBOL(x)
#endif

#define	ENTRY(y)	_ENTRY(y); _PROF_PROLOGUE
#define	ENTRY_NP(y)	_ENTRY(y)
#define	ENTRY_NB(y)	_ENTRY_NB(y); _PROF_PROLOGUE
#define	ASENTRY(y)	_ENTRY(y); _PROF_PROLOGUE
#define	ASENTRY_NP(y)	_ENTRY(y)
#define	END(y)		.size y, . - y
#define EENTRY(sym)	 .globl  sym; sym:
#define EEND(sym)

#ifdef __PIC__
#define	PIC_SYM(x,y)	x(y)
#else
#define	PIC_SYM(x,y)	x
#endif

#define	STRONG_ALIAS(alias,sym)						\
	.global alias;							\
	alias = sym
#define	WEAK_ALIAS(alias,sym)						\
	.weak alias;							\
	alias = sym

#define	SWAP_FAULT_HANDLER(handler, tmp0, tmp1)			\
	ld	tmp0, CI_CURPCB(tp);		/* Load the pcb */	\
	ld	tmp1, PCB_ONFAULT(tmp0);	/* Save old handler */	\
	sd	handler, PCB_ONFAULT(tmp0);	/* Set the handler */	\
	mv	handler, tmp1

#define	SET_FAULT_HANDLER(handler, pcb)					\
	ld	pcb, CI_CURPCB(tp);		/* Load the pcb */	\
	sd	handler, PCB_ONFAULT(pcb)	/* Set the handler */

#define	ENTER_USER_ACCESS(tmp)						\
	li	tmp, SSTATUS_SUM;					\
	csrs	sstatus, tmp

#define	EXIT_USER_ACCESS(tmp)						\
	li	tmp, SSTATUS_SUM;					\
	csrc	sstatus, tmp

#endif /* _MACHINE_ASM_H_ */
