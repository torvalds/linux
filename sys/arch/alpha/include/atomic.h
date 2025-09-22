/*	$OpenBSD: atomic.h,v 1.12 2014/03/29 18:09:28 guenther Exp $	*/
/* $NetBSD: atomic.h,v 1.7 2001/12/17 23:34:57 thorpej Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Misc. `atomic' operations.
 */

#ifndef _MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#if defined(_KERNEL)

/*
 * atomic_setbits_ulong:
 *
 *	Atomically set bits in a `unsigned long'.
 */
static __inline void
atomic_setbits_ulong(volatile unsigned long *ulp, unsigned long v)
{
	unsigned long t0;

	__asm volatile(
		"# BEGIN atomic_setbits_ulong\n"
		"1:	ldq_l	%0, %1		\n"
		"	or	%0, %2, %0	\n"
		"	stq_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_setbits_ulong"
		: "=&r" (t0), "=m" (*ulp)
		: "r" (v)
		: "memory");
}

/*
 * atomic_clearbits_ulong:
 *
 *	Atomically clear bits in a `unsigned long'.
 */
static __inline void
atomic_clearbits_ulong(volatile unsigned long *ulp, unsigned long v)
{
	unsigned long t0;

	__asm volatile(
		"# BEGIN atomic_clearbits_ulong\n"
		"1:	ldq_l	%0, %1		\n"
		"	and	%0, %2, %0	\n"
		"	stq_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_clearbits_ulong"
		: "=&r" (t0), "=m" (*ulp)
		: "r" (~v)
		: "memory");
}

/*
 * atomic_add_ulong:
 *
 *	Atomically add a value to a `unsigned long'.
 */
static __inline void
atomic_add_ulong(volatile unsigned long *ulp, unsigned long v)
{
	unsigned long t0;

	__asm volatile(
		"# BEGIN atomic_add_ulong\n"
		"1:	ldq_l	%0, %1		\n"
		"	addq	%0, %2, %0	\n"
		"	stq_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_add_ulong"
		: "=&r" (t0), "=m" (*ulp)
		: "r" (v)
		: "memory");
}

/*
 * atomic_sub_ulong:
 *
 *	Atomically subtract a value from a `unsigned long'.
 */
static __inline void
atomic_sub_ulong(volatile unsigned long *ulp, unsigned long v)
{
	unsigned long t0;

	__asm volatile(
		"# BEGIN atomic_sub_ulong\n"
		"1:	ldq_l	%0, %1		\n"
		"	subq	%0, %2, %0	\n"
		"	stq_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_sub_ulong"
		: "=&r" (t0), "=m" (*ulp)
		: "r" (v)
		: "memory");
}

/*
 * atomic_loadlatch_ulong:
 *
 *	Atomically load and latch a `unsigned long' value.
 */
static __inline unsigned long
atomic_loadlatch_ulong(volatile unsigned long *ulp, unsigned long v)
{
	unsigned long t0, v0;

	__asm volatile(
		"# BEGIN atomic_loadlatch_ulong\n"
		"1:	mov	%3, %0		\n"
		"	ldq_l	%1, %2		\n"
		"	stq_c	%0, %2		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_loadlatch_ulong"
		: "=&r" (t0), "=r" (v0), "=m" (*ulp)
		: "r" (v)
		: "memory");

	return (v0);
}

/*
 * atomic_setbits_int:
 *
 *	Atomically set bits in a `unsigned int'.
 */
static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int t0;

	__asm volatile(
		"# BEGIN atomic_setbits_ulong\n"
		"1:	ldl_l	%0, %1		\n"
		"	or	%0, %2, %0	\n"
		"	stl_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_setbits_int"
		: "=&r" (t0), "=m" (*uip)
		: "r" (v)
		: "memory");
}

/*
 * atomic_clearbits_int:
 *
 *	Atomically clear bits in a `unsigned int'.
 */
static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int t0;

	__asm volatile(
		"# BEGIN atomic_clearbits_int\n"
		"1:	ldl_l	%0, %1		\n"
		"	and	%0, %2, %0	\n"
		"	stl_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_clearbits_int"
		: "=&r" (t0), "=m" (*uip)
		: "r" (~v)
		: "memory");
}

/*
 * atomic_add_int:
 *
 *	Atomically add a value to an `int'.
 */
static __inline void
atomic_add_int(volatile int *ulp, int v)
{
	unsigned long t0;

	__asm volatile(
		"# BEGIN atomic_add_int\n"
		"1:	ldl_l	%0, %1		\n"
		"	addl	%0, %2, %0	\n"
		"	stl_c	%0, %1		\n"
		"	beq	%0, 2f		\n"
		"	mb			\n"
		"	br	3f		\n"
		"2:	br	1b		\n"
		"3:				\n"
		"	# END atomic_add_ulong"
		: "=&r" (t0), "=m" (*ulp)
		: "r" (v)
		: "memory");
}

#endif /* defined(_KERNEL) */
#endif /* _MACHINE_ATOMIC_H_ */
