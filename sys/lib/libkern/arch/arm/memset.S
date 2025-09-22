/*	$OpenBSD: memset.S,v 1.4 2015/06/08 14:22:05 jsg Exp $	*/
/*	$NetBSD: memset.S,v 1.1 2000/12/29 20:51:57 bjh21 Exp $	*/

/*
 * Copyright (c) 1995 Mark Brinicombe.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>

/*
 * Sets a block of memory to the specified value
 *
 * On entry:
 *   r0 - dest address
 *   r1 - byte to write
 *   r2 - number of bytes to write
 *
 * On exit:
 *   r0 - dest address
 */

.syntax unified

ENTRY(memset)
	stmfd	sp!, {r0}		/* Remember address for return value */
	and	r1, r1, #0x000000ff	/* We write bytes */

	cmp	r2, #0x00000004		/* Do we have less than 4 bytes */
	blt	Lmemset_lessthanfour

	/* Ok first we will word align the address */

	ands	r3, r0, #0x00000003	/* Get the bottom two bits */
	beq	Lmemset_addraligned	/* The address is word aligned */

	rsb	r3, r3, #0x00000004
	sub	r2, r2, r3
	cmp	r3, #0x00000002
	strb	r1, [r0], #0x0001	/* Set 1 byte */
	strbge	r1, [r0], #0x0001	/* Set another byte */
	strbgt	r1, [r0], #0x0001	/* and a third */

	cmp	r2, #0x00000004
	blt	Lmemset_lessthanfour

	/* Now we must be word aligned */

Lmemset_addraligned:

	orr	r3, r1, r1, lsl #8	/* Repeat the byte into a word */
	orr	r3, r3, r3, lsl #16

	/* We know we have at least 4 bytes ... */

	cmp	r2, #0x00000020		/* If less than 32 then use words */
	blt	Lmemset_lessthan32

	/* We have at least 32 so lets use quad words */

	stmfd	sp!, {r4-r6}		/* Store registers */
	mov	r4, r3			/* Duplicate data */
	mov	r5, r3
	mov	r6, r3

Lmemset_loop16:
	stmia	r0!, {r3-r6}		/* Store 16 bytes */
	sub	r2, r2, #0x00000010	/* Adjust count */
	cmp	r2, #0x00000010		/* Still got at least 16 bytes ? */
	bgt	Lmemset_loop16

	ldmfd	sp!, {r4-r6}		/* Restore registers */

	/* Do we need to set some words as well ? */

	cmp	r2, #0x00000004
	blt	Lmemset_lessthanfour

	/* Have either less than 16 or less than 32 depending on route taken */

Lmemset_lessthan32:

	/* We have at least 4 bytes so copy as words */

Lmemset_loop4:
	str	r3, [r0], #0x0004
	sub	r2, r2, #0x0004
	cmp	r2, #0x00000004
	bge	Lmemset_loop4

Lmemset_lessthanfour:
	cmp	r2, #0x00000000
	ldmfdeq	sp!, {r0}
	moveq	pc, lr			/* Zero length so exit */

	cmp	r2, #0x00000002
	strb	r1, [r0], #0x0001	/* Set 1 byte */
	strbge	r1, [r0], #0x0001	/* Set another byte */
	strbgt	r1, [r0], #0x0001	/* and a third */

	ldmfd	sp!, {r0}
	mov	pc, lr			/* Exit */
