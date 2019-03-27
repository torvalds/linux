/*-
 * Copyright (c) 2005 Olivier Houchard
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
__FBSDID("$FreeBSD$");

.syntax	unified

ENTRY(strlen)
	mov	r1, #0
	/* Check that the pointer is aligned on 32 bits. */
	ands	r3, r0, #3
	beq	.Loop
	sub	r0, r0, r3
	ldr	r2, [r0]
	add	r0, r0, #4
	cmp	r3, #2
	blt	.Ldo_3
	bgt	.Ldo_1
	/* So that the N bit is set. */
	cmp	r3, #0
	b	.Ldo_2

.Loop:
	ldr	r2, [r0]
	add	r0, r0, #4
#ifndef __ARMEB__
	ands	r3, r2, #0x000000ff
#else
	ands	r3, r2, #0xff000000
#endif
	it	ne
	addne	r1, r1, #1
.Ldo_3:
	itt	ne
#ifndef __ARMEB__
	andsne    r3, r2, #0x0000ff00
#else
  	andsne    r3, r2, #0x00ff0000
#endif  
	addne     r1, r1, #1
.Ldo_2:
	itt	ne
#ifndef __ARMEB__
	andsne    r3, r2, #0x00ff0000
#else
	andsne    r3, r2, #0x0000ff00
#endif  
	addne	r1, r1, #1
.Ldo_1:
	ittt	ne
#ifndef __ARMEB__
	andsne	r3, r2, #0xff000000
#else
	andsne	r3, r2, #0x000000ff
#endif  
	addne	r1, r1, #1
	bne	.Loop				
.Lexit:
	mov	r0, r1
	RET
END(strlen)

	.section .note.GNU-stack,"",%progbits
