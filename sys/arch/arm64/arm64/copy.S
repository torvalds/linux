/* $OpenBSD: copy.S,v 1.10 2023/08/12 06:28:13 miod Exp $ */
/*
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "assym.h"
#include <sys/syscall.h>
#include <machine/asm.h>
#include <sys/errno.h>

	.text
	.align	2

/*
 * x0 = user space address
 * x1 = kernel space address
 * x2 = length
 *
 * Copies bytes from user space to kernel space
 */
ENTRY(copyin)
	RETGUARD_SETUP(copy, x15)
	cbnz	x2, 1f
	mov	x0, 0
	RETGUARD_CHECK(copy, x15)
	ret

1:
	/* Check whether source+len overflows. */
	adds	x3, x0, x2
	b.cs	.Laddrfault
	/* Check that source+len is in userspace. */
	tst	x3, #(1ULL << 63)
	b.ne	.Laddrfault

	mrs	x3, tpidr_el1			// load cpuinfo
	ldr	x3, [x3, #(CI_CURPCB)]
	ldr	x4, [x3, #(PCB_ONFAULT)]
	adr	x5, .Lcopyfault
	str	x5, [x3, #(PCB_ONFAULT)]	// set handler

	cmp	x2, #16
	b.lo	.Lcopyin1
2:	ldtr	x6, [x0]
	ldtr	x7, [x0, #8]
	stp	x6, x7, [x1], #16
	add	x0, x0, #16
	sub	x2, x2, #16
	cmp	x2, #16
	b.hs	2b

.Lcopyin1:
	cbz	x2, .Lcopyin0
3:	ldtrb	w6, [x0]
	strb	w6, [x1], #1
	add	x0, x0, #1
	sub	x2, x2, #1
	cbnz	x2, 3b

.Lcopyin0:
	str	x4, [x3, #(PCB_ONFAULT)]	// clear handler
	mov	x0, xzr
	RETGUARD_CHECK(copy, x15)
	ret

.Lcopyfault:
	str	x4, [x3, #(PCB_ONFAULT)]
.Laddrfault:
	mov	x0, #EFAULT
	RETGUARD_CHECK(copy, x15)
	ret

/*
 * x0 = user space address
 * x1 = kernel space address
 *
 * Atomically copies a 32-bit word from user space to kernel space
 */
ENTRY(copyin32)
	RETGUARD_SETUP(copy, x15)

	/* Check source alignment. */
	tst	x0, #0x3
	b.ne	.Laddrfault
	/* Check that source is in userspace. */
	tst	x0, #(1ULL << 63)
	b.ne	.Laddrfault

	mrs	x3, tpidr_el1			// load cpuinfo
	ldr	x3, [x3, #(CI_CURPCB)]
	ldr	x4, [x3, #(PCB_ONFAULT)]
	adr	x5, .Lcopyfault
	str	x5, [x3, #(PCB_ONFAULT)]	// set handler

	ldtr	w6, [x0]
	str	w6, [x1]

	str	x4, [x3, #(PCB_ONFAULT)]	// clear handler
	mov	x0, xzr
	RETGUARD_CHECK(copy, x15)
	ret

/*
 * x0 = kernel space address
 * x1 = user space address
 * x2 = length
 *
 * Copies bytes from kernel space to user space
 */

ENTRY(copyout)
	RETGUARD_SETUP(copy, x15)
	cbnz	x2, 1f
	mov	x0, 0
	RETGUARD_CHECK(copy, x15)
	ret

1:
	/* Check whether dest+len overflows. */
	adds	x3, x1, x2
	b.cs	.Laddrfault
	/* Check that dest+len is in userspace. */
	tst	x3, #(1ULL << 63)
	b.ne	.Laddrfault

	mrs	x3, tpidr_el1			// load cpuinfo
	ldr	x3, [x3, #(CI_CURPCB)]
	ldr	x4, [x3, #(PCB_ONFAULT)]
	adr	x5, .Lcopyfault
	str	x5, [x3, #(PCB_ONFAULT)]	// set handler

	cmp	x2, #16
	b.lo	.Lcopyout1
2:	ldp	x6, x7, [x0], #16
	sttr	x6, [x1]
	sttr	x7, [x1, #8]
	add	x1, x1, #16
	sub	x2, x2, #16
	cmp	x2, #16
	b.hs	2b

.Lcopyout1:
	cbz	x2, .Lcopyout0
3:	ldrb	w6, [x0], #1
	sttrb	w6, [x1]
	add	x1, x1, #1
	sub	x2, x2, #1
	cbnz	x2, 3b

.Lcopyout0:
	str	x4, [x3, #(PCB_ONFAULT)]	// restore handler
	mov	x0, xzr
	RETGUARD_CHECK(copy, x15)
	ret

/*
 * x0 = kernel space source address
 * x1 = kernel space destination address
 * x2 = length
 *
 * Copies bytes from kernel space to kernel space, aborting on page fault
 */

ENTRY(kcopy)
	RETGUARD_SETUP(copy, x15)
	cbnz	x2, 1f
	mov	x0, 0
	RETGUARD_CHECK(copy, x15)
	ret
1:
	mrs	x3, tpidr_el1			// load cpuinfo
	ldr	x3, [x3, #(CI_CURPCB)]
	ldr	x4, [x3, #(PCB_ONFAULT)]
	adr	x5, .Lcopyfault
	str	x5, [x3, #(PCB_ONFAULT)]	// set handler

	cmp	x2, #16
	b.lo	.Lkcopy8
2:	ldp	x6, x7, [x0], #16
	stp	x6, x7, [x1], #16
	sub	x2, x2, #16
	cmp	x2, #16
	b.hs	2b

.Lkcopy8:
	tbz	x2, #3, .Lkcopy4
	ldr	x6, [x0], #8
	str	x6, [x1], #8
	sub	x2, x2, #8

.Lkcopy4:
	tbz	x2, #2, .Lkcopy1
	ldr	w6, [x0], #4
	str	w6, [x1], #4
	sub	x2, x2, #4

.Lkcopy1:
	cbz	x2, .Lkcopy0
3:	ldrb	w6, [x0], #1
	strb	w6, [x1], #1
	sub	x2, x2, #1
	cbnz	x2, 3b

.Lkcopy0:
	str	x4, [x3, #(PCB_ONFAULT)]	// restore handler
	mov	x0, xzr
	RETGUARD_CHECK(copy, x15)
	ret
