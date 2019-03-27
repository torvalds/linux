/*	$NetBSD: bcopyinout.S,v 1.11 2003/10/13 21:22:40 scw Exp $	*/

/*-
 * Copyright (c) 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Allen Briggs for Wasabi Systems, Inc.
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


#include "assym.inc"

#include <machine/asm.h>
#include <sys/errno.h>

.L_arm_memcpy:
	.word	_C_LABEL(_arm_memcpy)
.L_min_memcpy_size:
	.word	_C_LABEL(_min_memcpy_size)

__FBSDID("$FreeBSD$");
#ifdef _ARM_ARCH_5E
#include <arm/arm/bcopyinout_xscale.S>
#else

	.text
	.align	2

#if __ARM_ARCH >= 6
#define GET_PCB(tmp) \
	mrc p15, 0, tmp, c13, c0, 4; \
	add	tmp, tmp, #(TD_PCB)
#else
.Lcurpcb:
	.word	_C_LABEL(__pcpu) + PC_CURPCB

#define GET_PCB(tmp) \
	ldr	tmp, .Lcurpcb
#endif


#define SAVE_REGS	stmfd	sp!, {r4-r11}; _SAVE({r4-r11})
#define RESTORE_REGS	ldmfd	sp!, {r4-r11}

#if defined(_ARM_ARCH_5E)
#define HELLOCPP #
#define PREFETCH(rx,o)	pld	[ rx , HELLOCPP (o) ]
#else
#define PREFETCH(rx,o)
#endif

/*
 * r0 = user space address
 * r1 = kernel space address
 * r2 = length
 *
 * Copies bytes from user space to kernel space
 *
 * We save/restore r4-r11:
 * r4-r11 are scratch
 */
ENTRY(copyin)
	/* Quick exit if length is zero */
	teq	r2, #0
	moveq	r0, #0
	RETeq

	adds	r3, r0, r2
	movcs	r0, #EFAULT
	RETc(cs)

	ldr	r12, =(VM_MAXUSER_ADDRESS + 1)
	cmp	r3, r12
	movcs	r0, #EFAULT
	RETc(cs)

	ldr	r3, .L_arm_memcpy
	ldr	r3, [r3]
	cmp	r3, #0
	beq	.Lnormal
	ldr	r3, .L_min_memcpy_size
	ldr	r3, [r3]
	cmp	r2, r3
	blt	.Lnormal
	stmfd	sp!, {r0-r2, r4, lr}
	mov     r3, r0
	mov     r0, r1
	mov     r1, r3
	mov     r3, #2 /* SRC_IS_USER */
	ldr	r4, .L_arm_memcpy
	mov	lr, pc
	ldr	pc, [r4]
	cmp     r0, #0
	ldmfd   sp!, {r0-r2, r4, lr}
	moveq	r0, #0
	RETeq

.Lnormal:
	SAVE_REGS
	GET_PCB(r4)
	ldr	r4, [r4]


	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

	PREFETCH(r0, 0)
	PREFETCH(r1, 0)

	/*
	 * If not too many bytes, take the slow path.
	 */
	cmp	r2, #0x08
	blt	.Licleanup

	/*
	 * Align destination to word boundary.
	 */
	and	r6, r1, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Lialend
	.word	.Lialend
	.word	.Lial3
	.word	.Lial2
	.word	.Lial1
.Lial3:	ldrbt	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lial2:	ldrbt	r7, [r0], #1
	sub	r2, r2, #1
	strb	r7, [r1], #1
.Lial1:	ldrbt	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lialend:

	/*
	 * If few bytes left, finish slow.
	 */
	cmp	r2, #0x08
	blt	.Licleanup

	/*
	 * If source is not aligned, finish slow.
	 */
	ands	r3, r0, #0x03
	bne	.Licleanup

	cmp	r2, #0x60	/* Must be > 0x5f for unrolled cacheline */
	blt	.Licleanup8

	/*
	 * Align destination to cacheline boundary.
	 * If source and destination are nicely aligned, this can be a big
	 * win.  If not, it's still cheaper to copy in groups of 32 even if
	 * we don't get the nice cacheline alignment.
	 */
	and	r6, r1, #0x1f
	ldr	pc, [pc, r6]
	b	.Licaligned
	.word	.Licaligned
	.word	.Lical28
	.word	.Lical24
	.word	.Lical20
	.word	.Lical16
	.word	.Lical12
	.word	.Lical8
	.word	.Lical4
.Lical28:ldrt	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lical24:ldrt	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lical20:ldrt	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lical16:ldrt	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lical12:ldrt	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lical8:ldrt	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lical4:ldrt	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4

	/*
	 * We start with > 0x40 bytes to copy (>= 0x60 got us into this
	 * part of the code, and we may have knocked that down by as much
	 * as 0x1c getting aligned).
	 *
	 * This loop basically works out to:
	 * do {
	 * 	prefetch-next-cacheline(s)
	 *	bytes -= 0x20;
	 *	copy cacheline
	 * } while (bytes >= 0x40);
	 * bytes -= 0x20;
	 * copy cacheline
	 */
