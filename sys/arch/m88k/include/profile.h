#ifndef _M88K_PROFILE_H_
#define _M88K_PROFILE_H_
/*	$OpenBSD: profile.h,v 1.7 2013/02/14 05:56:02 miod Exp $ */
/*
 * Copyright (c) 2004, Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	_MCOUNT_DECL void _mcount

/*
 * On OpenBSD, calls to the function profiler save r2-r9 on stack. The
 * monitor point is found in r2. The function's return address is taken
 * from the stack frame pointed to by r30, and needs to be restored as
 * r1 hasn't have had a chance to be saved yet.
 */

#ifdef __PIC__
#define	MCOUNT_SYMBOL	"_mcount#plt"
#else
#define	MCOUNT_SYMBOL	"_mcount"
#endif

#define MCOUNT								\
__asm__ (".text;"							\
	 ".align 3;"							\
	 ".globl __mcount;"						\
	 ".type  __mcount,@function;"					\
	 "__mcount:"							\
	 "	subu	%r31, %r31, 16;"				\
	 "	st	%r1,  %r31, 4;"					\
	 "	bsr.n	" MCOUNT_SYMBOL ";"				\
	 "	 ld	%r3,  %r30, 4;"	/* function return address */	\
	 "	ld	%r2,  %r31, 4;"					\
	 "	addu	%r31, %r31, 16;"				\
	 "	jmp.n	%r2;"						\
	 "	 ld	%r1,  %r30, 4;"	/* restore r1 */		\
	 ".size	__mcount, .-__mcount");

#ifdef _KERNEL
#define	MCOUNT_ENTER	do { s = get_psr(); set_psr(s | PSR_IND); } while (0)
#define	MCOUNT_EXIT	set_psr(s)
#endif /* _KERNEL */

#endif /* _M88K_PROFILE_H_ */
