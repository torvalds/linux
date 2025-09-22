/*	$OpenBSD: brk.S,v 1.7 2022/12/08 01:25:43 guenther Exp $	*/
/*	$NetBSD: brk.S,v 1.7 2003/12/26 11:23:44 martin Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
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
 *	from: @(#)brk.s	5.2 (Berkeley) 12/17/90
 */

#include "SYS.h"

	.globl	_end
	.globl	__curbrk

	.data
	.align	3
	.type	__minbrk,#object
__minbrk:
	.quad	_end
	END(__minbrk)

/*
 * Change the data segment size
 */
ENTRY_NB(brk)
	RETGUARD_SETUP(brk, x15)
#ifdef __PIC__
	/* Setup the GOT */
	adrp	x3, :got:__minbrk
	ldr	x1, [x3, #:got_lo12:__minbrk]
#else
	ldr	x1, .Lminbrk
#endif
	/* Get the minimum allowable brk address */
	ldr	x1, [x1]

	/*
	 * Valid the address specified and set to the minimum
	 * if the address is below minbrk.
	 */
	cmp	x0, x1
	b.ge	1f
	mov	x0, x1
1:
	mov	x2, x0
	SYSTRAP(break)
	bcs	CERROR

#ifdef __PIC__
	adrp	x3, :got:__curbrk
	ldr	x1, [x3, #:got_lo12:__curbrk]
#else
	ldr	x1, .Lcurbrk
#endif
	/* Store the new address in curbrk */
	str	x2, [x1]

	/* Return 0 for success */
	mov	x0, #0x00000000
	RETGUARD_CHECK(brk, x15)
	ret

	.align	3
#if !defined(__PIC__)
.Lminbrk:
	.quad	__minbrk
.Lcurbrk:
	.quad	__curbrk
#endif
END(brk)
	.weak	brk
