/*-
 * Copyright (c) 2004 Olivier Houchard
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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

#include "assym.inc"

	.syntax	unified

.L_arm_memcpy:
	.word	_C_LABEL(_arm_memcpy)
.L_arm_bzero:
	.word	_C_LABEL(_arm_bzero)
.L_min_memcpy_size:
	.word	_C_LABEL(_min_memcpy_size)
.L_min_bzero_size:
	.word	_C_LABEL(_min_bzero_size)
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
/* LINTSTUB: Func: void bzero(void *, size_t) */
ENTRY(bzero)
	ldr	r3, .L_arm_bzero
	ldr	r3, [r3]
	cmp	r3, #0
	beq	.Lnormal0
	ldr	r2, .L_min_bzero_size
	ldr	r2, [r2]
	cmp	r1, r2
	blt	.Lnormal0
	stmfd	sp!, {r0, r1, lr}
	mov	r2, #0
	mov	lr, pc
	mov	pc, r3
	cmp	r0, #0
	ldmfd	sp!, {r0, r1, lr}
	RETeq
.Lnormal0:
	mov	r3, #0x00
	b	do_memset
END(bzero)
/* LINTSTUB: Func: void *memset(void *, int, size_t) */
ENTRY(memset)
	and	r3, r1, #0xff		/* We deal with bytes */
	mov	r1, r2
do_memset:
	cmp	r1, #0x04		/* Do we have less than 4 bytes */
	mov	ip, r0
	blt	.Lmemset_lessthanfour

	/* Ok first we will word align the address */
	ands	r2, ip, #0x03		/* Get the bottom two bits */
	bne	.Lmemset_wordunaligned	/* The address is not word aligned */

	/* We are now word aligned */
.Lmemset_wordaligned:
	orr	r3, r3, r3, lsl #8	/* Extend value to 16-bits */
#ifdef _ARM_ARCH_5E
	tst	ip, #0x04		/* Quad-align for armv5e */
#else
	cmp	r1, #0x10
#endif
	orr	r3, r3, r3, lsl #16	/* Extend value to 32-bits */
#ifdef _ARM_ARCH_5E
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
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
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
	RETeq			/* Zero length so just exit */

	add	r1, r1, #0x80		/* Adjust for extra sub */

	/* Do 32 bytes at a time */
.Lmemset_loop32:
	subs	r1, r1, #0x20
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
	RETeq			/* Zero length so just exit */

	adds	r1, r1, #0x10		/* Partially adjust for extra sub */

	/* Deal with 16 bytes or more */
#ifdef _ARM_ARCH_5E
	strdge	r2, [ip], #0x08
	strdge	r2, [ip], #0x08
#else
	stmiage	ip!, {r2-r3}
	stmiage	ip!, {r2-r3}
#endif
	RETeq			/* Zero length so just exit */

	addlt	r1, r1, #0x10		/* Possibly adjust for extra sub */

	/* We have at least 4 bytes so copy as words */
.Lmemset_loop4:
	subs	r1, r1, #0x04
	strge	r3, [ip], #0x04
	bgt	.Lmemset_loop4
	RETeq			/* Zero length so just exit */

#ifdef _ARM_ARCH_5E
	/* Compensate for 64-bit alignment check */
	adds	r1, r1, #0x04
	RETeq
	cmp	r1, #2
#else
	cmp	r1, #-2
#endif

	strb	r3, [ip], #0x01		/* Set 1 byte */
	strbge	r3, [ip], #0x01		/* Set another byte */
	strbgt	r3, [ip]		/* and a third */
	RET			/* Exit */

.Lmemset_wordunaligned:
	rsb	r2, r2, #0x004
	strb	r3, [ip], #0x01		/* Set 1 byte */
	cmp	r2, #0x02
	strbge	r3, [ip], #0x01		/* Set another byte */
	sub	r1, r1, r2
	strbgt	r3, [ip], #0x01		/* and a third */
	cmp	r1, #0x04		/* More than 4 bytes left? */
	bge	.Lmemset_wordaligned	/* Yup */

.Lmemset_lessthanfour:
	cmp	r1, #0x00
	RETeq			/* Zero length so exit */
	strb	r3, [ip], #0x01		/* Set 1 byte */
	cmp	r1, #0x02
	strbge	r3, [ip], #0x01		/* Set another byte */
	strbgt	r3, [ip]		/* and a third */
	RET			/* Exit */
EEND(memset)
END(bzero)

