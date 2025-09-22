/*	$OpenBSD: memcpy.S,v 1.6 2015/06/08 14:22:05 jsg Exp $	*/
/*	$NetBSD: memcpy.S,v 1.2 2001/11/20 00:29:20 chris Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Neil A. Carson and Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

/*
 * This is one fun bit of code ...
 * Some easy listening music is suggested while trying to understand this
 * code e.g. Iron Maiden
 *
 * For anyone attempting to understand it :
 *
 * The core code is implemented here with simple stubs for memcpy()
 * memmove() and bcopy().
 *
 * All local labels are prefixed with Lmemcpy_
 * Following the prefix a label starting f is used in the forward copy code
 * while a label using b is used in the backwards copy code
 * The source and destination addresses determine whether a forward or
 * backward copy is performed.
 * Separate bits of code are used to deal with the following situations
 * for both the forward and backwards copy.
 * unaligned source address
 * unaligned destination address
 * Separate copy routines are used to produce an optimised result for each
 * of these cases.
 * The copy code will use LDM/STM instructions to copy up to 32 bytes at
 * a time where possible.
 *
 * Note: r12 (aka ip) can be trashed during the function along with
 * r0-r3 although r0-r2 have defined uses i.e. src, dest, len through out.
 * Additional registers are preserved prior to use i.e. r4, r5 & lr
 *
 * Apologies for the state of the comments ;-)
 */

.syntax unified

ENTRY(memcpy)
ENTRY_NP(memmove)
	/* Determine copy direction */
	cmp	r1, r0

	moveq	pc, lr

	/* save leaf functions having to store this away */
	stmdb	sp!, {r0, lr}		/* memcpy() returns dest addr */

	bcc	Lmemcpy_backwards

	/* start of forwards copy */	
	subs	r2, r2, #4
	blt	Lmemcpy_fl4		/* less than 4 bytes */
	ands	r12, r0, #3
	bne	Lmemcpy_fdestul		/* oh unaligned destination addr */
	ands	r12, r1, #3
	bne	Lmemcpy_fsrcul		/* oh unaligned source addr */

Lmemcpy_ft8:
	/* We have aligned source and destination */
	subs	r2, r2, #8
	blt	Lmemcpy_fl12		/* less than 12 bytes (4 from above) */
	subs	r2, r2, #0x14         
	blt	Lmemcpy_fl32		/* less than 32 bytes (12 from above) */
	stmdb	sp!, {r4}		/* borrow r4 */

	/* blat 32 bytes at a time */
	/* XXX for really big copies perhaps we should use more registers */
Lmemcpy_floop32:	
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	subs	r2, r2, #0x20         
	bge	Lmemcpy_floop32

	cmn	r2, #0x10
	ldmiage	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmiage	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10         
	ldmia	sp!, {r4}		/* return r4 */

Lmemcpy_fl32:
	adds	r2, r2, #0x14         

	/* blat 12 bytes at a time */
Lmemcpy_floop12:
	ldmiage	r1!, {r3, r12, lr}
	stmiage	r0!, {r3, r12, lr}
	subsge	r2, r2, #0x0c
	bge	Lmemcpy_floop12

Lmemcpy_fl12:
	adds	r2, r2, #8
	blt	Lmemcpy_fl4

	subs	r2, r2, #4
	ldrlt	r3, [r1], #4
	strlt	r3, [r0], #4
	ldmiage	r1!, {r3, r12}
	stmiage	r0!, {r3, r12}
	subge	r2, r2, #4

Lmemcpy_fl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
#ifdef __APCS_26_
	ldmiaeq	sp!, {r0, pc}^		/* done */
#else
	ldmiaeq	sp!, {r0, pc}		/* done */
#endif
	/* copy the crud byte at a time */
	cmp	r2, #2
	ldrb	r3, [r1], #1
	strb	r3, [r0], #1
	ldrbge	r3, [r1], #1
	strbge	r3, [r0], #1
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0], #1
	ldmia	sp!, {r0, pc}

	/* erg - unaligned destination */
Lmemcpy_fdestul:
	rsb	r12, r12, #4
	cmp	r12, #2

	/* align destination with byte copies */
	ldrb	r3, [r1], #1
	strb	r3, [r0], #1
	ldrbge	r3, [r1], #1
	strbge	r3, [r0], #1
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0], #1
	subs	r2, r2, r12
	blt	Lmemcpy_fl4		/* less the 4 bytes */

	ands	r12, r1, #3
	beq	Lmemcpy_ft8		/* we have an aligned source */

	/* erg - unaligned source */
	/* This is where it gets nasty ... */
