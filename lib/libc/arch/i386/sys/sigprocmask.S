/*	$OpenBSD: sigprocmask.S,v 1.13 2023/12/10 16:45:51 deraadt Exp $ */
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
 */

#include "SYS.h"

SYSENTRY_HIDDEN(sigprocmask)
	movl	8(%esp),%ecx		# fetch new sigset pointer
	testl	%ecx,%ecx		# check new sigset pointer
	jnz	1f			# if not null, indirect
/*	movl	$0,8(%esp)		# null mask pointer: block empty set */
	movl	$1,4(%esp)		# SIG_BLOCK
	jmp	2f
1:	movl	(%ecx),%ecx		# fetch indirect  ...
	movl	%ecx,8(%esp)		# to new mask arg
2:	movl	$(SYS_sigprocmask),%eax
99:	int	$0x80
	PINSYSCALL(SYS_sigprocmask, 99b)
	jc	1f
	movl	12(%esp),%ecx		# fetch old mask requested
	testl	%ecx,%ecx		# test if old mask requested
	jz	out
	movl	%eax,(%ecx)		# store old mask
out:
	xorl	%eax,%eax
	ret
1:
	SET_ERRNO()
	ret
SYSCALL_END_HIDDEN(sigprocmask)
