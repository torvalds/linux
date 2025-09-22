/*	$OpenBSD: _setjmp.S,v 1.8 2022/05/25 17:32:36 guenther Exp $	*/
/*	$NetBSD: _setjmp.S,v 1.5 2003/04/05 23:08:51 bjh21 Exp $	*/

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

#include "DEFS.h"
#include <machine/setjmp.h>

	.section	.openbsd.randomdata,"aw",%progbits
	.balign	4
	.globl	__jmpxor
__jmpxor:
	.zero 4*2		/* (sp, lr) */
	END(__jmpxor)
	.type	__jmpxor,%object


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
 *       r1-r3 are scratch registers in functions
 */

ENTRY(_setjmp)
	ldr	r1, .L_setjmp_magic
	str	r1, [r0], #4
	ldr	r2, .L_jmpxor_setjmp
1:	add	r2, pc, r2			/* r2 = &__jmpxor */
	ldr	r3, [r2], #4			/* r3 = __jmpxor[1] */
	ldr	r2, [r2] 			/* r2 = __jmpxor[0] */
	eor	r2, r13, r2			/* r2 = sp ^ __jmpxor[0] */
	eor	r3, lr, r3			/* r3 = lr ^ __jmpxor[1] */

#ifdef SOFTFLOAT
	add	r0, r0, #68
#else
	/* Store fpcsr */
	vmrs	r1, fpscr
	str	r1, [r0], #4
	/* Store fp registers */
	vstmia	r0!, {d8-d15}
#endif	/* SOFTFLOAT */
	/* Store integer registers */
	stmia	r0, {r2-r11}

	mov	r0, #0x00000000
	mov	r2, r0				/* overwrite __jmpxor copies */
	mov	r3, r0
	mov	pc, lr

.L_setjmp_magic:
	.word	_JB_MAGIC__SETJMP

.L_jmpxor_setjmp:
	.word	__jmpxor - 1b
END_STRONG(_setjmp)

ENTRY(_longjmp)
	ldr	r2, .L_setjmp_magic
	ldr	r3, [r0], #4
	teq	r2, r3
	bne	.Lbotch

#ifdef SOFTFLOAT
	add	r0, r0, #68
#else
	/* Restore fpcsr */
	ldr	r4, [r0], #4
	vmsr	fpscr, r4
	/* Restore fp registers */
	vldmia	r0!, {d8-d15}
#endif	/* SOFTFLOAT */
	/* Restore integer registers */
	ldmia	r0, {r2-r11}

	ldr	r0, .L_jmpxor_longjmp
1:	add	r0, pc, r0			/* r0 = &__jmpxor */
	ldr	lr, [r0], #4			/* lr = __jmpxor[1] */
	eor	lr, r3, lr			/* lr ^= jmpbuf[LR] */
	ldr	r0, [r0] 			/* r0 = __jmpxor[0] */
	eor	r13, r0, r2		/* sp = __jmpxor[0] ^ jmpbuf[SP] */
	mov	r2, r1				/* overwrite __jmpxor copies */
	mov	r3, r1

	/* Validate sp and lr */
	teq	sp, #0
	teqne	lr, #0
	beq	.Lbotch

	/* Set return value */
	mov	r0, r1
	teq	r0, #0x00000000
	moveq	r0, #0x00000001
	mov	pc, lr

.L_jmpxor_longjmp:
	.word	__jmpxor - 1b

	/* validation failed, die die die. */
.Lbotch:
	bl	_HIDDEN(abort)
	b	. - 8		/* Cannot get here */
END_STRONG(_longjmp)