Lmemcpy_fsrcul:
	bic	r1, r1, #3
	ldr	lr, [r1], #4
	cmp	r12, #2
	bgt	Lmemcpy_fsrcul3
	beq	Lmemcpy_fsrcul2
	cmp	r2, #0x0c            
	blt	Lmemcpy_fsrcul1loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_fsrcul1loop16:
	mov	r3, lr, lsr #8
	ldmia	r1!, {r4, r5, r12, lr}
	orr	r3, r3, r4, lsl #24
	mov	r4, r4, lsr #8
	orr	r4, r4, r5, lsl #24
	mov	r5, r5, lsr #8
	orr	r5, r5, r12, lsl #24
	mov	r12, r12, lsr #8
	orr	r12, r12, lr, lsl #24
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_fsrcul1loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_fsrcul1l4

Lmemcpy_fsrcul1loop4:
	mov	r12, lr, lsr #8
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #24
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	Lmemcpy_fsrcul1loop4

Lmemcpy_fsrcul1l4:
	sub	r1, r1, #3
	b	Lmemcpy_fl4

Lmemcpy_fsrcul2:
	cmp	r2, #0x0c            
	blt	Lmemcpy_fsrcul2loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_fsrcul2loop16:
	mov	r3, lr, lsr #16
	ldmia	r1!, {r4, r5, r12, lr}
	orr	r3, r3, r4, lsl #16
	mov	r4, r4, lsr #16
	orr	r4, r4, r5, lsl #16
	mov	r5, r5, lsr #16
	orr	r5, r5, r12, lsl #16
	mov	r12, r12, lsr #16
	orr	r12, r12, lr, lsl #16
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_fsrcul2loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_fsrcul2l4

Lmemcpy_fsrcul2loop4:
	mov	r12, lr, lsr #16
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #16
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	Lmemcpy_fsrcul2loop4

Lmemcpy_fsrcul2l4:
	sub	r1, r1, #2
	b	Lmemcpy_fl4

Lmemcpy_fsrcul3:
	cmp	r2, #0x0c            
	blt	Lmemcpy_fsrcul3loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_fsrcul3loop16:
	mov	r3, lr, lsr #24
	ldmia	r1!, {r4, r5, r12, lr}
	orr	r3, r3, r4, lsl #8
	mov	r4, r4, lsr #24
	orr	r4, r4, r5, lsl #8
	mov	r5, r5, lsr #24
	orr	r5, r5, r12, lsl #8
	mov	r12, r12, lsr #24
	orr	r12, r12, lr, lsl #8
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_fsrcul3loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_fsrcul3l4

Lmemcpy_fsrcul3loop4:
	mov	r12, lr, lsr #24
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #8
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	Lmemcpy_fsrcul3loop4

Lmemcpy_fsrcul3l4:
	sub	r1, r1, #1
	b	Lmemcpy_fl4

Lmemcpy_backwards:
	add	r1, r1, r2
	add	r0, r0, r2
	subs	r2, r2, #4
	blt	Lmemcpy_bl4		/* less than 4 bytes */
	ands	r12, r0, #3
	bne	Lmemcpy_bdestul		/* oh unaligned destination addr */
	ands	r12, r1, #3
	bne	Lmemcpy_bsrcul		/* oh unaligned source addr */

Lmemcpy_bt8:
	/* We have aligned source and destination */
	subs	r2, r2, #8
	blt	Lmemcpy_bl12		/* less than 12 bytes (4 from above) */
	stmdb	sp!, {r4}
	subs	r2, r2, #0x14		/* less than 32 bytes (12 from above) */
	blt	Lmemcpy_bl32

	/* blat 32 bytes at a time */
	/* XXX for really big copies perhaps we should use more registers */
Lmemcpy_bloop32:
	ldmdb	r1!, {r3, r4, r12, lr}
	stmdb	r0!, {r3, r4, r12, lr}
	ldmdb	r1!, {r3, r4, r12, lr}
	stmdb	r0!, {r3, r4, r12, lr}
	subs	r2, r2, #0x20         
	bge	Lmemcpy_bloop32

Lmemcpy_bl32:
	cmn	r2, #0x10            
	ldmdbge	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmdbge	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10         
	adds	r2, r2, #0x14         
	ldmdbge	r1!, {r3, r12, lr}	/* blat a remaining 12 bytes */
	stmdbge	r0!, {r3, r12, lr}
	subge	r2, r2, #0x0c         
	ldmia	sp!, {r4}

