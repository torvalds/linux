/*	$OpenBSD: sbrk.S,v 1.10 2023/12/10 16:45:52 deraadt Exp $	*/
/*	$NetBSD: sbrk.S,v 1.7 2000/07/25 15:14:46 mycroft Exp $	*/

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

#include <machine/asm.h>

#include "SYS.h"

	.globl	__curbrk
	.hidden	__curbrk
	.globl	_end

	.data
	.align	8
__curbrk:
	.xword	_end
	END(__curbrk)
	OTYPE(__curbrk)
	.text

ENTRY_NB(sbrk)
#ifdef __PIC__
	PIC_PROLOGUE(%o5, %o4)
	set	__curbrk, %o3
	ldx	[%o5 + %o3], %o2
	ldx	[%o2], %o3			/* %o3 = old break */
	add	%o3, %o0, %o4			/* %o4 = new break */
	mov	%o4, %o0			/* copy for syscall */
	mov	SYS_break, %g1
99:	t	ST_SYSCALL			/* break(new_break) */
	PINSYSCALL(SYS_break, 99b)
	bcc,a	1f				/* if success, */
	 mov	%o3, %o0			/*    set return value */
	ERROR()
1:
	retl					/* and update curbrk */
	 stx	%o4, [%o2]
#else
	sethi	%hi(__curbrk), %o2
	ldx	[%o2 + %lo(__curbrk)], %o3	/* %o3 = old break */
	add	%o3, %o0, %o4			/* %o4 = new break */
	mov	%o4, %o0			/* copy for syscall */
	mov	SYS_break, %g1
98:	t	ST_SYSCALL			/* break(new_break) */
	PINSYSCALL(SYS_break, 98b)
	bcc,a	1f				/* if success, */
	 mov	%o3, %o0			/*    set return value */
	ERROR()
1:
	retl					/* and update curbrk */
	 stx	%o4, [%o2 + %lo(__curbrk)]
#endif
END(sbrk)
	.weak	sbrk
