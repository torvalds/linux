/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
 *
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <sys/errno.h>

#include <machine/vmparam.h>

#include "assym.inc"

/*
 * Fault handler for the copy{in,out} functions below.
 */
ENTRY(copyio_fault)
	SET_FAULT_HANDLER(xzr, x1) /* Clear the handler */
	EXIT_USER_ACCESS_CHECK(w0, x1)
copyio_fault_nopcb:
	mov	x0, #EFAULT
	ret
END(copyio_fault)

/*
 * Copies from a kernel to user address
 *
 * int copyout(const void *kaddr, void *udaddr, size_t len)
 */
ENTRY(copyout)
	cbz	x2, 1f
	adds	x3, x1, x2
	b.cs 	copyio_fault_nopcb
	ldr	x4, =VM_MAXUSER_ADDRESS
	cmp	x3, x4
	b.hi	copyio_fault_nopcb

	b	copycommon

1:	mov	x0, xzr		/* return 0 */
	ret

END(copyout)

/*
 * Copies from a user to kernel address
 *
 * int copyin(const void *uaddr, void *kdaddr, size_t len)
 */
ENTRY(copyin)
	cbz	x2, 1f
	adds	x3, x0, x2
	b.cs    copyio_fault_nopcb
	ldr	x4, =VM_MAXUSER_ADDRESS
	cmp	x3, x4
	b.hi	copyio_fault_nopcb

	b	copycommon

1:	mov	x0, xzr		/* return 0 */
	ret

END(copyin)

/*
 * Copies a string from a user to kernel address
 *
 * int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *done)
 */
ENTRY(copyinstr)
	mov	x5, xzr		/* count = 0 */
	mov	w4, #1		/* If zero return faulure */
	cbz	x2, 3f		/* If len == 0 then skip loop */

	adr	x6, copyio_fault /* Get the handler address */
	SET_FAULT_HANDLER(x6, x7) /* Set the handler */
	ENTER_USER_ACCESS(w6, x7)

	ldr	x7, =VM_MAXUSER_ADDRESS
1:	cmp	x0, x7
	b.cs	copyio_fault
	ldtrb	w4, [x0]	/* Load from uaddr */
	add	x0, x0, #1	/* Next char */
	strb	w4, [x1], #1	/* Store in kaddr */
	add	x5, x5, #1	/* count++ */
	cbz	w4, 2f		/* Break when NUL-terminated */
	sub	x2, x2, #1	/* len-- */
	cbnz	x2, 1b

2:	EXIT_USER_ACCESS(w6)
	SET_FAULT_HANDLER(xzr, x7) /* Clear the handler */


3:	cbz	x3, 4f		/* Check if done != NULL */
	str	x5, [x3]	/* done = count */

4:	mov	w1, #ENAMETOOLONG /* Load ENAMETOOLONG to return if failed */
	cmp	w4, #0		/* Check if we saved the NUL-terminator */
	csel	w0, wzr, w1, eq	/* If so return success, else failure */
	ret
END(copyinstr)

/*
 * Local helper
 *
 * x0 - src pointer
 * x1 - dst pointer
 * x2 - size
 * lr - the return address, so jump here instead of calling
 *
 * This function is optimized to minimize concurrent memory accesses. In
 * present form it is suited for cores with a single memory prefetching
 * unit.
 * ARM64TODO: 
 *   Consider using separate functions for each ARM64 core. Adding memory
 *   access interleaving might increase a total throughput on A57 or A72.
 */
	.text
	.align	4
	.local	copycommon
	.type	copycommon,@function

copycommon:
	adr	x6, copyio_fault /* Get the handler address */
	SET_FAULT_HANDLER(x6, x7) /* Set the handler */
	ENTER_USER_ACCESS(w6, x7)

	/* Check alignment */
	orr	x3, x0, x1
	ands	x3, x3, 0x07
	b.eq	aligned

	/* Unaligned is byte by byte copy */
byte_by_byte:
	ldrb	w3, [x0], #0x01
	strb	w3, [x1], #0x01
	subs	x2, x2, #0x01
	b.ne	byte_by_byte
	b	ending

aligned:
	cmp	x2, #0x10
	b.lt	lead_out
	cmp	x2, #0x40
	b.lt	by_dwords_start

	/* Block copy */
	lsr	x15, x2, #0x06
by_blocks:
	ldp	x3, x4, [x0], #0x10
	ldp	x5, x6, [x0], #0x10
	ldp	x7, x8, [x0], #0x10
	ldp	x9, x10, [x0], #0x10
	stp	x3, x4, [x1], #0x10
	stp	x5, x6, [x1], #0x10
	stp	x7, x8, [x1], #0x10
	stp	x9, x10, [x1], #0x10

	subs	x15, x15, #0x01
	b.ne	by_blocks

	and	x2, x2, #0x3f

by_dwords_start:
	lsr	x15, x2, #0x04
	cbz	x15, lead_out
by_dwords:
	ldp	x3, x4, [x0], #0x10
	stp	x3, x4, [x1], #0x10
	subs	x15, x15, #0x01
	b.ne  	by_dwords

	/* Less than 16 bytes to copy */
lead_out:
	tbz	x2, #0x03, last_word
	ldr	x3, [x0], #0x08
	str	x3, [x1], #0x08

last_word:
	tbz	x2, #0x02, last_hword
	ldr	w3, [x0], #0x04
	str	w3, [x1], #0x04

last_hword:
	tbz	x2, #0x01, last_byte
	ldrh	w3, [x0], #0x02
	strh	w3, [x1], #0x02

last_byte:
	tbz	x2, #0x00, ending
	ldrb	w3, [x0]
	strb	w3, [x1]

ending:
	EXIT_USER_ACCESS_CHECK(w6, x7)
	SET_FAULT_HANDLER(xzr, x7) /* Clear the handler */

	mov	x0, xzr		/* return 0 */
	ret
	.size	copycommon, . - copycommon
