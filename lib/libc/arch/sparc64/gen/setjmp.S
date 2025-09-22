/*	$OpenBSD: setjmp.S,v 1.7 2023/12/10 16:45:52 deraadt Exp $	*/
/*	$NetBSD: setjmp.S,v 1.4 2000/08/28 00:45:59 eeh Exp $	*/

/*
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
 */

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from
 * the last call to
 *	setjmp(a)
 * by restoring the previous context.
 * The previous signal state is restored.
 */

#include "SYS.h"

ENTRY(setjmp)
	mov	%o0, %o3		/* Save our jmp_buf in %o3 */

	mov	1, %o0			/* SIG_BLOCK */
	mov	SYS_sigprocmask, %g1	
	clr	%o1			/* sigprocmask(SIG_BLOCK, 0) */
99:	t	ST_SYSCALL
	PINSYSCALL(SYS_sigprocmask, 99b)
	stx	%o0, [%o3 + 0x10]

	stx	%sp, [%o3 + 0x00]	/* store caller's stack pointer */
	stx	%o7, [%o3 + 0x08]	/* ... and return pc */
	retl
	 clr	%o0			/* return 0 */

ENTRY(longjmp)
	save	%sp, -CC64FSZ, %sp
	flushw
	
	mov	3, %o0			/* SIG_SETMASK */
	ldx	[%i0 + 0x10], %o1
	mov	SYS_sigprocmask, %g1	
98:	t	ST_SYSCALL
	PINSYSCALL(SYS_sigprocmask, 98b)
	
	ldx	[%i0 + 0x00], %fp
	ldx	[%i0 + 0x08], %i7
	mov	1, %i0
	movrnz	%i1, %i1, %i0		! compute v ? v : 1
	ret
	 restore
