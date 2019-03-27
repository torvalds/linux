/*-
 * Copyright 2011 Semihalf
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
 */

#include <machine/asm.h>
#include <machine/armreg.h>

__FBSDID("$FreeBSD$");

.global _C_LABEL(mptramp_pmu_boot)

ASENTRY_NP(mptramp)
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7, 0

	mrs	r3, cpsr
	bic	r3, r3, #(PSR_MODE)
	orr	r3, r3, #(PSR_SVC32_MODE)
        msr	cpsr_fsxc, r3

	mrc	p15, 0, r0, c0, c0, 5
	and	r0, #0x0f		/* Get CPU ID */

	/* Read boot address for CPU */
	mov	r1, #0x100
	mul	r2, r0, r1
	ldr	r1, mptramp_pmu_boot
	add	r0, r2, r1
	ldr	r1, [r0], #0x00

	mov pc, r1

_C_LABEL(mptramp_pmu_boot):
	.word 0x0

END(mptramp)

	.global _C_LABEL(mptramp_end)
_C_LABEL(mptramp_end):
