/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)profile.h	8.1 (Berkeley) 6/11/93
 * $FreeBSD$
 */

#ifndef _MACHINE_PROFILE_H_
#define	_MACHINE_PROFILE_H_

/*
 * Config generates something to tell the compiler to align functions on 32
 * byte boundaries.  A strict alignment is good for keeping the tables small.
 */
#define	FUNCTION_ALIGNMENT	16


#define	_MCOUNT_DECL void mcount

typedef u_long	fptrdiff_t;

/*
 * Cannot implement mcount in C as GCC will trash the ip register when it
 * pushes a trapframe. Pity we cannot insert assembly before the function
 * prologue.
 */

#ifndef PLTSYM
#define	PLTSYM
#endif

#define	MCOUNT								\
	__asm__(".text");						\
	__asm__(".align	2");						\
	__asm__(".type	__mcount ,%function");				\
	__asm__(".global	__mcount");				\
	__asm__("__mcount:");						\
	/*								\
	 * Preserve registers that are trashed during mcount		\
	 */								\
	__asm__("stmfd	sp!, {r0-r3, ip, lr}");				\
	/*								\
	 * find the return address for mcount,				\
	 * and the return address for mcount's caller.			\
	 *								\
	 * frompcindex = pc pushed by call into self.			\
	 */								\
	__asm__("mov	r0, ip");					\
	/*								\
	 * selfpc = pc pushed by mcount call				\
	 */								\
	__asm__("mov	r1, lr");					\
	/*								\
	 * Call the real mcount code					\
	 */								\
	__asm__("bl	mcount");					\
	/*								\
	 * Restore registers that were trashed during mcount		\
	 */								\
	__asm__("ldmfd	sp!, {r0-r3, lr}");				\
	/*								\
	 * Return to the caller. Loading lr and pc in one instruction	\
	 * is deprecated on ARMv7 so we need this on its own.		\
	 */								\
	__asm__("ldmfd	sp!, {pc}");
void bintr(void);
void btrap(void);
void eintr(void);
void user(void);

#define	MCOUNT_FROMPC_USER(pc)					\
	((pc < (uintfptr_t)VM_MAXUSER_ADDRESS) ? (uintfptr_t)user : pc)

#define	MCOUNT_FROMPC_INTR(pc)					\
	((pc >= (uintfptr_t)btrap && pc < (uintfptr_t)eintr) ?	\
	    ((pc >= (uintfptr_t)bintr) ? (uintfptr_t)bintr :	\
		(uintfptr_t)btrap) : ~0U)


#ifdef _KERNEL

#define	MCOUNT_DECL(s)	register_t s;

#include <machine/asm.h>
#include <machine/cpufunc.h>
/*
 * splhigh() and splx() are heavyweight, and call mcount().  Therefore
 * we disabled interrupts (IRQ, but not FIQ) directly on the CPU.
 *
 * We're lucky that the CPSR and 's' both happen to be 'int's.
 */
#define	MCOUNT_ENTER(s)	{s = intr_disable(); }	/* kill IRQ */
#define	MCOUNT_EXIT(s)	{intr_restore(s); }	/* restore old value */

void	mcount(uintfptr_t frompc, uintfptr_t selfpc);

#else
typedef	u_int	uintfptr_t;
#endif /* _KERNEL */

#endif /* !_MACHINE_PROFILE_H_ */
