/*-
 * Copyright (c) 2014 The FreeBSD Foundation
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

ENTRY(.rtld_start)
	mov	x19, x0		/* Put ps_strings in a callee-saved register */
	mov	x20, sp		/* And the stack pointer */

	sub	sp, sp, #16	/* Make room for obj_main & exit proc */

	mov	x1, sp		/* exit_proc */
	add	x2, x1, #8	/* obj_main */
	bl	_rtld		/* Call the loader */
	mov	x8, x0		/* Backup the entry point */

	ldr	x2, [sp]	/* Load cleanup */
	ldr	x1, [sp, #8]	/* Load obj_main */
	mov	x0, x19		/* Restore ps_strings */
	mov	sp, x20		/* Restore the stack pointer */
	br	x8		/* Jump to the entry point */
END(.rtld_start)

/*
 * sp + 0 = &GOT[x + 3]
 * sp + 8 = RA
 * x16 = &GOT[2]
 * x17 = &_rtld_bind_start
 */
ENTRY(_rtld_bind_start)
	.cfi_startproc
	mov	x17, sp

	/* Save frame pointer and SP */
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	.cfi_def_cfa x29, 16
	.cfi_offset x30, -8
	.cfi_offset x29, -16

	/* Save the arguments */
	stp	x0, x1, [sp, #-16]!
	stp	x2, x3, [sp, #-16]!
	stp	x4, x5, [sp, #-16]!
	stp	x6, x7, [sp, #-16]!
	stp	x8, xzr, [sp, #-16]!

	/* Save any floating-point arguments */
	stp	q0, q1, [sp, #-32]!
	stp	q2, q3, [sp, #-32]!
	stp	q4, q5, [sp, #-32]!
	stp	q6, q7, [sp, #-32]!

	/* Calculate reloff */
	ldr	x2, [x17, #0]	/* Get the address of the entry */
	sub	x1, x2, x16	/* Find its offset */
	sub	x1, x1, #8	/* Adjust for x16 not being at offset 0 */
	/* Each rela item has 3 entriesso we need reloff = 3 * index */
	lsl	x3, x1, #1	/* x3 = 2 * offset */
	add	x1, x1, x3	/* x1 = x3 + offset = 3 * offset */

	/* Load obj */
	ldr	x0, [x16, #-8]

	/* Call into rtld */
	bl	_rtld_bind

	/* Backup the address to branch to */
	mov	x16, x0

	/* restore the arguments */
	ldp	q6, q7, [sp], #32
	ldp	q4, q5, [sp], #32
	ldp	q2, q3, [sp], #32
	ldp	q0, q1, [sp], #32
	ldp	x8, xzr, [sp], #16
	ldp	x6, x7, [sp], #16
	ldp	x4, x5, [sp], #16
	ldp	x2, x3, [sp], #16
	ldp	x0, x1, [sp], #16

	/* Restore frame pointer */
	ldp	x29, xzr, [sp], #16

	 /* Restore link register saved by the plt code */
	ldp	xzr, x30, [sp], #16

	/* Call into the correct function */
	br	x16
	.cfi_endproc
END(_rtld_bind_start)

/*
 * struct rel_tlsdesc {
 *  uint64_t resolver_fnc;
 *  uint64_t resolver_arg;
 *
 *
 * uint64_t _rtld_tlsdesc_static(struct rel_tlsdesc *);
 *
 * Resolver function for TLS symbols resolved at load time
 */
ENTRY(_rtld_tlsdesc_static)
	.cfi_startproc
	ldr	x0, [x0, #8]
	ret
	.cfi_endproc
END(_rtld_tlsdesc_static)

/*
 * uint64_t _rtld_tlsdesc_undef(void);
 *
 * Resolver function for weak and undefined TLS symbols
 */
ENTRY(_rtld_tlsdesc_undef)
	.cfi_startproc
	str	x1, [sp, #-16]!
	.cfi_adjust_cfa_offset	16

	mrs	x1, tpidr_el0
	ldr	x0, [x0, #8]
	sub	x0, x0, x1

	ldr	x1, [sp], #16
	.cfi_adjust_cfa_offset 	-16
	.cfi_endproc
	ret
END(_rtld_tlsdesc_undef)

/*
 * uint64_t _rtld_tlsdesc_dynamic(struct rel_tlsdesc *);
 *
 * Resolver function for TLS symbols from dlopen()
 */
ENTRY(_rtld_tlsdesc_dynamic)
	.cfi_startproc

	/* Save registers used in fast path */
	stp	x1,  x2, [sp, #(-2 * 16)]!
	stp	x3,  x4, [sp, #(1 * 16)]
	.cfi_adjust_cfa_offset	2 * 16
	.cfi_rel_offset		x1, 0
	.cfi_rel_offset		x2, 8
	.cfi_rel_offset		x3, 16
	.cfi_rel_offset		x4, 24

	/* Test fastpath - inlined version of tls_get_addr_common(). */
	ldr	x1, [x0, #8]		/* tlsdesc ptr */
	mrs	x4, tpidr_el0
	ldr	x0, [x4]		/* DTV pointer */
	ldr	x2, [x0]		/* dtv[0] (generation count) */
	ldr	x3, [x1]		/* tlsdec->dtv_gen */
	cmp	x2, x3
	b.ne	1f			/* dtv[0] != tlsdec->dtv_gen */

	ldr	w2, [x1, #8]		/* tlsdec->tls_index */
	add	w2, w2, #1
	ldr     x3, [x0, w2, sxtw #3]	/* dtv[tlsdesc->tls_index + 1] */
	cbz	x3, 1f

	/* Return (dtv[tlsdesc->tls_index + 1] + tlsdesc->tls_offs - tp) */
	ldr	x2, [x1, #16]		/* tlsdec->tls_offs */
	add 	x2, x2, x3
	sub	x0, x2, x4
	/* Restore registers and return */
	ldp	 x3,  x4, [sp, #(1 * 16)]
	ldp	 x1,  x2, [sp], #(2 * 16)
	.cfi_adjust_cfa_offset 	-2 * 16
	ret

	/*
	 * Slow path
	  * return(
	 *    tls_get_addr_common(tp, tlsdesc->tls_index, tlsdesc->tls_offs));
	 *
	 */
1:
	/* Save all interger registers */
	stp	x29, x30, [sp, #-(8 * 16)]!
	.cfi_adjust_cfa_offset	8 * 16
	.cfi_rel_offset		x29, 0
	.cfi_rel_offset		x30, 8

	mov	x29, sp
	stp	x5,   x6, [sp, #(1 * 16)]
	stp	x7,   x8, [sp, #(2 * 16)]
	stp	x9,  x10, [sp, #(3 * 16)]
	stp	x11, x12, [sp, #(4 * 16)]
	stp	x13, x14, [sp, #(5 * 16)]
	stp	x15, x16, [sp, #(6 * 16)]
	stp	x17, x18, [sp, #(7 * 16)]
	.cfi_rel_offset		 x5, 16
	.cfi_rel_offset		 x6, 24
	.cfi_rel_offset		 x7, 32
	.cfi_rel_offset		 x8, 40
	.cfi_rel_offset		 x9, 48
	.cfi_rel_offset		x10, 56
	.cfi_rel_offset		x11, 64
	.cfi_rel_offset		x12, 72
	.cfi_rel_offset		x13, 80
	.cfi_rel_offset		x14, 88
	.cfi_rel_offset		x15, 96
	.cfi_rel_offset		x16, 104
	.cfi_rel_offset		x17, 112
	.cfi_rel_offset		x18, 120

	/* Find the tls offset */
	mov	x0, x4			/* tp */
	mov	x3, x1			/* tlsdesc ptr */
	ldr	w1, [x3, #8]		/* tlsdec->tls_index */
	ldr	x2, [x3, #16]		/* tlsdec->tls_offs */
	bl	tls_get_addr_common
	mrs	x1, tpidr_el0
	sub	x0, x0, x1

	/* Restore slow patch registers */
	ldp	x17, x18, [sp, #(7 * 16)]
	ldp	x15, x16, [sp, #(6 * 16)]
	ldp	x13, x14, [sp, #(5 * 16)]
	ldp	x11, x12, [sp, #(4 * 16)]
	ldp	x9, x10,  [sp, #(3 * 16)]
	ldp	x7, x8,   [sp, #(2 * 16)]
	ldp	x5, x6,   [sp, #(1 * 16)]
	ldp	x29, x30, [sp], #(8 * 16)
	.cfi_adjust_cfa_offset 	-8 * 16
	.cfi_restore		x29
	.cfi_restore		x30

	/* Restore fast path registers and return */
	ldp	 x3,  x4, [sp, #16]
	ldp	 x1,  x2, [sp], #(2 * 16)
	.cfi_adjust_cfa_offset	-2 * 16
	.cfi_endproc
	ret
END(_rtld_tlsdesc_dynamic)
