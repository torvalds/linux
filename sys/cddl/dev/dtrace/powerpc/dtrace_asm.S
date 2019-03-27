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
 * Portions Copyright 2012,2013 Justin Hibbits <jhibbits@freebsd.org>
 *
 * $FreeBSD$
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "assym.inc"

#define _ASM

#include <sys/cpuvar_defs.h>
#include <sys/dtrace.h>

#include <machine/asm.h>
/*
#include <machine/cpu.h>
*/

/*
 * Primitives
 */

        .text

/*
void dtrace_membar_producer(void)
*/
ASENTRY_NOPROF(dtrace_membar_producer)
	sync
	blr
END(dtrace_membar_producer)

/*
void dtrace_membar_consumer(void)
*/
ASENTRY_NOPROF(dtrace_membar_consumer)
	isync
	blr
END(dtrace_membar_consumer)

/*
dtrace_icookie_t dtrace_interrupt_disable(void)
*/
ASENTRY_NOPROF(dtrace_interrupt_disable)
	mfmsr	%r3
#ifdef __powerpc64__
	/* Two-instruction sequence to clear EE flag */
	rldicl	%r0,%r3,48,1
	rotldi	%r0,%r0,16
#else
	rlwinm	%r0,%r3,0,17,15 /* Clear EE flag */
#endif
	mtmsr	%r0
	blr
END(dtrace_interrupt_disable)

/*
void dtrace_interrupt_enable(dtrace_icookie_t cookie)
*/
ASENTRY_NOPROF(dtrace_interrupt_enable)
	mtmsr	%r3
	blr
END(dtrace_interrupt_enable)

/*
uint32_t dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
*/
ASENTRY_NOPROF(dtrace_cas32)
1:
	lwarx	%r0,0,%r3
	cmpw	%r4,%r0
	bne	2f
	stwcx.	%r5,0,%r3
	bne	1b
2:	mr	%r3,%r0
	blr
END(dtrace_cas32)

/*
void *
dtrace_casptr(void *target, void *cmp, void *new)
*/
ASENTRY_NOPROF(dtrace_casptr)
#ifdef __powerpc64__
1:
	ldarx	%r0,0,%r3
	cmpd	%r4,%r0
	bne	2f
	stdcx.	%r5,0,%r3
	bne	1b
#else
1:
	lwarx	%r0,0,%r3
	cmpw	%r4,%r0
	bne	2f
	stwcx.	%r5,0,%r3
	bne	1b
#endif
2:	mr	%r3,%r0
	blr
END(dtrace_casptr)


/*
XXX: unoptimized
void
dtrace_copy(uintptr_t src, uintptr_t dest, size_t size)
*/
ASENTRY_NOPROF(dtrace_copy)
	subi	%r7,%r3,1
	subi	%r8,%r4,1
	mtctr	%r5
1:
	lbzu	%r3,1(%r7)
	stbu	%r3,1(%r8)
	bdnz	1b
2:
	blr
END(dtrace_copy)

/*
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
*/
ASENTRY_NOPROF(dtrace_copystr)
	addme	%r7,%r3
	addme	%r8,%r4
1:
	lbzu	%r3,1(%r7)
	stbu	%r3,1(%r8)
	addme	%r5,%r5
	beq	2f
	or	%r3,%r3,%r3
	beq	2f
	andi.	%r0,%r5,0x0fff
	beq	2f
	lwz	%r0,0(%r6)
	andi.	%r0,%r0,CPU_DTRACE_BADADDR
	beq	1b
2:
	blr
END(dtrace_copystr)

/*
uintptr_t
dtrace_caller(int aframes)
*/
ASENTRY_NOPROF(dtrace_caller)
	li	%r3, -1
	blr
END(dtrace_caller)
