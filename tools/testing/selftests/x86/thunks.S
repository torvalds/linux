/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * thunks.S - assembly helpers for mixed-bitness code
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * These are little helpers that make it easier to switch bitness on
 * the fly.
 */

	.text

	.global call32_from_64
	.type call32_from_64, @function
call32_from_64:
	// rdi: stack to use
	// esi: function to call

	// Save registers
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushfq

	// Switch stacks
	mov %rsp,(%rdi)
	mov %rdi,%rsp

	// Switch to compatibility mode
	pushq $0x23  /* USER32_CS */
	pushq $1f
	lretq

1:
	.code32
	// Call the function
	call *%esi
	// Switch back to long mode
	jmp $0x33,$1f
	.code64

1:
	// Restore the stack
	mov (%rsp),%rsp

	// Restore registers
	popfq
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx

	ret

.size call32_from_64, .-call32_from_64
