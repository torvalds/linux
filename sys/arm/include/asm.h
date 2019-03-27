/*	$NetBSD: asm.h,v 1.5 2003/08/07 16:26:53 agc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_ASM_H_
#define _MACHINE_ASM_H_
#include <sys/cdefs.h>
#include <machine/sysreg.h>

#define	_C_LABEL(x)	x
#define	_ASM_LABEL(x)	x

#ifndef _ALIGN_TEXT
# define _ALIGN_TEXT .align 2
#endif

#ifndef _STANDALONE
#define	STOP_UNWINDING	.cantunwind
#define	_FNSTART	.fnstart
#define	_FNEND		.fnend
#define	_SAVE(...)	.save __VA_ARGS__
#else
#define	STOP_UNWINDING
#define	_FNSTART
#define	_FNEND
#define	_SAVE(...)
#endif

/*
 * gas/arm uses @ as a single comment character and thus cannot be used here.
 * It recognises the # instead of an @ symbol in .type directives.
 */
#define	_ASM_TYPE_FUNCTION	#function
#define	_ASM_TYPE_OBJECT	#object

/* XXX Is this still the right prologue for profiling? */
#ifdef GPROF
#define	_PROF_PROLOGUE	\
	mov ip, lr;	\
	bl __mcount
#else
#define	_PROF_PROLOGUE
#endif

/*
 * EENTRY()/EEND() mark "extra" entry/exit points from a function.
 * LEENTRY()/LEEND() are the same for local symbols.
 * The unwind info cannot handle the concept of a nested function, or a function
 * with multiple .fnstart directives, but some of our assembler code is written
 * with multiple labels to allow entry at several points.  The EENTRY() macro
 * defines such an extra entry point without a new .fnstart, so that it's
 * basically just a label that you can jump to.  The EEND() macro does nothing
 * at all, except document the exit point associated with the same-named entry.
 */
#define	GLOBAL(x)	.global x

#ifdef __thumb__
#define	_FUNC_MODE	.code 16; .thumb_func
#else
#define	_FUNC_MODE	.code 32
#endif

#define	_LEENTRY(x) 	.type x,_ASM_TYPE_FUNCTION; _FUNC_MODE; x:
#define	_LEEND(x)	/* nothing */
#define	_EENTRY(x) 	GLOBAL(x); _LEENTRY(x)
#define	_EEND(x)	_LEEND(x)

#define	_LENTRY(x)	.text; _ALIGN_TEXT; _LEENTRY(x); _FNSTART
#define	_LEND(x)	.size x, . - x; _FNEND
#define	_ENTRY(x)	.text; _ALIGN_TEXT; _EENTRY(x); _FNSTART
#define	_END(x)		_LEND(x)

#define	ENTRY(y)	_ENTRY(_C_LABEL(y)); _PROF_PROLOGUE
#define	EENTRY(y)	_EENTRY(_C_LABEL(y));
#define	ENTRY_NP(y)	_ENTRY(_C_LABEL(y))
#define	EENTRY_NP(y)	_EENTRY(_C_LABEL(y))
#define	END(y)		_END(_C_LABEL(y))
#define	EEND(y)		_EEND(_C_LABEL(y))
#define	ASENTRY(y)	_ENTRY(_ASM_LABEL(y)); _PROF_PROLOGUE
#define	ASLENTRY(y)	_LENTRY(_ASM_LABEL(y)); _PROF_PROLOGUE
#define	ASEENTRY(y)	_EENTRY(_ASM_LABEL(y));
#define	ASLEENTRY(y)	_LEENTRY(_ASM_LABEL(y));
#define	ASENTRY_NP(y)	_ENTRY(_ASM_LABEL(y))
#define	ASLENTRY_NP(y)	_LENTRY(_ASM_LABEL(y))
#define	ASEENTRY_NP(y)	_EENTRY(_ASM_LABEL(y))
#define	ASLEENTRY_NP(y)	_LEENTRY(_ASM_LABEL(y))
#define	ASEND(y)	_END(_ASM_LABEL(y))
#define	ASLEND(y)	_LEND(_ASM_LABEL(y))
#define	ASEEND(y)	_EEND(_ASM_LABEL(y))
#define	ASLEEND(y)	_LEEND(_ASM_LABEL(y))

#define	ASMSTR		.asciz

#if defined(PIC)
#define	PLT_SYM(x)	PIC_SYM(x, PLT)
#define	GOT_SYM(x)	PIC_SYM(x, GOT)
#define	GOT_GET(x,got,sym)	\
	ldr	x, sym;		\
	ldr	x, [x, got]
#define	GOT_INIT(got,gotsym,pclabel) \
	ldr	got, gotsym;	\
	pclabel: add	got, pc
#ifdef __thumb__
#define	GOT_INITSYM(gotsym,pclabel) \
	.align 2;		\
	gotsym: .word _C_LABEL(_GLOBAL_OFFSET_TABLE_) - (pclabel+4)
