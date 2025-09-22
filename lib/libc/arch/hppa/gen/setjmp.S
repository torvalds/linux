/*	$OpenBSD: setjmp.S,v 1.12 2023/12/10 16:45:51 deraadt Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "SYS.h"

	.section	.openbsd.randomdata,"aw",@progbits
	.align	4
.L__jmpxor
	.zero	4*2		; (r2/rp, r30/sp)

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from
 * the last call to
 *	setjmp(a)
 * by restoring registers from the stack,
 * and a struct sigcontext, see <signal.h>
 */

ENTRY(sigsetjmp, 64)
	sub,<>	%r0, %arg1, %r0
	b	.L_setjmp$dosaves
	 stw	%arg1, 44(%arg0)	; last of reserved words

ALTENTRY(setjmp)
	copy	%arg0, %arg2		; save jmpbuf in %arg2

	ldi	1, %arg0		; how = SIG_BLOCK
	copy	%r0, %arg1		; set = empty
	stw	%rp, HPPA_FRAME_ERP(%sr0,%sp)
	ldil	L%SYSCALLGATE, %r1
99:	ble	4(%sr7, %r1)
	PINSYSCALL(SYS_sigprocmask, 99b)
	 ldi	SYS_sigprocmask, %t1
	ldw	HPPA_FRAME_ERP(%sr0,%sp), %rp

	copy	%arg2, %arg0		; restore jmpbuf to %arg0
	stw	%ret0, 4(%arg0)		; mask

ALTENTRY(_setjmp)
.L_setjmp$dosaves
#ifdef __PIC__
	addil	LT%.L__jmpxor, %r19
	ldw	RT%.L__jmpxor(%r1), %t2
#else
	ldil	L%.L__jmpxor, %t1
	ldo	R%.L__jmpxor(%t1), %t2
#endif
	ldw	0(%t2), %t1		; xor for r2/rp
	ldw	4(%t2), %t2		; xor for r30/sp, overwrite address

	xor	%sp, %t2, %t2
	stw	%t2, 12(%arg0)		; sc.sc_sp = %sp
	xor	%rp, %t1, %t1
	stw	%t1, 20(%arg0)		; sc.sc_pcoqh = %rp
	copy	%r0, %t1
	copy	%r0, %t2

	/* We store all callee-saved registers after the sigcontext. */
	ldo	48(%arg0), %r1		; offset to after sc
	stwm	%r3, 4(%r1)
	stwm	%r4, 4(%r1)
	stwm	%r5, 4(%r1)
	stwm	%r6, 4(%r1)
	stwm	%r7, 4(%r1)
	stwm	%r8, 4(%r1)
	stwm	%r9, 4(%r1)
	stwm	%r10, 4(%r1)
	stwm	%r11, 4(%r1)
	stwm	%r12, 4(%r1)
	stwm	%r13, 4(%r1)
	stwm	%r14, 4(%r1)
	stwm	%r15, 4(%r1)
	stwm	%r16, 4(%r1)
	stwm	%r17, 4(%r1)
	stwm	%r18, 4(%r1)

	/* Return 0. */
	bv	%r0(%rp)
	 copy	%r0, %ret0
EXIT(sigsetjmp)
	.size _setjmp, . - _setjmp
	.size setjmp, . - setjmp

ENTRY(siglongjmp,64)
	ldw	44(%arg0), %arg2
	sub,<>	%r0, %arg2, %r0
	b	.L_longjmp$restores
	 nop

ALTENTRY(longjmp)
	copy	%arg1, %arg3		; save val in %arg3
	copy	%arg0, %arg2		; save jmpbuf in %arg2

	ldw	4(%arg0), %arg1		; set from jumpbuf
	ldi	3, %arg0		; how = SIG_SETMASK
	stw	%rp, HPPA_FRAME_ERP(%sr0,%sp)
	ldil	L%SYSCALLGATE, %r1
98:	ble	4(%sr7, %r1)
	PINSYSCALL(SYS_sigprocmask, 98b)
	 ldi	SYS_sigprocmask, %t1
	ldw	HPPA_FRAME_ERP(%sr0,%sp), %rp

	copy	%arg2, %arg0		; restore jmpbuf to %arg0
	copy	%arg3, %arg1		; restore val to %arg1

ALTENTRY(_longjmp)
.L_longjmp$restores
	/* restore callee-saved registers */
	ldo	48(%arg0), %r1
	ldwm	4(%r1), %r3
	ldwm	4(%r1), %r4
	ldwm	4(%r1), %r5
	ldwm	4(%r1), %r6
	ldwm	4(%r1), %r7
	ldwm	4(%r1), %r8
	ldwm	4(%r1), %r9
	ldwm	4(%r1), %r10
	ldwm	4(%r1), %r11
	ldwm	4(%r1), %r12
	ldwm	4(%r1), %r13
	ldwm	4(%r1), %r14
	ldwm	4(%r1), %r15
	ldwm	4(%r1), %r16
	ldwm	4(%r1), %r17
	ldwm	4(%r1), %r18

	/* restore the rest */
#ifdef __PIC__
	addil	LT%.L__jmpxor, %r19
	ldw	RT%.L__jmpxor(%r1), %t2
#else
	ldil	L%.L__jmpxor, t1
	ldo	R%.L__jmpxor(t1), t2
#endif
	ldw	0(t2), t1		; xor for r2/rp
	ldw	4(t2), t2		; xor for r30/sp, overwrite address

	ldw	12(%arg0), %r1
	xor	%r1, %t2, %sp
	ldw	20(%arg0), %r1
	xor	%r1, %t1, %rp
	copy	%r0, %t1		; overwrite the cookies
	copy	%r0, %t2
	sub,<>	%arg1, %r0, %r0
	ldo	1(%arg1), %arg1
	bv	%r0(%rp)
	 copy	%arg1, %ret0
EXIT(siglongjmp)
	.size _longjmp, . - _longjmp
	.size longjmp, . - longjmp

	.end
