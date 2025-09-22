/*	$OpenBSD: sigsetjmp.S,v 1.4 2022/12/08 02:11:27 guenther Exp $	*/
/*	$NetBSD: sigsetjmp.S,v 1.9 2006/01/05 19:21:37 uwe Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *
 *	from: @(#)setjmp.s	5.1 (Berkeley) 4/23/90
 */

#include "SYS.h"

#include <machine/setjmp.h>

ENTRY(sigsetjmp)
	tst	r5,	r5		/* if (savemask == 0) */
	bt	2f

	/* identical to setjmp */
	PIC_PROLOGUE(.L_got_1)
	sts.l	pr,	@-sp
	mov.l	r4,	@-sp
	mov.l	r5,	@-sp

	mov.l	.L_sigprocmask_1, r0
	mov	r4,	r6
	mov	#1,	r4		/* how = SIG_BLOCK */
	mov	#0,	r5		/* new = NULL */
1:	CALL	r0
	 add	#4,	r6		/* old = &sigmask */

	mov.l	@sp+,	r5
	mov.l	@sp+,	r4
	lds.l	@sp+,	pr
	PIC_EPILOGUE

2:	/* identical to _setjmp except that first word is in r5 */
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
	add	#(_JBLEN * 4), r4
	sts	fpscr,	r1
	xor	r0,	r0
	mov.l	r1,	@-r4
	lds	r0,	fpscr
	sts.l	fpul,	@-r4
	fmov.s	fr15,	@-r4
	fmov.s	fr14,	@-r4
	fmov.s	fr13,	@-r4
	fmov.s	fr12,	@-r4
	frchg
	fmov.s	fr15,	@-r4
	fmov.s	fr14,	@-r4
	fmov.s	fr13,	@-r4
	fmov.s	fr12,	@-r4
	lds	r1,	fpscr
#else
	add	#((_JBLEN - 10) * 4), r4
#endif
	sts.l	mach,	@-r4
	sts.l	macl,	@-r4
	mov.l	r15,	@-r4
	mov.l	r14,	@-r4
	mov.l	r13,	@-r4
	mov.l	r12,	@-r4
	mov.l	r11,	@-r4
	mov.l	r10,	@-r4
	mov.l	r9,	@-r4
	mov.l	r8,	@-r4
	sts.l	pr,	@-r4
	add	#-4,	r4		/* skip signal mask */
	mov.l	r5,	@-r4		/* has signal mask? */
	rts
	 xor	r0,	r0

	.align	2
.L_got_1:		PIC_GOT_DATUM
.L_sigprocmask_1:	CALL_DATUM(_HIDDEN(sigprocmask), 1b)
	SET_ENTRY_SIZE(sigsetjmp)

ENTRY(siglongjmp)
	mov.l	@r4+,	r0
	tst	r0,	r0
	bt	2f			/* if no mask */

	/* identical to longjmp */
	/* we won't return here, so we don't need to save pr and r12 */
	PIC_PROLOGUE_NOSAVE(.L_got_2)
	mov.l	r5,	@-sp
	mov.l	r4,	@-sp

	mov.l	.L_sigprocmask_2, r0
	mov	r4,	r5		/* new = &sigmask */
	mov	#3,	r4		/* how = SIG_SETMASK */
1:	CALL	r0
	 mov	#0,	r6		/* old = NULL */

	mov.l	@sp+,	r4
	mov.l	@sp+,	r5

2:	/* identical to _longjmp */
	add	#4,	r4
	lds.l	@r4+,	pr
	mov.l	@r4+,	r8
	mov.l	@r4+,	r9
	mov.l	@r4+,	r10
	mov.l	@r4+,	r11
	mov.l	@r4+,	r12
	mov.l	@r4+,	r13
	mov.l	@r4+,	r14
	mov.l	@r4+,	r15
	lds.l	@r4+,	macl
	lds.l	@r4+,	mach
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
	xor	r0, 	r0
	lds	r0,	fpscr
	frchg
	fmov.s	@r4+,	fr12
	fmov.s	@r4+,	fr13
	fmov.s	@r4+,	fr14
	fmov.s	@r4+,	fr15
	frchg
	fmov.s	@r4+,	fr12
	fmov.s	@r4+,	fr13
	fmov.s	@r4+,	fr14
	fmov.s	@r4+,	fr15
	lds.l	@r4+,	fpul
	lds.l	@r4+,	fpscr
#endif

	mov	r5,	r0
	tst	r0,	r0	/* make sure return value is non-zero */
	bf	.L0
	add	#1,	r0
.L0:
	rts
	 nop

	.align	2
.L_got_2:		PIC_GOT_DATUM
.L_sigprocmask_2:	CALL_DATUM(_HIDDEN(sigprocmask), 1b)
	SET_ENTRY_SIZE(siglongjmp)
