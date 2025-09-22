/*	$OpenBSD: sigprocmask.S,v 1.10 2023/01/11 01:55:17 mortimer Exp $	*/
/*	$NetBSD: sigprocmask.S,v 1.1 2001/06/19 00:25:06 fvdl Exp $	*/

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
 *	from: @(#)sigprocmask.s	5.2 (Berkeley) 12/17/90
 */

#include <machine/asm.h>

#include "SYS.h"

SYSENTRY_HIDDEN(sigprocmask)
	RETGUARD_SETUP(_thread_sys_sigprocmask, r11);
	RETGUARD_PUSH(r11);
	testq	%rsi,%rsi		# check new sigset pointer
	jnz	1f			# if not null, indirect
	movl	$1,%edi			# SIG_BLOCK
	jmp	2f
1:	movl	(%rsi),%esi		# fetch indirect  ...
2:	SYSTRAP(sigprocmask)
	jc	1f
	testq	%rdx,%rdx		# test if old mask requested
	jz	3f
	movl	%eax,(%rdx)		# store old mask
3:
	xorl	%eax,%eax
	jmp	2f
1:
	SET_ERRNO
2:
	RETGUARD_POP(r11);
	RETGUARD_CHECK(_thread_sys_sigprocmask, r11);
	ret
SYSCALL_END_HIDDEN(sigprocmask)
