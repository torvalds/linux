/*	$NetBSD: memset.S,v 1.4 2003/10/14 07:51:45 scw Exp $	*/

/*
 * Copyright 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Steve C. Woodford for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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
__FBSDID("$FreeBSD$");

.syntax	unified

/*
 * memset: Sets a block of memory to the specified value
 *
 * On entry:
 *   r0 - dest address
 *   r1 - byte to write
 *   r2 - number of bytes to write
 *
 * On exit:
 *   r0 - dest address
 */
#ifdef _BZERO
/* LINTSTUB: Func: void bzero(void *, size_t) */
ENTRY(bzero)
	mov	r3, #0x00
#else
/* LINTSTUB: Func: void *memset(void *, int, size_t) */
ENTRY(memset)
	and	r3, r1, #0xff		/* We deal with bytes */
	mov	r1, r2
#endif
	cmp	r1, #0x04		/* Do we have less than 4 bytes */
	mov	ip, r0
	blt	.Lmemset_lessthanfour

	/* Ok first we will word align the address */
	ands	r2, ip, #0x03		/* Get the bottom two bits */
	bne	.Lmemset_wordunaligned	/* The address is not word aligned */

	/* We are now word aligned */
.Lmemset_wordaligned:
#ifndef _BZERO
	orr	r3, r3, r3, lsl #8	/* Extend value to 16-bits */
#endif
#ifdef _ARM_ARCH_5E
	tst	ip, #0x04		/* Quad-align for armv5e */
#else
	cmp	r1, #0x10
#endif
#ifndef _BZERO
	orr	r3, r3, r3, lsl #16	/* Extend value to 32-bits */
#endif
#ifdef _ARM_ARCH_5E
	itt	ne
	subne	r1, r1, #0x04		/* Quad-align if necessary */
	strne	r3, [ip], #0x04
	cmp	r1, #0x10
#endif
	blt	.Lmemset_loop4		/* If less than 16 then use words */
	mov	r2, r3			/* Duplicate data */
	cmp	r1, #0x80		/* If < 128 then skip the big loop */
	blt	.Lmemset_loop32

	/* Do 128 bytes at a time */
.Lmemset_loop128:
	subs	r1, r1, #0x80
#ifdef _ARM_ARCH_5E
	itttt	ge
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	itttt	ge
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	itttt	ge
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	itttt	ge
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
#else
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
#endif
	bgt	.Lmemset_loop128
	it	eq
	RETeq			/* Zero length so just exit */

	add	r1, r1, #0x80		/* Adjust for extra sub */

	/* Do 32 bytes at a time */
.Lmemset_loop32:
	subs	r1, r1, #0x20
	itttt	ge
#ifdef _ARM_ARCH_5E
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
#else
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
#endif
	bgt	.Lmemset_loop32
	it	eq
	RETeq			/* Zero length so just exit */

	adds	r1, r1, #0x10		/* Partially adjust for extra sub */

	/* Deal with 16 bytes or more */
	itt	ge
#ifdef _ARM_ARCH_5E
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
#else
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
#endif
	it	eq
	RETeq			/* Zero length so just exit */

	it	lt
	addlt	r1, r1, #0x10		/* Possibly adjust for extra sub */

	/* We have at least 4 bytes so copy as words */
.Lmemset_loop4:
	subs	r1, r1, #0x04
	it	ge
	strge	r3, [ip], #0x04
	bgt	.Lmemset_loop4
	it	eq
	RETeq			/* Zero length so just exit */

#ifdef _ARM_ARCH_5E
	/* Compensate for 64-bit alignment check */
	adds	r1, r1, #0x04
	it	eq
	RETeq
	cmp	r1, #2
#else
	cmp	r1, #-2
#endif

	strb	r3, [ip], #0x01		/* Set 1 byte */
	it	ge
	strbge	r3, [ip], #0x01		/* Set another byte */
	it	gt
	strbgt	r3, [ip]		/* and a third */
	RET			/* Exit */

.Lmemset_wordunaligned:
	rsb	r2, r2, #0x004
	strb	r3, [ip], #0x01		/* Set 1 byte */
	cmp	r2, #0x02
	it	ge
	strbge	r3, [ip], #0x01		/* Set another byte */
	sub	r1, r1, r2
	it	gt
	strbgt	r3, [ip], #0x01		/* and a third */
	cmp	r1, #0x04		/* More than 4 bytes left? */
	it	ge
	bge	.Lmemset_wordaligned	/* Yup */

.Lmemset_lessthanfour:
	cmp	r1, #0x00
	it	eq
	RETeq			/* Zero length so exit */
	strb	r3, [ip], #0x01		/* Set 1 byte */
	cmp	r1, #0x02
	it	ge
	strbge	r3, [ip], #0x01		/* Set another byte */
	it	gt
	strbgt	r3, [ip]		/* and a third */
	RET			/* Exit */
#ifdef _BZERO
END(bzero)
#else
END(memset)
#endif

	.section .note.GNU-stack,"",%progbits
