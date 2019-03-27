/*-
 * Copyright (c) 2017-2018 QCM Technologies.
 * Copyright (c) 2017-2018 Semihalf.
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

#include "assym.inc"

#include <machine/asm.h>

	.globl	CNAME(power_save_sequence)
	.p2align 3
ENTRY(enter_idle_powerx)
	mfsprg0	%r3			/* Get the pcpu pointer */
	ld	%r3,PC_CURTHREAD(%r3)	/* Get current thread */
	ld	%r3,TD_PCB(%r3)		/* Get PCB of current thread */
	std	%r12,PCB_CONTEXT(%r3)	/* Save the non-volatile GP regs. */
	std	%r13,PCB_CONTEXT+1*8(%r3)
	std	%r14,PCB_CONTEXT+2*8(%r3)
	std	%r15,PCB_CONTEXT+3*8(%r3)
	std	%r16,PCB_CONTEXT+4*8(%r3)
	std	%r17,PCB_CONTEXT+5*8(%r3)
	std	%r18,PCB_CONTEXT+6*8(%r3)
	std	%r19,PCB_CONTEXT+7*8(%r3)
	std	%r20,PCB_CONTEXT+8*8(%r3)
	std	%r21,PCB_CONTEXT+9*8(%r3)
	std	%r22,PCB_CONTEXT+10*8(%r3)
	std	%r23,PCB_CONTEXT+11*8(%r3)
	std	%r24,PCB_CONTEXT+12*8(%r3)
	std	%r25,PCB_CONTEXT+13*8(%r3)
	std	%r26,PCB_CONTEXT+14*8(%r3)
	std	%r27,PCB_CONTEXT+15*8(%r3)
	std	%r28,PCB_CONTEXT+16*8(%r3)
	std	%r29,PCB_CONTEXT+17*8(%r3)
	std	%r30,PCB_CONTEXT+18*8(%r3)
	std	%r31,PCB_CONTEXT+19*8(%r3)

	mfcr	%r16			/* Save the condition register */
	std	%r16,PCB_CR(%r3)
	mflr	%r16			/* Save the link register */
	std	%r16,PCB_LR(%r3)
	std	%r1,PCB_SP(%r3)		/* Save the stack pointer */
	std	%r2,PCB_TOC(%r3)	/* Save the TOC pointer */

	/* Set where we want to jump */
	bl	1f
	.llong power_save_sequence	/* Remember about 8 byte alignment */
1:	mflr	%r3
	ld	%r3,0(%r3)
	mtsrr0	%r3

	/* Set MSR */
	li	%r3,0
	ori	%r3,%r3,(PSL_ME | PSL_RI)
	li	%r8,0x9			/* PSL_SF and PSL_HV */
	insrdi	%r3,%r8,4,0
	mtsrr1	%r3

	rfid

	.p2align 2
CNAME(power_save_sequence):
	bl	1f
	.llong	0x0			/* Playground for power-save sequence */
1:	mflr	%r3

	/* Start power-save sequence */
	std	%r2,0(%r3)
	ptesync
	ld	%r2,0(%r3)
2:	cmpd	%r2,%r2
	bne	2b
	nap
	b	.
