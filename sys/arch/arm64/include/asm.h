/*	$OpenBSD: asm.h,v 1.12 2023/03/27 19:02:48 kettenis Exp $	*/
/*	$NetBSD: asm.h,v 1.4 2001/07/16 05:43:32 matt Exp $	*/

/*
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
#define _MACHINE_ASM_H_

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

/*
 * gas/arm uses @ as a single comment character and thus cannot be used here
 * Instead it recognised the # instead of an @ symbols in .type directives
 * We define a couple of macros so that assembly code will not be dependant
 * on one or the other.
 */
#define _ASM_TYPE_FUNCTION	#function
#define _ASM_TYPE_OBJECT	#object
/* NB == No Binding: use .globl or .weak as necessary */
#define _ENTRY_NB(x) \
	.text; _ALIGN_TEXT; .type x,_ASM_TYPE_FUNCTION; x:
#define _ENTRY(x)	.globl x; _ENTRY_NB(x)

#if defined(PROF) || defined(GPROF)
#  define _PROF_PROLOGUE	\
	stp	x29, x30, [sp, #-16]!; \
	mov fp, sp;		\
	bl __mcount; 		\
	ldp	x29, x30, [sp], #16;
#else
# define _PROF_PROLOGUE
#endif

#if defined(_RET_PROTECTOR)
# define RETGUARD_CALC_COOKIE(reg) \
	eor     reg, reg, x30

# define RETGUARD_LOAD_RANDOM(x, reg) \
	adrp    reg, __CONCAT(__retguard_, x); \
	ldr     reg, [reg, :lo12:__CONCAT(__retguard_, x)]

# define RETGUARD_SETUP(x, reg) \
	RETGUARD_SYMBOL(x); \
	RETGUARD_LOAD_RANDOM(x, reg); \
	RETGUARD_CALC_COOKIE(reg)

# define RETGUARD_CHECK(x, reg) \
	RETGUARD_CALC_COOKIE(reg); \
	RETGUARD_LOAD_RANDOM(x, x9); \
	subs    reg, reg, x9; \
	cbz     reg, 66f; \
	brk     #0x1; \
66:

# define RETGUARD_PUSH(reg) \
	str     reg, [sp, #-16]!

# define RETGUARD_POP(reg) \
	ldr     reg, [sp, #16]!

# define RETGUARD_SYMBOL(x) \
	.ifndef __CONCAT(__retguard_, x); \
	.hidden __CONCAT(__retguard_, x); \
	.type   __CONCAT(__retguard_, x),@object; \
	.pushsection .openbsd.randomdata.retguard,"aw",@progbits; \
	.weak   __CONCAT(__retguard_, x); \
	.p2align 3; \
	__CONCAT(__retguard_, x): ; \
	.xword 0; \
	.size __CONCAT(__retguard_, x), 8; \
	.popsection; \
	.endif
#else
# define RETGUARD_CALC_COOKIE(reg)
# define RETGUARD_LOAD_RANDOM(x, reg)
# define RETGUARD_SETUP(x, reg)
# define RETGUARD_CHECK(x, reg)
# define RETGUARD_PUSH(reg)
# define RETGUARD_POP(reg)
# define RETGUARD_SYMBOL(x)
#endif

#define	ENTRY(y)	_ENTRY(y); bti c; _PROF_PROLOGUE
#define	ENTRY_NP(y)	_ENTRY(y); bti c
#define	ENTRY_NB(y)	_ENTRY_NB(y); bti c; _PROF_PROLOGUE
#define	ASENTRY(y)	_ENTRY(y); bti c; _PROF_PROLOGUE
#define	ASENTRY_NP(y)	_ENTRY(y); bti c
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

#endif /* !_MACHINE_ASM_H_ */