.Licaligned:
	PREFETCH(r0, 32)
	PREFETCH(r1, 32)

	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldrt	r10, [r0], #4
	ldrt	r11, [r0], #4
	ldrt	r6, [r0], #4
	ldrt	r7, [r0], #4
	ldrt	r8, [r0], #4
	ldrt	r9, [r0], #4
	stmia	r1!, {r10-r11}
	ldrt	r10, [r0], #4
	ldrt	r11, [r0], #4
	stmia	r1!, {r6-r11}

	cmp	r2, #0x40
	bge	.Licaligned

	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldrt	r10, [r0], #4
	ldrt	r11, [r0], #4
	ldrt	r6, [r0], #4
	ldrt	r7, [r0], #4
	ldrt	r8, [r0], #4
	ldrt	r9, [r0], #4
	stmia	r1!, {r10-r11}
	ldrt	r10, [r0], #4
	ldrt	r11, [r0], #4
	stmia	r1!, {r6-r11}

	cmp	r2, #0x08
	blt	.Liprecleanup

.Licleanup8:
	ldrt	r8, [r0], #4
	ldrt	r9, [r0], #4
	sub	r2, r2, #8
	stmia	r1!, {r8, r9}
	cmp	r2, #8
	bge	.Licleanup8

.Liprecleanup:
	/*
	 * If we're done, bail.
	 */
	cmp	r2, #0
	beq	.Lout

.Licleanup:
	and	r6, r2, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Licend
	.word	.Lic4
	.word	.Lic1
	.word	.Lic2
	.word	.Lic3
.Lic4:	ldrbt	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lic3:	ldrbt	r7, [r0], #1
	sub	r2, r2, #1
	strb	r7, [r1], #1
.Lic2:	ldrbt	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lic1:	ldrbt	r7, [r0], #1
	subs	r2, r2, #1
	strb	r7, [r1], #1
.Licend:
	bne	.Licleanup

.Liout:
	mov	r0, #0

	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	RET

.Lcopyfault:
	ldr	r0, =EFAULT
	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	RET
END(copyin)

/*
 * r0 = kernel space address
 * r1 = user space address
 * r2 = length
 *
 * Copies bytes from kernel space to user space
 *
 * We save/restore r4-r11:
 * r4-r11 are scratch
 */

ENTRY(copyout)
	/* Quick exit if length is zero */
	teq	r2, #0
	moveq	r0, #0
	RETeq

	adds	r3, r1, r2
	movcs	r0, #EFAULT
	RETc(cs)

	ldr	r12, =(VM_MAXUSER_ADDRESS + 1)
	cmp	r3, r12
	movcs	r0, #EFAULT
	RETc(cs)

	ldr	r3, .L_arm_memcpy
	ldr	r3, [r3]
	cmp	r3, #0
	beq	.Lnormale
	ldr	r3, .L_min_memcpy_size
	ldr	r3, [r3]
	cmp	r2, r3
	blt	.Lnormale
	stmfd	sp!, {r0-r2, r4, lr}
	_SAVE({r0-r2, r4, lr})
	mov     r3, r0
	mov     r0, r1
	mov     r1, r3
	mov     r3, #1 /* DST_IS_USER */
	ldr	r4, .L_arm_memcpy
	mov	lr, pc
	ldr	pc, [r4]
	cmp     r0, #0
	ldmfd   sp!, {r0-r2, r4, lr}
	moveq	r0, #0
	RETeq

.Lnormale:
	SAVE_REGS
	GET_PCB(r4)
	ldr	r4, [r4]

	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

	PREFETCH(r0, 0)
	PREFETCH(r1, 0)

	/*
	 * If not too many bytes, take the slow path.
	 */
	cmp	r2, #0x08
	blt	.Lcleanup

	/*
	 * Align destination to word boundary.
	 */
	and	r6, r1, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Lalend
	.word	.Lalend
	.word	.Lal3
	.word	.Lal2
	.word	.Lal1
.Lal3:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strbt	r6, [r1], #1
.Lal2:	ldrb	r7, [r0], #1
	sub	r2, r2, #1
	strbt	r7, [r1], #1
.Lal1:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strbt	r6, [r1], #1
.Lalend:

	/*
	 * If few bytes left, finish slow.
	 */
	cmp	r2, #0x08
	blt	.Lcleanup

	/*
	 * If source is not aligned, finish slow.
	 */
	ands	r3, r0, #0x03
	bne	.Lcleanup

	cmp	r2, #0x60	/* Must be > 0x5f for unrolled cacheline */
	blt	.Lcleanup8

	/*
	 * Align source & destination to cacheline boundary.
	 */
	and	r6, r1, #0x1f
	ldr	pc, [pc, r6]
	b	.Lcaligned
	.word	.Lcaligned
	.word	.Lcal28
	.word	.Lcal24
	.word	.Lcal20
	.word	.Lcal16
	.word	.Lcal12
	.word	.Lcal8
	.word	.Lcal4
.Lcal28:ldr	r6, [r0], #4
	sub	r2, r2, #4
	strt	r6, [r1], #4
