/*	$NetBSD: _setjmp.S,v 1.12 2013/04/19 13:45:45 matt Exp $	*/

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

#if !defined(__SOFTFP__) && !defined(__VFP_FP__) && !defined(__ARM_PCS)
#error FPA is not supported anymore
#endif

#if !defined(_STANDALONE)
	.fpu	vfp
#endif

#include <machine/asm.h>
#include <machine/setjmp.h>

__FBSDID("$FreeBSD$");

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is NOT restored.
 *
 * Note: r0 is the return value
 *       r1-r3,ip are scratch registers in functions
 */

ENTRY(_setjmp)
	ldr	r1, .L_setjmp_magic

#if !defined(_STANDALONE) && __ARM_ARCH >= 6
	add	r2, r0, #(_JB_REG_D8 * 4)
	vstmia	r2, {d8-d15}
	vmrs	r2, fpscr
	str	r2, [r0, #(_JB_REG_FPSCR * 4)]
#endif /* !_STANDALONE && __ARM_ARCH >= 6 */

	str	r1, [r0]

	add	r0, r0, #(_JB_REG_R4 * 4)
	/* Store integer registers */
#ifndef __thumb__
        stmia	r0, {r4-r14}
#else
	stmia	r0, {r4-r12}
	str	r13, [r0, #((_JB_REG_R13 - _JB_REG_R4) * 4)]
	str	r14, [r0, #((_JB_REG_R14 - _JB_REG_R4) * 4)]
#endif

        mov	r0, #0x00000000
	RET
END(_setjmp)

.L_setjmp_magic:
	.word	_JB_MAGIC__SETJMP

WEAK_ALIAS(___longjmp, _longjmp)
ENTRY(_longjmp)
	ldr	r2, [r0]			/* get magic from jmp_buf */
	ldr	ip, .L_setjmp_magic		/* load magic */
	teq	ip, r2				/* magic correct? */
	bne	botch				/*   no, botch */

#if !defined(_STANDALONE) && __ARM_ARCH >= 6
	add	ip, r0, #(_JB_REG_D8 * 4)
	vldmia	ip, {d8-d15}
	ldr	ip, [r0, #(_JB_REG_FPSCR * 4)]
	vmsr	fpscr, ip
#endif /* !_STANDALONE && __ARM_ARCH >= 6 */

	add	r0, r0, #(_JB_REG_R4 * 4)
       	/* Restore integer registers */
#ifndef __thumb__
        ldmia	r0, {r4-r14}
#else
	ldmia	r0, {r4-r12}
	ldr	r13, [r0, #((_JB_REG_R13 - _JB_REG_R4) * 4)]
	ldr	r14, [r0, #((_JB_REG_R14 - _JB_REG_R4) * 4)]
#endif

	/* Validate sp and r14 */
	teq	sp, #0
	it	ne
	teqne	r14, #0
	it	eq
	beq	botch

	/* Set return value */
	movs	r0, r1
	it	eq
	moveq	r0, #0x00000001
	RET

	/* validation failed, die die die. */
botch:
#if !defined(_STANDALONE)
	bl	PIC_SYM(_C_LABEL(longjmperror), PLT)
	bl	PIC_SYM(_C_LABEL(abort), PLT)
1:	b	1b		/* Cannot get here */
#else
	b	.
#endif
END(_longjmp)

	.section .note.GNU-stack,"",%progbits
