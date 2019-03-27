/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *
 *	$NetBSD: asm.h,v 1.6.18.1 2000/07/25 08:37:14 kleink Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_ASM_H_
#define	_MACHINE_ASM_H_

#include <sys/cdefs.h>

#if defined(PIC) && !defined(__powerpc64__)
#define	PIC_PROLOGUE	XXX
#define	PIC_EPILOGUE	XXX
#define	PIC_PLT(x)	x@plt
#ifdef	__STDC__
#define	PIC_GOT(x)	XXX
#else	/* not __STDC__ */
#define	PIC_GOT(x)	XXX
#endif	/* __STDC__ */
#else
#define	PIC_PROLOGUE
#define	PIC_EPILOGUE
#define	PIC_PLT(x)	x
#define PIC_GOT(x)	x
#endif

#define	CNAME(csym)		csym
#define	ASMNAME(asmsym)		asmsym
#ifdef __powerpc64__
#define	HIDENAME(asmsym)	__CONCAT(_,asmsym)
#else
#define	HIDENAME(asmsym)	__CONCAT(.,asmsym)
#endif

#if !defined(_CALL_ELF) || _CALL_ELF == 1
#ifdef _KERNEL
/* ELFv1 kernel uses global dot symbols */
#define	DOT_LABEL(name)		__CONCAT(.,name)
#define	TYPE_ENTRY(name)	.size	name,24; \
				.type	DOT_LABEL(name),@function; \
				.globl	DOT_LABEL(name);
#define	END_SIZE(name)		.size	DOT_LABEL(name),.-DOT_LABEL(name);
#else /* !_KERNEL */
/* ELFv1 user code uses local function entry points */
#define	DOT_LABEL(name)		__CONCAT(.L.,name)
#define	TYPE_ENTRY(name)	.type	name,@function;
#define	END_SIZE(name)		.size	name,.-DOT_LABEL(name);
#endif /* _KERNEL */
#else
/* ELFv2 doesn't have any of this complication */
#define	DOT_LABEL(name)		name
#define	TYPE_ENTRY(name)	.type	name,@function;
#define	END_SIZE(name)		.size	name,.-DOT_LABEL(name);
#endif

#define	_GLOBAL(name) \
	.data; \
	.p2align 2; \
	.globl	name; \
	name:

#ifdef __powerpc64__
#define TOC_NAME_FOR_REF(name)	__CONCAT(.L,name)
#define	TOC_REF(name)	TOC_NAME_FOR_REF(name)@toc
#define TOC_ENTRY(name) \
	.section ".toc","aw"; \
	TOC_NAME_FOR_REF(name): \
        .tc name[TC],name
#endif

#ifdef __powerpc64__

#if !defined(_CALL_ELF) || _CALL_ELF == 1
#define	_ENTRY(name) \
	.section ".text"; \
	.p2align 2; \
	.globl	name; \
	.section ".opd","aw"; \
	.p2align 3; \
	name: \
	.quad	DOT_LABEL(name),.TOC.@tocbase,0; \
	.previous; \
	.p2align 4; \
	TYPE_ENTRY(name) \
DOT_LABEL(name):
#else
#define	_ENTRY(name) \
	.text; \
	.p2align 4; \
	.globl	name; \
	.type	name,@function; \
name: \
	addis	%r2, %r12, (.TOC.-name)@ha; \
	addi	%r2, %r2, (.TOC.-name)@l; \
	.localentry name, .-name;
#endif

#define	_END(name) \
	.long	0; \
	.byte	0,0,0,0,0,0,0,0; \
	END_SIZE(name)

#define	LOAD_ADDR(reg, var) \
	lis	reg, var@highest; \
	ori	reg, reg, var@higher; \
	rldicr	reg, reg, 32, 31; \
	oris	reg, reg, var@h; \
	ori	reg, reg, var@l;
#else /* !__powerpc64__ */
#define	_ENTRY(name) \
	.text; \
	.p2align 4; \
	.globl	name; \
	.type	name,@function; \
	name:
#define	_END(name)

#define	LOAD_ADDR(reg, var) \
	lis	reg, var@ha; \
	ori	reg, reg, var@l;
#endif /* __powerpc64__ */

#if defined(PROF) || (defined(_KERNEL) && defined(GPROF))
# ifdef __powerpc64__
#   define	_PROF_PROLOGUE	mflr 0;					\
				std 3,48(1);				\
				std 4,56(1);				\
				std 5,64(1);				\
				std 0,16(1);				\
				stdu 1,-112(1);				\
				bl _mcount;				\
				nop;					\
				ld 0,112+16(1);				\
				ld 3,112+48(1);				\
				ld 4,112+56(1);				\
				ld 5,112+64(1);				\
				mtlr 0;					\
				addi 1,1,112
# else
#   define	_PROF_PROLOGUE	mflr 0; stw 0,4(1); bl _mcount
# endif
#else
# define	_PROF_PROLOGUE
#endif

#define	ASENTRY(y)	_ENTRY(ASMNAME(y)); _PROF_PROLOGUE
#define	END(y)		_END(CNAME(y))
#define	ENTRY(y)	_ENTRY(CNAME(y)); _PROF_PROLOGUE
#define	GLOBAL(y)	_GLOBAL(CNAME(y))

#define	ASENTRY_NOPROF(y)	_ENTRY(ASMNAME(y))
#define	ENTRY_NOPROF(y)		_ENTRY(CNAME(y))

#define	ASMSTR		.asciz

#define	RCSID(x)	.text; .asciz x

#undef __FBSDID
#if !defined(lint) && !defined(STRIP_FBSDID)
#define __FBSDID(s)	.ident s
#else
#define __FBSDID(s)	/* nothing */
#endif /* not lint and not STRIP_FBSDID */

#define	WEAK_REFERENCE(sym, alias)				\
	.weak alias;						\
	.equ alias,sym

#ifdef __STDC__
#define	WARN_REFERENCES(_sym,_msg)				\
	.section .gnu.warning. ## _sym ; .ascii _msg ; .text
#else
#define	WARN_REFERENCES(_sym,_msg)				\
	.section .gnu.warning./**/_sym ; .ascii _msg ; .text
#endif /* __STDC__ */

#endif /* !_MACHINE_ASM_H_ */
