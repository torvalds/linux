/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <machine/asmacros.h>

#include "assym.inc"

	.text
ENTRY(efi_rt_arch_call)
	pushq	%rbp
	movq	%rsp, %rbp

	movq	%rbx, EC_RBX(%rdi)
	movq	%rsp, EC_RSP(%rdi)
	movq	%rbp, EC_RBP(%rdi)
	movq	%r12, EC_R12(%rdi)
	movq	%r13, EC_R13(%rdi)
	movq	%r14, EC_R14(%rdi)
	movq	%r15, EC_R15(%rdi)
	movq	PCPU(CURTHREAD), %rax
	movq	%rdi, TD_MD+MD_EFIRT_TMP(%rax)
	movq	PCPU(CURPCB), %rsi

	movl	EC_ARGCNT(%rdi), %ecx
	movl	%ecx, %ebx
	movl	$4, %eax
	cmpl	%eax, %ecx
	cmovbl	%eax, %ecx
	shll	$3, %ecx
	subq	%rcx, %rsp

	cmpl	$0, %ebx
	jz	1f
	movq	EC_ARG1(%rdi), %rcx
	decl	%ebx
	jz	1f
	movq	EC_ARG2(%rdi), %rdx
	decl	%ebx
	jz	1f
	movq	EC_ARG3(%rdi), %r8
	decl	%ebx
	jz	1f
	movq	EC_ARG4(%rdi), %r9
	decl	%ebx
	jz	1f
	movq	EC_ARG5(%rdi), %rax
	movq	%rax, 4*8(%rsp)
	decl	%ebx
	jz	1f
	movq	$efi_rt_panic_str, %rdi
	call	panic
1:	movq	EC_FPTR(%rdi), %rax
	movq	$efi_rt_fault, PCB_ONFAULT(%rsi)
	callq	*%rax

	movq	PCPU(CURTHREAD), %rbx
	movq	TD_MD+MD_EFIRT_TMP(%rbx), %rdi
	movq	%rax, EC_EFI_STATUS(%rdi)
	movq	PCPU(CURPCB), %rsi
	xorl	%eax, %eax
	movq	%rax, PCB_ONFAULT(%rsi)

efi_rt_arch_call_tail:
	movq	EC_R15(%rdi), %r15
	movq	EC_R14(%rdi), %r14
	movq	EC_R13(%rdi), %r13
	movq	EC_R12(%rdi), %r12
	movq	EC_RBP(%rdi), %rbp
	movq	EC_RSP(%rdi), %rsp
	movq	EC_RBX(%rdi), %rbx

	popq	%rbp
	ret
END(efi_rt_arch_call)

ENTRY(efi_rt_fault)
	xorl	%eax, %eax
	movq	PCPU(CURPCB), %rsi
	movq	%rax, PCB_ONFAULT(%rsi)
	movl	$EFAULT, %eax
	movq	PCPU(CURTHREAD), %rbx
	movq	TD_MD+MD_EFIRT_TMP(%rbx), %rdi
	jmp	efi_rt_arch_call_tail
END(efi_rt_fault)

efi_rt_panic_str:	.asciz	"efi_rt_arch_call: too many args"
