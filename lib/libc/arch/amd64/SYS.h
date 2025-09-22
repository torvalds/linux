/*	$OpenBSD: SYS.h,v 1.22 2023/12/10 16:45:50 deraadt Exp $	*/

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
 *	from: @(#)SYS.h	5.5 (Berkeley) 5/7/91
 *	$NetBSD: SYS.h,v 1.5 2002/06/03 18:30:32 fvdl Exp $
 */

#include "DEFS.h"
#include <sys/syscall.h>

/* offsetof(struct tib, tib_errno) - offsetof(struct tib, __tib_tcb) */
#define	TCB_OFFSET_ERRNO	32

#define SYSTRAP(x)							\
	movl $(SYS_ ## x),%eax;						\
	movq %rcx, %r10;						\
97:	syscall;							\
	PINSYSCALL(SYS_ ## x, 97b)

#define SYSENTRY(x)							\
	SYSENTRY_HIDDEN(x);						\
	WEAK_ALIAS(x, _thread_sys_##x)
#define SYSENTRY_HIDDEN(x)						\
	ENTRY(_thread_sys_ ## x)

#define	SYSCALL_END_HIDDEN(x)						\
	END(_thread_sys_ ## x);						\
	_HIDDEN_FALIAS(x,_thread_sys_##x);				\
	END(_HIDDEN(x))
#define	SYSCALL_END(x)		SYSCALL_END_HIDDEN(x); END(x)


#define SET_ERRNO							\
	movl	%eax,%fs:(TCB_OFFSET_ERRNO);				\
	movq	$-1, %rax
#define HANDLE_ERRNO							\
	jnc	99f;							\
	SET_ERRNO;							\
	99:

#define _SYSCALL_NOERROR(x,y)						\
	SYSENTRY(x);							\
	SYSTRAP(y)
#define _SYSCALL_HIDDEN_NOERROR(x,y)					\
	SYSENTRY_HIDDEN(x);						\
	SYSTRAP(y)

#define SYSCALL_NOERROR(x)						\
	_SYSCALL_NOERROR(x,x)

#define SYSCALL_HIDDEN(x)						\
	_SYSCALL_HIDDEN_NOERROR(x,x);					\
	HANDLE_ERRNO
#define SYSCALL(x)							\
	_SYSCALL_NOERROR(x,x);						\
	HANDLE_ERRNO


#define PSEUDO_NOERROR(x,y)						\
	SYSENTRY(x);							\
	RETGUARD_SETUP(_thread_sys_##x, r11);				\
	RETGUARD_PUSH(r11);						\
	SYSTRAP(y);							\
	RETGUARD_POP(r11);						\
	RETGUARD_CHECK(_thread_sys_##x, r11);				\
	ret;								\
	SYSCALL_END(x)

#define PSEUDO(x,y)							\
	SYSENTRY(x);							\
	RETGUARD_SETUP(_thread_sys_##x, r11);				\
	RETGUARD_PUSH(r11);						\
	SYSTRAP(y);							\
	HANDLE_ERRNO;							\
	RETGUARD_POP(r11);						\
	RETGUARD_CHECK(_thread_sys_##x, r11);				\
	ret;								\
	SYSCALL_END(x)

#define PSEUDO_HIDDEN(x,y)						\
	SYSENTRY_HIDDEN(x);						\
	RETGUARD_SETUP(_thread_sys_##x, r11);				\
	RETGUARD_PUSH(r11);						\
	SYSTRAP(y);							\
	HANDLE_ERRNO;							\
	RETGUARD_POP(r11);						\
	RETGUARD_CHECK(_thread_sys_##x , r11);				\
	ret;								\
	SYSCALL_END_HIDDEN(x)

#define RSYSCALL_NOERROR(x)						\
	PSEUDO_NOERROR(x,x)

#define RSYSCALL(x)							\
	PSEUDO(x,x)
#define RSYSCALL_HIDDEN(x)						\
	PSEUDO_HIDDEN(x,x)
