/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 *	from: NetBSD: profile.h,v 1.9 1997/04/06 08:47:37 cgd Exp
 *	from: FreeBSD: src/sys/alpha/include/profile.h,v 1.4 1999/12/29
 * $FreeBSD$
 */

#ifndef _MACHINE_PROFILE_H_
#define	_MACHINE_PROFILE_H_

#define	_MCOUNT_DECL	void __mcount

#define	FUNCTION_ALIGNMENT	4

typedef __ptrdiff_t	fptrdiff_t;

/*
 * The mcount trampoline macro, expanded in libc/gmon/mcount.c
 *
 * For PowerPC SVR4 ABI profiling, the compiler will insert
 * a data declaration and code sequence at the start of a routine of the form
 *
 * .function_mc:       	.data
 *			.align	2
 *			.long	0
 *			.text
 *
 * function:		mflr	%r0
 *			addis	%r11,%r0, .function_mc@ha
 *			stw	%r0,4(%r1)
 *			addi	%r0,%r11, .function_mc@l
 *			bl	_mcount
 *
 * The link register is saved in the LR save word in the caller's
 * stack frame, r0 is set up to point to the allocated longword,
 * and control is transferred to _mcount.
 *
 * On return from _mcount, the routine should function as it would
 * with no profiling so _mcount must restore register state to that upon
 * entry. Any routine called by the _mcount trampoline will save
 * callee-save registers, so _mcount must make sure it saves volatile
 * registers that may have state after it returns i.e. parameter registers.
 *
 * The FreeBSD libc mcount routine ignores the r0 longword pointer, but
 * instead requires as parameters the current PC and called PC. The current
 * PC is obtained from the link register, as a result of "bl _mcount" in
 * the stub, while the caller's PC is obtained from the LR save word.
 *
 * On return from libc mcount, the return is done indirectly with the
 * ctr register rather than the link register, to allow the link register
 * to be restored to what it was on entry to the profiled routine.
 */

#if defined(__powerpc64__)

#if !defined(_CALL_ELF) || _CALL_ELF == 1
#define MCOUNT_PREAMBLE \
	"	.align	2			\n" \
	"	.globl	_mcount			\n" \
	"	.section \".opd\",\"aw\"	\n" \
	"	.align	3			\n" \
	"_mcount:				\n" \
	"	.quad .L._mcount,.TOC.@tocbase,0\n" \
	"	.previous			\n" \
	"	.size   _mcount,24		\n" \
	"	.type	_mcount,@function	\n" \
	"	.align	4			\n" \
	".L._mcount:				\n" 
#else
#define MCOUNT_PREAMBLE \
	"	.globl	_mcount			\n" \
	"	.type	_mcount,@function	\n" \
	"	.align	4			\n" \
	"_mcount:				\n"
#endif

#define	MCOUNT					\
__asm(	MCOUNT_PREAMBLE \
	"	stdu	%r1,-(288+128)(%r1)	\n" \
	"	std	%r3,48(%r1)		\n" \
	"	std	%r4,56(%r1)		\n" \
	"	std	%r5,64(%r1)		\n" \
	"	std	%r6,72(%r1)		\n" \
	"	std	%r7,80(%r1)		\n" \
	"	std	%r8,88(%r1)		\n" \
	"	std	%r9,96(%r1)		\n" \
	"	std	%r10,104(%r1)		\n" \
	"	mflr	%r4			\n" \
	"	std	%r4,112(%r1)		\n" \
	"	ld	%r3,0(%r1)		\n" \
	"	ld	%r3,0(%r3)		\n" \
	"	ld	%r3,16(%r3)		\n" \
	"	bl	__mcount		\n" \
	"	nop				\n" \
	"	ld	%r4,112(%r1)		\n" \
	"	mtlr	%r4			\n" \
	"	ld	%r3,48(%r1)		\n" \
	"	ld	%r4,56(%r1)		\n" \
	"	ld	%r5,64(%r1)		\n" \
	"	ld	%r6,72(%r1)		\n" \
	"	ld	%r7,80(%r1)		\n" \
	"	ld	%r8,88(%r1)		\n" \
	"	ld	%r9,96(%r1)		\n" \
	"	ld	%r10,104(%r1)		\n" \
	"	addi	%r1,%r1,(288+128)	\n" \
	"	blr				\n");
