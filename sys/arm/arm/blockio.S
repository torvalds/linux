/*	$NetBSD: blockio.S,v 1.5 2002/08/15 01:38:16 briggs Exp $	*/

/*-
 * Copyright (c) 2001 Ben Harris.
 * Copyright (c) 1994 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * blockio.S
 *
 * optimised block read/write from/to IO routines.
 *
 * Created      : 08/10/94
 * Modified	: 22/01/99  -- R.Earnshaw
 *			       Faster, and small tweaks for StrongARM
 */

#include <machine/asm.h>

__FBSDID("$FreeBSD$");

	.syntax	unified

/*
 * Read bytes from an I/O address into a block of memory
 *
 * r0 = address to read from (IO)
 * r1 = address to write to (memory)
 * r2 = length
 */

/* This code will look very familiar if you've read _memcpy(). */
ENTRY(read_multi_1)
	mov	ip, sp
	stmfd	sp!, {fp, ip, lr, pc}
	sub	fp, ip, #4
	subs	r2, r2, #4		/* r2 = length - 4 */
	blt	.Lrm1_l4			/* less than 4 bytes */
	ands	r12, r1, #3
	beq	.Lrm1_main		/* aligned destination */
	rsb	r12, r12, #4
	cmp	r12, #2
	ldrb	r3, [r0]
	strb	r3, [r1], #1
	ldrbge	r3, [r0]
	strbge	r3, [r1], #1
	ldrbgt	r3, [r0]
	strbgt	r3, [r1], #1
	subs	r2, r2, r12
	blt	.Lrm1_l4
.Lrm1_main:
.Lrm1loop:
	ldrb	r3, [r0]
	ldrb	r12, [r0]
	orr	r3, r3, r12, lsl #8
	ldrb	r12, [r0]
	orr	r3, r3, r12, lsl #16
	ldrb	r12, [r0]
	orr	r3, r3, r12, lsl #24
	str	r3, [r1], #4
	subs	r2, r2, #4
	bge	.Lrm1loop
.Lrm1_l4:
	adds	r2, r2, #4			/* r2 = length again */
	ldmdbeq	fp, {fp, sp, pc}
	RETeq
	cmp	r2, #2
	ldrb	r3, [r0]
	strb	r3, [r1], #1
	ldrbge	r3, [r0]
	strbge	r3, [r1], #1
	ldrbgt	r3, [r0]
	strbgt	r3, [r1], #1
	ldmdb	fp, {fp, sp, pc}
END(read_multi_1)

/*
 * Write bytes to an I/O address from a block of memory
 *
 * r0 = address to write to (IO)
 * r1 = address to read from (memory)
 * r2 = length
 */

/* This code will look very familiar if you've read _memcpy(). */
ENTRY(write_multi_1)
	mov	ip, sp
	stmfd	sp!, {fp, ip, lr, pc}
	sub	fp, ip, #4
	subs	r2, r2, #4		/* r2 = length - 4 */
	blt	.Lwm1_l4		/* less than 4 bytes */
	ands	r12, r1, #3
	beq	.Lwm1_main		/* aligned source */
	rsb	r12, r12, #4
	cmp	r12, #2
	ldrb	r3, [r1], #1
	strb	r3, [r0]
	ldrbge	r3, [r1], #1
	strbge	r3, [r0]
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0]
	subs	r2, r2, r12
	blt	.Lwm1_l4
.Lwm1_main:
.Lwm1loop:
	ldr	r3, [r1], #4
	strb	r3, [r0]
	mov	r3, r3, lsr #8
	strb	r3, [r0]
	mov	r3, r3, lsr #8
	strb	r3, [r0]
	mov	r3, r3, lsr #8
	strb	r3, [r0]
	subs	r2, r2, #4
	bge	.Lwm1loop
.Lwm1_l4:
	adds	r2, r2, #4			/* r2 = length again */
	ldmdbeq	fp, {fp, sp, pc}
	cmp	r2, #2
	ldrb	r3, [r1], #1
	strb	r3, [r0]
	ldrbge	r3, [r1], #1
	strbge	r3, [r0]
	ldrbgt	r3, [r1], #1
	strbgt	r3, [r0]
	ldmdb	fp, {fp, sp, pc}
END(write_multi_1)

/*
 * Reads short ints (16 bits) from an I/O address into a block of memory
 *
 * r0 = address to read from (IO)
 * r1 = address to write to (memory)
 * r2 = length
 */

ENTRY(insw)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address and the size is word aligned, do it fast */

	tst	r2, #0x00000001
	tsteq	r1, #0x00000003
	beq	.Lfastinsw

