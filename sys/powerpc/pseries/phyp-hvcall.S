/*-
 * Copyright (C) 2010 Andreas Tobler
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <machine/asm.h>

/* Hypervisor entry call. */
#define  hc  .long 0x44000022

/*
 * Simple HV calls take the same arguments, with the same ABI, as this
 * C function
 */
ASENTRY(phyp_hcall)
	mflr	%r0
	std	%r0,16(%r1)
	ld	%r11,112(%r1)		/* Last couple args into volatile regs*/
	ld	%r12,120(%r1)
	hc				/* invoke the hypervisor */
	ld	%r0,16(%r1)
	mtlr	%r0
	blr				/* return r3 = status */

/*
 * PFT HV calls take a special ABI (see PAPR 14.5.4.1)
 *
 * r3-r7 arguments passed unchanged, r8-r10 are addresses of return values
 * HV takes the same r3-r7, but returns values in r3, r4-r6
 */
ASENTRY(phyp_pft_hcall)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-80(%r1)
	std	%r8,48(%r1)		/* save arguments */
	std	%r9,56(%r1)
	std	%r10,64(%r1)
	hc				/* invoke the hypervisor */
	ld	%r11,48(%r1)		/* store results */
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r1,0(%r1)		/* exit */
	ld	%r0,16(%r1)
	mtlr	%r0
	blr				/* return r3 = status */

