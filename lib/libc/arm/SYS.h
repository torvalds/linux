/*	$NetBSD: SYS.h,v 1.8 2003/08/07 16:42:02 agc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	from: @(#)SYS.h	5.5 (Berkeley) 5/7/91
 * $FreeBSD$
 */

#include <machine/asm.h>
#include <sys/syscall.h>
#include <machine/swi.h>

#define SYSTRAP(x)							\
			mov ip, r7;					\
			ldr r7, =SYS_ ## x;				\
			swi 0;						\
			mov r7, ip

#define	CERROR		_C_LABEL(cerror)
#define	CURBRK		_C_LABEL(curbrk)

#define _SYSCALL_NOERROR(x)						\
	ENTRY(__CONCAT(__sys_, x));					\
	.weak _C_LABEL(x);						\
	.set _C_LABEL(x), _C_LABEL(__CONCAT(__sys_,x));			\
	.weak _C_LABEL(__CONCAT(_,x));					\
	.set _C_LABEL(__CONCAT(_,x)),_C_LABEL(__CONCAT(__sys_,x));	\
	SYSTRAP(x)

#define _SYSCALL(x)							\
	_SYSCALL_NOERROR(x);						\
	it	cs;							\
	bcs PIC_SYM(CERROR, PLT)

#define SYSCALL(x)							\
	_SYSCALL(x)

#define PSEUDO(x)							\
	ENTRY(__CONCAT(__sys_, x));					\
	.weak _C_LABEL(__CONCAT(_,x));					\
	.set _C_LABEL(__CONCAT(_,x)),_C_LABEL(__CONCAT(__sys_,x));	\
	SYSTRAP(x);							\
	it	cs;							\
	bcs PIC_SYM(CERROR, PLT);					\
	RET

#define RSYSCALL(x)							\
	_SYSCALL(x);							\
	RET

	.globl  CERROR
