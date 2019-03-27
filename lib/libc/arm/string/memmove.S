/*	$NetBSD: memmove.S,v 1.4 2003/10/14 07:51:45 scw Exp $	*/

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
__FBSDID("$FreeBSD$");

.syntax	unified

#ifndef _BCOPY
/* LINTSTUB: Func: void *memmove(void *, const void *, size_t) */
ENTRY(memmove)
#else
/* bcopy = memcpy/memmove with arguments reversed. */
/* LINTSTUB: Func: void bcopy(void *, void *, size_t) */
ENTRY(bcopy)
	/* switch the source and destination registers */
	eor     r0, r1, r0 
	eor     r1, r0, r1 
	eor     r0, r1, r0 
#endif
	/* Do the buffers overlap? */
	cmp	r0, r1
	it	eq
	RETeq		/* Bail now if src/dst are the same */
	ite	cc
	subcc	r3, r0, r1	/* if (dst > src) r3 = dst - src */
	subcs	r3, r1, r0	/* if (src > dsr) r3 = src - dst */
	cmp	r3, r2		/* if (r3 < len) we have an overlap */
	bcc	PIC_SYM(_C_LABEL(memcpy), PLT)

	/* Determine copy direction */
	cmp	r1, r0
	it	cc
	bcc	.Lmemmove_backwards

	itt	eq
	moveq	r0, #0			/* Quick abort for len=0 */
	RETeq

	stmdb	sp!, {r0, lr}		/* memmove() returns dest addr */
	subs	r2, r2, #4
	blt	.Lmemmove_fl4		/* less than 4 bytes */
	ands	r12, r0, #3
	bne	.Lmemmove_fdestul	/* oh unaligned destination addr */
	ands	r12, r1, #3
	bne	.Lmemmove_fsrcul		/* oh unaligned source addr */

.Lmemmove_ft8:
	/* We have aligned source and destination */
	subs	r2, r2, #8
	blt	.Lmemmove_fl12		/* less than 12 bytes (4 from above) */
	subs	r2, r2, #0x14         
	blt	.Lmemmove_fl32		/* less than 32 bytes (12 from above) */
	stmdb	sp!, {r4}		/* borrow r4 */

	/* blat 32 bytes at a time */
	/* XXX for really big copies perhaps we should use more registers */
.Lmemmove_floop32:	
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	subs	r2, r2, #0x20         
	bge	.Lmemmove_floop32

	cmn	r2, #0x10
	ittt	ge
	ldmiage	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmiage	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10         
	ldmia	sp!, {r4}		/* return r4 */

.Lmemmove_fl32:
	adds	r2, r2, #0x14         

	/* blat 12 bytes at a time */
.Lmemmove_floop12:
	ittt	ge
	ldmiage	r1!, {r3, r12, lr}
	stmiage	r0!, {r3, r12, lr}
	subsge	r2, r2, #0x0c         
	bge	.Lmemmove_floop12

.Lmemmove_fl12:
	adds	r2, r2, #8
	blt	.Lmemmove_fl4

	subs	r2, r2, #4
	itt	lt
	ldrlt	r3, [r1], #4
	strlt	r3, [r0], #4
	ittt	ge
	ldmiage	r1!, {r3, r12}
	stmiage	r0!, {r3, r12}
	subge	r2, r2, #4

.Lmemmove_fl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
	it	eq
	ldmiaeq	sp!, {r0, pc}		/* done */

	/* copy the crud byte at a time */
	cmp	r2, #2
	ldrb	r3, [r1], #1
	strb	r3, [r0], #1
	itt	ge
	ldrbge	r3, [r1], #1
	strbge	r3, [r0], #1
	itt	gt
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0], #1
	ldmia	sp!, {r0, pc}

	/* erg - unaligned destination */
.Lmemmove_fdestul:
	rsb	r12, r12, #4
	cmp	r12, #2

	/* align destination with byte copies */
	ldrb	r3, [r1], #1
	strb	r3, [r0], #1
	itt	ge
	ldrbge	r3, [r1], #1
	strbge	r3, [r0], #1
	itt	gt
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0], #1
	subs	r2, r2, r12
	blt	.Lmemmove_fl4		/* less the 4 bytes */

	ands	r12, r1, #3
	beq	.Lmemmove_ft8		/* we have an aligned source */

	/* erg - unaligned source */
	/* This is where it gets nasty ... */
.Lmemmove_fsrcul:
	bic	r1, r1, #3
	ldr	lr, [r1], #4
	cmp	r12, #2
	bgt	.Lmemmove_fsrcul3
	beq	.Lmemmove_fsrcul2
	cmp	r2, #0x0c            
	blt	.Lmemmove_fsrcul1loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

