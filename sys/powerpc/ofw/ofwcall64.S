/*-
 * Copyright (C) 2009-2011 Nathan Whitehorn
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

#include <sys/syscall.h>

#include <machine/trap.h>
#include <machine/param.h>
#include <machine/spr.h>
#include <machine/asm.h>

#define	OFWSTKSZ	4096		/* 4K Open Firmware stack */

/*
 * Globals
 */
	.data
	.align	4
ofwstk:
	.space	OFWSTKSZ
rtas_regsave:
	.space	32 /* 4 * sizeof(register_t) */
GLOBAL(ofmsr)
	.llong  0, 0, 0, 0, 0		/* msr/sprg0-3 used in Open Firmware */
GLOBAL(rtasmsr)
	.llong	0
GLOBAL(openfirmware_entry)
	.llong	0			/* Open Firmware entry point */
GLOBAL(rtas_entry)
	.llong	0			/* RTAS entry point */

TOC_ENTRY(ofmsr)
TOC_ENTRY(ofwstk)
TOC_ENTRY(rtasmsr)
TOC_ENTRY(openfirmware_entry)
TOC_ENTRY(rtas_entry)
TOC_ENTRY(rtas_regsave)

/*
 * Open Firmware Real-mode Entry Point. This is a huge pain.
 */

ASENTRY_NOPROF(ofwcall)
	mflr	%r8
	std	%r8,16(%r1)
	stdu	%r1,-208(%r1)

	/*
	 * We need to save the following, because OF's register save/
	 * restore code assumes that the contents of registers are
	 * at most 32 bits wide: lr, cr, r2, r13-r31, the old MSR. These
	 * get placed in that order in the stack.
	 */

	mfcr	%r4
	std	%r4,48(%r1)
	std	%r13,56(%r1)
	std	%r14,64(%r1)
	std	%r15,72(%r1)
	std	%r16,80(%r1)
	std	%r17,88(%r1)
	std	%r18,96(%r1)
	std	%r19,104(%r1)
	std	%r20,112(%r1)
	std	%r21,120(%r1)
	std	%r22,128(%r1)
	std	%r23,136(%r1)
	std	%r24,144(%r1)
	std	%r25,152(%r1)
	std	%r26,160(%r1)
	std	%r27,168(%r1)
	std	%r28,176(%r1)
	std	%r29,184(%r1)
	std	%r30,192(%r1)
	std	%r31,200(%r1)

	/* Record the old MSR */
	mfmsr	%r6

	/* read client interface handler */
	ld	%r4,TOC_REF(openfirmware_entry)(%r2)
	ld	%r4,0(%r4)

	/* Get OF stack pointer */
	ld	%r7,TOC_REF(ofwstk)(%r2)
	addi	%r7,%r7,OFWSTKSZ-40

	/*
	 * Set the MSR to the OF value. This has the side effect of disabling
	 * exceptions, which is important for the next few steps.
	 */

	ld	%r5,TOC_REF(ofmsr)(%r2)
	ld	%r5,0(%r5)
	mtmsrd	%r5
	isync

	/*
	 * Set up OF stack. This needs to be accessible in real mode and
	 * use the 32-bit ABI stack frame format. The pointer to the current
	 * kernel stack is placed at the very top of the stack along with
	 * the old MSR so we can get them back later.
	 */
	mr	%r5,%r1
	mr	%r1,%r7
	std	%r5,8(%r1)	/* Save real stack pointer */
	std	%r2,16(%r1)	/* Save old TOC */
	std	%r6,24(%r1)	/* Save old MSR */
	std	%r8,32(%r1)	/* Save high 32-bits of the kernel's PC */

	li	%r5,0
	stw	%r5,4(%r1)
	stw	%r5,0(%r1)

	/* Finally, branch to OF */
	mtctr	%r4
	bctrl

	/* Reload stack pointer, MSR, and reference PC from the OFW stack */
	ld	%r7,32(%r1)
	ld	%r6,24(%r1)
	ld	%r2,16(%r1)
	ld	%r1,8(%r1)

	/* Get back to the MSR/PC we want, using the cached high bits of PC */
	mtsrr1	%r6
	clrrdi	%r7,%r7,32
	bl	1f
1:	mflr	%r8
	or	%r8,%r8,%r7
	addi	%r8,%r8,2f-1b
	mtsrr0	%r8
	rfid			/* Turn on MMU, exceptions, and 64-bit mode */

2:
	/* Sign-extend the return value from OF */
	extsw	%r3,%r3

	/* Restore all the non-volatile registers */
	ld	%r5,48(%r1)
	mtcr	%r5
	ld	%r13,56(%r1)
	ld	%r14,64(%r1)
	ld	%r15,72(%r1)
	ld	%r16,80(%r1)
	ld	%r17,88(%r1)
	ld	%r18,96(%r1)
	ld	%r19,104(%r1)
	ld	%r20,112(%r1)
	ld	%r21,120(%r1)
	ld	%r22,128(%r1)
	ld	%r23,136(%r1)
	ld	%r24,144(%r1)
	ld	%r25,152(%r1)
	ld	%r26,160(%r1)
	ld	%r27,168(%r1)
	ld	%r28,176(%r1)
	ld	%r29,184(%r1)
	ld	%r30,192(%r1)
	ld	%r31,200(%r1)

	/* Restore the stack and link register */
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr 	%r0
	blr

