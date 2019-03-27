/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2019 Mitchell Horne
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include <machine/riscvreg.h>
#include <sys/errno.h>

#include "assym.inc"

/*
 * Fault handler for the copy{in,out} functions below.
 */
ENTRY(copyio_fault)
	SET_FAULT_HANDLER(x0, a1) /* Clear the handler */
	EXIT_USER_ACCESS(a1)
copyio_fault_nopcb:
	li	a0, EFAULT
	ret
END(copyio_fault)

/*
 * copycommon - common copy routine
 *
 * a0 - Source address
 * a1 - Destination address
 * a2 - Size of copy
 */
	.macro copycommon
	la	a6, copyio_fault	/* Get the handler address */
	SET_FAULT_HANDLER(a6, a7)	/* Set the handler */
	ENTER_USER_ACCESS(a7)

	li	t2, XLEN_BYTES
	blt	a2, t2, 3f		/* Byte-copy if len < XLEN_BYTES */

	/*
	 * Compare lower bits of src and dest.
	 * If they are aligned with each other, we can do word copy.
	 */
	andi	t0, a0, (XLEN_BYTES-1)	/* Low bits of src */
	andi	t1, a1, (XLEN_BYTES-1)	/* Low bits of dest */
	bne	t0, t1, 3f		/* Misaligned. Go to byte copy */
	beqz	t0, 2f			/* Already word-aligned, skip ahead */

	/* Byte copy until the first word-aligned address */
1:	lb	a4, 0(a0)		/* Load byte from src */
	addi	a0, a0, 1
	sb	a4, 0(a1)		/* Store byte in dest */
	addi	a1, a1, 1
	addi	a2, a2, -1		/* len-- */
	andi	t0, a0, (XLEN_BYTES-1)
	bnez	t0, 1b

	/* Copy words */
2:	ld	a4, 0(a0)		/* Load word from src */
	addi	a0, a0, XLEN_BYTES
	sd	a4, 0(a1)		/* Store word in dest */
	addi	a1, a1, XLEN_BYTES
	addi	a2, a2, -XLEN_BYTES	/* len -= XLEN_BYTES */
	bgeu	a2, t2, 2b		/* Again if len >= XLEN_BYTES */

	/* Check if we're finished */
	beqz	a2, 4f

	/* Copy any remaining bytes */
3:	lb	a4, 0(a0)		/* Load byte from src */
	addi	a0, a0, 1
	sb	a4, 0(a1)		/* Store byte in dest */
	addi	a1, a1, 1
	addi	a2, a2, -1		/* len-- */
	bnez	a2, 3b

4:	EXIT_USER_ACCESS(a7)
	SET_FAULT_HANDLER(x0, a7)	/* Clear the handler */
	.endm

/*
 * Copies from a kernel to user address
 *
 * int copyout(const void *kaddr, void *udaddr, size_t len)
 */
ENTRY(copyout)
	beqz	a2, copyout_end	/* If len == 0 then skip loop */
	add	a3, a1, a2
	li	a4, VM_MAXUSER_ADDRESS
	bgt	a3, a4, copyio_fault_nopcb

	copycommon

copyout_end:
	li	a0, 0		/* return 0 */
	ret
END(copyout)

/*
 * Copies from a user to kernel address
 *
 * int copyin(const void *uaddr, void *kaddr, size_t len)
 */
ENTRY(copyin)
	beqz	a2, copyin_end	/* If len == 0 then skip loop */
	add	a3, a0, a2
	li	a4, VM_MAXUSER_ADDRESS
	bgt	a3, a4, copyio_fault_nopcb

	copycommon

copyin_end:
	li	a0, 0		/* return 0 */
	ret
END(copyin)

/*
 * Copies a string from a user to kernel address
 *
 * int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *done)
 */
ENTRY(copyinstr)
	mv	a5, x0		/* count = 0 */
	beqz	a2, 3f		/* If len == 0 then skip loop */

	la	a6, copyio_fault /* Get the handler address */
	SET_FAULT_HANDLER(a6, a7) /* Set the handler */
	ENTER_USER_ACCESS(a7)

	li	a7, VM_MAXUSER_ADDRESS
1:	bgt	a0, a7, copyio_fault
	lb	a4, 0(a0)	/* Load from uaddr */
	addi	a0, a0, 1
	sb	a4, 0(a1)	/* Store in kaddr */
	addi	a1, a1, 1
	beqz	a4, 2f
	addi	a2, a2, -1	/* len-- */
	addi	a5, a5, 1	/* count++ */
	bnez	a2, 1b

2:	EXIT_USER_ACCESS(a7)
	SET_FAULT_HANDLER(x0, a7) /* Clear the handler */

3:	beqz	a3, 4f		/* Check if done != NULL */
	addi	a5, a5, 1	/* count++ */
	sd	a5, 0(a3)	/* done = count */

4:	mv	a0, x0		/* return 0 */
	beqz	a4, 5f
	li	a0, ENAMETOOLONG
5:
	ret
END(copyinstr)
