/*	$OpenBSD: apmcall.S,v 1.7 2022/12/08 01:25:44 guenther Exp $	*/

/*
 * Copyright (c) 2000,2001 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

#ifndef	APM_DISABLE_INTERRUPTS
#define	APM_DISABLE_INTERRUPTS	1
#endif /* APM_DISABLE_INTERRUPTS */

	.data
	.globl	apm_ep
	.globl	apm_cli
apm_cli:
	.long	APM_DISABLE_INTERRUPTS
/*
 * int apmcall(u_int f, int dev, struct apmregs *r)
 *
 */
	.text
ENTRY(apmcall)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi

	movl	16(%ebp), %ebx
	movl	8(%ebx), %ecx
	movl	12(%ebx), %edx
	movl	12(%ebp), %ebx

	pushfl

	cmpl	$0, apm_cli
	je	1f
	cli
1:
#if defined(DEBUG) || defined(DIAGNOSTIC)
	pushl	%ds
	pushl	%es
	pushl	%fs
	pushl	%gs
	xorl	%eax, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %fs
	movl	%eax, %gs
#endif
	movl	8(%ebp), %eax

	clc
	lcall	*%cs:(apm_ep)
	pushl	%eax
	setc	%al
	movzbl	%al, %esi
	popl	%eax

#if defined(DEBUG) || defined(DIAGNOSTIC)
	popl	%gs
	popl	%fs
	popl	%es
	popl	%ds
#endif
	popfl

	movl	16(%ebp), %edi
	movl	%eax, 0(%edi)
	movl	%ebx, 4(%edi)
	movl	%ecx, 8(%edi)
	movl	%edx, 12(%edi)

	testl	$0xff00, %eax
	jnz	1f
	xorl	%esi, %esi
1:
	movl	%esi, %eax

	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret

	.end