#else
#define	GOT_INITSYM(gotsym,pclabel) \
	.align 2;		\
	gotsym: .word _C_LABEL(_GLOBAL_OFFSET_TABLE_) - (pclabel+8)
#endif

#ifdef __STDC__
#define	PIC_SYM(x,y)	x ## ( ## y ## )
#else
#define	PIC_SYM(x,y)	x/**/(/**/y/**/)
#endif

#else
#define	PLT_SYM(x)	x
#define	GOT_SYM(x)	x
#define	GOT_GET(x,got,sym)	\
	ldr	x, sym;
#define	GOT_INIT(got,gotsym,pclabel)
#define	GOT_INITSYM(gotsym,pclabel)
#define	PIC_SYM(x,y)	x
#endif	/* PIC */

#undef __FBSDID
#if !defined(lint) && !defined(STRIP_FBSDID)
#define __FBSDID(s)     .ident s
#else
#define __FBSDID(s)     /* nothing */
#endif


#define	WEAK_ALIAS(alias,sym)						\
	.weak alias;							\
	alias = sym

#ifdef __STDC__
#define	WARN_REFERENCES(sym,msg)					\
	.stabs msg ## ,30,0,0,0 ;					\
	.stabs __STRING(_C_LABEL(sym)) ## ,1,0,0,0
#else
#define	WARN_REFERENCES(sym,msg)					\
	.stabs msg,30,0,0,0 ;						\
	.stabs __STRING(sym),1,0,0,0
#endif /* __STDC__ */

/* Exactly one of the __ARM_ARCH_*__ macros will be defined by the compiler. */
/* The _ARM_ARCH_* macros are deprecated and will be removed soon. */
/* This should be moved into another header so it can be used in
 * both asm and C code. machine/asm.h cannot be included in C code. */
#if defined (__ARM_ARCH_7__) || defined (__ARM_ARCH_7A__)
#define _ARM_ARCH_7
#define _HAVE_ARMv7_INSTRUCTIONS 1
#endif

#if defined (_HAVE_ARMv7_INSTRUCTIONS) || defined (__ARM_ARCH_6__) || \
	defined (__ARM_ARCH_6J__) || defined (__ARM_ARCH_6K__) || \
	defined (__ARM_ARCH_6KZ__) || \
	defined (__ARM_ARCH_6Z__) || defined (__ARM_ARCH_6ZK__)
#define _ARM_ARCH_6
#define _HAVE_ARMv6_INSTRUCTIONS 1
#endif

#if defined (_HAVE_ARMv6_INSTRUCTIONS) || defined (__ARM_ARCH_5TE__) || \
    defined (__ARM_ARCH_5TEJ__) || defined (__ARM_ARCH_5E__)
#define _ARM_ARCH_5E
#define _HAVE_ARMv5E_INSTRUCTIONS 1
#endif

#if defined (_HAVE_ARMv5E_INSTRUCTIONS) || defined (__ARM_ARCH_5__) || \
    defined (__ARM_ARCH_5T__)
#define _ARM_ARCH_5
#define _HAVE_ARMv5_INSTRUCTIONS 1
#endif

#if defined (_HAVE_ARMv5_INSTRUCTIONS) || defined (__ARM_ARCH_4T__)
#define _ARM_ARCH_4T
#define _HAVE_ARMv4T_INSTRUCTIONS 1
#endif

/* FreeBSD requires ARMv4, so this is always set. */
#define _HAVE_ARMv4_INSTRUCTIONS 1

#if defined (_HAVE_ARMv4T_INSTRUCTIONS)
# define RET	bx	lr
# define RETeq	bxeq	lr
# define RETne	bxne	lr
# define RETc(c) bx##c	lr
#else
# define RET	mov	pc, lr
# define RETeq	moveq	pc, lr
# define RETne	movne	pc, lr
# define RETc(c) mov##c	pc, lr
#endif

#if __ARM_ARCH >= 7
#define ISB	isb
#define DSB	dsb
#define DMB	dmb
#define WFI	wfi

#if defined(__ARM_ARCH_7VE__) || defined(__clang__)
#define	MSR_ELR_HYP(regnum)	msr	elr_hyp, lr
#define	ERET	eret
#else
#define MSR_ELR_HYP(regnum) .word (0xe12ef300 | regnum)
#define ERET .word 0xe160006e
#endif

#elif __ARM_ARCH == 6
#define ISB	mcr CP15_CP15ISB
#define DSB	mcr CP15_CP15DSB
#define DMB	mcr CP15_CP15DMB
#define WFI	mcr CP15_CP15WFI
#else
#define ISB	mcr CP15_CP15ISB
#define DSB	mcr CP15_CP15DSB	/* DSB and DMB are the */
#define DMB	mcr CP15_CP15DSB	/* same prior to v6.*/
/* No form of WFI available on v4, define nothing to get an error on use. */
#endif

#endif /* !_MACHINE_ASM_H_ */
