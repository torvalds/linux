/* $OpenBSD: sigsetjmp.S,v 1.6 2016/05/08 21:50:58 guenther Exp $ */
/*-
 * Copyright (c) 1991, 1993, 1995,
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Havard Eidnes.
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

#include <machine/asm.h>
#include <machine/regnum.h>
#include <machine/setjmp.h>

/*
 * trampolines for sigsetjmp and  siglongjmp save and restore mask.
 *
 */
FRAMESZ= MKFSIZ(1,1)
GPOFF= FRAMESZ-2*REGSZ

LEAF(sigsetjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, sigsetjmp)
	.set	reorder
	REG_S	a1, (_JBLEN*REGSZ)(a0)		# save "savemask"
	bne	a1, 0x0, 1f			# do saving of signal mask?
	LA	t9, _libc__setjmp
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr t9

1:	LA	t9, _libc_setjmp
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr t9
END(sigsetjmp)

LEAF(siglongjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, siglongjmp)
	.set	reorder
	REG_L	t0, (_JBLEN*REGSZ)(a0)		# get "savemask"
	bne	t0, 0x0, 1f			# restore signal mask?
	LA	t9, _libc__longjmp
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr	t9
1:
	LA	t9, _libc_longjmp
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr	t9
END(siglongjmp)
