/*	$OpenBSD: tfork_thread.S,v 1.8 2023/12/10 16:45:51 deraadt Exp $	*/

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

#define NOP	or %r0, %r0, %r0

/*
 * int __tfork_thread(const struct __tfork *param, size_t psize, void (*func)(void *), void *arg);
 */
ENTRY(__tfork_thread)
	or	%r13, %r0,  __SYSCALLNAME(SYS_,__tfork)
99:	tb0	0,    %r0,  450	/* corrupts r2 and r3 in the child */
	PINSYSCALL(SYS___tfork, 99b)
	br	CERROR

	bcnd	eq0,  %r2,  1f

	/*
	 * In parent process: just return.
	 */
	jmp	%r1

1:
	/*
	 * In child process: invoke function, then exit.
	 */
	jsr.n	%r4		/* func */
	 or	%r2,  %r5,  %r0	/* arg */

	or	%r13, %r0,  __SYSCALLNAME(SYS_,__threxit)
98:	tb0	0,    %r0,  450
	PINSYSCALL(SYS___threxit, 98b)
	NOP
	tb0     0, %r0, 130	/* breakpoint */
END(__tfork_thread)