#else

#ifdef PIC
#define _PLT "@plt"
#else
#define _PLT
#endif

#define	MCOUNT					\
__asm(	"	.globl	_mcount			\n" \
	"	.type	_mcount,@function	\n" \
	"	.align	4			\n" \
	"_mcount:				\n" \
	"	stwu	%r1,-64(%r1)		\n" \
	"	stw	%r3,16(%r1)		\n" \
	"	stw	%r4,20(%r1)		\n" \
	"	stw	%r5,24(%r1)		\n" \
	"	stw	%r6,28(%r1)		\n" \
	"	stw	%r7,32(%r1)		\n" \
	"	stw	%r8,36(%r1)		\n" \
	"	stw	%r9,40(%r1)		\n" \
	"	stw	%r10,44(%r1)		\n" \
	"	mflr	%r4			\n" \
	"	stw	%r4,48(%r1)		\n" \
	"	lwz	%r3,68(%r1)		\n" \
	"	bl	__mcount" _PLT "	\n" \
	"	lwz	%r3,68(%r1)		\n" \
	"	mtlr	%r3			\n" \
	"	lwz	%r4,48(%r1)		\n" \
	"	mtctr	%r4			\n" \
	"	lwz	%r3,16(%r1)		\n" \
	"	lwz	%r4,20(%r1)		\n" \
	"	lwz	%r5,24(%r1)		\n" \
	"	lwz	%r6,28(%r1)		\n" \
	"	lwz	%r7,32(%r1)		\n" \
	"	lwz	%r8,36(%r1)		\n" \
	"	lwz	%r9,40(%r1)		\n" \
	"	lwz	%r10,44(%r1)		\n" \
	"	addi	%r1,%r1,64		\n" \
	"	bctr				\n" \
	"_mcount_end:				\n" \
	"	.size	_mcount,_mcount_end-_mcount");
#endif

#ifdef _KERNEL
#define	MCOUNT_ENTER(s)		s = intr_disable()
#define	MCOUNT_EXIT(s)		intr_restore(s)
#define	MCOUNT_DECL(s)		register_t s;

#ifndef COMPILING_LINT
#ifdef AIM
#include <machine/trap.h>
#define	__PROFILE_VECTOR_BASE	EXC_RST
#define	__PROFILE_VECTOR_TOP	(EXC_LAST + 0x100)
#endif	/* AIM */
#if defined(BOOKE)
extern char interrupt_vector_base[];
extern char interrupt_vector_top[];
#define	__PROFILE_VECTOR_BASE	(uintfptr_t)interrupt_vector_base
#define	__PROFILE_VECTOR_TOP	(uintfptr_t)interrupt_vector_top
#endif	/* BOOKE_E500 || BOOKE_PPC4XX */

#endif	/* !COMPILING_LINT */

#ifndef __PROFILE_VECTOR_BASE
#define	__PROFILE_VECTOR_BASE	0
#endif
#ifndef __PROFILE_VECTOR_TOP
#define	__PROFILE_VECTOR_TOP	1
#endif

static __inline void
powerpc_profile_interrupt(void)
{
}

static __inline void
powerpc_profile_userspace(void)
{
}

#define	MCOUNT_FROMPC_USER(pc)				\
	((pc < (uintfptr_t)VM_MAXUSER_ADDRESS) ?	\
	    (uintfptr_t)powerpc_profile_userspace : pc)

#define	MCOUNT_FROMPC_INTR(pc)				\
	((pc >= __PROFILE_VECTOR_BASE &&		\
	  pc < __PROFILE_VECTOR_TOP) ?			\
	    (uintfptr_t)powerpc_profile_interrupt : ~0U)

void __mcount(uintfptr_t frompc, uintfptr_t selfpc);

#else	/* !_KERNEL */

#ifdef __powerpc64__
typedef u_long	uintfptr_t;
#else
typedef u_int	uintfptr_t;
#endif

#endif	/* _KERNEL */

#endif /* !_MACHINE_PROFILE_H_ */
