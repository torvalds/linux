/*	$OpenBSD: asm.h,v 1.7 2022/12/07 23:25:59 guenther Exp $	*/

/*
 * Copyright (c) 2020 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _POWERPC64_ASM_H_
#define _POWERPC64_ASM_H_

#define _C_LABEL(x)	x
#define _ASM_LABEL(x)	x

#define _TMP_LABEL(x)	.L_ ## x
#define _GEP_LABEL(x)	.L_ ## x ## _gep0
#define _LEP_LABEL(x)	.L_ ## x ## _lep0

#define _ENTRY(x)						\
	.text; .align 2; .globl x; .type x,@function; x:	\
	_GEP_LABEL(x):						\
	addis	%r2, %r12, .TOC.-_GEP_LABEL(x)@ha;		\
	addi	%r2, %r2, .TOC.-_GEP_LABEL(x)@l;		\
	_LEP_LABEL(x):						\
	.localentry	x, _LEP_LABEL(x)-_GEP_LABEL(x);

#if defined(PROF) || defined(GPROF)
# define _PROF_PROLOGUE(y)					\
	.section ".data";					\
	.align 2;						\
_TMP_LABEL(y):;							\
	.long	0;						\
	.section ".text";					\
	mflr	%r0;						\
	addis	%r11, %r2, _TMP_LABEL(y)@toc@ha;		\
	std	%r0, 8(%r1);					\
	addi	%r0, %r11, _TMP_LABEL(y)@toc@l;			\
	bl _mcount; 
#else
# define _PROF_PROLOGUE(y)
#endif

#define ENTRY(y)	_ENTRY(y); _PROF_PROLOGUE(y)
#define ASENTRY(y)	_ENTRY(y); _PROF_PROLOGUE(y)
#define END(y)		.size y, . - y

#define STRONG_ALIAS(alias,sym) \
	.global alias; .set alias,sym
#define WEAK_ALIAS(alias,sym) \
	.weak alias; .set alias,sym

#if defined(_RET_PROTECTOR)
# define RETGUARD_SETUP(x, reg)						\
	RETGUARD_SYMBOL(x);						\
	mflr %r0;							\
	addis reg, %r2, (__retguard_ ## x)@toc@ha;			\
	ld reg, ((__retguard_ ## x)@toc@l)(reg);			\
	xor reg, reg, %r0
# define RETGUARD_CHECK(x, reg)						\
	mflr %r0;							\
	xor reg, reg, %r0;						\
	addis %r12, %r2, (__retguard_ ## x)@toc@ha;			\
	ld %r12, ((__retguard_ ## x)@toc@l)(%r12);			\
	tdne reg, %r12
# define RETGUARD_SAVE(reg, loc)					\
	std reg, loc
# define RETGUARD_LOAD(reg, loc)					\
	ld reg, loc
# define RETGUARD_SYMBOL(x)						\
	.ifndef __retguard_ ## x;					\
	.hidden __retguard_ ## x;					\
	.type   __retguard_ ## x,@object;				\
	.pushsection .openbsd.randomdata.retguard,"aw",@progbits; 	\
	.weak   __retguard_ ## x;					\
	.p2align 3;							\
	__retguard_ ## x: ;						\
	.quad 0;							\
	.size __retguard_ ## x, 8;					\
	.popsection;							\
	.endif
#else
# define RETGUARD_SETUP(x, reg)
# define RETGUARD_CHECK(x, reg)
# define RETGUARD_SAVE(reg, loc)
# define RETGUARD_LOAD(reg, loc)
# define RETGUARD_SYMBOL(x)
#endif


#endif /* !_POWERPC64_ASM_H_ */
