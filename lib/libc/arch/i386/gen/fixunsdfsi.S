/* $OpenBSD: fixunsdfsi.S,v 1.7 2014/11/14 07:31:13 daniel Exp $ */
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
 */

#include <machine/asm.h>

ENTRY(__fixunsdfsi)
	fldl	4(%esp)		/* argument double to accum stack */
	frndint			/* create integer */
#ifdef __PIC__
	PIC_PROLOGUE
	leal	PIC_GOTOFF(fbiggestsigned),%eax
	PIC_EPILOGUE
	fcoml	(%eax)
#else
	fcoml	fbiggestsigned	/* bigger than biggest signed? */
#endif
	fstsw	%ax
	sahf
	jnb	1f

	fistpl	4(%esp)
	movl	4(%esp),%eax
	ret

1:
#ifdef __PIC__
	PIC_PROLOGUE
	leal	PIC_GOTOFF(fbiggestsigned),%eax
	PIC_EPILOGUE
	fsubl	(%eax)
#else
	fsubl	fbiggestsigned	/* reduce for proper conversion */
#endif
	fistpl	4(%esp)		/* convert */
	movl	4(%esp),%eax
	orl	$0x80000000,%eax	/* restore bias */
	ret

fbiggestsigned:	.double	2147483648.0
