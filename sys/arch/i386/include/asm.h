/*	$OpenBSD: asm.h,v 1.19 2022/12/08 01:25:45 guenther Exp $	*/
/*	$NetBSD: asm.h,v 1.7 1994/10/27 04:15:56 cgd Exp $	*/

/*-
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
 *	@(#)asm.h	5.5 (Berkeley) 5/7/91
 */

#ifndef _MACHINE_ASM_H_
#define _MACHINE_ASM_H_

#ifdef __PIC__
#define PIC_PROLOGUE	\
	pushl	%ebx;	\
	call	666f;	\
666:			\
	popl	%ebx;	\
	addl	$_GLOBAL_OFFSET_TABLE_+[.-666b], %ebx
#define PIC_EPILOGUE	\
	popl	%ebx
#define PIC_PLT(x)	x@PLT
#define PIC_GOT(x)	x@GOT(%ebx)
#define PIC_GOTOFF(x)	x@GOTOFF(%ebx)
#else
#define PIC_PROLOGUE
#define PIC_EPILOGUE
#define PIC_PLT(x)	x
#define PIC_GOT(x)	x
#define PIC_GOTOFF(x)	x
#endif

#define _C_LABEL(name)	name
#define	_ASM_LABEL(x)	x

#define CVAROFF(x, y)	x + y

#ifdef __STDC__
# define __CONCAT(x,y)	x ## y
# define __STRING(x)	#x
#else
# define __CONCAT(x,y)	x/**/y
# define __STRING(x)	"x"
#endif

/*
 * STRONG_ALIAS, WEAK_ALIAS
 *	Create a strong or weak alias.
 */
#define STRONG_ALIAS(alias,sym) \
	.global alias; \
	alias = sym
#define WEAK_ALIAS(alias,sym) \
	.weak alias; \
	alias = sym

/* let kernels and others override entrypoint alignment */
#ifndef _ALIGN_TEXT
# define _ALIGN_TEXT .align 2, 0x90
#endif

/* NB == No Binding: use .globl or .weak as necessary */
#define _ENTRY_NB(x) \
	.text; _ALIGN_TEXT; .type x,@function; x:
#define _ENTRY(x)	.globl x; _ENTRY_NB(x)

#ifdef _KERNEL
#define KUTEXT	.section .kutext, "ax"

#define IDTVEC(name)    \
	KUTEXT; ALIGN_TEXT;	\
	.globl X##name; X##name:
#define KIDTVEC(name)    \
	.text; ALIGN_TEXT;	\
	.globl X##name; X##name:
#define KUENTRY(x) \
	KUTEXT; _ALIGN_TEXT; .globl x; .type x,@function; x:

#endif	/* _KERNEL */

#if defined(PROF) || defined(GPROF)
# define _PROF_PROLOGUE	\
	pushl %ebp; movl %esp,%ebp; call PIC_PLT(mcount); popl %ebp
#else
# define _PROF_PROLOGUE
#endif

#define	ENTRY(y)	_ENTRY(y); _PROF_PROLOGUE
#define	ENTRY_NB(y)	_ENTRY_NB(y); _PROF_PROLOGUE
#define	NENTRY(y)	_ENTRY(y)
#define	ASENTRY(y)	_ENTRY(y); _PROF_PROLOGUE
#define	NASENTRY(y)	_ENTRY(y)
#define	END(y)		.size y, . - y

#define	ALTENTRY(name)	.globl name; name:

#ifdef _KERNEL

#define CPUVAR(var)	%fs:__CONCAT(CPU_INFO_,var)

#endif /* _KERNEL */

#endif /* !_MACHINE_ASM_H_ */