/*
 * RTAS 32-bit Entry Point. Similar to the OF one, but simpler (no separate
 * stack)
 *
 * C prototype: int rtascall(void *callbuffer, void *rtas_privdat);
 */

ASENTRY_NOPROF(rtascall)
	mflr	%r9
	std	%r9,16(%r1)
	stdu	%r1,-208(%r1)

	/*
	 * We need to save the following, because RTAS's register save/
	 * restore code assumes that the contents of registers are
	 * at most 32 bits wide: lr, cr, r2, r13-r31, the old MSR. These
	 * get placed in that order in the stack.
	 */

	mfcr	%r5
	std	%r5,48(%r1)
	std	%r13,56(%r1)
	std	%r14,64(%r1)
	std	%r15,72(%r1)
	std	%r16,80(%r1)
	std	%r17,88(%r1)
	std	%r18,96(%r1)
	std	%r19,104(%r1)
	std	%r20,112(%r1)
	std	%r21,120(%r1)
	std	%r22,128(%r1)
	std	%r23,136(%r1)
	std	%r24,144(%r1)
	std	%r25,152(%r1)
	std	%r26,160(%r1)
	std	%r27,168(%r1)
	std	%r28,176(%r1)
	std	%r29,184(%r1)
	std	%r30,192(%r1)
	std	%r31,200(%r1)

	/* Record the old MSR */
	mfmsr	%r6

	/* Read RTAS entry and reg save area pointers */
	ld	%r5,TOC_REF(rtas_entry)(%r2)
	ld	%r5,0(%r5)
	ld	%r8,TOC_REF(rtas_regsave)(%r2)

	/*
	 * Set the MSR to the RTAS value. This has the side effect of disabling
	 * exceptions, which is important for the next few steps.
	 */

	ld	%r7,TOC_REF(rtasmsr)(%r2)
	ld	%r7,0(%r7)
	mtmsrd	%r7
	isync

	/*
	 * Set up RTAS register save area, so that we can get back all of
	 * our 64-bit pointers. Save our stack pointer, the TOC, and the MSR.
	 * Put this in r1, since RTAS is obliged to save it. Kernel globals
	 * are below 4 GB, so this is safe.
	 */
	mr	%r7,%r1
	mr	%r1,%r8
	std	%r7,0(%r1)	/* Save 64-bit stack pointer */
	std	%r2,8(%r1)	/* Save TOC */
	std	%r6,16(%r1)	/* Save MSR */
	std	%r9,24(%r1)	/* Save reference PC for high 32 bits */

	/* Finally, branch to RTAS */
	mtctr	%r5
	bctrl

	/* 
	 * Reload stack pointer, MSR, reg PC from the reg save area in r1. We
	 * are running in 32-bit mode at this point, so it doesn't matter if r1
	 * has become sign-extended.
	 */
	ld	%r7,24(%r1)
	ld	%r6,16(%r1)
	ld	%r2,8(%r1)
	ld	%r1,0(%r1)

	/*
	 * Get back to the right PC. We need to atomically re-enable
	 * exceptions, 64-bit mode, and the MMU. One thing that has likely
	 * happened is that, if we were running in the high-memory direct
	 * map, we no longer are as a result of LR truncation in RTAS.
	 * Fix this by copying the high-order bits of the LR at function
	 * entry onto the current PC and then jumping there while flipping
	 * all the MSR bits.
	 */
	mtsrr1	%r6
	clrrdi	%r7,%r7,32
	bl	1f
1:	mflr	%r8
	or	%r8,%r8,%r7
	addi	%r8,%r8,2f-1b
	mtsrr0	%r8
	rfid			/* Turn on MMU, exceptions, and 64-bit mode */

2:
	/* Sign-extend the return value from RTAS */
	extsw	%r3,%r3

	/* Restore all the non-volatile registers */
	ld	%r5,48(%r1)
	mtcr	%r5
	ld	%r13,56(%r1)
	ld	%r14,64(%r1)
	ld	%r15,72(%r1)
	ld	%r16,80(%r1)
	ld	%r17,88(%r1)
	ld	%r18,96(%r1)
	ld	%r19,104(%r1)
	ld	%r20,112(%r1)
	ld	%r21,120(%r1)
	ld	%r22,128(%r1)
	ld	%r23,136(%r1)
	ld	%r24,144(%r1)
	ld	%r25,152(%r1)
	ld	%r26,160(%r1)
	ld	%r27,168(%r1)
	ld	%r28,176(%r1)
	ld	%r29,184(%r1)
	ld	%r30,192(%r1)
	ld	%r31,200(%r1)

	/* Restore the stack and link register */
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr 	%r0
	blr

