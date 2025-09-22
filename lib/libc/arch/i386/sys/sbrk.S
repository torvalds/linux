/*	$OpenBSD: sbrk.S,v 1.16 2023/12/10 16:45:51 deraadt Exp $ */
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

#include "SYS.h"

	.globl	_end
	.globl	__curbrk
	.hidden	__curbrk

	.data
__curbrk:	.long	_end
	END(__curbrk)
	.type	__curbrk,@object

ENTRY_NB(sbrk)
#ifdef __PIC__
	movl	4(%esp),%ecx
	PIC_PROLOGUE
	movl	PIC_GOT(__curbrk),%edx
	PIC_EPILOGUE
	movl	(%edx),%eax
	addl	%eax,4(%esp)
	movl	$(SYS_break),%eax
99:	int	$0x80
	PINSYSCALL(SYS_break, 99b)
	jc	2f
	PIC_PROLOGUE
	movl	PIC_GOT(__curbrk),%edx
	PIC_EPILOGUE
	movl	(%edx),%eax
	addl	%ecx,(%edx)

#else

	movl	4(%esp),%ecx
	movl	__curbrk,%eax
	addl	%eax,4(%esp)
	movl	$(SYS_break),%eax
98:	int	$0x80
	PINSYSCALL(SYS_break, 98b)
	jc	2f
	movl	__curbrk,%eax
	addl	%ecx,__curbrk
#endif
	ret
2:
	SET_ERRNO()
	ret
END(sbrk)
	.weak	sbrk
