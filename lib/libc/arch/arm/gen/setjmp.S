/*	$OpenBSD: setjmp.S,v 1.8 2022/05/24 17:21:17 guenther Exp $	*/
/*	$NetBSD: setjmp.S,v 1.5 2003/04/05 23:08:51 bjh21 Exp $	*/

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

#include "SYS.h"
#include <machine/setjmp.h>

	.hidden	__jmpxor

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is restored.
 */

ENTRY(setjmp)
	/* Block all signals and retrieve the old signal mask */
	mov	r2, r0
	mov	r1, #0x00000000
	mov	r0, #0x00000001			/* SIG_BLOCK */
	SYSTRAP(sigprocmask)

	/* Store signal mask */
	str	r0, [r2, #(29 * 4)]

	ldr	r1, .Lsetjmp_magic
	str	r1, [r2], #4

	ldr	r12, .L_jmpxor_setjmp
1:	add	r12, pc, r12			/* r12 = &__jmpxor */
	ldr	r3, [r12], #4			/* r3 = __jmpxor[1] */
	ldr	r12, [r12] 			/* r12 = __jmpxor[0] */
	eor	r12, r13, r12			/* r12 = sp ^ __jmpxor[0] */
	eor	r3, lr, r3			/* r3 = lr ^ __jmpxor[1] */

#ifdef SOFTFLOAT
	add	r2, r2, #68
#else
	/* Store fpcsr */
	vmrs	r1, fpscr
	str	r1, [r2], #4
	/* Store fp registers */
	vstmia	r2!, {d8-d15}
#endif	/* SOFTFLOAT */
	/* Store integer registers */
	stmia	r2, {r3-r12}

	mov	r0, #0x00000000
	mov	r12, r0				/* overwrite __jmpxor copies */
	mov	r3, r0
	mov	pc, lr

.Lsetjmp_magic:
	.word	_JB_MAGIC_SETJMP
.L_jmpxor_setjmp:
	.word	__jmpxor - 1b
END_STRONG(setjmp)


ENTRY(longjmp)
	ldr	r2, .Lsetjmp_magic
	ldr	r3, [r0]
	teq	r2, r3
	bne	.Lbotch

	/* Fetch signal mask and call sigprocmask */
	mov	r3, r0				/* r3 = jmpbuf */
	mov	r2, r1				/* r2 = retvalue */
	ldr	r1, [r0, #(29 * 4)]
	mov	r0, #0x00000003			/* SIG_SETMASK */
	SYSTRAP(sigprocmask)

	add	r3, r3, #4
#ifdef SOFTFLOAT
	add	r3, r3, #68
#else
	/* Restore fpcsr */
	ldr	r4, [r3], #4
	vmsr	fpscr, r4
	/* Restore fp registers */
	vldmia	r3!, {d8-d15}
#endif	/* SOFTFLOAT */
	/* Restore integer registers */
	ldmia	r3, {r3-r12}

	ldr	r0, .L_jmpxor_longjmp
1:	add	r0, pc, r0		/* r0 = &__jmpxor */
	ldr	lr, [r0], #4		/* lr = __jmpxor[1] */
	eor	lr, r3, lr		/* lr ^= jmpbuf[LR] */
	ldr	r0, [r0] 		/* r0 = __jmpxor[0] */
	eor	r13, r0, r12		/* sp = __jmpxor[0] ^ jmpbuf[SP] */
	mov	r12, r2			/* overwrite __jmpxor copies */
	mov	r3, r2

	/* Validate sp and lr */
	teq	sp, #0
	teqne	lr, #0
	beq	.Lbotch

	/* Set return value */
	mov	r0, r12
	teq	r0, #0x00000000
	moveq	r0, #0x00000001
	mov	pc, lr

.L_jmpxor_longjmp:
	.word	__jmpxor - 1b

	/* validation failed, die die die. */
.Lbotch:
	bl	_HIDDEN(abort)
	b	. - 8		/* Cannot get here */
END_STRONG(longjmp)
