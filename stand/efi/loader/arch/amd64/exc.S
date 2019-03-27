/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship
 * from the FreeBSD Foundation.
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

	.macro	EH	N, err=1
	.align	8
	.globl	EXC\N\()_handler
EXC\N\()_handler:
	.if	\err != 1
	pushq	$0
	.endif
	pushq	%rax
	pushq	%rdx
	pushq	%rcx
	movl	$\N,%ecx
	jmp	all_handlers
	.endm

	.text
	EH	0,0
	EH	1,0
	EH	2,0
	EH	3,0
	EH	4,0
	EH	5,0
	EH	6,0
	EH	7,0
	EH	8
	EH	9,0
	EH	10
	EH	11
	EH	12
	EH	13
	EH	14
	EH	16,0
	EH	17
	EH	18,0
	EH	19,0
	EH	20,0

	.globl	exc_rsp
all_handlers:
	cmpq	%rsp,exc_rsp(%rip)
	je	exception

	/*
	 * Interrupt, not exception.
	 * First, copy the hardware interrupt frame to the previous stack.
	 * Our handler always has private IST stack.
	 */
	movq	(6*8)(%rsp),%rax	/* saved %rsp value, AKA old stack */
	subq	(5*8),%rax
	movq	(3*8)(%rsp),%rdx	/* copy %rip to old stack */
	movq	%rdx,(%rax)
	movq	(4*8)(%rsp),%rdx	/* copy %cs */
	movq	%rdx,(1*8)(%rax)
	movq	(5*8)(%rsp),%rdx	/* copy %rflags */
	movq	%rdx,(2*8)(%rax)
	movq	(6*8)(%rsp),%rdx	/* copy %rsp */
	movq	%rdx,(3*8)(%rax)
	movq	(7*8)(%rsp),%rdx	/* copy %ss */
	movq	%rdx,(4*8)(%rax)

	/*
	 * Now simulate invocation of the original interrupt handler
	 * with retq.  We switch stacks and execute retq from the old
	 * stack since there is no free registers at the last moment.
	 */
	subq	$16,%rax
	leaq	fw_intr_handlers(%rip),%rdx
	movq	(%rdx,%rcx,8),%rdx /* push intr handler address on old stack */
	movq	%rdx,8(%rax)
	movq	(2*8)(%rsp),%rcx   /* saved %rax is put on top of old stack */
	movq	%rcx,(%rax)
	movq	(%rsp),%rcx
	movq	8(%rsp),%rdx

	movq	32(%rsp),%rsp	/* switch to old stack */
	popq	%rax
	retq

exception:
	/*
	 * Form the struct trapframe on our IST stack.
	 * Skip three words, which are currently busy with temporal
	 * saves.
	 */
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%r11
	pushq	%r10
	pushq	%rbp
	pushq	%rbx
	pushq	$0	/* %rax	*/
	pushq	%r9
	pushq	%r8
	pushq	$0	/* %rcx */
	pushq	$0	/* %rdx	*/
	pushq	%rsi
	pushq	%rdi

	/*
	 * Move %rax, %rdx, %rcx values into the final location,
	 * from the three words which were skipped above.
	 */
	movq	0x88(%rsp),%rax
	movq	%rax,0x30(%rsp)	/* tf_rax */
	movq	0x78(%rsp),%rax
	movq	%rax,0x18(%rsp)	/* tf_rcx */
	movq	0x80(%rsp),%rax
	movq	%rax,0x10(%rsp)	/* tf_rdx */

	/*
	 * And fill the three words themself.
	 */
	movq	%cr2,%rax
	movq	%rax,0x80(%rsp)	/* tf_addr */
	movl	%ecx,0x78(%rsp)	/* tf_trapno */
	movw	%ds,0x8e(%rsp)
	movw	%es,0x8c(%rsp)
	movw	%fs,0x7c(%rsp)
	movw	%gs,0x7e(%rsp)
	movw	$0,0x88(%rsp)	/* tf_flags */

	/*
	 * Call dump routine.
	 */
	movq	%rsp,%rdi
	callq	report_exc

	/*
	 * Hang after reporting. Interrupts are already disabled.
	 */
1:
	hlt
	jmp	1b
