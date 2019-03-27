/*
 * Copyright (C) 2014 Andrew Turner
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#define	PCR_Z	(1 << 30)
#define	PCR_C	(1 << 29)

/*
 * These functions return the result in the CPSR register.
 *
 * For __aeabi_cdcmple:
 *      Z C
 *   LT 0 0
 *   EQ 1 1
 * else 0 1
 *
 * __aeabi_cdrcmple is the same as __aeabi_cdcmple, however the arguments
 * have been swapped.
 */
ENTRY(__aeabi_cdcmple)
	push	{r4, r5, r6, r7, ip, lr}

	/* Backup the input registers */
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	mov	r7, r3
	/* Is it less than? */
	bl	__aeabi_dcmplt
	cmp	r0, #1
	bne	1f
	/* Yes, clear Z and C */
	mov	ip, #(0)
	b	99f

1:
	/* Restore the input regsters for the next function call */
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	mov	r3, r7
	/* Is it equal? */
	bl	__aeabi_dcmpeq
	cmp	r0, #1
	bne	2f
	/* Yes, set Z and C */
	mov	ip, #(PCR_Z | PCR_C)
	b 99f

2:
	/* Not less than or equal, set C and clear Z */
	mov	ip, #(PCR_C)

99:
	msr	cpsr_c, ip
	pop	{r4, r5, r6, r7, ip, pc}
END(__aeabi_cdcmple)

ENTRY(__aeabi_cdrcmple)
	/* Swap the first half of the arguments */
	mov	ip, r0
	mov	r0, r2
	mov	r2, ip

	/* And the second half */
	mov	ip, r1
	mov	r1, r3
	mov	r3, ip

	b	__aeabi_cdcmple
END(__aeabi_cdrcmple)

/*
 * This is just like __aeabi_cdcmple except it will not throw an exception
 * in the presence of a quiet NaN. If either argument is a signalling NaN we
 * will still signal.
 */
ENTRY(__aeabi_cdcmpeq)
	/* Check if we can call __aeabi_cfcmple safely */
	push	{r0, r1, r2, r3, r4, lr}
	bl	__aeabi_cdcmpeq_helper
	cmp	r0, #1
	pop	{r0, r1, r2, r3, r4, lr}
	beq	1f

	bl	__aeabi_cdcmple
	RET

1:
	mov	ip, #(PCR_C)
	msr	cpsr_c, ip
	RET
END(__aeabi_cdcmpeq)

	.section .note.GNU-stack,"",%progbits
