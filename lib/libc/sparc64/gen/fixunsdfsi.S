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
 * from: Header: fixunsdfsi.s,v 1.3 91/10/08 00:03:15 torek Exp
 */

#include <machine/asm.h>

#if defined(LIBC_SCCS) && !defined(lint)
	.asciz "@(#)fixunsdfsi.s	8.1 (Berkeley) 6/4/93"
#if 0
	RCSID("$NetBSD: fixunsdfsi.S,v 1.3 2000/07/25 04:26:12 mycroft Exp $")
#endif
#endif /* LIBC_SCCS and not lint */
__FBSDID("$FreeBSD$");

/*
 * Convert double to unsigned integer (for gcc).
 *
 * I have made the output for NaN agree with the Sun compiler, not
 * that it really matters, by using `fbul,a'.
 */


	.align	8
.Lbig:
	.word	0x43e00000		! .double 2^63
	.word	0			! (who me, not trust the assembler?)

/*
 * Same as above but to unsigned long
 */
ENTRY(__dtoul)
	PIC_PROLOGUE(%o4, %o5)
	sub	%sp, 16, %sp
	std	%f2, [%sp + CCFSZ + SPOFF + 8]
	SET(.Lbig, %o5, %o3)
	ldd	[%o3], %f2
	fcmped	%f0, %f2		! d < 2^63, or NaN, or -Inf?
	fbul,a	1f			! if so, use fdtox to convert to long
	 fdtox	%f0, %f0		!        (this includes negatives!)

	! d does not fit in a long, so subtract 2^63, convert,
	! and add 2^63 again (sigh).  Just hope the intermediate
	! fits (if not, the result is undefined anyway).

	fsubd	%f0, %f2, %f0		! d -= 2^63
	fdtox	%f0, %f0		! convert to long
	std	%f0, [%sp + CCFSZ + SPOFF]	! move into return reg
	ldx	[%sp + CCFSZ + SPOFF], %o0
	sethi	%hi(0x80000000), %o1
	sllx	%o1, 32, %o1
	add	%o0, %o1, %o0		! add 2^63
	ldd	[%sp + CCFSZ + SPOFF + 8], %f2
	retl
	 add	%sp, 16, %sp

1:
	std	%f0, [%sp + CCFSZ + SPOFF]	! return result
	ldx	[%sp + CCFSZ + SPOFF], %o0
	ldd	[%sp + CCFSZ + SPOFF + 8], %f2
	retl
	 add	%sp, 16, %sp
END(__dtoul)
