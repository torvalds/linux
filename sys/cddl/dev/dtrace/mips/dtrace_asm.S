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
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/regnum.h>

        .set    noreorder               # Noreorder is default style!

/*
 * Primitives
 */

        .text

/*
void dtrace_membar_producer(void)
*/
LEAF(dtrace_membar_producer)
	j	ra
	nop
END(dtrace_membar_producer)

/*
void dtrace_membar_consumer(void)
*/
LEAF(dtrace_membar_consumer)
	j	ra
	nop
END(dtrace_membar_consumer)

/*
dtrace_icookie_t dtrace_interrupt_disable(void)
*/
LEAF(dtrace_interrupt_disable)
	mfc0	t0, MIPS_COP_0_STATUS
	move	v0, t0
	and	v0, v0, MIPS_SR_INT_IE
	and	t0, t0, ~MIPS_SR_INT_IE
	mtc0	t0, MIPS_COP_0_STATUS
	j	ra
	nop
END(dtrace_interrupt_disable)

/*
void dtrace_interrupt_enable(dtrace_icookie_t cookie)
*/
LEAF(dtrace_interrupt_enable)
	mfc0	t0, MIPS_COP_0_STATUS
	beqz	a0, not_enabled
	or	t0, t0, MIPS_SR_INT_IE
	mtc0	t0, MIPS_COP_0_STATUS
not_enabled:
	j	ra
	nop
END(dtrace_interrupt_enable)

/*
uint32_t dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
*/
LEAF(dtrace_cas32)
1:
	move	t1, a2
	ll	t0, 0(a0)
	bne	t0, a1, 2f
	nop
	sc	t1, 0(a0)
	beqz	t1, 1b
	nop
2:	move	v0, t0
	j	ra
	nop
END(dtrace_cas32)

/*
void *
dtrace_casptr(void *target, void *cmp, void *new)
*/
LEAF(dtrace_casptr)
1:
	move	t1, a2
	PTR_LL	t0, 0(a0)
	bne	t0, a1, 2f
	nop
	PTR_SC	t1, 0(a0)
	beqz	t1, 1b
	nop
2:	move	v0, t0
	j	ra
	nop
END(dtrace_casptr)


/*
uintptr_t
dtrace_fulword(void *addr)
*/
LEAF(dtrace_fulword)
END(dtrace_fulword)

/*
uint8_t
dtrace_fuword8_nocheck(void *addr)
*/
LEAF(dtrace_fuword8_nocheck)
	lbu	v0, 0(a0)
	j	ra
	nop
END(dtrace_fuword8_nocheck)

/*
uint16_t
dtrace_fuword16_nocheck(void *addr)
*/
LEAF(dtrace_fuword16_nocheck)
	lhu	v0, 0(a0)
	j	ra
	nop
END(dtrace_fuword16_nocheck)

/*
uint32_t
dtrace_fuword32_nocheck(void *addr)
*/
LEAF(dtrace_fuword32_nocheck)
	lw	v0, 0(a0)
	j	ra
	nop
END(dtrace_fuword32_nocheck)

/*
uint64_t
dtrace_fuword64_nocheck(void *addr)
*/
LEAF(dtrace_fuword64_nocheck)
#if defined(__mips_n64) || defined(__mips_n32)
	ld	v0, 0(a0)
#else
	lw	v1,4(a0)
	lw	v0,0(a0)
#endif
	j	ra
	nop
END(dtrace_fuword64_nocheck)

/*
XXX: unoptimized
void
dtrace_copy(uintptr_t src, uintptr_t dest, size_t size)
*/
LEAF(dtrace_copy)
1:
	beqz	a2, 2f
	nop
	lbu	t0, 0(a0)
	sb	t0, 0(a1)
	PTR_ADDU	a0, a0, 1
	PTR_ADDU	a1, a1, 1
	INT_SUBU	a2, a2, 1
	j	1b
	nop
2:
	j	ra
	nop
END(dtrace_copy)

/*
XXX: Unoptimized. Check for flags on page boundaries only(?)
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
*/
LEAF(dtrace_copystr)
1:
	lbu	t0, 0(a0)
	sb	t0, 0(a1)
	PTR_ADDU	a0, a0, 1
	PTR_ADDU	a1, a1, 1
	INT_SUBU	a2, a2, 1
	beqz	t0, 2f
	nop
	lhu	t1, (a3)
	and	t1, t1, CPU_DTRACE_BADADDR
	bnez	t1, 2f
	nop

	bnez	a2, 1b
	nop
2:
	j	ra
	nop
END(dtrace_copystr)

/*
uintptr_t
dtrace_caller(int aframes)
*/
LEAF(dtrace_caller)
	li	v0, -1
	j	ra
	nop
END(dtrace_caller)
