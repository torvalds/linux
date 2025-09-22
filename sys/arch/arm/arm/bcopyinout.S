/*	$OpenBSD: bcopyinout.S,v 1.11 2023/01/31 15:18:54 deraadt Exp $	*/
/*	$NetBSD: bcopyinout.S,v 1.13 2003/10/31 16:54:05 scw Exp $	*/

/*
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

#include "assym.h"

#include <sys/errno.h>

#include <machine/asm.h>
#include <arm/sysreg.h>

	.text
	.align	2

#ifdef MULTIPROCESSOR
.Lcpu_info:
	.word	cpu_info
#else
.Lcpu_info_primary:
	.word cpu_info_primary
#endif

#define SAVE_REGS	stmfd	sp!, {r4-r11}
#define RESTORE_REGS	ldmfd	sp!, {r4-r11}
		
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
ENTRY(_copyin)
	/* Quick exit if length is zero */	
	teq	r2, #0
	moveq	r0, #0
	moveq	pc, lr

	SAVE_REGS

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

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

	mov	pc, lr

.Lcopyfault:
	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	mov	pc, lr

/*
 * r0 = user space address
 * r1 = kernel space address
 *
 * Atomically copies a 32-bit word from user space to kernel space
 *
 * We save/restore r4-r11:
 * r4-r11 are scratch
 */
ENTRY(copyin32)
	/* Quick exit if unaligned */	
	tst	r0, #0x3
	movne	r0, #EFAULT
	movne	pc, lr

	SAVE_REGS

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

	ldr	r6, [r0]
	str	r6, [r1]
	mov	r0, #0

	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	mov	pc, lr

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
	moveq	pc, lr

	SAVE_REGS

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

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

	mov	pc, lr

/*
 * r0 = kernel space source address
 * r1 = kernel space destination address
 * r2 = length
 *
 * Copies bytes from kernel space to kernel space, aborting on page fault
 *
 * Copy of copyout, but without the ldrt/strt instructions.
 */

ENTRY(kcopy)
	/* Quick exit if length is zero */	
	teq	r2, #0
	moveq	r0, #0
	moveq	pc, lr

	SAVE_REGS

	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r4)
	ldr	r4, [r4, #CI_CURPCB]

	ldr	r5, [r4, #PCB_ONFAULT]
	adr	r3, .Lcopyfault
	str	r3, [r4, #PCB_ONFAULT]

	/*
	 * If not too many bytes, take the slow path.
	 */
	cmp	r2, #0x08
	blt	.Lkcleanup

	/*
	 * Align destination to word boundary.
	 */
	and	r6, r1, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Lkalend
	.word	.Lkalend
	.word	.Lkal3
	.word	.Lkal2
	.word	.Lkal1
.Lkal3:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lkal2:	ldrb	r7, [r0], #1
	sub	r2, r2, #1
	strb	r7, [r1], #1
.Lkal1:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lkalend:

	/*
	 * If few bytes left, finish slow.
	 */
	cmp	r2, #0x08
	blt	.Lkcleanup

	/*
	 * If source is not aligned, finish slow.
	 */
	ands	r3, r0, #0x03
	bne	.Lkcleanup

	cmp	r2, #0x60	/* Must be > 0x5f for unrolled cacheline */
	blt	.Lkcleanup8

	/*
	 * Align source & destination to cacheline boundary.
	 */
	and	r6, r1, #0x1f
	ldr	pc, [pc, r6]
	b	.Lkcaligned
	.word	.Lkcaligned
	.word	.Lkcal28
	.word	.Lkcal24
	.word	.Lkcal20
	.word	.Lkcal16
	.word	.Lkcal12
	.word	.Lkcal8
	.word	.Lkcal4
.Lkcal28:ldr	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lkcal24:ldr	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lkcal20:ldr	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lkcal16:ldr	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lkcal12:ldr	r6, [r0], #4
	sub	r2, r2, #4
	str	r6, [r1], #4
.Lkcal8:ldr	r7, [r0], #4
	sub	r2, r2, #4
	str	r7, [r1], #4
.Lkcal4:ldr	r6, [r0], #4
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
.Lkcaligned:
	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldmia	r0!, {r6-r11}
	stmia	r1!, {r6, r7}
	ldmia	r0!, {r6, r7}
	stmia	r1!, {r8-r11}
	stmia	r1!, {r6, r7}

	cmp	r2, #0x40
	bge	.Lkcaligned

	sub	r2, r2, #0x20

	/* Copy a cacheline */
	ldmia	r0!, {r6-r11}
	stmia	r1!, {r6-r7}
	ldmia	r0!, {r6-r7}
	stmia	r1!, {r8-r11}
	stmia	r1!, {r6-r7}

	cmp	r2, #0x08
	blt	.Lkprecleanup

.Lkcleanup8:
	ldmia	r0!, {r8-r9}
	sub	r2, r2, #8
	stmia	r1!, {r8-r9}
	cmp	r2, #8
	bge	.Lkcleanup8

.Lkprecleanup:
	/*
	 * If we're done, bail.
	 */
	cmp	r2, #0
	beq	.Lkout

.Lkcleanup:
	and	r6, r2, #0x3
	ldr	pc, [pc, r6, lsl #2]
	b	.Lkcend
	.word	.Lkc4
	.word	.Lkc1
	.word	.Lkc2
	.word	.Lkc3
.Lkc4:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lkc3:	ldrb	r7, [r0], #1
	sub	r2, r2, #1
	strb	r7, [r1], #1
.Lkc2:	ldrb	r6, [r0], #1
	sub	r2, r2, #1
	strb	r6, [r1], #1
.Lkc1:	ldrb	r7, [r0], #1
	subs	r2, r2, #1
	strb	r7, [r1], #1
.Lkcend:
	bne	.Lkcleanup

.Lkout:
	mov	r0, #0

	str	r5, [r4, #PCB_ONFAULT]
	RESTORE_REGS

	mov	pc, lr

/*
 * int badaddr_read_1(const uint8_t *src, uint8_t *dest)
 *
 * Copies a single 8-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_1)
	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r2)
	ldr	r2, [r2, #CI_CURPCB]
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
	mov	pc, lr

/*
 * int badaddr_read_2(const uint16_t *src, uint16_t *dest)
 *
 * Copies a single 16-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_2)
	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r2)
	ldr	r2, [r2, #CI_CURPCB]
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
	mov	pc, lr

/*
 * int badaddr_read_4(const uint32_t *src, uint32_t *dest)
 *
 * Copies a single 32-bit value from src to dest, returning 0 on success,
 * else EFAULT if a page fault occurred.
 */
ENTRY(badaddr_read_4)
	/* Get curcpu from TPIDRPRW. */
	mrc	CP15_TPIDRPRW(r2)
	ldr	r2, [r2, #CI_CURPCB]
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
	mov	pc, lr
