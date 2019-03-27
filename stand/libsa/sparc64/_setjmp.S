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
 *
 *	$Header: _setjmp.s,v 1.1 91/07/06 16:45:53 torek Exp
 */

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
	.asciz "@(#)_setjmp.s	8.1 (Berkeley) 6/4/93"
#else
	RCSID("$NetBSD: _setjmp.S,v 1.4 1998/10/08 02:27:59 eeh Exp $")
#endif
#endif /* LIBC_SCCS and not lint */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#define	_JB_FP		0x0
#define	_JB_PC		0x8
#define	_JB_SP		0x10

	.register %g2,#ignore
	.register %g3,#ignore

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v?v:1)" from
 * the last call to
 *	setjmp(a)
 * by restoring the previous context.
 */

ENTRY(_setjmp)
	stx	%sp, [%o0 + _JB_SP]
	stx	%o7, [%o0 + _JB_PC]
	stx	%fp, [%o0 + _JB_FP]
	retl
	 clr	%o0
END(_setjmp)

ENTRY(_longjmp)
	mov	1, %g1
	movrnz	%o1, %o1, %g1
	mov	%o0, %g2
	ldx	[%g2 + _JB_FP], %g3
1:	cmp	%fp, %g3
	bl,a	1b
	 restore
	be,a	2f
	 ldx	[%g2 + _JB_SP], %o0

.Lbotch:
	illtrap

2:	cmp	%o0, %sp
	bge,a	3f
	 mov	%o0, %sp
	b,a	.Lbotch
	 nop
3:	ldx	[%g2 + _JB_PC], %o7
	retl
	 mov	%g1, %o0
END(_longjmp)