ENTRY(bcmp)
	mov	ip, r0
	cmp	r2, #0x06
	beq	.Lmemcmp_6bytes
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
	ldrbeq	r3, [ip, #0x01]		/* r3 = b1#1 */
	RETne			/* Return if mismatch on #0 */
	subs	r0, r3, r2		/* r0 = b1#1 - b2#1 */
	ldrbeq	r3, [r1, #0x02]		/* r3 = b2#2 */
	ldrbeq	r0, [ip, #0x02]		/* r0 = b1#2 */
	RETne			/* Return if mismatch on #1 */
	ldrb	r2, [r1, #0x03]		/* r2 = b2#3 */
	subs	r0, r0, r3		/* r0 = b1#2 - b2#2 */
	ldrbeq	r3, [ip, #0x03]		/* r3 = b1#3 */
	RETne			/* Return if mismatch on #2 */
	subs	r0, r3, r2		/* r0 = b1#3 - b2#3 */
	ldrbeq	r3, [r1, #0x04]		/* r3 = b2#4 */
	ldrbeq	r0, [ip, #0x04]		/* r0 = b1#4 */
	RETne			/* Return if mismatch on #3 */
	ldrb	r2, [r1, #0x05]		/* r2 = b2#5 */
	subs	r0, r0, r3		/* r0 = b1#4 - b2#4 */
	ldrbeq	r3, [ip, #0x05]		/* r3 = b1#5 */
	RETne			/* Return if mismatch on #4 */
	sub	r0, r3, r2		/* r0 = b1#5 - b2#5 */
	RET
END(bcmp)

ENTRY(bcopy)
	/* switch the source and destination registers */
	eor     r0, r1, r0
	eor     r1, r0, r1
	eor     r0, r1, r0
EENTRY(memmove)
	/* Do the buffers overlap? */
	cmp	r0, r1
	RETeq		/* Bail now if src/dst are the same */
	subcc	r3, r0, r1	/* if (dst > src) r3 = dst - src */
	subcs	r3, r1, r0	/* if (src > dsr) r3 = src - dst */
	cmp	r3, r2		/* if (r3 < len) we have an overlap */
	bcc	PIC_SYM(_C_LABEL(memcpy), PLT)

	/* Determine copy direction */
	cmp	r1, r0
	bcc	.Lmemmove_backwards

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
	ldmiage	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmiage	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10
	ldmia	sp!, {r4}		/* return r4 */

.Lmemmove_fl32:
	adds	r2, r2, #0x14

	/* blat 12 bytes at a time */
.Lmemmove_floop12:
	ldmiage	r1!, {r3, r12, lr}
	stmiage	r0!, {r3, r12, lr}
	subsge	r2, r2, #0x0c
	bge	.Lmemmove_floop12

.Lmemmove_fl12:
	adds	r2, r2, #8
	blt	.Lmemmove_fl4

	subs	r2, r2, #4
	ldrlt	r3, [r1], #4
	strlt	r3, [r0], #4
	ldmiage	r1!, {r3, r12}
	stmiage	r0!, {r3, r12}
	subge	r2, r2, #4

.Lmemmove_fl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
	ldmiaeq	sp!, {r0, pc}		/* done */

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
.Lmemmove_fdestul:
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
	ldmdbge	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmdbge	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10
	adds	r2, r2, #0x14
	ldmdbge	r1!, {r3, r12, lr}	/* blat a remaining 12 bytes */
	stmdbge	r0!, {r3, r12, lr}
	subge	r2, r2, #0x0c
	ldmia	sp!, {r4, lr}

.Lmemmove_bl12:
	adds	r2, r2, #8
	blt	.Lmemmove_bl4
	subs	r2, r2, #4
	ldrlt	r3, [r1, #-4]!
	strlt	r3, [r0, #-4]!
	ldmdbge	r1!, {r3, r12}
	stmdbge	r0!, {r3, r12}
	subge	r2, r2, #4

.Lmemmove_bl4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
	RETeq			/* done */

	/* copy the crud byte at a time */
	cmp	r2, #2
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
	ldrbgt	r3, [r1, #-1]!
	strbgt	r3, [r0, #-1]!
	RET

	/* erg - unaligned destination */
.Lmemmove_bdestul:
	cmp	r12, #2

	/* align destination with byte copies */
	ldrb	r3, [r1, #-1]!
	strb	r3, [r0, #-1]!
	ldrbge	r3, [r1, #-1]!
	strbge	r3, [r0, #-1]!
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
EEND(memmove)
END(bcopy)

#if !defined(_ARM_ARCH_5E)
ENTRY(memcpy)
	/* save leaf functions having to store this away */
	/* Do not check arm_memcpy if we're running from flash */
#if defined(FLASHADDR) && defined(PHYSADDR)
#if FLASHADDR > PHYSADDR
	ldr	r3, =FLASHADDR
	cmp	r3, pc
	bls	.Lnormal
#else
	ldr	r3, =FLASHADDR
	cmp	r3, pc
	bhi	.Lnormal
#endif
#endif
	ldr	r3, .L_arm_memcpy
	ldr	r3, [r3]
	cmp	r3, #0
	beq	.Lnormal
	ldr	r3, .L_min_memcpy_size
	ldr	r3, [r3]
	cmp	r2, r3
	blt	.Lnormal
	stmfd	sp!, {r0-r2, r4, lr}
	mov	r3, #0
	ldr	r4, .L_arm_memcpy
	mov	lr, pc
	ldr	pc, [r4]
	cmp	r0, #0
	ldmfd	sp!, {r0-r2, r4, lr}
	RETeq

.Lnormal:
	stmdb	sp!, {r0, lr}		/* memcpy() returns dest addr */

	subs	r2, r2, #4
	blt	.Lmemcpy_l4		/* less than 4 bytes */
	ands	r12, r0, #3
	bne	.Lmemcpy_destul		/* oh unaligned destination addr */
	ands	r12, r1, #3
	bne	.Lmemcpy_srcul		/* oh unaligned source addr */

.Lmemcpy_t8:
	/* We have aligned source and destination */
	subs	r2, r2, #8
	blt	.Lmemcpy_l12		/* less than 12 bytes (4 from above) */
	subs	r2, r2, #0x14
	blt	.Lmemcpy_l32		/* less than 32 bytes (12 from above) */
	stmdb	sp!, {r4}		/* borrow r4 */

	/* blat 32 bytes at a time */
	/* XXX for really big copies perhaps we should use more registers */
.Lmemcpy_loop32:
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	ldmia	r1!, {r3, r4, r12, lr}
	stmia	r0!, {r3, r4, r12, lr}
	subs	r2, r2, #0x20
	bge	.Lmemcpy_loop32

	cmn	r2, #0x10
	ldmiage	r1!, {r3, r4, r12, lr}	/* blat a remaining 16 bytes */
	stmiage	r0!, {r3, r4, r12, lr}
	subge	r2, r2, #0x10
	ldmia	sp!, {r4}		/* return r4 */

.Lmemcpy_l32:
	adds	r2, r2, #0x14

	/* blat 12 bytes at a time */
.Lmemcpy_loop12:
	ldmiage	r1!, {r3, r12, lr}
	stmiage	r0!, {r3, r12, lr}
	subsge	r2, r2, #0x0c
	bge	.Lmemcpy_loop12

.Lmemcpy_l12:
	adds	r2, r2, #8
	blt	.Lmemcpy_l4

	subs	r2, r2, #4
	ldrlt	r3, [r1], #4
	strlt	r3, [r0], #4
	ldmiage	r1!, {r3, r12}
	stmiage	r0!, {r3, r12}
	subge	r2, r2, #4

.Lmemcpy_l4:
	/* less than 4 bytes to go */
	adds	r2, r2, #4
#ifdef __APCS_26_
	ldmiaeq sp!, {r0, pc}^		/* done */
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
.Lmemcpy_destul:
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
	blt	.Lmemcpy_l4		/* less the 4 bytes */

	ands	r12, r1, #3
	beq	.Lmemcpy_t8		/* we have an aligned source */

	/* erg - unaligned source */
	/* This is where it gets nasty ... */
.Lmemcpy_srcul:
	bic	r1, r1, #3
	ldr	lr, [r1], #4
	cmp	r12, #2
	bgt	.Lmemcpy_srcul3
	beq	.Lmemcpy_srcul2
	cmp	r2, #0x0c
	blt	.Lmemcpy_srcul1loop4
	sub	r2, r2, #0x0c
	stmdb	sp!, {r4, r5}

.Lmemcpy_srcul1loop16:
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
	bge	.Lmemcpy_srcul1loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c
	blt	.Lmemcpy_srcul1l4

.Lmemcpy_srcul1loop4:
	mov	r12, lr, lsr #8
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #24
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemcpy_srcul1loop4

.Lmemcpy_srcul1l4:
	sub	r1, r1, #3
	b	.Lmemcpy_l4

.Lmemcpy_srcul2:
	cmp	r2, #0x0c
	blt	.Lmemcpy_srcul2loop4
	sub	r2, r2, #0x0c
	stmdb	sp!, {r4, r5}

.Lmemcpy_srcul2loop16:
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
	bge	.Lmemcpy_srcul2loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c
	blt	.Lmemcpy_srcul2l4

.Lmemcpy_srcul2loop4:
	mov	r12, lr, lsr #16
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #16
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemcpy_srcul2loop4

.Lmemcpy_srcul2l4:
	sub	r1, r1, #2
	b	.Lmemcpy_l4

.Lmemcpy_srcul3:
	cmp	r2, #0x0c
	blt	.Lmemcpy_srcul3loop4
	sub	r2, r2, #0x0c
	stmdb	sp!, {r4, r5}

.Lmemcpy_srcul3loop16:
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
	bge	.Lmemcpy_srcul3loop16
	ldmia	sp!, {r4, r5}
	adds	r2, r2, #0x0c
	blt	.Lmemcpy_srcul3l4

.Lmemcpy_srcul3loop4:
	mov	r12, lr, lsr #24
	ldr	lr, [r1], #4
	orr	r12, r12, lr, lsl #8
	str	r12, [r0], #4
	subs	r2, r2, #4
	bge	.Lmemcpy_srcul3loop4

.Lmemcpy_srcul3l4:
	sub	r1, r1, #1
	b	.Lmemcpy_l4
END(memcpy)

#else
/* LINTSTUB: Func: void *memcpy(void *dst, const void *src, size_t len) */
ENTRY(memcpy)
	pld	[r1]
	cmp	r2, #0x0c
	ble	.Lmemcpy_short		/* <= 12 bytes */
#ifdef FLASHADDR
#if FLASHADDR > PHYSADDR
	ldr	r3, =FLASHADDR
	cmp	r3, pc
	bls	.Lnormal
#else
	ldr	r3, =FLASHADDR
	cmp	r3, pc
	bhi	.Lnormal
#endif
#endif
	ldr	r3, .L_arm_memcpy
	ldr	r3, [r3]
	cmp	r3, #0
	beq	.Lnormal
	ldr	r3, .L_min_memcpy_size
	ldr	r3, [r3]
	cmp	r2, r3
	blt	.Lnormal
	stmfd	sp!, {r0-r2, r4, lr}
	mov	r3, #0
	ldr	r4, .L_arm_memcpy
	mov	lr, pc
	ldr	pc, [r4]
	cmp	r0, #0
	ldmfd	sp!, {r0-r2, r4, lr}
	RETeq
.Lnormal:
	mov	r3, r0			/* We must not clobber r0 */

	/* Word-align the destination buffer */
	ands	ip, r3, #0x03		/* Already word aligned? */
	beq	.Lmemcpy_wordaligned	/* Yup */
	cmp	ip, #0x02
	ldrb	ip, [r1], #0x01
	sub	r2, r2, #0x01
	strb	ip, [r3], #0x01
	ldrble	ip, [r1], #0x01
	suble	r2, r2, #0x01
	strble	ip, [r3], #0x01
	ldrblt	ip, [r1], #0x01
	sublt	r2, r2, #0x01
	strblt	ip, [r3], #0x01

	/* Destination buffer is now word aligned */
.Lmemcpy_wordaligned:
	ands	ip, r1, #0x03		/* Is src also word-aligned? */
	bne	.Lmemcpy_bad_align	/* Nope. Things just got bad */

	/* Quad-align the destination buffer */
	tst	r3, #0x07		/* Already quad aligned? */
	ldrne	ip, [r1], #0x04
	stmfd	sp!, {r4-r9}		/* Free up some registers */
	subne	r2, r2, #0x04
	strne	ip, [r3], #0x04

	/* Destination buffer quad aligned, source is at least word aligned */
	subs	r2, r2, #0x80
	blt	.Lmemcpy_w_lessthan128

	/* Copy 128 bytes at a time */
.Lmemcpy_w_loop128:
	ldr	r4, [r1], #0x04		/* LD:00-03 */
	ldr	r5, [r1], #0x04		/* LD:04-07 */
	pld	[r1, #0x18]		/* Prefetch 0x20 */
	ldr	r6, [r1], #0x04		/* LD:08-0b */
	ldr	r7, [r1], #0x04		/* LD:0c-0f */
	ldr	r8, [r1], #0x04		/* LD:10-13 */
	ldr	r9, [r1], #0x04		/* LD:14-17 */
	strd	r4, [r3], #0x08		/* ST:00-07 */
	ldr	r4, [r1], #0x04		/* LD:18-1b */
	ldr	r5, [r1], #0x04		/* LD:1c-1f */
	strd	r6, [r3], #0x08		/* ST:08-0f */
	ldr	r6, [r1], #0x04		/* LD:20-23 */
	ldr	r7, [r1], #0x04		/* LD:24-27 */
	pld	[r1, #0x18]		/* Prefetch 0x40 */
	strd	r8, [r3], #0x08		/* ST:10-17 */
	ldr	r8, [r1], #0x04		/* LD:28-2b */
	ldr	r9, [r1], #0x04		/* LD:2c-2f */
	strd	r4, [r3], #0x08		/* ST:18-1f */
	ldr	r4, [r1], #0x04		/* LD:30-33 */
	ldr	r5, [r1], #0x04		/* LD:34-37 */
	strd	r6, [r3], #0x08		/* ST:20-27 */
	ldr	r6, [r1], #0x04		/* LD:38-3b */
	ldr	r7, [r1], #0x04		/* LD:3c-3f */
	strd	r8, [r3], #0x08		/* ST:28-2f */
	ldr	r8, [r1], #0x04		/* LD:40-43 */
	ldr	r9, [r1], #0x04		/* LD:44-47 */
	pld	[r1, #0x18]		/* Prefetch 0x60 */
	strd	r4, [r3], #0x08		/* ST:30-37 */
	ldr	r4, [r1], #0x04		/* LD:48-4b */
	ldr	r5, [r1], #0x04		/* LD:4c-4f */
	strd	r6, [r3], #0x08		/* ST:38-3f */
	ldr	r6, [r1], #0x04		/* LD:50-53 */
	ldr	r7, [r1], #0x04		/* LD:54-57 */
	strd	r8, [r3], #0x08		/* ST:40-47 */
	ldr	r8, [r1], #0x04		/* LD:58-5b */
	ldr	r9, [r1], #0x04		/* LD:5c-5f */
	strd	r4, [r3], #0x08		/* ST:48-4f */
	ldr	r4, [r1], #0x04		/* LD:60-63 */
	ldr	r5, [r1], #0x04		/* LD:64-67 */
	pld	[r1, #0x18]		/* Prefetch 0x80 */
	strd	r6, [r3], #0x08		/* ST:50-57 */
	ldr	r6, [r1], #0x04		/* LD:68-6b */
	ldr	r7, [r1], #0x04		/* LD:6c-6f */
	strd	r8, [r3], #0x08		/* ST:58-5f */
	ldr	r8, [r1], #0x04		/* LD:70-73 */
	ldr	r9, [r1], #0x04		/* LD:74-77 */
	strd	r4, [r3], #0x08		/* ST:60-67 */
	ldr	r4, [r1], #0x04		/* LD:78-7b */
	ldr	r5, [r1], #0x04		/* LD:7c-7f */
	strd	r6, [r3], #0x08		/* ST:68-6f */
	strd	r8, [r3], #0x08		/* ST:70-77 */
	subs	r2, r2, #0x80
	strd	r4, [r3], #0x08		/* ST:78-7f */
	bge	.Lmemcpy_w_loop128

.Lmemcpy_w_lessthan128:
	adds	r2, r2, #0x80		/* Adjust for extra sub */
	ldmfdeq	sp!, {r4-r9}
	RETeq			/* Return now if done */
	subs	r2, r2, #0x20
	blt	.Lmemcpy_w_lessthan32

	/* Copy 32 bytes at a time */
.Lmemcpy_w_loop32:
	ldr	r4, [r1], #0x04
	ldr	r5, [r1], #0x04
	pld	[r1, #0x18]
	ldr	r6, [r1], #0x04
	ldr	r7, [r1], #0x04
	ldr	r8, [r1], #0x04
	ldr	r9, [r1], #0x04
	strd	r4, [r3], #0x08
	ldr	r4, [r1], #0x04
	ldr	r5, [r1], #0x04
	strd	r6, [r3], #0x08
	strd	r8, [r3], #0x08
	subs	r2, r2, #0x20
	strd	r4, [r3], #0x08
	bge	.Lmemcpy_w_loop32

.Lmemcpy_w_lessthan32:
	adds	r2, r2, #0x20		/* Adjust for extra sub */
	ldmfdeq	sp!, {r4-r9}
	RETeq			/* Return now if done */

	and	r4, r2, #0x18
	rsbs	r4, r4, #0x18
	addne	pc, pc, r4, lsl #1
	nop

	/* At least 24 bytes remaining */
	ldr	r4, [r1], #0x04
	ldr	r5, [r1], #0x04
	sub	r2, r2, #0x08
	strd	r4, [r3], #0x08

	/* At least 16 bytes remaining */
	ldr	r4, [r1], #0x04
	ldr	r5, [r1], #0x04
	sub	r2, r2, #0x08
	strd	r4, [r3], #0x08

	/* At least 8 bytes remaining */
	ldr	r4, [r1], #0x04
	ldr	r5, [r1], #0x04
	subs	r2, r2, #0x08
	strd	r4, [r3], #0x08

	/* Less than 8 bytes remaining */
	ldmfd	sp!, {r4-r9}
	RETeq			/* Return now if done */
	subs	r2, r2, #0x04
	ldrge	ip, [r1], #0x04
	strge	ip, [r3], #0x04
	RETeq			/* Return now if done */
	addlt	r2, r2, #0x04
	ldrb	ip, [r1], #0x01
	cmp	r2, #0x02
	ldrbge	r2, [r1], #0x01
	strb	ip, [r3], #0x01
	ldrbgt	ip, [r1]
	strbge	r2, [r3], #0x01
	strbgt	ip, [r3]
	RET
/* Place a literal pool here for the above ldr instructions to use */
.ltorg


/*
 * At this point, it has not been possible to word align both buffers.
 * The destination buffer is word aligned, but the source buffer is not.
 */
.Lmemcpy_bad_align:
	stmfd	sp!, {r4-r7}
	bic	r1, r1, #0x03
	cmp	ip, #2
	ldr	ip, [r1], #0x04
	bgt	.Lmemcpy_bad3
	beq	.Lmemcpy_bad2
	b	.Lmemcpy_bad1

.Lmemcpy_bad1_loop16:
#ifdef __ARMEB__
	mov	r4, ip, lsl #8
#else
	mov	r4, ip, lsr #8
#endif
	ldr	r5, [r1], #0x04
	pld	[r1, #0x018]
	ldr	r6, [r1], #0x04
	ldr	r7, [r1], #0x04
	ldr	ip, [r1], #0x04
#ifdef __ARMEB__
	orr	r4, r4, r5, lsr #24
	mov	r5, r5, lsl #8
	orr	r5, r5, r6, lsr #24
	mov	r6, r6, lsl #8
	orr	r6, r6, r7, lsr #24
	mov	r7, r7, lsl #8
	orr	r7, r7, ip, lsr #24
#else
	orr	r4, r4, r5, lsl #24
	mov	r5, r5, lsr #8
	orr	r5, r5, r6, lsl #24
	mov	r6, r6, lsr #8
	orr	r6, r6, r7, lsl #24
	mov	r7, r7, lsr #8
	orr	r7, r7, ip, lsl #24
#endif
	str	r4, [r3], #0x04
	str	r5, [r3], #0x04
	str	r6, [r3], #0x04
	str	r7, [r3], #0x04
.Lmemcpy_bad1:
	subs	r2, r2, #0x10
	bge	.Lmemcpy_bad1_loop16

	adds	r2, r2, #0x10
	ldmfdeq	sp!, {r4-r7}
	RETeq			/* Return now if done */
	subs	r2, r2, #0x04
	sublt	r1, r1, #0x03
	blt	.Lmemcpy_bad_done

.Lmemcpy_bad1_loop4:
#ifdef __ARMEB__
	mov	r4, ip, lsl #8
#else
	mov	r4, ip, lsr #8
#endif
	ldr	ip, [r1], #0x04
	subs	r2, r2, #0x04
#ifdef __ARMEB__
	orr	r4, r4, ip, lsr #24
#else
	orr	r4, r4, ip, lsl #24
#endif
	str	r4, [r3], #0x04
	bge	.Lmemcpy_bad1_loop4
	sub	r1, r1, #0x03
	b	.Lmemcpy_bad_done

.Lmemcpy_bad2_loop16:
#ifdef __ARMEB__
	mov	r4, ip, lsl #16
#else
	mov	r4, ip, lsr #16
#endif
	ldr	r5, [r1], #0x04
	pld	[r1, #0x018]
	ldr	r6, [r1], #0x04
	ldr	r7, [r1], #0x04
	ldr	ip, [r1], #0x04
#ifdef __ARMEB__
	orr	r4, r4, r5, lsr #16
	mov	r5, r5, lsl #16
	orr	r5, r5, r6, lsr #16
	mov	r6, r6, lsl #16
	orr	r6, r6, r7, lsr #16
	mov	r7, r7, lsl #16
	orr	r7, r7, ip, lsr #16
#else
	orr	r4, r4, r5, lsl #16
	mov	r5, r5, lsr #16
	orr	r5, r5, r6, lsl #16
	mov	r6, r6, lsr #16
	orr	r6, r6, r7, lsl #16
	mov	r7, r7, lsr #16
	orr	r7, r7, ip, lsl #16
#endif
	str	r4, [r3], #0x04
	str	r5, [r3], #0x04
	str	r6, [r3], #0x04
	str	r7, [r3], #0x04
.Lmemcpy_bad2:
	subs	r2, r2, #0x10
	bge	.Lmemcpy_bad2_loop16

	adds	r2, r2, #0x10
	ldmfdeq	sp!, {r4-r7}
	RETeq			/* Return now if done */
	subs	r2, r2, #0x04
	sublt	r1, r1, #0x02
	blt	.Lmemcpy_bad_done

.Lmemcpy_bad2_loop4:
#ifdef __ARMEB__
	mov	r4, ip, lsl #16
#else
	mov	r4, ip, lsr #16
#endif
	ldr	ip, [r1], #0x04
	subs	r2, r2, #0x04
#ifdef __ARMEB__
	orr	r4, r4, ip, lsr #16
#else
	orr	r4, r4, ip, lsl #16
#endif
	str	r4, [r3], #0x04
	bge	.Lmemcpy_bad2_loop4
	sub	r1, r1, #0x02
	b	.Lmemcpy_bad_done

.Lmemcpy_bad3_loop16:
#ifdef __ARMEB__
	mov	r4, ip, lsl #24
#else
	mov	r4, ip, lsr #24
#endif
	ldr	r5, [r1], #0x04
	pld	[r1, #0x018]
	ldr	r6, [r1], #0x04
	ldr	r7, [r1], #0x04
	ldr	ip, [r1], #0x04
#ifdef __ARMEB__
	orr	r4, r4, r5, lsr #8
	mov	r5, r5, lsl #24
	orr	r5, r5, r6, lsr #8
	mov	r6, r6, lsl #24
	orr	r6, r6, r7, lsr #8
	mov	r7, r7, lsl #24
	orr	r7, r7, ip, lsr #8
#else
	orr	r4, r4, r5, lsl #8
	mov	r5, r5, lsr #24
	orr	r5, r5, r6, lsl #8
	mov	r6, r6, lsr #24
	orr	r6, r6, r7, lsl #8
	mov	r7, r7, lsr #24
	orr	r7, r7, ip, lsl #8
#endif
	str	r4, [r3], #0x04
	str	r5, [r3], #0x04
	str	r6, [r3], #0x04
	str	r7, [r3], #0x04
.Lmemcpy_bad3:
	subs	r2, r2, #0x10
	bge	.Lmemcpy_bad3_loop16

	adds	r2, r2, #0x10
	ldmfdeq	sp!, {r4-r7}
	RETeq			/* Return now if done */
	subs	r2, r2, #0x04
	sublt	r1, r1, #0x01
	blt	.Lmemcpy_bad_done

.Lmemcpy_bad3_loop4:
#ifdef __ARMEB__
	mov	r4, ip, lsl #24
#else
	mov	r4, ip, lsr #24
#endif
	ldr	ip, [r1], #0x04
	subs	r2, r2, #0x04
#ifdef __ARMEB__
	orr	r4, r4, ip, lsr #8
#else
	orr	r4, r4, ip, lsl #8
#endif
	str	r4, [r3], #0x04
	bge	.Lmemcpy_bad3_loop4
	sub	r1, r1, #0x01

.Lmemcpy_bad_done:
	ldmfd	sp!, {r4-r7}
	adds	r2, r2, #0x04
	RETeq
	ldrb	ip, [r1], #0x01
	cmp	r2, #0x02
	ldrbge	r2, [r1], #0x01
	strb	ip, [r3], #0x01
	ldrbgt	ip, [r1]
	strbge	r2, [r3], #0x01
	strbgt	ip, [r3]
	RET


/*
 * Handle short copies (less than 16 bytes), possibly misaligned.
 * Some of these are *very* common, thanks to the network stack,
 * and so are handled specially.
 */
.Lmemcpy_short:
	add	pc, pc, r2, lsl #2
	nop
	RET			/* 0x00 */
	b	.Lmemcpy_bytewise	/* 0x01 */
	b	.Lmemcpy_bytewise	/* 0x02 */
	b	.Lmemcpy_bytewise	/* 0x03 */
	b	.Lmemcpy_4		/* 0x04 */
	b	.Lmemcpy_bytewise	/* 0x05 */
	b	.Lmemcpy_6		/* 0x06 */
	b	.Lmemcpy_bytewise	/* 0x07 */
	b	.Lmemcpy_8		/* 0x08 */
	b	.Lmemcpy_bytewise	/* 0x09 */
	b	.Lmemcpy_bytewise	/* 0x0a */
	b	.Lmemcpy_bytewise	/* 0x0b */
	b	.Lmemcpy_c		/* 0x0c */
.Lmemcpy_bytewise:
	mov	r3, r0			/* We must not clobber r0 */
	ldrb	ip, [r1], #0x01
1:	subs	r2, r2, #0x01
	strb	ip, [r3], #0x01
	ldrbne	ip, [r1], #0x01
	bne	1b
	RET

/******************************************************************************
 * Special case for 4 byte copies
 */
#define	LMEMCPY_4_LOG2	6	/* 64 bytes */
#define	LMEMCPY_4_PAD	.align LMEMCPY_4_LOG2
	LMEMCPY_4_PAD
.Lmemcpy_4:
	and	r2, r1, #0x03
	orr	r2, r2, r0, lsl #2
	ands	r2, r2, #0x0f
	sub	r3, pc, #0x14
	addne	pc, r3, r2, lsl #LMEMCPY_4_LOG2

/*
 * 0000: dst is 32-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
	str	r2, [r0]
	RET
	LMEMCPY_4_PAD

/*
 * 0001: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #-1]		/* BE:r3 = x012  LE:r3 = 210x */
	ldr	r2, [r1, #3]		/* BE:r2 = 3xxx  LE:r2 = xxx3 */
#ifdef __ARMEB__
	mov	r3, r3, lsl #8		/* r3 = 012. */
	orr	r3, r3, r2, lsr #24	/* r3 = 0123 */
#else
	mov	r3, r3, lsr #8		/* r3 = .210 */
	orr	r3, r3, r2, lsl #24	/* r3 = 3210 */
#endif
	str	r3, [r0]
	RET
	LMEMCPY_4_PAD

/*
 * 0010: dst is 32-bit aligned, src is 16-bit aligned
 */
#ifdef __ARMEB__
	ldrh	r3, [r1]
	ldrh	r2, [r1, #0x02]
#else
	ldrh	r3, [r1, #0x02]
	ldrh	r2, [r1]
#endif
	orr	r3, r2, r3, lsl #16
	str	r3, [r0]
	RET
	LMEMCPY_4_PAD

/*
 * 0011: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #-3]		/* BE:r3 = xxx0  LE:r3 = 0xxx */
	ldr	r2, [r1, #1]		/* BE:r2 = 123x  LE:r2 = x321 */
#ifdef __ARMEB__
	mov	r3, r3, lsl #24		/* r3 = 0... */
	orr	r3, r3, r2, lsr #8	/* r3 = 0123 */
#else
	mov	r3, r3, lsr #24		/* r3 = ...0 */
	orr	r3, r3, r2, lsl #8	/* r3 = 3210 */
#endif
	str	r3, [r0]
	RET
	LMEMCPY_4_PAD

/*
 * 0100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
#ifdef __ARMEB__
	strb	r2, [r0, #0x03]
	mov	r3, r2, lsr #8
	mov	r1, r2, lsr #24
	strb	r1, [r0]
#else
	strb	r2, [r0]
	mov	r3, r2, lsr #8
	mov	r1, r2, lsr #24
	strb	r1, [r0, #0x03]
#endif
	strh	r3, [r0, #0x01]
	RET
	LMEMCPY_4_PAD

/*
 * 0101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrb	r1, [r1, #0x03]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strb	r1, [r0, #0x03]
	RET
	LMEMCPY_4_PAD

/*
 * 0110: dst is 8-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldrh	r3, [r1, #0x02]		/* LE:r3 = ..23  LE:r3 = ..32 */
#ifdef __ARMEB__
	mov	r1, r2, lsr #8		/* r1 = ...0 */
	strb	r1, [r0]
	mov	r2, r2, lsl #8		/* r2 = .01. */
	orr	r2, r2, r3, lsr #8	/* r2 = .012 */
#else
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = ...1 */
	orr	r2, r2, r3, lsl #8	/* r2 = .321 */
	mov	r3, r3, lsr #8		/* r3 = ...3 */
#endif
	strh	r2, [r0, #0x01]
	strb	r3, [r0, #0x03]
	RET
	LMEMCPY_4_PAD

/*
 * 0111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrb	r1, [r1, #0x03]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strb	r1, [r0, #0x03]
	RET
	LMEMCPY_4_PAD

/*
 * 1000: dst is 16-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
#ifdef __ARMEB__
	strh	r2, [r0, #0x02]
	mov	r3, r2, lsr #16
	strh	r3, [r0]
#else
	strh	r2, [r0]
	mov	r3, r2, lsr #16
	strh	r3, [r0, #0x02]
#endif
	RET
	LMEMCPY_4_PAD

/*
 * 1001: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldr	r2, [r1, #-1]		/* BE:r2 = x012  LE:r2 = 210x */
	ldr	r3, [r1, #3]		/* BE:r3 = 3xxx  LE:r3 = xxx3 */
	mov	r1, r2, lsr #8		/* BE:r1 = .x01  LE:r1 = .210 */
	strh	r1, [r0]
#ifdef __ARMEB__
	mov	r2, r2, lsl #8		/* r2 = 012. */
	orr	r2, r2, r3, lsr #24	/* r2 = 0123 */
#else
	mov	r2, r2, lsr #24		/* r2 = ...2 */
	orr	r2, r2, r3, lsl #8	/* r2 = xx32 */
#endif
	strh	r2, [r0, #0x02]
	RET
	LMEMCPY_4_PAD

/*
 * 1010: dst is 16-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]
	ldrh	r3, [r1, #0x02]
	strh	r2, [r0]
	strh	r3, [r0, #0x02]
	RET
	LMEMCPY_4_PAD

/*
 * 1011: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #1]		/* BE:r3 = 123x  LE:r3 = x321 */
	ldr	r2, [r1, #-3]		/* BE:r2 = xxx0  LE:r2 = 0xxx */
	mov	r1, r3, lsr #8		/* BE:r1 = .123  LE:r1 = .x32 */
	strh	r1, [r0, #0x02]
#ifdef __ARMEB__
	mov	r3, r3, lsr #24		/* r3 = ...1 */
	orr	r3, r3, r2, lsl #8	/* r3 = xx01 */
#else
	mov	r3, r3, lsl #8		/* r3 = 321. */
	orr	r3, r3, r2, lsr #24	/* r3 = 3210 */
#endif
	strh	r3, [r0]
	RET
	LMEMCPY_4_PAD

/*
 * 1100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
#ifdef __ARMEB__
	strb	r2, [r0, #0x03]
	mov	r3, r2, lsr #8
	mov	r1, r2, lsr #24
	strh	r3, [r0, #0x01]
	strb	r1, [r0]
#else
	strb	r2, [r0]
	mov	r3, r2, lsr #8
	mov	r1, r2, lsr #24
	strh	r3, [r0, #0x01]
	strb	r1, [r0, #0x03]
#endif
	RET
	LMEMCPY_4_PAD

/*
 * 1101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrb	r1, [r1, #0x03]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strb	r1, [r0, #0x03]
	RET
	LMEMCPY_4_PAD

/*
 * 1110: dst is 8-bit aligned, src is 16-bit aligned
 */
#ifdef __ARMEB__
	ldrh	r3, [r1, #0x02]		/* BE:r3 = ..23  LE:r3 = ..32 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	strb	r3, [r0, #0x03]
	mov	r3, r3, lsr #8		/* r3 = ...2 */
	orr	r3, r3, r2, lsl #8	/* r3 = ..12 */
	strh	r3, [r0, #0x01]
	mov	r2, r2, lsr #8		/* r2 = ...0 */
	strb	r2, [r0]
#else
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldrh	r3, [r1, #0x02]		/* BE:r3 = ..23  LE:r3 = ..32 */
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = ...1 */
	orr	r2, r2, r3, lsl #8	/* r2 = .321 */
	strh	r2, [r0, #0x01]
	mov	r3, r3, lsr #8		/* r3 = ...3 */
	strb	r3, [r0, #0x03]
#endif
	RET
	LMEMCPY_4_PAD

/*
 * 1111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrb	r1, [r1, #0x03]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strb	r1, [r0, #0x03]
	RET
	LMEMCPY_4_PAD


/******************************************************************************
 * Special case for 6 byte copies
 */
#define	LMEMCPY_6_LOG2	6	/* 64 bytes */
#define	LMEMCPY_6_PAD	.align LMEMCPY_6_LOG2
	LMEMCPY_6_PAD
.Lmemcpy_6:
	and	r2, r1, #0x03
	orr	r2, r2, r0, lsl #2
	ands	r2, r2, #0x0f
	sub	r3, pc, #0x14
	addne	pc, r3, r2, lsl #LMEMCPY_6_LOG2

/*
 * 0000: dst is 32-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
	ldrh	r3, [r1, #0x04]
	str	r2, [r0]
	strh	r3, [r0, #0x04]
	RET
	LMEMCPY_6_PAD

/*
 * 0001: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldr	r2, [r1, #-1]		/* BE:r2 = x012  LE:r2 = 210x */
	ldr	r3, [r1, #0x03]		/* BE:r3 = 345x  LE:r3 = x543 */
#ifdef __ARMEB__
	mov	r2, r2, lsl #8		/* r2 = 012. */
	orr	r2, r2, r3, lsr #24	/* r2 = 0123 */
#else
	mov	r2, r2, lsr #8		/* r2 = .210 */
	orr	r2, r2, r3, lsl #24	/* r2 = 3210 */
#endif
	mov	r3, r3, lsr #8		/* BE:r3 = .345  LE:r3 = .x54 */
	str	r2, [r0]
	strh	r3, [r0, #0x04]
	RET
	LMEMCPY_6_PAD

/*
 * 0010: dst is 32-bit aligned, src is 16-bit aligned
 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
#ifdef __ARMEB__
	mov	r1, r3, lsr #16		/* r1 = ..23 */
	orr	r1, r1, r2, lsl #16	/* r1 = 0123 */
	str	r1, [r0]
	strh	r3, [r0, #0x04]
#else
	mov	r1, r3, lsr #16		/* r1 = ..54 */
	orr	r2, r2, r3, lsl #16	/* r2 = 3210 */
	str	r2, [r0]
	strh	r1, [r0, #0x04]
#endif
	RET
	LMEMCPY_6_PAD

/*
 * 0011: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldr	r2, [r1, #-3]		/* BE:r2 = xxx0  LE:r2 = 0xxx */
	ldr	r3, [r1, #1]		/* BE:r3 = 1234  LE:r3 = 4321 */
	ldr	r1, [r1, #5]		/* BE:r1 = 5xxx  LE:r3 = xxx5 */
#ifdef __ARMEB__
	mov	r2, r2, lsl #24		/* r2 = 0... */
	orr	r2, r2, r3, lsr #8	/* r2 = 0123 */
	mov	r3, r3, lsl #8		/* r3 = 234. */
	orr	r1, r3, r1, lsr #24	/* r1 = 2345 */
#else
	mov	r2, r2, lsr #24		/* r2 = ...0 */
	orr	r2, r2, r3, lsl #8	/* r2 = 3210 */
	mov	r1, r1, lsl #8		/* r1 = xx5. */
	orr	r1, r1, r3, lsr #24	/* r1 = xx54 */
#endif
	str	r2, [r0]
	strh	r1, [r0, #0x04]
	RET
	LMEMCPY_6_PAD

/*
 * 0100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r3, [r1]		/* BE:r3 = 0123  LE:r3 = 3210 */
	ldrh	r2, [r1, #0x04]		/* BE:r2 = ..45  LE:r2 = ..54 */
	mov	r1, r3, lsr #8		/* BE:r1 = .012  LE:r1 = .321 */
	strh	r1, [r0, #0x01]
#ifdef __ARMEB__
	mov	r1, r3, lsr #24		/* r1 = ...0 */
	strb	r1, [r0]
	mov	r3, r3, lsl #8		/* r3 = 123. */
	orr	r3, r3, r2, lsr #8	/* r3 = 1234 */
#else
	strb	r3, [r0]
	mov	r3, r3, lsr #24		/* r3 = ...3 */
	orr	r3, r3, r2, lsl #8	/* r3 = .543 */
	mov	r2, r2, lsr #8		/* r2 = ...5 */
#endif
	strh	r3, [r0, #0x03]
	strb	r2, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 0101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrh	ip, [r1, #0x03]
	ldrb	r1, [r1, #0x05]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strh	ip, [r0, #0x03]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 0110: dst is 8-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r1, [r1, #0x02]		/* BE:r1 = 2345  LE:r1 = 5432 */
#ifdef __ARMEB__
	mov	r3, r2, lsr #8		/* r3 = ...0 */
	strb	r3, [r0]
	strb	r1, [r0, #0x05]
	mov	r3, r1, lsr #8		/* r3 = .234 */
	strh	r3, [r0, #0x03]
	mov	r3, r2, lsl #8		/* r3 = .01. */
	orr	r3, r3, r1, lsr #24	/* r3 = .012 */
	strh	r3, [r0, #0x01]
#else
	strb	r2, [r0]
	mov	r3, r1, lsr #24
	strb	r3, [r0, #0x05]
	mov	r3, r1, lsr #8		/* r3 = .543 */
	strh	r3, [r0, #0x03]
	mov	r3, r2, lsr #8		/* r3 = ...1 */
	orr	r3, r3, r1, lsl #8	/* r3 = 4321 */
	strh	r3, [r0, #0x01]
#endif
	RET
	LMEMCPY_6_PAD

/*
 * 0111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrh	ip, [r1, #0x03]
	ldrb	r1, [r1, #0x05]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strh	ip, [r0, #0x03]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 1000: dst is 16-bit aligned, src is 32-bit aligned
 */
#ifdef __ARMEB__
	ldr	r2, [r1]		/* r2 = 0123 */
	ldrh	r3, [r1, #0x04]		/* r3 = ..45 */
	mov	r1, r2, lsr #16		/* r1 = ..01 */
	orr	r3, r3, r2, lsl#16	/* r3 = 2345 */
	strh	r1, [r0]
	str	r3, [r0, #0x02]
#else
	ldrh	r2, [r1, #0x04]		/* r2 = ..54 */
	ldr	r3, [r1]		/* r3 = 3210 */
	mov	r2, r2, lsl #16		/* r2 = 54.. */
	orr	r2, r2, r3, lsr #16	/* r2 = 5432 */
	strh	r3, [r0]
	str	r2, [r0, #0x02]
#endif
	RET
	LMEMCPY_6_PAD

/*
 * 1001: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #-1]		/* BE:r3 = x012  LE:r3 = 210x */
	ldr	r2, [r1, #3]		/* BE:r2 = 345x  LE:r2 = x543 */
	mov	r1, r3, lsr #8		/* BE:r1 = .x01  LE:r1 = .210 */
#ifdef __ARMEB__
	mov	r2, r2, lsr #8		/* r2 = .345 */
	orr	r2, r2, r3, lsl #24	/* r2 = 2345 */
#else
	mov	r2, r2, lsl #8		/* r2 = 543. */
	orr	r2, r2, r3, lsr #24	/* r2 = 5432 */
#endif
	strh	r1, [r0]
	str	r2, [r0, #0x02]
	RET
	LMEMCPY_6_PAD

/*
 * 1010: dst is 16-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]
	ldr	r3, [r1, #0x02]
	strh	r2, [r0]
	str	r3, [r0, #0x02]
	RET
	LMEMCPY_6_PAD

/*
 * 1011: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldrb	r3, [r1]		/* r3 = ...0 */
	ldr	r2, [r1, #0x01]		/* BE:r2 = 1234  LE:r2 = 4321 */
	ldrb	r1, [r1, #0x05]		/* r1 = ...5 */
#ifdef __ARMEB__
	mov	r3, r3, lsl #8		/* r3 = ..0. */
	orr	r3, r3, r2, lsr #24	/* r3 = ..01 */
	orr	r1, r1, r2, lsl #8	/* r1 = 2345 */
#else
	orr	r3, r3, r2, lsl #8	/* r3 = 3210 */
	mov	r1, r1, lsl #24		/* r1 = 5... */
	orr	r1, r1, r2, lsr #8	/* r1 = 5432 */
#endif
	strh	r3, [r0]
	str	r1, [r0, #0x02]
	RET
	LMEMCPY_6_PAD

/*
 * 1100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
	ldrh	r1, [r1, #0x04]		/* BE:r1 = ..45  LE:r1 = ..54 */
#ifdef __ARMEB__
	mov	r3, r2, lsr #24		/* r3 = ...0 */
	strb	r3, [r0]
	mov	r2, r2, lsl #8		/* r2 = 123. */
	orr	r2, r2, r1, lsr #8	/* r2 = 1234 */
#else
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = .321 */
	orr	r2, r2, r1, lsl #24	/* r2 = 4321 */
	mov	r1, r1, lsr #8		/* r1 = ...5 */
#endif
	str	r2, [r0, #0x01]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 1101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldrh	ip, [r1, #0x03]
	ldrb	r1, [r1, #0x05]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	strh	ip, [r0, #0x03]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 1110: dst is 8-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r1, [r1, #0x02]		/* BE:r1 = 2345  LE:r1 = 5432 */
#ifdef __ARMEB__
	mov	r3, r2, lsr #8		/* r3 = ...0 */
	strb	r3, [r0]
	mov	r2, r2, lsl #24		/* r2 = 1... */
	orr	r2, r2, r1, lsr #8	/* r2 = 1234 */
#else
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = ...1 */
	orr	r2, r2, r1, lsl #8	/* r2 = 4321 */
	mov	r1, r1, lsr #24		/* r1 = ...5 */
#endif
	str	r2, [r0, #0x01]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD

/*
 * 1111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldr	r3, [r1, #0x01]
	ldrb	r1, [r1, #0x05]
	strb	r2, [r0]
	str	r3, [r0, #0x01]
	strb	r1, [r0, #0x05]
	RET
	LMEMCPY_6_PAD


/******************************************************************************
 * Special case for 8 byte copies
 */
#define	LMEMCPY_8_LOG2	6	/* 64 bytes */
#define	LMEMCPY_8_PAD	.align LMEMCPY_8_LOG2
	LMEMCPY_8_PAD
.Lmemcpy_8:
	and	r2, r1, #0x03
	orr	r2, r2, r0, lsl #2
	ands	r2, r2, #0x0f
	sub	r3, pc, #0x14
	addne	pc, r3, r2, lsl #LMEMCPY_8_LOG2

/*
 * 0000: dst is 32-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
	ldr	r3, [r1, #0x04]
	str	r2, [r0]
	str	r3, [r0, #0x04]
	RET
	LMEMCPY_8_PAD

/*
 * 0001: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #-1]		/* BE:r3 = x012  LE:r3 = 210x */
	ldr	r2, [r1, #0x03]		/* BE:r2 = 3456  LE:r2 = 6543 */
	ldrb	r1, [r1, #0x07]		/* r1 = ...7 */
#ifdef __ARMEB__
	mov	r3, r3, lsl #8		/* r3 = 012. */
	orr	r3, r3, r2, lsr #24	/* r3 = 0123 */
	orr	r2, r1, r2, lsl #8	/* r2 = 4567 */
#else
	mov	r3, r3, lsr #8		/* r3 = .210 */
	orr	r3, r3, r2, lsl #24	/* r3 = 3210 */
	mov	r1, r1, lsl #24		/* r1 = 7... */
	orr	r2, r1, r2, lsr #8	/* r2 = 7654 */
#endif
	str	r3, [r0]
	str	r2, [r0, #0x04]
	RET
	LMEMCPY_8_PAD

/*
 * 0010: dst is 32-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldrh	r1, [r1, #0x06]		/* BE:r1 = ..67  LE:r1 = ..76 */
#ifdef __ARMEB__
	mov	r2, r2, lsl #16		/* r2 = 01.. */
	orr	r2, r2, r3, lsr #16	/* r2 = 0123 */
	orr	r3, r1, r3, lsl #16	/* r3 = 4567 */
#else
	orr	r2, r2, r3, lsl #16	/* r2 = 3210 */
	mov	r3, r3, lsr #16		/* r3 = ..54 */
	orr	r3, r3, r1, lsl #16	/* r3 = 7654 */
#endif
	str	r2, [r0]
	str	r3, [r0, #0x04]
	RET
	LMEMCPY_8_PAD

/*
 * 0011: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldrb	r3, [r1]		/* r3 = ...0 */
	ldr	r2, [r1, #0x01]		/* BE:r2 = 1234  LE:r2 = 4321 */
	ldr	r1, [r1, #0x05]		/* BE:r1 = 567x  LE:r1 = x765 */
#ifdef __ARMEB__
	mov	r3, r3, lsl #24		/* r3 = 0... */
	orr	r3, r3, r2, lsr #8	/* r3 = 0123 */
	mov	r2, r2, lsl #24		/* r2 = 4... */
	orr	r2, r2, r1, lsr #8	/* r2 = 4567 */
#else
	orr	r3, r3, r2, lsl #8	/* r3 = 3210 */
	mov	r2, r2, lsr #24		/* r2 = ...4 */
	orr	r2, r2, r1, lsl #8	/* r2 = 7654 */
#endif
	str	r3, [r0]
	str	r2, [r0, #0x04]
	RET
	LMEMCPY_8_PAD

/*
 * 0100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r3, [r1]		/* BE:r3 = 0123  LE:r3 = 3210 */
	ldr	r2, [r1, #0x04]		/* BE:r2 = 4567  LE:r2 = 7654 */
#ifdef __ARMEB__
	mov	r1, r3, lsr #24		/* r1 = ...0 */
	strb	r1, [r0]
	mov	r1, r3, lsr #8		/* r1 = .012 */
	strb	r2, [r0, #0x07]
	mov	r3, r3, lsl #24		/* r3 = 3... */
	orr	r3, r3, r2, lsr #8	/* r3 = 3456 */
#else
	strb	r3, [r0]
	mov	r1, r2, lsr #24		/* r1 = ...7 */
	strb	r1, [r0, #0x07]
	mov	r1, r3, lsr #8		/* r1 = .321 */
	mov	r3, r3, lsr #24		/* r3 = ...3 */
	orr	r3, r3, r2, lsl #8	/* r3 = 6543 */
#endif
	strh	r1, [r0, #0x01]
	str	r3, [r0, #0x03]
	RET
	LMEMCPY_8_PAD

/*
 * 0101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldr	ip, [r1, #0x03]
	ldrb	r1, [r1, #0x07]
	strb	r2, [r0]
	strh	r3, [r0, #0x01]
	str	ip, [r0, #0x03]
	strb	r1, [r0, #0x07]
	RET
	LMEMCPY_8_PAD

/*
 * 0110: dst is 8-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldrh	r1, [r1, #0x06]		/* BE:r1 = ..67  LE:r1 = ..76 */
#ifdef __ARMEB__
	mov	ip, r2, lsr #8		/* ip = ...0 */
	strb	ip, [r0]
	mov	ip, r2, lsl #8		/* ip = .01. */
	orr	ip, ip, r3, lsr #24	/* ip = .012 */
	strb	r1, [r0, #0x07]
	mov	r3, r3, lsl #8		/* r3 = 345. */
	orr	r3, r3, r1, lsr #8	/* r3 = 3456 */
#else
	strb	r2, [r0]		/* 0 */
	mov	ip, r1, lsr #8		/* ip = ...7 */
	strb	ip, [r0, #0x07]		/* 7 */
	mov	ip, r2, lsr #8		/* ip = ...1 */
	orr	ip, ip, r3, lsl #8	/* ip = 4321 */
	mov	r3, r3, lsr #8		/* r3 = .543 */
	orr	r3, r3, r1, lsl #24	/* r3 = 6543 */
#endif
	strh	ip, [r0, #0x01]
	str	r3, [r0, #0x03]
	RET
	LMEMCPY_8_PAD

/*
 * 0111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r3, [r1]		/* r3 = ...0 */
	ldr	ip, [r1, #0x01]		/* BE:ip = 1234  LE:ip = 4321 */
	ldrh	r2, [r1, #0x05]		/* BE:r2 = ..56  LE:r2 = ..65 */
	ldrb	r1, [r1, #0x07]		/* r1 = ...7 */
	strb	r3, [r0]
	mov	r3, ip, lsr #16		/* BE:r3 = ..12  LE:r3 = ..43 */
#ifdef __ARMEB__
	strh	r3, [r0, #0x01]
	orr	r2, r2, ip, lsl #16	/* r2 = 3456 */
#else
	strh	ip, [r0, #0x01]
	orr	r2, r3, r2, lsl #16	/* r2 = 6543 */
#endif
	str	r2, [r0, #0x03]
	strb	r1, [r0, #0x07]
	RET
	LMEMCPY_8_PAD

/*
 * 1000: dst is 16-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
	ldr	r3, [r1, #0x04]		/* BE:r3 = 4567  LE:r3 = 7654 */
	mov	r1, r2, lsr #16		/* BE:r1 = ..01  LE:r1 = ..32 */
#ifdef __ARMEB__
	strh	r1, [r0]
	mov	r1, r3, lsr #16		/* r1 = ..45 */
	orr	r2, r1 ,r2, lsl #16	/* r2 = 2345 */
#else
	strh	r2, [r0]
	orr	r2, r1, r3, lsl #16	/* r2 = 5432 */
	mov	r3, r3, lsr #16		/* r3 = ..76 */
#endif
	str	r2, [r0, #0x02]
	strh	r3, [r0, #0x06]
	RET
	LMEMCPY_8_PAD

/*
 * 1001: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldr	r2, [r1, #-1]		/* BE:r2 = x012  LE:r2 = 210x */
	ldr	r3, [r1, #0x03]		/* BE:r3 = 3456  LE:r3 = 6543 */
	ldrb	ip, [r1, #0x07]		/* ip = ...7 */
	mov	r1, r2, lsr #8		/* BE:r1 = .x01  LE:r1 = .210 */
	strh	r1, [r0]
#ifdef __ARMEB__
	mov	r1, r2, lsl #24		/* r1 = 2... */
	orr	r1, r1, r3, lsr #8	/* r1 = 2345 */
	orr	r3, ip, r3, lsl #8	/* r3 = 4567 */
#else
	mov	r1, r2, lsr #24		/* r1 = ...2 */
	orr	r1, r1, r3, lsl #8	/* r1 = 5432 */
	mov	r3, r3, lsr #24		/* r3 = ...6 */
	orr	r3, r3, ip, lsl #8	/* r3 = ..76 */
#endif
	str	r1, [r0, #0x02]
	strh	r3, [r0, #0x06]
	RET
	LMEMCPY_8_PAD

/*
 * 1010: dst is 16-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]
	ldr	ip, [r1, #0x02]
	ldrh	r3, [r1, #0x06]
	strh	r2, [r0]
	str	ip, [r0, #0x02]
	strh	r3, [r0, #0x06]
	RET
	LMEMCPY_8_PAD

/*
 * 1011: dst is 16-bit aligned, src is 8-bit aligned
 */
	ldr	r3, [r1, #0x05]		/* BE:r3 = 567x  LE:r3 = x765 */
	ldr	r2, [r1, #0x01]		/* BE:r2 = 1234  LE:r2 = 4321 */
	ldrb	ip, [r1]		/* ip = ...0 */
	mov	r1, r3, lsr #8		/* BE:r1 = .567  LE:r1 = .x76 */
	strh	r1, [r0, #0x06]
#ifdef __ARMEB__
	mov	r3, r3, lsr #24		/* r3 = ...5 */
	orr	r3, r3, r2, lsl #8	/* r3 = 2345 */
	mov	r2, r2, lsr #24		/* r2 = ...1 */
	orr	r2, r2, ip, lsl #8	/* r2 = ..01 */
#else
	mov	r3, r3, lsl #24		/* r3 = 5... */
	orr	r3, r3, r2, lsr #8	/* r3 = 5432 */
	orr	r2, ip, r2, lsl #8	/* r2 = 3210 */
#endif
	str	r3, [r0, #0x02]
	strh	r2, [r0]
	RET
	LMEMCPY_8_PAD

/*
 * 1100: dst is 8-bit aligned, src is 32-bit aligned
 */
	ldr	r3, [r1, #0x04]		/* BE:r3 = 4567  LE:r3 = 7654 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
	mov	r1, r3, lsr #8		/* BE:r1 = .456  LE:r1 = .765 */
	strh	r1, [r0, #0x05]
#ifdef __ARMEB__
	strb	r3, [r0, #0x07]
	mov	r1, r2, lsr #24		/* r1 = ...0 */
	strb	r1, [r0]
	mov	r2, r2, lsl #8		/* r2 = 123. */
	orr	r2, r2, r3, lsr #24	/* r2 = 1234 */
	str	r2, [r0, #0x01]
#else
	strb	r2, [r0]
	mov	r1, r3, lsr #24		/* r1 = ...7 */
	strb	r1, [r0, #0x07]
	mov	r2, r2, lsr #8		/* r2 = .321 */
	orr	r2, r2, r3, lsl #24	/* r2 = 4321 */
	str	r2, [r0, #0x01]
#endif
	RET
	LMEMCPY_8_PAD

/*
 * 1101: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r3, [r1]		/* r3 = ...0 */
	ldrh	r2, [r1, #0x01]		/* BE:r2 = ..12  LE:r2 = ..21 */
	ldr	ip, [r1, #0x03]		/* BE:ip = 3456  LE:ip = 6543 */
	ldrb	r1, [r1, #0x07]		/* r1 = ...7 */
	strb	r3, [r0]
	mov	r3, ip, lsr #16		/* BE:r3 = ..34  LE:r3 = ..65 */
#ifdef __ARMEB__
	strh	ip, [r0, #0x05]
	orr	r2, r3, r2, lsl #16	/* r2 = 1234 */
#else
	strh	r3, [r0, #0x05]
	orr	r2, r2, ip, lsl #16	/* r2 = 4321 */
#endif
	str	r2, [r0, #0x01]
	strb	r1, [r0, #0x07]
	RET
	LMEMCPY_8_PAD

/*
 * 1110: dst is 8-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldrh	r1, [r1, #0x06]		/* BE:r1 = ..67  LE:r1 = ..76 */
#ifdef __ARMEB__
	mov	ip, r2, lsr #8		/* ip = ...0 */
	strb	ip, [r0]
	mov	ip, r2, lsl #24		/* ip = 1... */
	orr	ip, ip, r3, lsr #8	/* ip = 1234 */
	strb	r1, [r0, #0x07]
	mov	r1, r1, lsr #8		/* r1 = ...6 */
	orr	r1, r1, r3, lsl #8	/* r1 = 3456 */
#else
	strb	r2, [r0]
	mov	ip, r2, lsr #8		/* ip = ...1 */
	orr	ip, ip, r3, lsl #8	/* ip = 4321 */
	mov	r2, r1, lsr #8		/* r2 = ...7 */
	strb	r2, [r0, #0x07]
	mov	r1, r1, lsl #8		/* r1 = .76. */
	orr	r1, r1, r3, lsr #24	/* r1 = .765 */
#endif
	str	ip, [r0, #0x01]
	strh	r1, [r0, #0x05]
	RET
	LMEMCPY_8_PAD

/*
 * 1111: dst is 8-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]
	ldr	ip, [r1, #0x01]
	ldrh	r3, [r1, #0x05]
	ldrb	r1, [r1, #0x07]
	strb	r2, [r0]
	str	ip, [r0, #0x01]
	strh	r3, [r0, #0x05]
	strb	r1, [r0, #0x07]
	RET
	LMEMCPY_8_PAD

/******************************************************************************
 * Special case for 12 byte copies
 */
#define	LMEMCPY_C_LOG2	7	/* 128 bytes */
#define	LMEMCPY_C_PAD	.align LMEMCPY_C_LOG2
	LMEMCPY_C_PAD
.Lmemcpy_c:
	and	r2, r1, #0x03
	orr	r2, r2, r0, lsl #2
	ands	r2, r2, #0x0f
	sub	r3, pc, #0x14
	addne	pc, r3, r2, lsl #LMEMCPY_C_LOG2

/*
 * 0000: dst is 32-bit aligned, src is 32-bit aligned
 */
	ldr	r2, [r1]
	ldr	r3, [r1, #0x04]
	ldr	r1, [r1, #0x08]
	str	r2, [r0]
	str	r3, [r0, #0x04]
	str	r1, [r0, #0x08]
	RET
	LMEMCPY_C_PAD

/*
 * 0001: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1, #0xb]		/* r2 = ...B */
	ldr	ip, [r1, #0x07]		/* BE:ip = 789A  LE:ip = A987 */
	ldr	r3, [r1, #0x03]		/* BE:r3 = 3456  LE:r3 = 6543 */
	ldr	r1, [r1, #-1]		/* BE:r1 = x012  LE:r1 = 210x */
#ifdef __ARMEB__
	orr	r2, r2, ip, lsl #8	/* r2 = 89AB */
	str	r2, [r0, #0x08]
	mov	r2, ip, lsr #24		/* r2 = ...7 */
	orr	r2, r2, r3, lsl #8	/* r2 = 4567 */
	mov	r1, r1, lsl #8		/* r1 = 012. */
	orr	r1, r1, r3, lsr #24	/* r1 = 0123 */
#else
	mov	r2, r2, lsl #24		/* r2 = B... */
	orr	r2, r2, ip, lsr #8	/* r2 = BA98 */
	str	r2, [r0, #0x08]
	mov	r2, ip, lsl #24		/* r2 = 7... */
	orr	r2, r2, r3, lsr #8	/* r2 = 7654 */
	mov	r1, r1, lsr #8		/* r1 = .210 */
	orr	r1, r1, r3, lsl #24	/* r1 = 3210 */
#endif
	str	r2, [r0, #0x04]
	str	r1, [r0]
	RET
	LMEMCPY_C_PAD

/*
 * 0010: dst is 32-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldr	ip, [r1, #0x06]		/* BE:ip = 6789  LE:ip = 9876 */
	ldrh	r1, [r1, #0x0a]		/* BE:r1 = ..AB  LE:r1 = ..BA */
#ifdef __ARMEB__
	mov	r2, r2, lsl #16		/* r2 = 01.. */
	orr	r2, r2, r3, lsr #16	/* r2 = 0123 */
	str	r2, [r0]
	mov	r3, r3, lsl #16		/* r3 = 45.. */
	orr	r3, r3, ip, lsr #16	/* r3 = 4567 */
	orr	r1, r1, ip, lsl #16	/* r1 = 89AB */
#else
	orr	r2, r2, r3, lsl #16	/* r2 = 3210 */
	str	r2, [r0]
	mov	r3, r3, lsr #16		/* r3 = ..54 */
	orr	r3, r3, ip, lsl #16	/* r3 = 7654 */
	mov	r1, r1, lsl #16		/* r1 = BA.. */
	orr	r1, r1, ip, lsr #16	/* r1 = BA98 */
#endif
	str	r3, [r0, #0x04]
	str	r1, [r0, #0x08]
	RET
	LMEMCPY_C_PAD

/*
 * 0011: dst is 32-bit aligned, src is 8-bit aligned
 */
	ldrb	r2, [r1]		/* r2 = ...0 */
	ldr	r3, [r1, #0x01]		/* BE:r3 = 1234  LE:r3 = 4321 */
	ldr	ip, [r1, #0x05]		/* BE:ip = 5678  LE:ip = 8765 */
	ldr	r1, [r1, #0x09]		/* BE:r1 = 9ABx  LE:r1 = xBA9 */
#ifdef __ARMEB__
	mov	r2, r2, lsl #24		/* r2 = 0... */
	orr	r2, r2, r3, lsr #8	/* r2 = 0123 */
	str	r2, [r0]
	mov	r3, r3, lsl #24		/* r3 = 4... */
	orr	r3, r3, ip, lsr #8	/* r3 = 4567 */
	mov	r1, r1, lsr #8		/* r1 = .9AB */
	orr	r1, r1, ip, lsl #24	/* r1 = 89AB */
#else
	orr	r2, r2, r3, lsl #8	/* r2 = 3210 */
	str	r2, [r0]
	mov	r3, r3, lsr #24		/* r3 = ...4 */
	orr	r3, r3, ip, lsl #8	/* r3 = 7654 */
	mov	r1, r1, lsl #8		/* r1 = BA9. */
	orr	r1, r1, ip, lsr #24	/* r1 = BA98 */
#endif
	str	r3, [r0, #0x04]
	str	r1, [r0, #0x08]
	RET
	LMEMCPY_C_PAD

/*
 * 0100: dst is 8-bit aligned (byte 1), src is 32-bit aligned
 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
	ldr	r3, [r1, #0x04]		/* BE:r3 = 4567  LE:r3 = 7654 */
	ldr	ip, [r1, #0x08]		/* BE:ip = 89AB  LE:ip = BA98 */
	mov	r1, r2, lsr #8		/* BE:r1 = .012  LE:r1 = .321 */
	strh	r1, [r0, #0x01]
#ifdef __ARMEB__
	mov	r1, r2, lsr #24		/* r1 = ...0 */
	strb	r1, [r0]
	mov	r1, r2, lsl #24		/* r1 = 3... */
	orr	r2, r1, r3, lsr #8	/* r1 = 3456 */
	mov	r1, r3, lsl #24		/* r1 = 7... */
	orr	r1, r1, ip, lsr #8	/* r1 = 789A */
#else
	strb	r2, [r0]
	mov	r1, r2, lsr #24		/* r1 = ...3 */
	orr	r2, r1, r3, lsl #8	/* r1 = 6543 */
	mov	r1, r3, lsr #24		/* r1 = ...7 */
	orr	r1, r1, ip, lsl #8	/* r1 = A987 */
	mov	ip, ip, lsr #24		/* ip = ...B */
#endif
	str	r2, [r0, #0x03]
	str	r1, [r0, #0x07]
	strb	ip, [r0, #0x0b]
	RET
	LMEMCPY_C_PAD

/*
 * 0101: dst is 8-bit aligned (byte 1), src is 8-bit aligned (byte 1)
 */
	ldrb	r2, [r1]
	ldrh	r3, [r1, #0x01]
	ldr	ip, [r1, #0x03]
	strb	r2, [r0]
	ldr	r2, [r1, #0x07]
	ldrb	r1, [r1, #0x0b]
	strh	r3, [r0, #0x01]
	str	ip, [r0, #0x03]
	str	r2, [r0, #0x07]
	strb	r1, [r0, #0x0b]
	RET
	LMEMCPY_C_PAD

/*
 * 0110: dst is 8-bit aligned (byte 1), src is 16-bit aligned
 */
	ldrh	r2, [r1]		/* BE:r2 = ..01  LE:r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* BE:r3 = 2345  LE:r3 = 5432 */
	ldr	ip, [r1, #0x06]		/* BE:ip = 6789  LE:ip = 9876 */
	ldrh	r1, [r1, #0x0a]		/* BE:r1 = ..AB  LE:r1 = ..BA */
#ifdef __ARMEB__
	mov	r2, r2, ror #8		/* r2 = 1..0 */
	strb	r2, [r0]
	mov	r2, r2, lsr #16		/* r2 = ..1. */
	orr	r2, r2, r3, lsr #24	/* r2 = ..12 */
	strh	r2, [r0, #0x01]
	mov	r2, r3, lsl #8		/* r2 = 345. */
	orr	r3, r2, ip, lsr #24	/* r3 = 3456 */
	mov	r2, ip, lsl #8		/* r2 = 789. */
	orr	r2, r2, r1, lsr #8	/* r2 = 789A */
#else
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = ...1 */
	orr	r2, r2, r3, lsl #8	/* r2 = 4321 */
	strh	r2, [r0, #0x01]
	mov	r2, r3, lsr #8		/* r2 = .543 */
	orr	r3, r2, ip, lsl #24	/* r3 = 6543 */
	mov	r2, ip, lsr #8		/* r2 = .987 */
	orr	r2, r2, r1, lsl #24	/* r2 = A987 */
	mov	r1, r1, lsr #8		/* r1 = ...B */
#endif
	str	r3, [r0, #0x03]
	str	r2, [r0, #0x07]
	strb	r1, [r0, #0x0b]
	RET
	LMEMCPY_C_PAD

/*
 * 0111: dst is 8-bit aligned (byte 1), src is 8-bit aligned (byte 3)
 */
	ldrb	r2, [r1]
	ldr	r3, [r1, #0x01]		/* BE:r3 = 1234  LE:r3 = 4321 */
	ldr	ip, [r1, #0x05]		/* BE:ip = 5678  LE:ip = 8765 */
	ldr	r1, [r1, #0x09]		/* BE:r1 = 9ABx  LE:r1 = xBA9 */
	strb	r2, [r0]
#ifdef __ARMEB__
	mov	r2, r3, lsr #16		/* r2 = ..12 */
	strh	r2, [r0, #0x01]
	mov	r3, r3, lsl #16		/* r3 = 34.. */
	orr	r3, r3, ip, lsr #16	/* r3 = 3456 */
	mov	ip, ip, lsl #16		/* ip = 78.. */
	orr	ip, ip, r1, lsr #16	/* ip = 789A */
	mov	r1, r1, lsr #8		/* r1 = .9AB */
#else
	strh	r3, [r0, #0x01]
	mov	r3, r3, lsr #16		/* r3 = ..43 */
	orr	r3, r3, ip, lsl #16	/* r3 = 6543 */
	mov	ip, ip, lsr #16		/* ip = ..87 */
	orr	ip, ip, r1, lsl #16	/* ip = A987 */
	mov	r1, r1, lsr #16		/* r1 = ..xB */
#endif
	str	r3, [r0, #0x03]
	str	ip, [r0, #0x07]
	strb	r1, [r0, #0x0b]
	RET
	LMEMCPY_C_PAD

/*
 * 1000: dst is 16-bit aligned, src is 32-bit aligned
 */
	ldr	ip, [r1]		/* BE:ip = 0123  LE:ip = 3210 */
	ldr	r3, [r1, #0x04]		/* BE:r3 = 4567  LE:r3 = 7654 */
	ldr	r2, [r1, #0x08]		/* BE:r2 = 89AB  LE:r2 = BA98 */
	mov	r1, ip, lsr #16		/* BE:r1 = ..01  LE:r1 = ..32 */
#ifdef __ARMEB__
	strh	r1, [r0]
	mov	r1, ip, lsl #16		/* r1 = 23.. */
	orr	r1, r1, r3, lsr #16	/* r1 = 2345 */
	mov	r3, r3, lsl #16		/* r3 = 67.. */
	orr	r3, r3, r2, lsr #16	/* r3 = 6789 */
#else
	strh	ip, [r0]
	orr	r1, r1, r3, lsl #16	/* r1 = 5432 */
	mov	r3, r3, lsr #16		/* r3 = ..76 */
	orr	r3, r3, r2, lsl #16	/* r3 = 9876 */
	mov	r2, r2, lsr #16		/* r2 = ..BA */
#endif
	str	r1, [r0, #0x02]
	str	r3, [r0, #0x06]
	strh	r2, [r0, #0x0a]
	RET
	LMEMCPY_C_PAD

/*
 * 1001: dst is 16-bit aligned, src is 8-bit aligned (byte 1)
 */
	ldr	r2, [r1, #-1]		/* BE:r2 = x012  LE:r2 = 210x */
	ldr	r3, [r1, #0x03]		/* BE:r3 = 3456  LE:r3 = 6543 */
	mov	ip, r2, lsr #8		/* BE:ip = .x01  LE:ip = .210 */
	strh	ip, [r0]
	ldr	ip, [r1, #0x07]		/* BE:ip = 789A  LE:ip = A987 */
	ldrb	r1, [r1, #0x0b]		/* r1 = ...B */
#ifdef __ARMEB__
	mov	r2, r2, lsl #24		/* r2 = 2... */
	orr	r2, r2, r3, lsr #8	/* r2 = 2345 */
	mov	r3, r3, lsl #24		/* r3 = 6... */
	orr	r3, r3, ip, lsr #8	/* r3 = 6789 */
	orr	r1, r1, ip, lsl #8	/* r1 = 89AB */
#else
	mov	r2, r2, lsr #24		/* r2 = ...2 */
	orr	r2, r2, r3, lsl #8	/* r2 = 5432 */
	mov	r3, r3, lsr #24		/* r3 = ...6 */
	orr	r3, r3, ip, lsl #8	/* r3 = 9876 */
	mov	r1, r1, lsl #8		/* r1 = ..B. */
	orr	r1, r1, ip, lsr #24	/* r1 = ..BA */
#endif
	str	r2, [r0, #0x02]
	str	r3, [r0, #0x06]
	strh	r1, [r0, #0x0a]
	RET
	LMEMCPY_C_PAD

/*
 * 1010: dst is 16-bit aligned, src is 16-bit aligned
 */
	ldrh	r2, [r1]
	ldr	r3, [r1, #0x02]
	ldr	ip, [r1, #0x06]
	ldrh	r1, [r1, #0x0a]
	strh	r2, [r0]
	str	r3, [r0, #0x02]
	str	ip, [r0, #0x06]
	strh	r1, [r0, #0x0a]
	RET
	LMEMCPY_C_PAD

/*
 * 1011: dst is 16-bit aligned, src is 8-bit aligned (byte 3)
 */
	ldr	r2, [r1, #0x09]		/* BE:r2 = 9ABx  LE:r2 = xBA9 */
	ldr	r3, [r1, #0x05]		/* BE:r3 = 5678  LE:r3 = 8765 */
	mov	ip, r2, lsr #8		/* BE:ip = .9AB  LE:ip = .xBA */
	strh	ip, [r0, #0x0a]
	ldr	ip, [r1, #0x01]		/* BE:ip = 1234  LE:ip = 4321 */
	ldrb	r1, [r1]		/* r1 = ...0 */
#ifdef __ARMEB__
	mov	r2, r2, lsr #24		/* r2 = ...9 */
	orr	r2, r2, r3, lsl #8	/* r2 = 6789 */
	mov	r3, r3, lsr #24		/* r3 = ...5 */
	orr	r3, r3, ip, lsl #8	/* r3 = 2345 */
	mov	r1, r1, lsl #8		/* r1 = ..0. */
	orr	r1, r1, ip, lsr #24	/* r1 = ..01 */
#else
	mov	r2, r2, lsl #24		/* r2 = 9... */
	orr	r2, r2, r3, lsr #8	/* r2 = 9876 */
	mov	r3, r3, lsl #24		/* r3 = 5... */
	orr	r3, r3, ip, lsr #8	/* r3 = 5432 */
	orr	r1, r1, ip, lsl #8	/* r1 = 3210 */
#endif
	str	r2, [r0, #0x06]
	str	r3, [r0, #0x02]
	strh	r1, [r0]
	RET
	LMEMCPY_C_PAD

/*
 * 1100: dst is 8-bit aligned (byte 3), src is 32-bit aligned
 */
	ldr	r2, [r1]		/* BE:r2 = 0123  LE:r2 = 3210 */
	ldr	ip, [r1, #0x04]		/* BE:ip = 4567  LE:ip = 7654 */
	ldr	r1, [r1, #0x08]		/* BE:r1 = 89AB  LE:r1 = BA98 */
#ifdef __ARMEB__
	mov	r3, r2, lsr #24		/* r3 = ...0 */
	strb	r3, [r0]
	mov	r2, r2, lsl #8		/* r2 = 123. */
	orr	r2, r2, ip, lsr #24	/* r2 = 1234 */
	str	r2, [r0, #0x01]
	mov	r2, ip, lsl #8		/* r2 = 567. */
	orr	r2, r2, r1, lsr #24	/* r2 = 5678 */
	str	r2, [r0, #0x05]
	mov	r2, r1, lsr #8		/* r2 = ..9A */
	strh	r2, [r0, #0x09]
	strb	r1, [r0, #0x0b]
#else
	strb	r2, [r0]
	mov	r3, r2, lsr #8		/* r3 = .321 */
	orr	r3, r3, ip, lsl #24	/* r3 = 4321 */
	str	r3, [r0, #0x01]
	mov	r3, ip, lsr #8		/* r3 = .765 */
	orr	r3, r3, r1, lsl #24	/* r3 = 8765 */
	str	r3, [r0, #0x05]
	mov	r1, r1, lsr #8		/* r1 = .BA9 */
	strh	r1, [r0, #0x09]
	mov	r1, r1, lsr #16		/* r1 = ...B */
	strb	r1, [r0, #0x0b]
#endif
	RET
	LMEMCPY_C_PAD

/*
 * 1101: dst is 8-bit aligned (byte 3), src is 8-bit aligned (byte 1)
 */
	ldrb	r2, [r1, #0x0b]		/* r2 = ...B */
	ldr	r3, [r1, #0x07]		/* BE:r3 = 789A  LE:r3 = A987 */
	ldr	ip, [r1, #0x03]		/* BE:ip = 3456  LE:ip = 6543 */
	ldr	r1, [r1, #-1]		/* BE:r1 = x012  LE:r1 = 210x */
	strb	r2, [r0, #0x0b]
#ifdef __ARMEB__
	strh	r3, [r0, #0x09]
	mov	r3, r3, lsr #16		/* r3 = ..78 */
	orr	r3, r3, ip, lsl #16	/* r3 = 5678 */
	mov	ip, ip, lsr #16		/* ip = ..34 */
	orr	ip, ip, r1, lsl #16	/* ip = 1234 */
	mov	r1, r1, lsr #16		/* r1 = ..x0 */
#else
	mov	r2, r3, lsr #16		/* r2 = ..A9 */
	strh	r2, [r0, #0x09]
	mov	r3, r3, lsl #16		/* r3 = 87.. */
	orr	r3, r3, ip, lsr #16	/* r3 = 8765 */
	mov	ip, ip, lsl #16		/* ip = 43.. */
	orr	ip, ip, r1, lsr #16	/* ip = 4321 */
	mov	r1, r1, lsr #8		/* r1 = .210 */
#endif
	str	r3, [r0, #0x05]
	str	ip, [r0, #0x01]
	strb	r1, [r0]
	RET
	LMEMCPY_C_PAD

/*
 * 1110: dst is 8-bit aligned (byte 3), src is 16-bit aligned
 */
#ifdef __ARMEB__
	ldrh	r2, [r1, #0x0a]		/* r2 = ..AB */
	ldr	ip, [r1, #0x06]		/* ip = 6789 */
	ldr	r3, [r1, #0x02]		/* r3 = 2345 */
	ldrh	r1, [r1]		/* r1 = ..01 */
	strb	r2, [r0, #0x0b]
	mov	r2, r2, lsr #8		/* r2 = ...A */
	orr	r2, r2, ip, lsl #8	/* r2 = 789A */
	mov	ip, ip, lsr #8		/* ip = .678 */
	orr	ip, ip, r3, lsl #24	/* ip = 5678 */
	mov	r3, r3, lsr #8		/* r3 = .234 */
	orr	r3, r3, r1, lsl #24	/* r3 = 1234 */
	mov	r1, r1, lsr #8		/* r1 = ...0 */
	strb	r1, [r0]
	str	r3, [r0, #0x01]
	str	ip, [r0, #0x05]
	strh	r2, [r0, #0x09]
#else
	ldrh	r2, [r1]		/* r2 = ..10 */
	ldr	r3, [r1, #0x02]		/* r3 = 5432 */
	ldr	ip, [r1, #0x06]		/* ip = 9876 */
	ldrh	r1, [r1, #0x0a]		/* r1 = ..BA */
	strb	r2, [r0]
	mov	r2, r2, lsr #8		/* r2 = ...1 */
	orr	r2, r2, r3, lsl #8	/* r2 = 4321 */
	mov	r3, r3, lsr #24		/* r3 = ...5 */
	orr	r3, r3, ip, lsl #8	/* r3 = 8765 */
	mov	ip, ip, lsr #24		/* ip = ...9 */
	orr	ip, ip, r1, lsl #8	/* ip = .BA9 */
	mov	r1, r1, lsr #8		/* r1 = ...B */
	str	r2, [r0, #0x01]
	str	r3, [r0, #0x05]
	strh	ip, [r0, #0x09]
	strb	r1, [r0, #0x0b]
#endif
	RET
	LMEMCPY_C_PAD

/*
 * 1111: dst is 8-bit aligned (byte 3), src is 8-bit aligned (byte 3)
 */
	ldrb	r2, [r1]
	ldr	r3, [r1, #0x01]
	ldr	ip, [r1, #0x05]
	strb	r2, [r0]
	ldrh	r2, [r1, #0x09]
	ldrb	r1, [r1, #0x0b]
	str	r3, [r0, #0x01]
	str	ip, [r0, #0x05]
	strh	r2, [r0, #0x09]
	strb	r1, [r0, #0x0b]
	RET
END(memcpy)
#endif /* _ARM_ARCH_5E */

#ifdef GPROF

ENTRY(user)
	nop
END(user)
ENTRY(btrap)
	nop
END(btrap)
ENTRY(etrap)
	nop
END(etrap)
ENTRY(bintr)
	nop
END(bintr)
ENTRY(eintr)
	nop
END(eintr)
#endif
