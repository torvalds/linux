/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2016-20 Intel Corporation.
 */

	.macro ENCLU
	.byte 0x0f, 0x01, 0xd7
	.endm

	.section ".tcs", "aw"
	.balign	4096

	.fill	1, 8, 0			# STATE (set by CPU)
	.fill	1, 8, 0			# FLAGS
	.quad	encl_ssa		# OSSA
	.fill	1, 4, 0			# CSSA (set by CPU)
	.fill	1, 4, 1			# NSSA
	.quad	encl_entry		# OENTRY
	.fill	1, 8, 0			# AEP (set by EENTER and ERESUME)
	.fill	1, 8, 0			# OFSBASE
	.fill	1, 8, 0			# OGSBASE
	.fill	1, 4, 0xFFFFFFFF 	# FSLIMIT
	.fill	1, 4, 0xFFFFFFFF	# GSLIMIT
	.fill	4024, 1, 0		# Reserved

	# Identical to the previous TCS.
	.fill	1, 8, 0			# STATE (set by CPU)
	.fill	1, 8, 0			# FLAGS
	.quad	encl_ssa		# OSSA
	.fill	1, 4, 0			# CSSA (set by CPU)
	.fill	1, 4, 1			# NSSA
	.quad	encl_entry		# OENTRY
	.fill	1, 8, 0			# AEP (set by EENTER and ERESUME)
	.fill	1, 8, 0			# OFSBASE
	.fill	1, 8, 0			# OGSBASE
	.fill	1, 4, 0xFFFFFFFF 	# FSLIMIT
	.fill	1, 4, 0xFFFFFFFF	# GSLIMIT
	.fill	4024, 1, 0		# Reserved

	.text

encl_entry:
	# RBX contains the base address for TCS, which is also the first address
	# inside the enclave. By adding the value of le_stack_end to it, we get
	# the absolute address for the stack.
	lea	(encl_stack)(%rbx), %rax
	xchg	%rsp, %rax
	push	%rax

	push	%rcx # push the address after EENTER
	push	%rbx # push the enclave base address

	call	encl_body

	pop	%rbx # pop the enclave base address

	/* Clear volatile GPRs, except RAX (EEXIT function). */
	xor     %rcx, %rcx
	xor     %rdx, %rdx
	xor     %rdi, %rdi
	xor     %rsi, %rsi
	xor     %r8, %r8
	xor     %r9, %r9
	xor     %r10, %r10
	xor     %r11, %r11

	# Reset status flags.
	add     %rdx, %rdx # OF = SF = AF = CF = 0; ZF = PF = 1

	# Prepare EEXIT target by popping the address of the instruction after
	# EENTER to RBX.
	pop	%rbx

	# Restore the caller stack.
	pop	%rax
	mov	%rax, %rsp

	# EEXIT
	mov	$4, %rax
	enclu

	.section ".data", "aw"

encl_ssa:
	.space 4096

	.balign 4096
	.space 8192
encl_stack:
