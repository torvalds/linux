/* $OpenBSD: support.S,v 1.10 2022/11/06 11:44:30 kettenis Exp $ */
/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014-2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation
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

#include <machine/setjmp.h>
#include <machine/param.h>
#include <machine/vmparam.h>

#include "assym.h"
#include "efi.h"

#if defined(DDB) || NEFI > 0
ENTRY(setjmp)
	RETGUARD_SETUP(setjmp, x15)
	/* Store the stack pointer */
	mov	x8, sp
	str	x8, [x0], #8

	/* Store the general purpose registers and lr */
	stp	x19, x20, [x0], #16
	stp	x21, x22, [x0], #16
	stp	x23, x24, [x0], #16
	stp	x25, x26, [x0], #16
	stp	x27, x28, [x0], #16
	stp	x29, lr, [x0], #16

	/* Return zero */
	mov	x0, #0
	RETGUARD_CHECK(setjmp, x15)
	ret
END(setjmp)

ENTRY(longjmp)
	RETGUARD_SYMBOL(longjmp)
	RETGUARD_LOAD_RANDOM(longjmp, x15)
	/* Restore the stack pointer */
	ldr	x8, [x0], #8
	mov	sp, x8

	/* Restore the general purpose registers and lr */
	ldp	x19, x20, [x0], #16
	ldp	x21, x22, [x0], #16
	ldp	x23, x24, [x0], #16
	ldp	x25, x26, [x0], #16
	ldp	x27, x28, [x0], #16
	ldp	x29, lr, [x0], #16
	RETGUARD_CALC_COOKIE(x15)

	/* Return nonzero */
	mov	x0, #1
	RETGUARD_CHECK(longjmp, x15)
	ret
END(longjmp)
#endif

/*
 * pagezero, simple implementation
 */
ENTRY(pagezero_simple)
	RETGUARD_SETUP(pagezero_simple, x15)
	add	x1, x0, #PAGE_SIZE

1:
	stp	xzr, xzr, [x0], #0x10
	stp	xzr, xzr, [x0], #0x10
	stp	xzr, xzr, [x0], #0x10
	stp	xzr, xzr, [x0], #0x10
	cmp	x0, x1
	b.ne	1b
	RETGUARD_CHECK(pagezero_simple, x15)
	ret

END(pagezero_simple)

/*
 * pagezero, cache assisted
 */
ENTRY(pagezero_cache)
	RETGUARD_SETUP(pagezero_cache, x15)
	add	x1, x0, #PAGE_SIZE

	ldr	x2, =dczva_line_size
	ldr	x2, [x2]

1:
	dc	zva, x0
	add	x0, x0, x2
	cmp	x0, x1
	b.ne	1b
	RETGUARD_CHECK(pagezero_cache, x15)
	ret

END(pagezero_cache)

ENTRY(smc_call)
	smc	#0
	ret

END(smc_call)

ENTRY(hvc_call)
	hvc	#0
	ret

END(hvc_call)
