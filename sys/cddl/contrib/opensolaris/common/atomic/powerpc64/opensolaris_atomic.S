/*-
 * Copyright (C) 2010 Nathan Whitehorn
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

ENTRY(atomic_add_64_nv)
   1:	ldarx	%r5,0,%r3
	add	%r5,%r4,%r5
	stdcx.	%r5,0,%r3
	bne-	1b

	mr	%r3,%r5
	blr

ENTRY(atomic_cas_32)
   1:	lwarx	%r6,0,%r3
	cmplw	%r6,%r4
	bne	2f
	stwcx.	%r5,0,%r3
	bne-	1b
	b	3f

   2:	stwcx.	%r6,0,%r3	/* clear reservation */

   3:	mr	%r3,%r6
	blr

ENTRY(atomic_cas_64)
   1:	ldarx	%r6,0,%r3
	cmpld	%r6,%r4
	bne	2f
	stdcx.	%r5,0,%r3
	bne-	1b
	b	3f

   2:	stdcx.	%r6,0,%r3	/* clear reservation */

   3:	mr	%r3,%r6
	blr

ENTRY(atomic_or_8_nv)
	li	%r6,3
	andc.	%r6,%r3,%r6		/* r6 = r3 & ~4 */ 
	addi	%r7,%r6,3
	sub	%r7,%r7,%r3		/* offset in r7 */
	sldi	%r7,%r7,3		/* bits to shift in r7 */

	rlwinm	%r4,%r4,0,24,31		/* mask and rotate the argument */
	slw	%r4,%r4,%r7

   1:	lwarx	%r5,0,%r6
	or	%r5,%r4,%r5
	stwcx.	%r5,0,%r6
	bne-	1b

	srw	%r3,%r5,%r7
	rlwinm	%r3,%r3,0,24,31		/* mask return value */
	
	blr

ENTRY(membar_producer)
	eieio
	blr

