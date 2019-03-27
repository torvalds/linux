/*-
 * Copyright (c) 2002 Peter Grehan.
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
/*      $NetBSD: cerror.S,v 1.5 2000/01/27 14:58:48 kleink Exp $        */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");
	
#include "SYS.h"

	.globl  HIDENAME(cerror)
	.globl  CNAME(__error)

	/*
	 * The __error() function is thread aware. For non-threaded
	 * programs and the initial threaded in threaded programs,
	 * it returns a pointer to the global errno variable.
	 */
HIDENAME(cerror):
	mflr	%r0
	stwu	%r1,-16(%r1)		/* allocate new stack frame */
	stw	%r0,20(%r1)		/* and save lr, r31 */
	stw	%r31,8(%r1)
	mr	%r31,%r3          /* stash errval in callee-saved register */
	bl	PIC_PLT(CNAME(__error))
	stw	%r31,0(%r3)		/* store errval into &errno */
	lwz	%r0,20(%r1)
	lwz	%r31,8(%r1)
	mtlr	%r0
	la	%r1,16(%r1)
	li	%r3,-1
	li	%r4,-1
	blr				/* return to callers caller */

	.section .note.GNU-stack,"",%progbits
