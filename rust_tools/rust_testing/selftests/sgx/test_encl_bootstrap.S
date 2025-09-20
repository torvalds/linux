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
	.quad	encl_ssa_tcs1		# OSSA
	.fill	1, 4, 0			# CSSA (set by CPU)
	.fill	1, 4, 1			# NSSA
	.quad	encl_entry		# OENTRY
	.fill	1, 8, 0			# AEP (set by EENTER and ERESUME)
	.fill	1, 8, 0			# OFSBASE
	.fill	1, 8, 0			# OGSBASE
	.fill	1, 4, 0xFFFFFFFF 	# FSLIMIT
	.fill	1, 4, 0xFFFFFFFF	# GSLIMIT
	.fill	4024, 1, 0		# Reserved

	# TCS2
	.fill	1, 8, 0			# STATE (set by CPU)
	.fill	1, 8, 0			# FLAGS
	.quad	encl_ssa_tcs2		# OSSA
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
	# RBX contains the base address for TCS, which is the first address
	# inside the enclave for TCS #1 and one page into the enclave for
	# TCS #2. First make it relative by substracting __encl_base and
	# then add the address of encl_stack to get the address for the stack.
	lea __encl_base(%rip), %rax
	sub %rax, %rbx
	lea encl_stack(%rip), %rax
	add %rbx, %rax
	jmp encl_entry_core
encl_dyn_entry:
	# Entry point for dynamically created TCS page expected to follow
	# its stack directly.
	lea -1(%rbx), %rax
encl_entry_core:
	xchg	%rsp, %rax
	push	%rax

	push	%rcx # push the address after EENTER

	# NOTE: as the selftest enclave is *not* intended for production,
	# simplify the code by not initializing ABI registers on entry or
	# cleansing caller-save registers on exit.
	call	encl_body

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

encl_ssa_tcs1:
	.space 4096
encl_ssa_tcs2:
	.space 4096

	.balign 4096
	# Stack of TCS #1
	.space 4096
encl_stack:
	.balign 4096
	# Stack of TCS #2
	.space 4096
