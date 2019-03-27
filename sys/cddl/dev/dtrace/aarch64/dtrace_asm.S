/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#define _ASM
#define _LOCORE

#include <sys/cpuvar_defs.h>
#include <sys/dtrace.h>

#include <machine/armreg.h>
#include <machine/asm.h>

#include "assym.inc"

/*
void dtrace_membar_producer(void)
*/
ENTRY(dtrace_membar_producer)
	RET
END(dtrace_membar_producer)

/*
void dtrace_membar_consumer(void)
*/
ENTRY(dtrace_membar_consumer)
	RET
END(dtrace_membar_consumer)

/*
dtrace_icookie_t dtrace_interrupt_disable(void)
*/
ENTRY(dtrace_interrupt_disable)
	mrs	x0, daif
	msr	daifset, #2
	RET
END(dtrace_interrupt_disable)

/*
void dtrace_interrupt_enable(dtrace_icookie_t cookie)
*/
ENTRY(dtrace_interrupt_enable)
	msr	daif, x0
	RET
END(dtrace_interrupt_enable)
/*
uint8_t
dtrace_fuword8_nocheck(void *addr)
*/
ENTRY(dtrace_fuword8_nocheck)
	ldrb	w0, [x0]
	RET
END(dtrace_fuword8_nocheck)

/*
uint16_t
dtrace_fuword16_nocheck(void *addr)
*/
ENTRY(dtrace_fuword16_nocheck)
	ldrh	w0, [x0]
	RET
END(dtrace_fuword16_nocheck)

/*
uint32_t
dtrace_fuword32_nocheck(void *addr)
*/
ENTRY(dtrace_fuword32_nocheck)
	ldr	w0, [x0]
	RET
END(dtrace_fuword32_nocheck)

/*
uint64_t
dtrace_fuword64_nocheck(void *addr)
*/
ENTRY(dtrace_fuword64_nocheck)
	ldr	x0, [x0]
	RET
END(dtrace_fuword64_nocheck)

/*
void
dtrace_copy(uintptr_t uaddr, uintptr_t kaddr, size_t size)
*/
ENTRY(dtrace_copy)
	cbz	x2, 2f		/* If len == 0 then skip loop */
1:
	ldrb	w4, [x0], #1	/* Load from uaddr */
	strb	w4, [x1], #1	/* Store in kaddr */
	sub	x2, x2, #1	/* len-- */
	cbnz	x2, 1b
2:
	RET
END(dtrace_copy)

/*
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
XXX: Check for flags?
*/
ENTRY(dtrace_copystr)
	cbz     x2, 2f          /* If len == 0 then skip loop */
 
1:	ldrb    w4, [x0], #1    /* Load from uaddr */
	strb    w4, [x1], #1    /* Store in kaddr */
	cbz     w4, 2f          /* If == 0 then break */
	sub     x2, x2, #1      /* len-- */
	cbnz    x2, 1b
2:
	RET
END(dtrace_copystr)

/*
uintptr_t
dtrace_caller(int aframes)
*/
ENTRY(dtrace_caller)
	mov	x0, #-1
	RET
END(dtrace_caller)

/*
uint32_t
dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
*/
ENTRY(dtrace_cas32)
1:	ldxr	w3, [x0]	/* Load target */
	cmp	w3, w1		/* Check if *target == cmp */
	bne	2f		/* No, return */
	stxr	w12, w2, [x0]	/* Store new to target */
	cbnz	w12, 1b		/* Try again if store not succeed */
2:	mov	w0, w3		/* Return the value loaded from target */
	RET
END(dtrace_cas32)

/*
void *
dtrace_casptr(volatile void *target, volatile void *cmp, volatile void *new)
*/
ENTRY(dtrace_casptr)
1:	ldxr	x3, [x0]	/* Load target */
	cmp	x3, x1		/* Check if *target == cmp */
	bne	2f		/* No, return */
	stxr	w12, x2, [x0]	/* Store new to target */
	cbnz	w12, 1b		/* Try again if store not succeed */
2:	mov	x0, x3		/* Return the value loaded from target */
	RET
END(dtrace_casptr)
