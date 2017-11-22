/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _IA32_SYS_ASM_LINKAGE_H
#define	_IA32_SYS_ASM_LINKAGE_H

#include <sys/stack.h>
#include <sys/trap.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/*
 * make annoying differences in assembler syntax go away
 */

/*
 * D16 and A16 are used to insert instructions prefixes; the
 * macros help the assembler code be slightly more portable.
 */
#if !defined(__GNUC_AS__)
/*
 * /usr/ccs/bin/as prefixes are parsed as separate instructions
 */
#define	D16	data16;
#define	A16	addr16;

/*
 * (There are some weird constructs in constant expressions)
 */
#define	_CONST(const)		[const]
#define	_BITNOT(const)		-1!_CONST(const)
#define	_MUL(a, b)		_CONST(a \* b)

#else
/*
 * Why not use the 'data16' and 'addr16' prefixes .. well, the
 * assembler doesn't quite believe in real mode, and thus argues with
 * us about what we're trying to do.
 */
#define	D16	.byte	0x66;
#define	A16	.byte	0x67;

#define	_CONST(const)		(const)
#define	_BITNOT(const)		~_CONST(const)
#define	_MUL(a, b)		_CONST(a * b)

#endif

/*
 * C pointers are different sizes between i386 and amd64.
 * These constants can be used to compute offsets into pointer arrays.
 */
#if defined(__amd64)
#define	CLONGSHIFT	3
#define	CLONGSIZE	8
#define	CLONGMASK	7
#elif defined(__i386)
#define	CLONGSHIFT	2
#define	CLONGSIZE	4
#define	CLONGMASK	3
#endif

/*
 * Since we know we're either ILP32 or LP64 ..
 */
#define	CPTRSHIFT	CLONGSHIFT
#define	CPTRSIZE	CLONGSIZE
#define	CPTRMASK	CLONGMASK

#if CPTRSIZE != (1 << CPTRSHIFT) || CLONGSIZE != (1 << CLONGSHIFT)
#error	"inconsistent shift constants"
#endif

#if CPTRMASK != (CPTRSIZE - 1) || CLONGMASK != (CLONGSIZE - 1)
#error	"inconsistent mask constants"
#endif

#define	ASM_ENTRY_ALIGN	16

/*
 * SSE register alignment and save areas
 */

#define	XMM_SIZE	16
#define	XMM_ALIGN	16

#if defined(__amd64)

#define	SAVE_XMM_PROLOG(sreg, nreg)				\
	subq	$_CONST(_MUL(XMM_SIZE, nreg)), %rsp;		\
	movq	%rsp, sreg

#define	RSTOR_XMM_EPILOG(sreg, nreg)				\
	addq	$_CONST(_MUL(XMM_SIZE, nreg)), %rsp

#elif defined(__i386)

#define	SAVE_XMM_PROLOG(sreg, nreg)				\
	subl	$_CONST(_MUL(XMM_SIZE, nreg) + XMM_ALIGN), %esp; \
	movl	%esp, sreg;					\
	addl	$XMM_ALIGN, sreg;				\
	andl	$_BITNOT(XMM_ALIGN-1), sreg

#define	RSTOR_XMM_EPILOG(sreg, nreg)				\
	addl	$_CONST(_MUL(XMM_SIZE, nreg) + XMM_ALIGN), %esp;

#endif	/* __i386 */

/*
 * profiling causes definitions of the MCOUNT and RTMCOUNT
 * particular to the type
 */
#ifdef GPROF

#define	MCOUNT(x) \
	pushl	%ebp; \
	movl	%esp, %ebp; \
	call	_mcount; \
	popl	%ebp

#endif /* GPROF */

#ifdef PROF

#define	MCOUNT(x) \
/* CSTYLED */ \
	.lcomm .L_/**/x/**/1, 4, 4; \
	pushl	%ebp; \
	movl	%esp, %ebp; \
/* CSTYLED */ \
	movl	$.L_/**/x/**/1, %edx; \
	call	_mcount; \
	popl	%ebp

#endif /* PROF */

/*
 * if we are not profiling, MCOUNT should be defined to nothing
 */
#if !defined(PROF) && !defined(GPROF)
#define	MCOUNT(x)
#endif /* !defined(PROF) && !defined(GPROF) */

#define	RTMCOUNT(x)	MCOUNT(x)

/*
 * Macro to define weak symbol aliases. These are similar to the ANSI-C
 *	#pragma weak _name = name
 * except a compiler can determine type. The assembler must be told. Hence,
 * the second parameter must be the type of the symbol (i.e.: function,...)
 */
#define	ANSI_PRAGMA_WEAK(sym, stype)	\
/* CSTYLED */ \
	.weak	_/**/sym; \
/* CSTYLED */ \
	.type	_/**/sym, @stype; \
/* CSTYLED */ \
_/**/sym = sym

/*
 * Like ANSI_PRAGMA_WEAK(), but for unrelated names, as in:
 *	#pragma weak sym1 = sym2
 */
#define	ANSI_PRAGMA_WEAK2(sym1, sym2, stype)	\
	.weak	sym1; \
	.type sym1, @stype; \
sym1	= sym2

/*
 * ENTRY provides the standard procedure entry code and an easy way to
 * insert the calls to mcount for profiling. ENTRY_NP is identical, but
 * never calls mcount.
 */
#define	ENTRY(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:	MCOUNT(x)

#define	ENTRY_NP(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:

#define	RTENTRY(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:	RTMCOUNT(x)

/*
 * ENTRY2 is identical to ENTRY but provides two labels for the entry point.
 */
#define	ENTRY2(x, y) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x, y; \
	.type	x, @function; \
	.type	y, @function; \
/* CSTYLED */ \
x:	; \
y:	MCOUNT(x)

#define	ENTRY_NP2(x, y) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x, y; \
	.type	x, @function; \
	.type	y, @function; \
/* CSTYLED */ \
x:	; \
y:


/*
 * ALTENTRY provides for additional entry points.
 */
#define	ALTENTRY(x) \
	.globl x; \
	.type	x, @function; \
x:

/*
 * DGDEF and DGDEF2 provide global data declarations.
 *
 * DGDEF provides a word aligned word of storage.
 *
 * DGDEF2 allocates "sz" bytes of storage with **NO** alignment.  This
 * implies this macro is best used for byte arrays.
 *
 * DGDEF3 allocates "sz" bytes of storage with "algn" alignment.
 */
#define	DGDEF2(name, sz) \
	.data; \
	.globl	name; \
	.type	name, @object; \
	.size	name, sz; \
name:

#define	DGDEF3(name, sz, algn) \
	.data; \
	.align	algn; \
	.globl	name; \
	.type	name, @object; \
	.size	name, sz; \
name:

#define	DGDEF(name)	DGDEF3(name, 4, 4)

/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#define	SET_SIZE(x) \
	.size	x, [.-x]

/*
 * NWORD provides native word value.
 */
#if defined(__amd64)

/*CSTYLED*/
#define	NWORD	quad

#elif defined(__i386)

#define	NWORD	long

#endif  /* __i386 */

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _IA32_SYS_ASM_LINKAGE_H */
