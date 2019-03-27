/*-
 * Copyright (C) 2015 Andrew Turner
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
 *
 * $FreeBSD$
 */

#include <machine/asm.h>

/*
 * uint64_t atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
 */
ENTRY(atomic_add_64_nv)
1:	ldxr	x2, [x0]	/* Load *target */
	add	x2, x2, x1	/* x2 = x2 + delta */
	stxr	w3, x2, [x0]	/* Store *target */
	cbnz	w3, 1b		/* Check if the store succeeded */
	mov	x0, x2		/* Return the new value */
	ret
END(atomic_add_64_nv)

/*
 * uint32_t
 * atomic_cas_32(volatile uint32_t *target, uint32_t cmp, uint32_t newval)
 */
ENTRY(atomic_cas_32)
1:	ldxr	w3, [x0]	/* Load *target */
	cmp	w3, w1		/* Does *targe == cmp? */
	b.ne	2f		/* If not exit */
	stxr	w4, w2, [x0]	/* Store newval to *target */
	cbnz	w4, 1b		/* Check if the store succeeded */
2:	mov	w0, w3		/* Return the old value */
	ret
END(atomic_cas_32)

/*
 * uint64_t
 * atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
 */
ENTRY(atomic_cas_64)
1:	ldxr	x3, [x0]	/* Load *target */
	cmp	x3, x1		/* Does *targe == cmp? */
	b.ne	2f		/* If not exit */
	stxr	w4, x2, [x0]	/* Store newval to *target */
	cbnz	w4, 1b		/* Check if the store succeeded */
2:	mov	x0, x3		/* Return the old value */
	ret
END(atomic_cas_64)

/*
 * uint8_t atomic_or_8_nv(volatile uint8_t *target, uint8_t value)
 */
ENTRY(atomic_or_8_nv)
1:	ldxrb	w2, [x0]	/* Load *target */
	orr	w2, w2, w1	/* x2 = x2 | delta */
	stxrb	w3, w2, [x0]	/* Store *target */
	cbnz	w3, 1b		/* Check if the store succeeded */
	mov	w0, w2		/* Return the new value */
	ret
END(atomic_or_8_nv)

ENTRY(membar_producer)
	dmb	ish
	ret
END(membar_producer)

