/*-
 * Copyright (C) 2011 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <machine/param.h>

.Lpj4b_sf_ctrl_reg:
	.word	0xf1021820

ENTRY(pj4b_config)

	/* Set Auxiliary Debug Modes Control 0 register */
	mrc	p15, 1, r0, c15, c1, 0
	/* ARMADAXP errata fix: ARM-CPU-6136 */
	bic	r0, r0, #(1 << 12)	/* LDSTM first issue is single word */

	orr	r0, r0, #(1 << 22)	/* DVM_WAKEUP disable */
	mcr	p15, 1, r0, c15, c1, 0

	/* Set Auxiliary Debug Modes Control 1 register */
	mrc	p15, 1, r0, c15, c1, 1
	/* ARMADAXP errata fix: ARM-CPU-6409 */
	bic	r0, r0, #(1 << 2)	/* Disable static branch prediction */

	orr	r0, r0, #(1 << 5)	/* STREX backoff disable */
	orr	r0, r0, #(1 << 8)	/* Internal parity handling disable */
	orr	r0, r0, #(1 << 16)	/* Disable data transfer for clean line */
	mcr	p15, 1, r0, c15, c1, 1

	/* Set Auxiliary Function Modes Control 0 register */
	mrc	p15, 1, r0, c15, c2, 0
#if defined(SMP)
	orr	r0, r0, #(1 << 1)	/* SMP/nAMP enabled (coherency) */
#endif
	orr	r0, r0, #(1 << 2)	/* L1 parite enable */
	orr	r0, r0, #(1 << 8)	/* Cache and TLB maintenance broadcast enable */
	mcr	p15, 1, r0, c15, c2, 0

	/* Set Auxiliary Debug Modes Control 2 register */
	mrc	p15, 1, r0, c15, c1, 2
	bic	r0, r0, #(1 << 23)	/* Enable fast LDR */
	orr	r0, r0, #(1 << 25)	/* Intervention Interleave disable */
	orr	r0, r0, #(1 << 27)	/* Critical word first sequencing disable */
	orr	r0, r0, #(1 << 29)	/* Disable MO device read / write */
	orr	r0, r0, #(1 << 30)	/* L1 cache strict round-robin replacement policy*/
	orr	r0, r0, #(1 << 31)	/* Enable write evict */
	mcr	p15, 1, r0, c15, c1, 2
#if defined(SMP)
	/* Set SMP mode in Auxiliary Control Register */
	mrc	p15, 0, r0, c1, c0, 1
	orr	r0, r0, #(1 << 5)
	mcr	p15, 0, r0, c1, c0, 1
#endif

	/* Load CPU number */
	mrc	p15, 0, r0, c0, c0, 5
	and	r0, r0, #0xf

	/* SF Enable and invalidate */
	ldr	r1, .Lpj4b_sf_ctrl_reg
	ldr	r2, [r1, r0, lsl #8]
	orr	r2, r2, #(1 << 0)
	bic	r2, r2, #(1 << 8)
	str	r2, [r1, r0, lsl #8]

	RET
END(pj4b_config)