.Lmemmove_fsrcul1loop16:
#ifdef __ARMEB__
	mov	r3, lr, lsl #8
#else
	mov	r3, lr, lsr #8
#endif
	ldmia	r1!, {r4, r5, r12, lr}
#ifdef __ARMEB__
	orr	r3, r3, r4, lsr #24
	mov	r4, r4, lsl #8
	orr	r4, r4, r5, lsr #24
	mov	r5, r5, lsl #8
	orr	r5, r5, r12, lsr #24
	mov	r12, r12, lsl #8
	orr	r12, r12, lr, lsr #24
#else
	orr	r3, r3, r4, lsl #24
	mov	r4, r4, lsr #8
	orr	r4, r4, r5, lsl #24
	mov	r5, r5, lsr #8
	orr	r5, r5, r12, lsl #24
	mov	r12, r12, lsr #8
	orr	r12, r12, lr, lsl #24
#endif
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_fsrcul1loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_fsrcul1l4

.Lmemmove_fsrcul1loop4:
#ifdef __ARMEB__
	mov	r12, lr, lsl #8
#else
	mov	r12, lr, lsr #8
#endif
	ldr	lr, [r1], #4
#ifdef __ARMEB__
	orr	r12, r12, lr, lsr #24
#else
	orr	r12, r12, lr, lsl #24
#endif
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemmove_fsrcul1loop4

.Lmemmove_fsrcul1l4:
	sub	r1, r1, #3
	b	.Lmemmove_fl4

.Lmemmove_fsrcul2:
	cmp	r2, #0x0c            
	blt	.Lmemmove_fsrcul2loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

.Lmemmove_fsrcul2loop16:
#ifdef __ARMEB__
	mov	r3, lr, lsl #16
#else
	mov	r3, lr, lsr #16
#endif
	ldmia	r1!, {r4, r5, r12, lr}
#ifdef __ARMEB__
	orr	r3, r3, r4, lsr #16
	mov	r4, r4, lsl #16
	orr	r4, r4, r5, lsr #16
	mov	r5, r5, lsl #16
	orr	r5, r5, r12, lsr #16
	mov	r12, r12, lsl #16
	orr	r12, r12, lr, lsr #16
#else
	orr	r3, r3, r4, lsl #16
	mov	r4, r4, lsr #16
	orr	r4, r4, r5, lsl #16
	mov	r5, r5, lsr #16
	orr	r5, r5, r12, lsl #16
	mov	r12, r12, lsr #16
	orr	r12, r12, lr, lsl #16
#endif
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_fsrcul2loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_fsrcul2l4

.Lmemmove_fsrcul2loop4:
#ifdef __ARMEB__
	mov	r12, lr, lsl #16
#else
	mov	r12, lr, lsr #16
#endif
	ldr	lr, [r1], #4
#ifdef __ARMEB__
	orr	r12, r12, lr, lsr #16
#else
	orr	r12, r12, lr, lsl #16
#endif
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemmove_fsrcul2loop4

.Lmemmove_fsrcul2l4:
	sub	r1, r1, #2
	b	.Lmemmove_fl4

.Lmemmove_fsrcul3:
	cmp	r2, #0x0c            
	blt	.Lmemmove_fsrcul3loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5}

.Lmemmove_fsrcul3loop16:
#ifdef __ARMEB__
	mov	r3, lr, lsl #24
#else
	mov	r3, lr, lsr #24
#endif
	ldmia	r1!, {r4, r5, r12, lr}
#ifdef __ARMEB__
	orr	r3, r3, r4, lsr #8
	mov	r4, r4, lsl #24
	orr	r4, r4, r5, lsr #8
	mov	r5, r5, lsl #24
	orr	r5, r5, r12, lsr #8
	mov	r12, r12, lsl #24
	orr	r12, r12, lr, lsr #8
#else
	orr	r3, r3, r4, lsl #8
	mov	r4, r4, lsr #24
	orr	r4, r4, r5, lsl #8
	mov	r5, r5, lsr #24
	orr	r5, r5, r12, lsl #8
	mov	r12, r12, lsr #24
	orr	r12, r12, lr, lsl #8
#endif
	stmia	r0!, {r3-r5, r12}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_fsrcul3loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_fsrcul3l4

.Lmemmove_fsrcul3loop4:
#ifdef __ARMEB__
	mov	r12, lr, lsl #24
