/*	$OpenBSD: asm.h,v 1.19 2023/02/03 06:13:08 miod Exp $	*/
/*	$NetBSD: asm.h,v 1.1 1996/09/30 16:34:20 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _POWERPC_ASM_H_
#define _POWERPC_ASM_H_

#define _C_LABEL(x)	x
#define	_ASM_LABEL(x)	x

#ifdef __STDC__
# define _TMP_LABEL(x)	.L_ ## x
#else
# define _TMP_LABEL(x)	.L_/**/x
#endif

#define _ENTRY_NB(x) \
	.text; .align 2; .type x,@function; x:
#define _ENTRY(x)	.globl x; _ENTRY_NB(x)

#if defined(PROF) || defined(GPROF)
# define _PROF_PROLOGUE(y)	\
	.section ".data"; \
	.align 2; \
_TMP_LABEL(y):; \
	.long 0; \
	.section ".text"; \
	mflr 0; \
	addis 11, 11, _TMP_LABEL(y)@ha; \
	stw 0, 4(1); \
	addi 0, 11,_TMP_LABEL(y)@l; \
	bl _mcount; 
#else
# define _PROF_PROLOGUE(y)
#endif

#define	ENTRY(y)	_ENTRY(y); _PROF_PROLOGUE(y)
#define	ENTRY_NB(y)	_ENTRY_NB(y); _PROF_PROLOGUE(y)
#define	ASENTRY(y)	_ENTRY(y); _PROF_PROLOGUE(y)
#define	END(y)		.size y, . - y

#define STRONG_ALIAS(alias,sym) \
	.global alias; .set alias,sym
#define WEAK_ALIAS(alias,sym) \
	.weak alias; .set alias,sym

#if defined(_RET_PROTECTOR)
# if defined(__PIC__)
#  define RETGUARD_LOAD_RANDOM(x, reg)					\
	bcl	20, 31, 66f;						\
66:	mflr	reg;							\
	addis	reg, reg, (__retguard_ ## x - 66b)@ha;			\
	lwz	reg, ((__retguard_ ## x - 66b)@l)(reg)
# else
#  define RETGUARD_LOAD_RANDOM(x, reg)					\
	lis	reg, (__retguard_ ## x)@ha;				\
	lwz	reg, ((__retguard_ ## x)@l)(reg)
# endif
# define RETGUARD_SETUP(x, reg, retreg)					\
	mflr	retreg;							\
	RETGUARD_SETUP_LATE(x, reg, retreg)
# define RETGUARD_SETUP_LATE(x, reg, retreg)				\
	RETGUARD_SYMBOL(x);						\
	RETGUARD_LOAD_RANDOM(x, reg);					\
	xor	reg, reg, retreg
# define RETGUARD_CHECK(x, reg, retreg)					\
	xor	reg, reg, retreg;					\
	RETGUARD_LOAD_RANDOM(x, %r10);					\
	mtlr	retreg;							\
	twne	reg, %r10
# define RETGUARD_SAVE(reg, loc)					\
	stw reg, loc
# define RETGUARD_LOAD(reg, loc)					\
	lwz reg, loc
# define RETGUARD_SYMBOL(x)						\
	.ifndef __retguard_ ## x;					\
	.hidden __retguard_ ## x;					\
	.type   __retguard_ ## x,@object;				\
	.pushsection .openbsd.randomdata.retguard,"aw",@progbits; 	\
	.weak   __retguard_ ## x;					\
	.p2align 2;							\
	__retguard_ ## x: ;						\
	.long 0;							\
	.size __retguard_ ## x, 4;					\
	.popsection;							\
	.endif
#else
# define RETGUARD_LOAD_RANDOM(x, reg)
# define RETGUARD_SETUP(x, reg, retreg)
# define RETGUARD_SETUP_LATE(x, reg, retreg)
# define RETGUARD_CHECK(x, reg, retreg)
# define RETGUARD_SAVE(reg, loc)
# define RETGUARD_LOAD(reg, loc)
# define RETGUARD_SYMBOL(x)
#endif

#endif /* !_POWERPC_ASM_H_ */
