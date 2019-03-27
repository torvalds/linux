/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2011 Marius Strobl <marius@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_ASMACROS_H_
#define	_MACHINE_ASMACROS_H_

#ifdef _KERNEL

/*
 * Normal and alternate %g6 point to the pcb of the current process.  Normal,
 * alternate and interrupt %g7 point to per-cpu data.
 */
#define	PCB_REG		%g6
#define	PCPU_REG	%g7

/*
 * Alternate %g5 points to a per-cpu panic stack, which is used as a last
 * resort, and for temporarily saving alternate globals.
 */
#define	ASP_REG		%g5

#ifdef LOCORE

/*
 * Atomically decrement an integer in memory.
 */
#define	ATOMIC_DEC_INT(r1, r2, r3)					\
	lduw	[r1], r2 ;						\
9:	sub	r2, 1, r3 ;						\
	casa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%icc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically increment an integer in memory.
 */
#define	ATOMIC_INC_INT(r1, r2, r3)					\
	lduw	[r1], r2 ;						\
9:	add	r2, 1, r3 ;						\
	casa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%icc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically increment a long in memory.
 */
#define	ATOMIC_INC_LONG(r1, r2, r3)					\
	ldx	[r1], r2 ;						\
9:	add	r2, 1, r3 ;						\
	casxa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%xcc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically clear a number of bits of an integer in memory.
 */
#define	ATOMIC_CLEAR_INT(r1, r2, r3, bits)				\
	lduw	[r1], r2 ;						\
9:	andn	r2, bits, r3 ;						\
	casa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%icc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically clear a number of bits of a long in memory.
 */
#define	ATOMIC_CLEAR_LONG(r1, r2, r3, bits)				\
	ldx	[r1], r2 ;						\
9:	andn	r2, bits, r3 ;						\
	casxa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%xcc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically load an integer from memory.
 */
#define	ATOMIC_LOAD_INT(r1, val)					\
	clr	val ;							\
	casa	[r1] ASI_N, %g0, val

/*
 * Atomically load a long from memory.
 */
#define	ATOMIC_LOAD_LONG(r1, val)					\
	clr	val ;							\
	casxa	[r1] ASI_N, %g0, val

/*
 * Atomically set a number of bits of an integer in memory.
 */
#define	ATOMIC_SET_INT(r1, r2, r3, bits)				\
	lduw	[r1], r2 ;						\
9:	or	r2, bits, r3 ;						\
	casa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%icc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically set a number of bits of a long in memory.
 */
#define	ATOMIC_SET_LONG(r1, r2, r3, bits)				\
	ldx	[r1], r2 ;						\
9:	or	r2, bits, r3 ;						\
	casxa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%xcc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically store an integer in memory.
 */
#define	ATOMIC_STORE_INT(r1, r2, r3, val)				\
	lduw	[r1], r2 ;						\
9:	mov	val, r3 ;						\
	casa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%icc, 9b ;						\
	 mov	r3, r2

/*
 * Atomically store a long in memory.
 */
#define	ATOMIC_STORE_LONG(r1, r2, r3, val)				\
	ldx	[r1], r2 ;						\
9:	mov	val, r3 ;						\
	casxa	[r1] ASI_N, r2, r3 ;					\
	cmp	r2, r3 ;						\
	bne,pn	%xcc, 9b ;						\
	 mov	r3, r2

#define	PCPU(member)	PCPU_REG + PC_ ## member
#define	PCPU_ADDR(member, reg)						\
	add	PCPU_REG, PC_ ## member, reg

#define	DEBUGGER()							\
	ta	%xcc, 1

#define	PANIC(msg, r1)							\
	.sect	.rodata ;						\
9:	.asciz	msg ;							\
	.previous ;							\
	SET(9b, r1, %o0) ;						\
	call	panic ;							\
	 nop

#ifdef INVARIANTS
#define	KASSERT(r1, msg)						\
	brnz,pt	r1, 8f ;						\
	 nop ;								\
	PANIC(msg, r1) ;						\
8:
#else
#define	KASSERT(r1, msg)
#endif

#define	PUTS(msg, r1)							\
	.sect	.rodata ;						\
9:	.asciz	msg ;							\
	.previous ;							\
	SET(9b, r1, %o0) ;						\
	call	printf ;						\
	 nop

#define	_ALIGN_DATA	.align 8

#define	DATA(name)							\
	.data ;								\
	_ALIGN_DATA ;							\
	.globl	name ;							\
	.type	name, @object ;						\
name:

#define	EMPTY

/*
 * Generate atomic compare and swap, load and store instructions for the
 * corresponding width and ASI (or not).  Note that we want to evaluate the
 * macro args before concatenating, so that EMPTY really turns into nothing.
 */
#define		_LD(w, a)	ld ## w ## a
#define		_ST(w, a)	st ## w ## a
#define		_CAS(w, a)	cas ## w ## a

#define		LD(w, a)	_LD(w, a)
#define		ST(w, a)	_ST(w, a)
#define		CAS(w, a)	_CAS(w, a)

#endif /* LOCORE */

#endif /* _KERNEL */

#endif /* !_MACHINE_ASMACROS_H_ */