#else
	mov	r12, lr, lsr #24
#endif
	ldr	lr, [r1], #4
#ifdef __ARMEB__
	orr	r12, r12, lr, lsr #8
#else
	orr	r12, r12, lr, lsl #8
#endif
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemmove_fsrcul3loop4

.Lmemmove_fsrcul3l4:
	sub	r1, r1, #1
	b	.Lmemmove_fl4

.Lmemmove_backwards:
	add	r1, r1, r2
	add	r0, r0, r2
	subs	r2, r2, #4
	blt	.Lmemmove_bl4		/* less than 4 bytes */
	ands	r12, r0, #3
	bne	.Lmemmove_bdestul	/* oh unaligned destination addr */
	ands	r12, r1, #3
	bne	.Lmemmove_bsrcul		/* oh unaligned source addr */

.Lmemmove_bt8:
	/* We have aligned source and destination */
	subs	r2, r2, #8
	blt	.Lmemmove_bl12		/* less than 12 bytes (4 from above) */
	stmdb	sp!, {r4, lr}
	subs	r2, r2, #0x14		/* less than 32 bytes (12 from above) */
	blt	.Lmemmove_bl32

	/* blat 32 bytes at a time */
	/* XXX for really big copies perhaps we should use more registers */
.Lmemmove_bloop32:
	ldmdb	r1!, {r3, r4, r12, lr}
	stmdb	r0!, {r3, r4, r12, lr}
	ldmdb	r1!, {r3, r4, r12, lr}
	stmdb	r0!, {r3, r4, r12, lr}
	subs	r2, r2, #0x20         
	bge	.Lmemmove_bloop32

.Lmemmove_bl32:
	cmn	r2, #0x10            
	ittt	ge
	ldmdbge	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmdbge	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10         
	adds	r2, r2, #0x14         
	ittt	ge
	ldmdbge	r1!, {r3, r12, lr}	/* blat a remaining 12 bytes */
	stmdbge	r0!, {r3, r12, lr}
	subge	r2, r2, #0x0c         
	ldmia	sp!, {r4, lr}

