/*	$OpenBSD: tfork_thread.S,v 1.5 2023/12/10 16:45:52 deraadt Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat
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

#include "SYS.h"

/*
 * int __tfork_thread(const struct __tfork *param, size_t psize, void (*func)(void *), void *arg);
 */
ENTRY(__tfork_thread)
	/*
	 * We can not invoke __tfork as a G2-style system call since we want
	 * different return paths.
	 */
	mov	SYS___tfork, %g1
99:	t	ST_SYSCALL
	PINSYSCALL(SYS___tfork, 99b)
	bcs	9f
	 nop

	cmp	%o0, 0
	be	1f
	 nop

	/*
	 * In parent process: just return.
	 */
	retl
	 nop

1:
	/*
	 * In child process: invoke function, then exit.
	 */
	call	%o2				/* func */
	 mov	%o3, %o0			/* arg */

	mov	SYS___threxit, %g1
	clr	%o0
98:	t	ST_SYSCALL	/* will not return */
	PINSYSCALL(SYS___threxit, 98b)
	unimp

9:
	/*
	 * System call failure.
	 */
	ERROR()
END(__tfork_thread)
