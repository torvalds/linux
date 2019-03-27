/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 *	@(#)fpu_arith.h	8.1 (Berkeley) 6/11/93
 *	$NetBSD: fpu_arith.h,v 1.3 2000/07/24 04:11:03 mycroft Exp $
 * $FreeBSD$
 */

/*
 * Extended-precision arithmetic.
 *
 * We hold the notion of a `carry register', which may or may not be a
 * machine carry bit or register.  On the SPARC, it is just the machine's
 * carry bit.
 *
 * In the worst case, you can compute the carry from x+y as
 *	(unsigned)(x + y) < (unsigned)x
 * and from x+y+c as
 *	((unsigned)(x + y + c) <= (unsigned)x && (y|c) != 0)
 * for example.
 */

/* set up for extended-precision arithemtic */
#define	FPU_DECL_CARRY

/*
 * We have three kinds of add:
 *	add with carry:					  r = x + y + c
 *	add (ignoring current carry) and set carry:	c'r = x + y + 0
 *	add with carry and set carry:			c'r = x + y + c
 * The macros use `C' for `use carry' and `S' for `set carry'.
 * Note that the state of the carry is undefined after ADDC and SUBC,
 * so if all you have for these is `add with carry and set carry',
 * that is OK.
 *
 * The same goes for subtract, except that we compute x - y - c.
 *
 * Finally, we have a way to get the carry into a `regular' variable,
 * or set it from a value.  SET_CARRY turns 0 into no-carry, nonzero
 * into carry; GET_CARRY sets its argument to 0 or 1.
 */
#define	FPU_ADDC(r, x, y) \
	__asm __volatile("addx %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))
#define	FPU_ADDS(r, x, y) \
	__asm __volatile("addcc %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))
#define	FPU_ADDCS(r, x, y) \
	__asm __volatile("addxcc %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))
#define	FPU_SUBC(r, x, y) \
	__asm __volatile("subx %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))
#define	FPU_SUBS(r, x, y) \
	__asm __volatile("subcc %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))
#define	FPU_SUBCS(r, x, y) \
	__asm __volatile("subxcc %1,%2,%0" : "=r"(r) : "r"(x), "r"(y))

#define	FPU_GET_CARRY(r) __asm __volatile("addx %%g0,%%g0,%0" : "=r"(r))
#define	FPU_SET_CARRY(v) __asm __volatile("addcc %0,-1,%%g0" : : "r"(v))

#define	FPU_SHL1_BY_ADD	/* shift left 1 faster by ADDC than (a<<1)|(b>>31) */
