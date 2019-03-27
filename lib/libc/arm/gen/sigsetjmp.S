/*	$NetBSD: sigsetjmp.S,v 1.6 2013/04/19 16:50:22 matt Exp $	*/

/*
 * Copyright (c) 1997 Mark Brinicombe
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <machine/setjmp.h>

/*
 * C library -- sigsetjmp, siglongjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	setjmp(a, m)
 * by restoring registers from the stack.
 * The previous signal state is restored.
 */

ENTRY(sigsetjmp)
	teq	r1, #0
	beq	PIC_SYM(_C_LABEL(_setjmp), PLT)
	b	PIC_SYM(_C_LABEL(setjmp), PLT)
END(sigsetjmp)

.L_setjmp_magic:
	.word	_JB_MAGIC__SETJMP
WEAK_ALIAS(__siglongjmp, siglongjmp)

ENTRY(siglongjmp)
	ldr	r2, .L_setjmp_magic		/* load magic */
	ldr	r3, [r0]			/* get magic from jmp_buf */
	bic	r3, r3, #(_JB_MAGIC__SETJMP ^ _JB_MAGIC__SETJMP_VFP)
						/* ignore VFP-ness of magic */
	teq	r2, r3				/* magic correct? */
	beq	PIC_SYM(_C_LABEL(_longjmp), PLT)
	b	PIC_SYM(_C_LABEL(longjmp), PLT)
END(siglongjmp)

	.section .note.GNU-stack,"",%progbits
