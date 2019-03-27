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
GLOBAL(ofmsr)
	.long	0, 0, 0, 0, 0		/* msr/sprg0-3 used in Open Firmware */
GLOBAL(rtasmsr)
	.long	0
GLOBAL(openfirmware_entry)
	.long	0			/* Open Firmware entry point */
GLOBAL(rtas_entry)
	.long	0			/* RTAS entry point */

	.align	4
ofwstk:
	.space	OFWSTKSZ
rtas_regsave:
	.space	4

/*
 * Open Firmware Entry Point. May need to enter real mode.
 *
 * C prototype: int ofwcall(void *callbuffer);
 */

ASENTRY(ofwcall)
	mflr	%r0
	stw	%r0,4(%r1)

	/* Record the old MSR */
	mfmsr	%r6

	/* GOT pointer in r7 */
	bl	_GLOBAL_OFFSET_TABLE_@local-4
	mflr	%r7

	/* read client interface handler */
	lwz	%r4,openfirmware_entry@got(%r7)
	lwz	%r4,0(%r4)

	/*
	 * Set the MSR to the OF value. This has the side effect of disabling
	 * exceptions, which prevents preemption later.
	 */

	lwz	%r5,ofmsr@got(%r7)
	lwz	%r5,0(%r5)
	mtmsr	%r5
	isync

	/*
	 * Set up OF stack. This needs to be potentially accessible in real mode
	 * The pointer to the current kernel stack is placed at the very
	 * top of the stack along with the old MSR so we can get them back
	 * later.
	 */
	mr	%r5,%r1
	lwz	%r1,ofwstk@got(%r7)
	addi	%r1,%r1,(OFWSTKSZ-32)
	stw	%r5,20(%r1)	/* Save real stack pointer */
	stw	%r2,24(%r1)	/* Save curthread */
	stw	%r6,28(%r1)	/* Save old MSR */
	li	%r5,0
	stw	%r5,4(%r1)
	stw	%r5,0(%r1)

	/* Finally, branch to OF */
	mtctr	%r4
	bctrl

	/* Reload stack pointer and MSR from the OFW stack */
	lwz	%r6,28(%r1)
	lwz	%r2,24(%r1)
	lwz	%r1,20(%r1)

	/* Now set the real MSR */
	mtmsr	%r6
	isync

	/* Return */
	lwz	%r0,4(%r1)
	mtlr 	%r0
	blr

/*
 * RTAS Entry Point. Similar to the OF one, but simpler (no separate stack)
 *
 * C prototype: int rtascall(void *callbuffer, void *rtas_privdat);
 */

ASENTRY(rtascall)
	mflr	%r0
	stw	%r0,4(%r1)

	/* GOT pointer in r7 */
	bl	_GLOBAL_OFFSET_TABLE_@local-4
	mflr	%r7

	/* Record the old MSR to real-mode-accessible area */
	mfmsr	%r0
	lwz	%r5,rtas_regsave@got(%r7)
	stw	%r0,0(%r5)

	/* read client interface handler */
	lwz	%r5,rtas_entry@got(%r7)
	lwz	%r5,0(%r5)

	/* Set the MSR to the RTAS value */
	lwz	%r6,rtasmsr@got(%r7)
	lwz	%r6,0(%r6)
	mtmsr	%r6
	isync

	/* Branch to RTAS */
	mtctr	%r5
	bctrl

	/* GOT pointer in r7 */
	bl	_GLOBAL_OFFSET_TABLE_@local-4
	mflr	%r7

	/* Now set the MSR back */
	lwz	%r6,rtas_regsave@got(%r7)
	lwz	%r6,0(%r6)
	mtmsr	%r6
	isync

	/* And return */
	lwz	%r0,4(%r1)
	mtlr 	%r0
	blr

