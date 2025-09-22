/*	$OpenBSD: brk.S,v 1.11 2023/12/10 16:45:51 deraadt Exp $ */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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

	.globl	__curbrk

	.data
__minbrk:
	PTR_VAL	_end
	.size	__minbrk, . - __minbrk
	.type	__minbrk,@object
	.text

FRAMESZ= MKFSIZ(1,1)
GPOFF= FRAMESZ-2*REGSZ

LEAF(brk, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, brk)
	.set	reorder
	PTR_L	v0, __minbrk
	bgtu	a0, v0, 1f
	
	move	a0, v0		# dont allow break < minbrk
1:
	li	v0, SYS_break
99:	syscall
	PINSYSCALL(SYS_break, 99b)
	bne	a3, zero, 2f
	PTR_S	a0, __curbrk
	move	v0, zero
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	j	ra
2:
	LA	t9, CERROR
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	jr	t9
END(brk)
	.weak	brk