.Lcal24:ldr	r7, [r0], #4
	sub	r2, r2, #4
	strt	r7, [r1], #4
.Lcal20:ldr	r6, [r0], #4
	sub	r2, r2, #4
	strt	r6, [r1], #4
.Lcal16:ldr	r7, [r0], #4
	sub	r2, r2, #4
	strt	r7, [r1], #4
.Lcal12:ldr	r6, [r0], #4
	sub	r2, r2, #4
	strt	r6, [r1], #4
.Lcal8:	ldr	r7, [r0], #4
	sub	r2, r2, #4
	strt	r7, [r1], #4
.Lcal4:	ldr	r6, [r0], #4
	sub	r2, r2, #4
	strt	r6, [r1], #4

	/*
	 * We start with > 0x40 bytes to copy (>= 0x60 got us into this
	 * part of the code, and we may have knocked that down by as much
	 * as 0x1c getting aligned).
	 *
	 * This loop basically works out to:
	 * do {
	 * 	prefetch-next-cacheline(s)
	 *	bytes -= 0x20;
	 *	copy cacheline
	 * } while (bytes >= 0x40);
	 * bytes -= 0x20;
	 * copy cacheline
	 */
.Lcaligned:
	PREFETCH(r0, 32)
	PREFETCH(r1, 32)

	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldmia	r0!, {r6-r11}
	strt	r6, [r1], #4
	strt	r7, [r1], #4
	ldmia	r0!, {r6-r7}
	strt	r8, [r1], #4
	strt	r9, [r1], #4
	strt	r10, [r1], #4
	strt	r11, [r1], #4
	strt	r6, [r1], #4
	strt	r7, [r1], #4

	cmp	r2, #0x40
	bge	.Lcaligned

	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldmia	r0!, {r6-r11}
	strt	r6, [r1], #4
	strt	r7, [r1], #4
	ldmia	r0!, {r6-r7}
	strt	r8, [r1], #4
	strt	r9, [r1], #4
	strt	r10, [r1], #4
	strt	r11, [r1], #4
	strt	r6, [r1], #4
	strt	r7, [r1], #4

	cmp	r2, #0x08
	blt	.Lprecleanup

.Lcleanup8:
	ldmia	r0!, {r8-r9}
	sub	r2, r2, #8
	strt	r8, [r1], #4
	strt	r9, [r1], #4
	cmp	r2, #8
	bge	.Lcleanup8

.Lprecleanup:
	/*
	 * If we're done, bail.
	 */
	cmp	r2, #0
	beq	.Lout

.Lcleanup:
	and	r6, r2, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Lcend
	.word	.Lc4
	.word	.Lc1
	.word	.Lc2
	.word	.Lc3
.Lc4:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strbt	r6, [r1], #1
.Lc3:	ldrb	r7, [r0], #1
	sub	r2, r2, #1
	strbt	r7, [r1], #1
.Lc2:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strbt	r6, [r1], #1
.Lc1:	ldrb	r7, [r0], #1
	subs	r2, r2, #1
	strbt	r7, [r1], #1
.Lcend:
	bne	.Lcleanup

.Lout:
	mov	r0, #0

	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	RET
END(copyout)
#endif

/*
 * int badaddr_read_1(const uint8_t *src, uint8_t *dest)
 *
 * Copies a single 8-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_1)
	GET_PCB(r2)
	ldr	r2, [r2]

	ldr	ip, [r2, #PCB_ONFAULT]
	adr	r3, 1f
	str	r3, [r2, #PCB_ONFAULT]
	nop
	nop
	nop
	ldrb	r3, [r0]
	nop
	nop
	nop
	strb	r3, [r1]
	mov	r0, #0		/* No fault */
1:	str	ip, [r2, #PCB_ONFAULT]
	RET
END(badaddr_read_1)

/*
 * int badaddr_read_2(const uint16_t *src, uint16_t *dest)
 *
 * Copies a single 16-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_2)
	GET_PCB(r2)
	ldr	r2, [r2]

	ldr	ip, [r2, #PCB_ONFAULT]
	adr	r3, 1f
	str	r3, [r2, #PCB_ONFAULT]
	nop
	nop
	nop
	ldrh	r3, [r0]
	nop
	nop
	nop
	strh	r3, [r1]
	mov	r0, #0		/* No fault */
1:	str	ip, [r2, #PCB_ONFAULT]
	RET
END(badaddr_read_2)

/*
 * int badaddr_read_4(const uint32_t *src, uint32_t *dest)
 *
 * Copies a single 32-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_4)
	GET_PCB(r2)
	ldr	r2, [r2]

	ldr	ip, [r2, #PCB_ONFAULT]
	adr	r3, 1f
	str	r3, [r2, #PCB_ONFAULT]
	nop
	nop
	nop
	ldr	r3, [r0]
	nop
	nop
	nop
	str	r3, [r1]
	mov	r0, #0		/* No fault */
1:	str	ip, [r2, #PCB_ONFAULT]
	RET
END(badaddr_read_4)

