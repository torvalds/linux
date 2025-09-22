/*	$OpenBSD: _setjmp.S,v 1.4 2007/11/25 18:25:36 deraadt Exp $	*/
/*	$NetBSD: _setjmp.S,v 1.1.1.1 1998/06/20 05:18:14 eeh Exp $	*/

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
 * Header: _setjmp.s,v 1.1 91/07/06 16:45:53 torek Exp
 */

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v?v:1)" from
 * the last call to
 *	_setjmp(a)
 * by unwinding the call stack.
 * The previous signal state is NOT restored.
 */

#include "DEFS.h"

ENTRY(_setjmp)
 	btst	1, %sp
	bne,a,pt %xcc, 1f	! 32 or 64-bit stack?
	 std	%sp, [%o0+0]	! 32-bit stack pointer & return pc
	
	stx	%sp, [%o0+0]	! 64-bit stack pointer
	stx	%o7, [%o0+8]	! 64-bit return pc
	stx	%fp, [%o0+16]	! 64-bit frame pointer
	retl
	 clr	%o0
1:	
	st	%fp, [%o0+8]	! 32-bit frame pointer
	retl
	 clr	%o0

ENTRY(_longjmp)
	set	1, %g6
	btst	1, %fp		! 32 or 64-bit stack?
	movrz	%o1, %o1, %g6	! compute v ? v : 1 in a global register
	bne,pt	%xcc, 2f	! 64-bit
	 mov	%o0, %g1	! save a in another global register
	ld	[%g1+8], %g7	/* get caller's frame */
	
1:
	cmp	%fp, %g7	! compare against desired frame
	bl,a	1b		! if below,
	 restore		!    pop frame and loop
	bne,pn	%icc, Llongjmpbotch		! if there,
	 ldd	[%g1+0], %o2	!    fetch return %sp and pc, and get out
	ba,a,pt	%icc, 4f
2:
	ldx	[%g1+16], %g7	/* get caller's frame */

3:
	cmp	%fp, %g7	! compare against desired frame
	bl,a,pt	%xcc, 3b	! if below,
	 restore		!    pop frame and loop
	bne,pn	%xcc, Llongjmpbotch	! if there,
	 ldx	[%g1+0], %o2	!    fetch return %sp and pc, and get out
	ldx	[%g1+8], %o3
4:
	cmp	%o2, %sp	! %sp must not decrease
	blt,pn	%icc, Llongjmpbotch
	 movge	%icc, %o2, %sp	! it is OK, put it in place
	jmp	%o3 + 8		! success, return %g6
	 mov	%g6, %o0

Lbotch:
	call	_longjmperror	! otherwise, went too far; bomb out
	nop
	unimp	0
				! otherwise, went too far; bomb out

	


