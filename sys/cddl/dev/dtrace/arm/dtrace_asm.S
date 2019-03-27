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

#include <machine/asm.h>
#include <machine/armreg.h>

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
	mrs	r0, cpsr
	mov	r1, r0
	orr	r1, r1, #(PSR_I | PSR_F)
	msr	cpsr_c, r1
	RET
END(dtrace_interrupt_disable)

/*
void dtrace_interrupt_enable(dtrace_icookie_t cookie)
*/
ENTRY(dtrace_interrupt_enable)
	and	r0, r0, #(PSR_I | PSR_F) 
	mrs	r1, cpsr
	bic	r1, r1, #(PSR_I | PSR_F)
	orr	r1, r1, r0
	msr	cpsr_c, r1
	RET
END(dtrace_interrupt_enable)
/*
uint8_t
dtrace_fuword8_nocheck(void *addr)
*/
ENTRY(dtrace_fuword8_nocheck)
	ldrb	r3, [r0]
	mov 	r0, r3
	RET
END(dtrace_fuword8_nocheck)

/*
uint16_t
dtrace_fuword16_nocheck(void *addr)
*/
ENTRY(dtrace_fuword16_nocheck)
	ldrh	r3, [r0]
	mov 	r0, r3
	RET
END(dtrace_fuword16_nocheck)

/*
uint32_t
dtrace_fuword32_nocheck(void *addr)
*/
ENTRY(dtrace_fuword32_nocheck)
	ldr	r3, [r0]
	mov 	r0, r3
	RET
END(dtrace_fuword32_nocheck)

/*
uint64_t
dtrace_fuword64_nocheck(void *addr)
*/
ENTRY(dtrace_fuword64_nocheck)
	ldm	r0, {r2, r3}

	mov	r0, r2
	mov	r1, r3
#if defined(__BIG_ENDIAN__)
/* big endian */
	mov	r0, r3
	mov	r1, r2
#else
/* little endian */
	mov	r0, r2
	mov	r1, r3

#endif
	RET
END(dtrace_fuword64_nocheck)

/*
void
dtrace_copy(uintptr_t uaddr, uintptr_t kaddr, size_t size)
*/
ENTRY(dtrace_copy)
	stmfd	sp!, {r4-r5}			/* stack is 8 byte aligned */
	teq	r2, #0x00000000
	mov	r5, #0x00000000
	beq	2f

1:	ldrb	r4, [r0], #0x0001
	add	r5, r5, #0x00000001
	strb	r4, [r1], #0x0001
	teqne	r5, r2
	bne	1b

2:	ldmfd	sp!, {r4-r5}			/* stack is 8 byte aligned */
	RET
END(dtrace_copy)

/*
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
XXX: Check for flags?
*/
ENTRY(dtrace_copystr)
	stmfd	sp!, {r4-r5}			/* stack is 8 byte aligned */
	teq	r2, #0x00000000
	mov	r5, #0x00000000
	beq	2f

1:	ldrb	r4, [r0], #0x0001
	add	r5, r5, #0x00000001
	teq	r4, #0x00000000
	strb	r4, [r1], #0x0001
	teqne	r5, r2
	bne	1b

2:	ldmfd	sp!, {r4-r5}			/* stack is 8 byte aligned */
	RET
END(dtrace_copystr)

/*
uintptr_t
dtrace_caller(int aframes)
*/
ENTRY(dtrace_caller)
	mov	r0, #-1
	RET
END(dtrace_caller)

/*
uint32_t
dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)

void *
dtrace_casptr(volatile void *target, volatile void *cmp, volatile void *new)
*/
ENTRY(dtrace_cas32)
EENTRY(dtrace_casptr)
1:	ldrex	r3, [r0]	/* Load target */
	cmp	r3, r1		/* Check if *target == cmp */
	bne	2f		/* No, return */
	strex	ip, r2, [r0]	/* Store new to target */
	cmp	ip, #0		/* Did the store succeed? */
	bne	1b		/* No, try again */
2:	mov	r0, r3		/* Return the value loaded from target */
	RET
EEND(dtrace_casptr)
END(dtrace_cas32)
