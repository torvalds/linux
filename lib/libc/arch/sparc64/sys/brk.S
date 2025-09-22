/*	$OpenBSD: brk.S,v 1.12 2023/12/10 16:45:52 deraadt Exp $	*/
/*	$NetBSD: brk.S,v 1.9 2000/07/25 20:15:40 mycroft Exp $	*/

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
	.globl	_end

	.data
	.align	8
__minbrk:
	.xword	_end			/* lower brk limit; also for gmon code */
	END(__minbrk)
	OTYPE(__minbrk)
	.text

ENTRY_NB(brk)
#ifdef __PIC__
	PIC_PROLOGUE(%o5, %o4)
	set	__minbrk, %o4
	ldx	[%o5 + %o4], %o4
	ldx	[%o4], %o1		/* %o1 = minbrk */
	cmp	%o1, %o0		/* if (minbrk > %o0) */
	movgu	%xcc, %o1, %o0		/*	%o0 = minbrk */
	mov	%o0, %o2		/* save argument to syscall */
	mov	SYS_break, %g1
99:	t	ST_SYSCALL
	PINSYSCALL(SYS_break, 99b)
	set	__curbrk, %o3
	bcc,a,pt	%icc, 1f
	 ldx	[%o5 + %o3], %o4
	ERROR()
1:
	retl				/* success, return 0 & record new break */
	 stx	%o2, [%o4]
#else
	sethi	%hi(__minbrk), %o1	/* %o1 = minbrk */
	ldx	[%o1 + %lo(__minbrk)], %o1
	cmp	%o1, %o0		/* if (minbrk > %o0) */
	movgu	%xcc, %o1, %o0		/*	%o0 = minbrk */
	mov	%o0, %o2		/* save argument to syscall */
	mov	SYS_break, %g1
98:	t	ST_SYSCALL
	PINSYSCALL(SYS_break, 98b)
	bcc,a,pt	%icc,1f
	 sethi	%hi(__curbrk), %g1
	ERROR()
1:
	retl				/* success, return 0 & record new break */
	 stx	%o2, [%g1 + %lo(__curbrk)]
#endif
END(brk)
	.weak	brk