/* Non aligned insw */

.Linswloop:
	ldr	r3, [r0]
	subs	r2, r2, #0x00000001	/* Loop test in load delay slot */
	strb	r3, [r1], #0x0001
	mov	r3, r3, lsr #8
	strb	r3, [r1], #0x0001
	bgt	.Linswloop

	RET

/* Word aligned insw */

.Lfastinsw:

.Lfastinswloop:
	ldr	r3, [r0, #0x0002]	/* take advantage of nonaligned
					 * word accesses */
	ldr	ip, [r0]
	mov	r3, r3, lsr #16		/* Put the two shorts together */
	orr	r3, r3, ip, lsl #16
	str	r3, [r1], #0x0004	/* Store */
	subs	r2, r2, #0x00000002	/* Next */
	bgt	.Lfastinswloop

	RET
END(insw)

/*
 * Writes short ints (16 bits) from a block of memory to an I/O address
 *
 * r0 = address to write to (IO)
 * r1 = address to read from (memory)
 * r2 = length
 */

ENTRY(outsw)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address and the size is word aligned, do it fast */

	tst	r2, #0x00000001
	tsteq	r1, #0x00000003
	beq	.Lfastoutsw

/* Non aligned outsw */

.Loutswloop:
	ldrb	r3, [r1], #0x0001
	ldrb	ip, [r1], #0x0001
	subs	r2, r2, #0x00000001	/* Loop test in load delay slot */
	orr	r3, r3, ip, lsl #8
	orr	r3, r3, r3, lsl #16
	str	r3, [r0]
	bgt	.Loutswloop

	RET

/* Word aligned outsw */

.Lfastoutsw:

.Lfastoutswloop:
	ldr	r3, [r1], #0x0004	/* r3 = (H)(L) */
	subs	r2, r2, #0x00000002	/* Loop test in load delay slot */

	eor	ip, r3, r3, lsr #16	/* ip = (H)(H^L) */
	eor	r3, r3, ip, lsl #16	/* r3 = (H^H^L)(L) = (L)(L) */
	eor	ip, ip, r3, lsr #16	/* ip = (H)(H^L^L) = (H)(H) */

	str	r3, [r0]
	str	ip, [r0]

/*	mov	ip, r3, lsl #16
 *	orr	ip, ip, ip, lsr #16
 *	str	ip, [r0]
 *
 *	mov	ip, r3, lsr #16
 *	orr	ip, ip, ip, lsl #16
 *	str	ip, [r0]
 */

	bgt	.Lfastoutswloop

	RET
END(outsw)

/*
 * reads short ints (16 bits) from an I/O address into a block of memory
 * with a length garenteed to be a multiple of 16 bytes
 * with a word aligned destination address
 *
 * r0 = address to read from (IO)
 * r1 = address to write to (memory)
 * r2 = length
 */

ENTRY(insw16)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address is word aligned and the size suitably
   aligned, do it fast */

	tst	r2, #0x00000007
	tsteq	r1, #0x00000003

	bne	_C_LABEL(insw)

/* Word aligned insw */

	stmfd	sp!, {r4,r5,lr}

.Linsw16loop:
	ldr	r3, [r0, #0x0002]	/* take advantage of nonaligned
					 * word accesses */
	ldr	lr, [r0]
	mov	r3, r3, lsr #16		/* Put the two shorts together */
	orr	r3, r3, lr, lsl #16

	ldr	r4, [r0, #0x0002]	/* take advantage of nonaligned
					 * word accesses */
	ldr	lr, [r0]
	mov	r4, r4, lsr #16		/* Put the two shorts together */
	orr	r4, r4, lr, lsl #16

	ldr	r5, [r0, #0x0002]	/* take advantage of nonaligned
					 * word accesses */
	ldr	lr, [r0]
	mov	r5, r5, lsr #16		/* Put the two shorts together */
	orr	r5, r5, lr, lsl #16

	ldr	ip, [r0, #0x0002]	/* take advantage of nonaligned
					 * word accesses */
	ldr	lr, [r0]
	mov	ip, ip, lsr #16		/* Put the two shorts together */
	orr	ip, ip, lr, lsl #16

	stmia	r1!, {r3-r5,ip}
	subs	r2, r2, #0x00000008	/* Next */
	bgt	.Linsw16loop

	ldmfd	sp!, {r4,r5,pc}		/* Restore regs and go home */
END(insw16)

/*
 * Writes short ints (16 bits) from a block of memory to an I/O address
 *
 * r0 = address to write to (IO)
 * r1 = address to read from (memory)
 * r2 = length
 */

ENTRY(outsw16)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address is word aligned and the size suitably
   aligned, do it fast */

	tst	r2, #0x00000007
	tsteq	r1, #0x00000003

	bne	_C_LABEL(outsw)

/* Word aligned outsw */

	stmfd	sp!, {r4,r5,lr}

.Loutsw16loop:
	ldmia	r1!, {r4,r5,ip,lr}

	eor	r3, r4, r4, lsl #16	/* r3 = (A^B)(B) */
	eor	r4, r4, r3, lsr #16	/* r4 = (A)(B^A^B) = (A)(A) */
	eor	r3, r3, r4, lsl #16	/* r3 = (A^B^A)(B) = (B)(B) */
	str	r3, [r0]
	str	r4, [r0]

/*	mov	r3, r4, lsl #16
 *	orr	r3, r3, r3, lsr #16
 *	str	r3, [r0]
 *
 *	mov	r3, r4, lsr #16
 *	orr	r3, r3, r3, lsl #16
 *	str	r3, [r0]
 */

	eor	r3, r5, r5, lsl #16	/* r3 = (A^B)(B) */
	eor	r5, r5, r3, lsr #16	/* r4 = (A)(B^A^B) = (A)(A) */
	eor	r3, r3, r5, lsl #16	/* r3 = (A^B^A)(B) = (B)(B) */
	str	r3, [r0]
	str	r5, [r0]

	eor	r3, ip, ip, lsl #16	/* r3 = (A^B)(B) */
	eor	ip, ip, r3, lsr #16	/* r4 = (A)(B^A^B) = (A)(A) */
	eor	r3, r3, ip, lsl #16	/* r3 = (A^B^A)(B) = (B)(B) */
	str	r3, [r0]
	str	ip, [r0]

	eor	r3, lr, lr, lsl #16	/* r3 = (A^B)(B) */
	eor	lr, lr, r3, lsr #16	/* r4 = (A)(B^A^B) = (A)(A) */
	eor	r3, r3, lr, lsl #16	/* r3 = (A^B^A)(B) = (B)(B) */
	str	r3, [r0]
	str	lr, [r0]

	subs	r2, r2, #0x00000008
	bgt	.Loutsw16loop

	ldmfd	sp!, {r4,r5,pc}		/* and go home */
END(outsw16)

/*
 * reads short ints (16 bits) from an I/O address into a block of memory
 * The I/O address is assumed to be mapped multiple times in a block of
 * 8 words.
 * The destination address should be word aligned.
 *
 * r0 = address to read from (IO)
 * r1 = address to write to (memory)
 * r2 = length
 */

ENTRY(inswm8)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address is word aligned and the size suitably
   aligned, do it fast */

	tst	r1, #0x00000003

	bne	_C_LABEL(insw)

/* Word aligned insw */

	stmfd	sp!, {r4-r9,lr}

	mov	lr, #0xff000000
	orr	lr, lr, #0x00ff0000

.Linswm8_loop8:
	cmp	r2, #8
	bcc	.Linswm8_l8

	ldmia	r0, {r3-r9,ip}

	bic	r3, r3, lr
	orr	r3, r3, r4, lsl #16
	bic	r5, r5, lr
	orr	r4, r5, r6, lsl #16
	bic	r7, r7, lr
	orr	r5, r7, r8, lsl #16
	bic	r9, r9, lr
	orr	r6, r9, ip, lsl #16

	stmia	r1!, {r3-r6}

	subs	r2, r2, #0x00000008	/* Next */
	bne	.Linswm8_loop8
	beq	.Linswm8_l1

.Linswm8_l8:
	cmp	r2, #4
	bcc	.Linswm8_l4

	ldmia	r0, {r3-r6}

	bic	r3, r3, lr
	orr	r3, r3, r4, lsl #16
	bic	r5, r5, lr
	orr	r4, r5, r6, lsl #16

	stmia	r1!, {r3-r4}

	subs	r2, r2, #0x00000004
	beq	.Linswm8_l1

.Linswm8_l4:
	cmp	r2, #2
	bcc	.Linswm8_l2

	ldmia	r0, {r3-r4}

	bic	r3, r3, lr
	orr	r3, r3, r4, lsl #16
	str	r3, [r1], #0x0004

	subs	r2, r2, #0x00000002
	beq	.Linswm8_l1

.Linswm8_l2:
	cmp	r2, #1
	bcc	.Linswm8_l1

	ldr	r3, [r0]
	subs	r2, r2, #0x00000001	/* Test in load delay slot */
					/* XXX, why don't we use result?  */

	strb	r3, [r1], #0x0001
	mov	r3, r3, lsr #8
	strb	r3, [r1], #0x0001


.Linswm8_l1:
	ldmfd	sp!, {r4-r9,pc}		/* And go home */
END(inswm8)

/*
 * write short ints (16 bits) to an I/O address from a block of memory
 * The I/O address is assumed to be mapped multiple times in a block of
 * 8 words.
 * The source address should be word aligned.
 *
 * r0 = address to read to (IO)
 * r1 = address to write from (memory)
 * r2 = length
 */

ENTRY(outswm8)
/* Make sure that we have a positive length */
	cmp	r2, #0x00000000
	movle	pc, lr

/* If the destination address is word aligned and the size suitably
   aligned, do it fast */

	tst	r1, #0x00000003

	bne	_C_LABEL(outsw)

/* Word aligned outsw */

	stmfd	sp!, {r4-r8,lr}

.Loutswm8_loop8:
	cmp	r2, #8
	bcc	.Loutswm8_l8

	ldmia	r1!, {r3,r5,r7,ip}

	eor	r4, r3, r3, lsr #16	/* r4 = (A)(A^B) */
	eor	r3, r3, r4, lsl #16	/* r3 = (A^A^B)(B) = (B)(B) */
	eor	r4, r4, r3, lsr #16	/* r4 = (A)(B^A^B) = (A)(A) */

	eor	r6, r5, r5, lsr #16	/* r6 = (A)(A^B) */
	eor	r5, r5, r6, lsl #16	/* r5 = (A^A^B)(B) = (B)(B) */
	eor	r6, r6, r5, lsr #16	/* r6 = (A)(B^A^B) = (A)(A) */

	eor	r8, r7, r7, lsr #16	/* r8 = (A)(A^B) */
	eor	r7, r7, r8, lsl #16	/* r7 = (A^A^B)(B) = (B)(B) */
	eor	r8, r8, r7, lsr #16	/* r8 = (A)(B^A^B) = (A)(A) */

	eor	lr, ip, ip, lsr #16	/* lr = (A)(A^B) */
	eor	ip, ip, lr, lsl #16	/* ip = (A^A^B)(B) = (B)(B) */
	eor	lr, lr, ip, lsr #16	/* lr = (A)(B^A^B) = (A)(A) */

	stmia	r0, {r3-r8,ip,lr}

	subs	r2, r2, #0x00000008	/* Next */
	bne	.Loutswm8_loop8
	beq	.Loutswm8_l1

.Loutswm8_l8:
	cmp	r2, #4
	bcc	.Loutswm8_l4

	ldmia	r1!, {r3-r4}

	eor	r6, r3, r3, lsr #16	/* r6 = (A)(A^B) */
	eor	r5, r3, r6, lsl #16	/* r5 = (A^A^B)(B) = (B)(B) */
	eor	r6, r6, r5, lsr #16	/* r6 = (A)(B^A^B) = (A)(A) */

	eor	r8, r4, r4, lsr #16	/* r8 = (A)(A^B) */
	eor	r7, r4, r8, lsl #16	/* r7 = (A^A^B)(B) = (B)(B) */
	eor	r8, r8, r7, lsr #16	/* r8 = (A)(B^A^B) = (A)(A) */

	stmia	r0, {r5-r8}

	subs	r2, r2, #0x00000004
	beq	.Loutswm8_l1

.Loutswm8_l4:
	cmp	r2, #2
	bcc	.Loutswm8_l2

	ldr	r3, [r1], #0x0004	/* r3 = (A)(B) */
	subs	r2, r2, #0x00000002	/* Done test in Load delay slot */

	eor	r5, r3, r3, lsr #16	/* r5 = (A)(A^B)*/
	eor	r4, r3, r5, lsl #16	/* r4 = (A^A^B)(B) = (B)(B) */
	eor	r5, r5, r4, lsr #16	/* r5 = (A)(B^A^B) = (A)(A) */

	stmia	r0, {r4, r5}

	beq	.Loutswm8_l1

.Loutswm8_l2:
	cmp	r2, #1
	bcc	.Loutswm8_l1

	ldrb	r3, [r1], #0x0001
	ldrb	r4, [r1], #0x0001
	subs	r2, r2, #0x00000001	/* Done test in load delay slot */
					/* XXX This test isn't used?  */
	orr	r3, r3, r4, lsl #8
	orr	r3, r3, r3, lsl #16
	str	r3, [r0]

.Loutswm8_l1:
	ldmfd	sp!, {r4-r8,pc}		/* And go home */
END(outswm8)

