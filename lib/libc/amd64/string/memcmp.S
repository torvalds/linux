/*-
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mateusz Guzik <mjg@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

ENTRY(memcmp)
	cmpq	$16,%rdx
	jae	5f
1:
	testq	%rdx,%rdx
	je	3f
	xorl	%ecx,%ecx
2:
	movzbl	(%rdi,%rcx,1),%eax
	movzbl	(%rsi,%rcx,1),%r8d
	cmpb	%r8b,%al
	jne	4f
	addq	$1,%rcx
	cmpq	%rcx,%rdx
	jz	3f
	movzbl	(%rdi,%rcx,1),%eax
	movzbl	(%rsi,%rcx,1),%r8d
	cmpb	%r8b,%al
	jne	4f
	addq	$1,%rcx
	cmpq	%rcx,%rdx
	jz	3f
	movzbl	(%rdi,%rcx,1),%eax
	movzbl	(%rsi,%rcx,1),%r8d
	cmpb	%r8b,%al
	jne	4f
	addq	$1,%rcx
	cmpq	%rcx,%rdx
	jz	3f
	movzbl	(%rdi,%rcx,1),%eax
	movzbl	(%rsi,%rcx,1),%r8d
	cmpb	%r8b,%al
	jne	4f
	addq	$1,%rcx
	cmpq	%rcx,%rdx
	jne	2b
3:
	xorl	%eax,%eax
	ret
4:
	subl	%r8d,%eax
	ret
5:
	cmpq	$32,%rdx
	jae	7f
6:
	/*
	 * 8 bytes
	 */
	movq	(%rdi),%r8
	movq	(%rsi),%r9
	cmpq	%r8,%r9
	jne	1b
	leaq	8(%rdi),%rdi
	leaq	8(%rsi),%rsi
	subq	$8,%rdx
	cmpq	$8,%rdx
	jae	6b
	jl	1b
	jmp	3b
7:
	/*
	 * 32 bytes
	 */
	movq	(%rsi),%r8
	movq	8(%rsi),%r9
	subq	(%rdi),%r8
	subq	8(%rdi),%r9
	or	%r8,%r9
	jnz	1b

	movq	16(%rsi),%r8
	movq	24(%rsi),%r9
	subq	16(%rdi),%r8
	subq	24(%rdi),%r9
	or	%r8,%r9
	jnz	1b

	leaq	32(%rdi),%rdi
	leaq	32(%rsi),%rsi
	subq	$32,%rdx
	cmpq	$32,%rdx
	jae	7b
	jnz	1b
	jmp	3b
END(memcmp)

	.section .note.GNU-stack,"",%progbits
