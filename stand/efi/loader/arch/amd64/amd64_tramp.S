/*-
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Benno Rice under sponsorship from
 * the FreeBSD Foundation.
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

	.text
	.globl	amd64_tramp

/*
 * void amd64_tramp(uint64_t stack, void *copy_finish, uint64_t kernend,
 *		    uint64_t modulep, uint64_t pagetable, uint64_t entry)
 */
amd64_tramp:
	cli			/* Make sure we don't get interrupted. */
	movq	%rdi,%rsp	/* Switch to our temporary stack. */

	movq	%rdx,%r12	/* Stash the kernel values for later. */
	movq	%rcx,%r13
	movq	%r8,%r14
	movq	%r9,%r15

	callq	*%rsi		/* Call copy_finish so we're all ready to go. */

	pushq	%r12		/* Push kernend. */
	salq	$32,%r13	/* Shift modulep and push it. */
	pushq	%r13
	pushq	%r15		/* Push the entry address. */
	movq	%r14,%cr3	/* Switch page tables. */
	ret			/* "Return" to kernel entry. */

	ALIGN_TEXT
amd64_tramp_end:

	.data
	.globl	amd64_tramp_size
amd64_tramp_size:
	.long	amd64_tramp_end-amd64_tramp