.Lmemmove_bl12:
	adds	r2, r2, #8
	blt	.Lmemmove_bl4
	subs	r2, r2, #4
	itt	lt
	ldrlt	r3, [r1, #-4]!
	strlt	r3, [r0, #-4]!
	ittt	ge
	ldmdbge	r1!, {r3, r12}
	stmdbge	r0!, {r3, r12}
	subge	r2, r2, #4

.Lmemmove_bl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
	it	eq
	RETeq			/* done */

	/* copy the crud byte at a time */
	cmp	r2, #2
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	itt	ge
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
	itt	gt
	ldrbgt	r3, [r1, #-1]!
	strbgt	r3, [r0, #-1]!
	RET

	/* erg - unaligned destination */
.Lmemmove_bdestul:
	cmp	r12, #2

	/* align destination with byte copies */
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	itt	ge
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
	itt	gt
	ldrbgt	r3, [r1, #-1]!
	strbgt	r3, [r0, #-1]!
	subs	r2, r2, r12
	blt	.Lmemmove_bl4		/* less than 4 bytes to go */
	ands	r12, r1, #3
	beq	.Lmemmove_bt8		/* we have an aligned source */

	/* erg - unaligned source */
	/* This is where it gets nasty ... */
.Lmemmove_bsrcul:
	bic	r1, r1, #3
	ldr	r3, [r1, #0]
	cmp	r12, #2
	blt	.Lmemmove_bsrcul1
	beq	.Lmemmove_bsrcul2
	cmp	r2, #0x0c            
	blt	.Lmemmove_bsrcul3loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5, lr}

.Lmemmove_bsrcul3loop16:
#ifdef __ARMEB__
	mov	lr, r3, lsr #8
#else
	mov	lr, r3, lsl #8
#endif
	ldmdb	r1!, {r3-r5, r12}
#ifdef __ARMEB__
	orr	lr, lr, r12, lsl #24
	mov	r12, r12, lsr #8
	orr	r12, r12, r5, lsl #24
	mov	r5, r5, lsr #8
	orr	r5, r5, r4, lsl #24
	mov	r4, r4, lsr #8
	orr	r4, r4, r3, lsl #24
#else
	orr	lr, lr, r12, lsr #24
	mov	r12, r12, lsl #8
	orr	r12, r12, r5, lsr #24
	mov	r5, r5, lsl #8
	orr	r5, r5, r4, lsr #24
	mov	r4, r4, lsl #8
	orr	r4, r4, r3, lsr #24
#endif
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_bsrcul3loop16
	ldmia	sp!, {r4, r5, lr}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_bsrcul3l4

.Lmemmove_bsrcul3loop4:
#ifdef __ARMEB__
	mov	r12, r3, lsr #8
#else
	mov	r12, r3, lsl #8
#endif
	ldr	r3, [r1, #-4]!
#ifdef __ARMEB__
	orr	r12, r12, r3, lsl #24
#else
	orr	r12, r12, r3, lsr #24
#endif
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	.Lmemmove_bsrcul3loop4

.Lmemmove_bsrcul3l4:
	add	r1, r1, #3
	b	.Lmemmove_bl4

.Lmemmove_bsrcul2:
	cmp	r2, #0x0c            
	blt	.Lmemmove_bsrcul2loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5, lr}

.Lmemmove_bsrcul2loop16:
#ifdef __ARMEB__
	mov	lr, r3, lsr #16
#else
	mov	lr, r3, lsl #16
#endif
	ldmdb	r1!, {r3-r5, r12}
#ifdef __ARMEB__
	orr	lr, lr, r12, lsl #16
	mov	r12, r12, lsr #16
	orr	r12, r12, r5, lsl #16
	mov	r5, r5, lsr #16
	orr	r5, r5, r4, lsl #16
	mov	r4, r4, lsr #16
	orr	r4, r4, r3, lsl #16
#else
	orr	lr, lr, r12, lsr #16
	mov	r12, r12, lsl #16
	orr	r12, r12, r5, lsr #16
	mov	r5, r5, lsl #16
	orr	r5, r5, r4, lsr #16
	mov	r4, r4, lsl #16
	orr	r4, r4, r3, lsr #16
#endif
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_bsrcul2loop16
	ldmia	sp!, {r4, r5, lr}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_bsrcul2l4

.Lmemmove_bsrcul2loop4:
#ifdef __ARMEB__
	mov	r12, r3, lsr #16
#else
	mov	r12, r3, lsl #16
#endif
	ldr	r3, [r1, #-4]!
#ifdef __ARMEB__
	orr	r12, r12, r3, lsl #16
#else
	orr	r12, r12, r3, lsr #16
#endif
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	.Lmemmove_bsrcul2loop4

.Lmemmove_bsrcul2l4:
	add	r1, r1, #2
	b	.Lmemmove_bl4

.Lmemmove_bsrcul1:
	cmp	r2, #0x0c            
	blt	.Lmemmove_bsrcul1loop4
	sub	r2, r2, #0x0c         
	stmdb	sp!, {r4, r5, lr}

.Lmemmove_bsrcul1loop32:
#ifdef __ARMEB__
	mov	lr, r3, lsr #24
#else
	mov	lr, r3, lsl #24
#endif
	ldmdb	r1!, {r3-r5, r12}
#ifdef __ARMEB__
	orr	lr, lr, r12, lsl #8
	mov	r12, r12, lsr #24
	orr	r12, r12, r5, lsl #8
	mov	r5, r5, lsr #24
	orr	r5, r5, r4, lsl #8
	mov	r4, r4, lsr #24
	orr	r4, r4, r3, lsl #8
#else
	orr	lr, lr, r12, lsr #8
	mov	r12, r12, lsl #24
	orr	r12, r12, r5, lsr #8
	mov	r5, r5, lsl #24
	orr	r5, r5, r4, lsr #8
	mov	r4, r4, lsl #24
	orr	r4, r4, r3, lsr #8
#endif
	stmdb	r0!, {r4, r5, r12, lr}
	subs	r2, r2, #0x10         
	bge	.Lmemmove_bsrcul1loop32
	ldmia	sp!, {r4, r5, lr}
	adds	r2, r2, #0x0c         
	blt	.Lmemmove_bsrcul1l4

.Lmemmove_bsrcul1loop4:
#ifdef __ARMEB__
	mov	r12, r3, lsr #24
#else
	mov	r12, r3, lsl #24
#endif
	ldr	r3, [r1, #-4]!
#ifdef __ARMEB__
	orr	r12, r12, r3, lsl #8
#else
	orr	r12, r12, r3, lsr #8
#endif
	str	r12, [r0, #-4]!
	subs	r2, r2, #4
	bge	.Lmemmove_bsrcul1loop4

.Lmemmove_bsrcul1l4:
	add	r1, r1, #1
	b	.Lmemmove_bl4
#ifndef _BCOPY
END(memmove)
#else
END(bcopy)
#endif

	.section .note.GNU-stack,"",%progbits
