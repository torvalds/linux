/*	$OpenBSD: _setjmp.S,v 1.2 2007/03/02 06:11:54 miod Exp $	*/
/*	$NetBSD: _setjmp.S,v 1.7 2006/01/05 02:04:41 uwe Exp $	*/

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
 *	from: @(#)_setjmp.s	5.1 (Berkeley) 4/23/90
 */

#include <machine/asm.h>
#include <machine/setjmp.h>

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the stack.
 * The previous signal state is NOT restored.
 */

ENTRY(_setjmp)
	xor	r0,	r0
#if defined(__SH4__) && !defined(__SH4_NOFPU__)
	add	#(_JBLEN * 4), r4
	sts	fpscr,	r1
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
	mov.l	r0,	@-r4		/* dummy signal mask */
	rts
	 mov.l	r0,	@-r4		/* no saved signal mask */
	SET_ENTRY_SIZE(_setjmp)

ENTRY(_longjmp)
	add	#8,	r4
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
	tst	r0,	r0
	bf	.L0
	add	#1,	r0
.L0:
	rts
	 nop
	SET_ENTRY_SIZE(_longjmp)
