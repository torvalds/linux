/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * thunks_32.S - assembly helpers for mixed-bitness code
 * Copyright (c) 2015 Denys Vlasenko
 *
 * These are little helpers that make it easier to switch bitness on
 * the fly.
 */

	.text
	.code32

	.global call64_from_32
	.type call32_from_64, @function

	// 4(%esp): function to call
call64_from_32:
	// Fetch function address
	mov	4(%esp), %eax

	// Save registers which are callee-clobbered by 64-bit ABI
	push	%ecx
	push	%edx
	push	%esi
	push	%edi

	// Switch to long mode
	jmp	$0x33,$1f
1:	.code64

	// Call the function
	call	*%rax

	// Switch to compatibility mode
	push	$0x23  /* USER32_CS */
	.code32; push $1f; .code64 /* hack: can't have X86_64_32S relocation in 32-bit ELF */
	lretq
1:	.code32

	pop	%edi
	pop	%esi
	pop	%edx
	pop	%ecx

	ret

.size call64_from_32, .-call64_from_32
