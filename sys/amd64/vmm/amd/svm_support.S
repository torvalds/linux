/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013, Anish Gupta (akgupt3@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <machine/asmacros.h>

#include "svm_assym.h"

/*
 * Be friendly to DTrace FBT's prologue/epilogue pattern matching.
 *
 * They are also responsible for saving/restoring the host %rbp across VMRUN.
 */
#define	VENTER  push %rbp ; mov %rsp,%rbp
#define	VLEAVE  pop %rbp

#define	VMLOAD	.byte 0x0f, 0x01, 0xda
#define	VMRUN	.byte 0x0f, 0x01, 0xd8
#define	VMSAVE	.byte 0x0f, 0x01, 0xdb

/*
 * svm_launch(uint64_t vmcb, struct svm_regctx *gctx, struct pcpu *pcpu)
 * %rdi: physical address of VMCB
 * %rsi: pointer to guest context
 * %rdx: pointer to the pcpu data
 */
ENTRY(svm_launch)
	VENTER

	/* save pointer to the pcpu data */
	push %rdx

	/*
	 * Host register state saved across a VMRUN.
	 *
	 * All "callee saved registers" except:
	 * %rsp: because it is preserved by the processor across VMRUN.
	 * %rbp: because it is saved/restored by the function prologue/epilogue.
	 */
	push %rbx
	push %r12
	push %r13
	push %r14
	push %r15

	/* Save the physical address of the VMCB in %rax */
	movq %rdi, %rax

	push %rsi		/* push guest context pointer on the stack */

	/*
	 * Restore guest state.
	 */
	movq SCTX_R8(%rsi), %r8
	movq SCTX_R9(%rsi), %r9
	movq SCTX_R10(%rsi), %r10
	movq SCTX_R11(%rsi), %r11
	movq SCTX_R12(%rsi), %r12
	movq SCTX_R13(%rsi), %r13
	movq SCTX_R14(%rsi), %r14
	movq SCTX_R15(%rsi), %r15
	movq SCTX_RBP(%rsi), %rbp
	movq SCTX_RBX(%rsi), %rbx
	movq SCTX_RCX(%rsi), %rcx
	movq SCTX_RDX(%rsi), %rdx
	movq SCTX_RDI(%rsi), %rdi
	movq SCTX_RSI(%rsi), %rsi	/* %rsi must be restored last */

	VMLOAD
	VMRUN
	VMSAVE

	pop %rax		/* pop guest context pointer from the stack */

	/*
	 * Save guest state.
	 */
	movq %r8, SCTX_R8(%rax)
	movq %r9, SCTX_R9(%rax)
	movq %r10, SCTX_R10(%rax)
	movq %r11, SCTX_R11(%rax)
	movq %r12, SCTX_R12(%rax)
	movq %r13, SCTX_R13(%rax)
	movq %r14, SCTX_R14(%rax)
	movq %r15, SCTX_R15(%rax)
	movq %rbp, SCTX_RBP(%rax)
	movq %rbx, SCTX_RBX(%rax)
	movq %rcx, SCTX_RCX(%rax)
	movq %rdx, SCTX_RDX(%rax)
	movq %rdi, SCTX_RDI(%rax)
	movq %rsi, SCTX_RSI(%rax)

	/*
	 * To prevent malicious branch target predictions from
	 * affecting the host, overwrite all entries in the RSB upon
	 * exiting a guest.
	 */
	mov $16, %ecx	/* 16 iterations, two calls per loop */
	mov %rsp, %rax
0:	call 2f		/* create an RSB entry. */
1:	pause
	call 1b		/* capture rogue speculation. */
2:	call 2f		/* create an RSB entry. */
1:	pause
	call 1b		/* capture rogue speculation. */
2:	sub $1, %ecx
	jnz 0b
	mov %rax, %rsp

	/* Restore host state */
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbx

	/* Restore %GS.base to point to the host's pcpu data */
	pop %rdx
	mov %edx, %eax
	shr $32, %rdx
	mov $MSR_GSBASE, %rcx
	wrmsr

	/*
	 * Clobber the remaining registers with guest contents so they
	 * can't be misused.
	 */
	xor %rbp, %rbp
	xor %rdi, %rdi
	xor %rsi, %rsi
	xor %r8, %r8
	xor %r9, %r9
	xor %r10, %r10
	xor %r11, %r11

	VLEAVE
	ret
END(svm_launch)