Lmemcpy_bl12:
	adds	r2, r2, #8
	blt	Lmemcpy_bl4
	subs	r2, r2, #4
	ldrlt	r3, [r1, #-4]!
	strlt	r3, [r0, #-4]!
	ldmdbge	r1!, {r3, r12}
	stmdbge	r0!, {r3, r12}
	subge	r2, r2, #4

Lmemcpy_bl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
	ldmiaeq	sp!, {r0, pc}

	/* copy the crud byte at a time */
	cmp	r2, #2
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
	ldrbgt	r3, [r1, #-1]!
	strbgt	r3, [r0, #-1]!
	ldmia	sp!, {r0, pc}

	/* erg - unaligned destination */
Lmemcpy_bdestul:
	cmp	r12, #2

	/* align destination with byte copies */
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
	ldrbgt	r3, [r1, #-1]!
	strbgt	r3, [r0, #-1]!
	subs	r2, r2, r12
	blt	Lmemcpy_bl4		/* less than 4 bytes to go */
	ands	r12, r1, #3
	beq	Lmemcpy_bt8		/* we have an aligned source */

	/* erg - unaligned source */
	/* This is where it gets nasty ... */
Lmemcpy_bsrcul:
	bic	r1, r1, #3
	ldr	r3, [r1, #0]
	cmp	r12, #2
	blt	Lmemcpy_bsrcul1
	beq	Lmemcpy_bsrcul2
	cmp	r2, #0x0c            
	blt	Lmemcpy_bsrcul3loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_bsrcul3loop16:
	mov	lr, r3, lsl #8
	ldmdb	r1!, {r3-r5, r12}
	orr	lr, lr, r12, lsr #24
	mov	r12, r12, lsl #8
	orr	r12, r12, r5, lsr #24
	mov	r5, r5, lsl #8
	orr	r5, r5, r4, lsr #24
	mov	r4, r4, lsl #8
	orr	r4, r4, r3, lsr #24
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_bsrcul3loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_bsrcul3l4

Lmemcpy_bsrcul3loop4:
	mov	r12, r3, lsl #8
	ldr	r3, [r1, #-4]!
	orr	r12, r12, r3, lsr #24
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	Lmemcpy_bsrcul3loop4

Lmemcpy_bsrcul3l4:
	add	r1, r1, #3
	b	Lmemcpy_bl4

Lmemcpy_bsrcul2:
	cmp	r2, #0x0c            
	blt	Lmemcpy_bsrcul2loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_bsrcul2loop16:
	mov	lr, r3, lsl #16
	ldmdb	r1!, {r3-r5, r12}
	orr	lr, lr, r12, lsr #16
	mov	r12, r12, lsl #16
	orr	r12, r12, r5, lsr #16
	mov	r5, r5, lsl #16
	orr	r5, r5, r4, lsr #16
	mov	r4, r4, lsl #16
	orr	r4, r4, r3, lsr #16
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_bsrcul2loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_bsrcul2l4

Lmemcpy_bsrcul2loop4:
	mov	r12, r3, lsl #16
	ldr	r3, [r1, #-4]!
	orr	r12, r12, r3, lsr #16
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	Lmemcpy_bsrcul2loop4

Lmemcpy_bsrcul2l4:
	add	r1, r1, #2
	b	Lmemcpy_bl4

Lmemcpy_bsrcul1:
	cmp	r2, #0x0c            
	blt	Lmemcpy_bsrcul1loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

Lmemcpy_bsrcul1loop32:
	mov	lr, r3, lsl #24
	ldmdb	r1!, {r3-r5, r12}
	orr	lr, lr, r12, lsr #8
	mov	r12, r12, lsl #24
	orr	r12, r12, r5, lsr #8
	mov	r5, r5, lsl #24
	orr	r5, r5, r4, lsr #8
	mov	r4, r4, lsl #24
	orr	r4, r4, r3, lsr #8
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	Lmemcpy_bsrcul1loop32
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	Lmemcpy_bsrcul1l4

Lmemcpy_bsrcul1loop4:
	mov	r12, r3, lsl #24
	ldr	r3, [r1, #-4]!
	orr	r12, r12, r3, lsr #8
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	Lmemcpy_bsrcul1loop4

Lmemcpy_bsrcul1l4:
	add	r1, r1, #1
	b	Lmemcpy_bl4
	
