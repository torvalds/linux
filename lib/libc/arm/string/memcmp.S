/*      $NetBSD: memcmp.S,v 1.3 2003/10/14 07:51:45 scw Exp $ */

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
 * Copyright (c) 2002 ARM Ltd
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
 * 3. The name of the company may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ARM LTD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ARM LTD BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

__FBSDID("$FreeBSD$");

.syntax	unified

ENTRY(memcmp)
	mov	ip, r0
#if defined(_KERNEL) && !defined(_STANDALONE)
	cmp	r2, #0x06
	beq	.Lmemcmp_6bytes
#endif
	mov	r0, #0x00

	/* Are both addresses aligned the same way? */
	cmp	r2, #0x00
	eorsne	r3, ip, r1
	RETeq			/* len == 0, or same addresses! */
	tst	r3, #0x03
	subne	r2, r2, #0x01
	bne	.Lmemcmp_bytewise2	/* Badly aligned. Do it the slow way */

	/* Word-align the addresses, if necessary */
	sub	r3, r1, #0x05
	ands	r3, r3, #0x03
	add	r3, r3, r3, lsl #1
	addne	pc, pc, r3, lsl #3
	nop

	/* Compare up to 3 bytes */
	ldrb	r0, [ip], #0x01
	ldrb	r3, [r1], #0x01
	subs	r0, r0, r3
	RETne
	subs	r2, r2, #0x01
	RETeq

	/* Compare up to 2 bytes */
	ldrb	r0, [ip], #0x01
	ldrb	r3, [r1], #0x01
	subs	r0, r0, r3
	RETne
	subs	r2, r2, #0x01
	RETeq

	/* Compare 1 byte */
	ldrb	r0, [ip], #0x01
	ldrb	r3, [r1], #0x01
	subs	r0, r0, r3
	RETne
	subs	r2, r2, #0x01
	RETeq

	/* Compare 4 bytes at a time, if possible */
	subs	r2, r2, #0x04
	bcc	.Lmemcmp_bytewise
.Lmemcmp_word_aligned:
	ldr	r0, [ip], #0x04
	ldr	r3, [r1], #0x04
	subs	r2, r2, #0x04
	cmpcs	r0, r3
	beq	.Lmemcmp_word_aligned
	sub	r0, r0, r3

	/* Correct for extra subtraction, and check if done */
	adds	r2, r2, #0x04
	cmpeq	r0, #0x00		/* If done, did all bytes match? */
	RETeq			/* Yup. Just return */

	/* Re-do the final word byte-wise */
	sub	ip, ip, #0x04
	sub	r1, r1, #0x04

.Lmemcmp_bytewise:
	add	r2, r2, #0x03
.Lmemcmp_bytewise2:
	ldrb	r0, [ip], #0x01
	ldrb	r3, [r1], #0x01
	subs	r2, r2, #0x01
	cmpcs	r0, r3
	beq	.Lmemcmp_bytewise2
	sub	r0, r0, r3
	RET

#if defined(_KERNEL) && !defined(_STANDALONE)
	/*
	 * 6 byte compares are very common, thanks to the network stack.
	 * This code is hand-scheduled to reduce the number of stalls for
	 * load results. Everything else being equal, this will be ~32%
	 * faster than a byte-wise memcmp.
	 */
	.align	5
.Lmemcmp_6bytes:
	ldrb	r3, [r1, #0x00]		/* r3 = b2#0 */
	ldrb	r0, [ip, #0x00]		/* r0 = b1#0 */
	ldrb	r2, [r1, #0x01]		/* r2 = b2#1 */
	subs	r0, r0, r3		/* r0 = b1#0 - b2#0 */
	ldreqb	r3, [ip, #0x01]		/* r3 = b1#1 */
	RETne			/* Return if mismatch on #0 */
	subs	r0, r3, r2		/* r0 = b1#1 - b2#1 */
	ldreqb	r3, [r1, #0x02]		/* r3 = b2#2 */
	ldreqb	r0, [ip, #0x02]		/* r0 = b1#2 */
	RETne			/* Return if mismatch on #1 */
	ldrb	r2, [r1, #0x03]		/* r2 = b2#3 */
	subs	r0, r0, r3		/* r0 = b1#2 - b2#2 */
	ldreqb	r3, [ip, #0x03]		/* r3 = b1#3 */
	RETne			/* Return if mismatch on #2 */
	subs	r0, r3, r2		/* r0 = b1#3 - b2#3 */
	ldreqb	r3, [r1, #0x04]		/* r3 = b2#4 */
	ldreqb	r0, [ip, #0x04]		/* r0 = b1#4 */
	RETne			/* Return if mismatch on #3 */
	ldrb	r2, [r1, #0x05]		/* r2 = b2#5 */
	subs	r0, r0, r3		/* r0 = b1#4 - b2#4 */
	ldreqb	r3, [ip, #0x05]		/* r3 = b1#5 */
	RETne			/* Return if mismatch on #4 */
	sub	r0, r3, r2		/* r0 = b1#5 - b2#5 */
	RET
#endif
END(memcmp)

	.section .note.GNU-stack,"",%progbits
