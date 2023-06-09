/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2002 Andi Kleen */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/export.h>

.section .noinstr.text, "ax"

/*
 * memcpy - Copy a memory block.
 *
 * Input:
 *  rdi destination
 *  rsi source
 *  rdx count
 *
 * Output:
 * rax original destination
 *
 * The FSRM alternative should be done inline (avoiding the call and
 * the disgusting return handling), but that would require some help
 * from the compiler for better calling conventions.
 *
 * The 'rep movsb' itself is small enough to replace the call, but the
 * two register moves blow up the code. And one of them is "needed"
 * only for the return value that is the same as the source input,
 * which the compiler could/should do much better anyway.
 */
SYM_TYPED_FUNC_START(__memcpy)
	ALTERNATIVE "jmp memcpy_orig", "", X86_FEATURE_FSRM

	movq %rdi, %rax
	movq %rdx, %rcx
	rep movsb
	RET
SYM_FUNC_END(__memcpy)
EXPORT_SYMBOL(__memcpy)

SYM_FUNC_ALIAS(memcpy, __memcpy)
EXPORT_SYMBOL(memcpy)

SYM_FUNC_START_LOCAL(memcpy_orig)
	movq %rdi, %rax

	cmpq $0x20, %rdx
	jb .Lhandle_tail

	/*
	 * We check whether memory false dependence could occur,
	 * then jump to corresponding copy mode.
	 */
	cmp  %dil, %sil
	jl .Lcopy_backward
	subq $0x20, %rdx
.Lcopy_forward_loop:
	subq $0x20,	%rdx

	/*
	 * Move in blocks of 4x8 bytes:
	 */
	movq 0*8(%rsi),	%r8
	movq 1*8(%rsi),	%r9
	movq 2*8(%rsi),	%r10
	movq 3*8(%rsi),	%r11
	leaq 4*8(%rsi),	%rsi

	movq %r8,	0*8(%rdi)
	movq %r9,	1*8(%rdi)
	movq %r10,	2*8(%rdi)
	movq %r11,	3*8(%rdi)
	leaq 4*8(%rdi),	%rdi
	jae  .Lcopy_forward_loop
	addl $0x20,	%edx
	jmp  .Lhandle_tail

.Lcopy_backward:
	/*
	 * Calculate copy position to tail.
	 */
	addq %rdx,	%rsi
	addq %rdx,	%rdi
	subq $0x20,	%rdx
	/*
	 * At most 3 ALU operations in one cycle,
	 * so append NOPS in the same 16 bytes trunk.
	 */
	.p2align 4
.Lcopy_backward_loop:
	subq $0x20,	%rdx
	movq -1*8(%rsi),	%r8
	movq -2*8(%rsi),	%r9
	movq -3*8(%rsi),	%r10
	movq -4*8(%rsi),	%r11
	leaq -4*8(%rsi),	%rsi
	movq %r8,		-1*8(%rdi)
	movq %r9,		-2*8(%rdi)
	movq %r10,		-3*8(%rdi)
	movq %r11,		-4*8(%rdi)
	leaq -4*8(%rdi),	%rdi
	jae  .Lcopy_backward_loop

	/*
	 * Calculate copy position to head.
	 */
	addl $0x20,	%edx
	subq %rdx,	%rsi
	subq %rdx,	%rdi
.Lhandle_tail:
	cmpl $16,	%edx
	jb   .Lless_16bytes

	/*
	 * Move data from 16 bytes to 31 bytes.
	 */
	movq 0*8(%rsi), %r8
	movq 1*8(%rsi),	%r9
	movq -2*8(%rsi, %rdx),	%r10
	movq -1*8(%rsi, %rdx),	%r11
	movq %r8,	0*8(%rdi)
	movq %r9,	1*8(%rdi)
	movq %r10,	-2*8(%rdi, %rdx)
	movq %r11,	-1*8(%rdi, %rdx)
	RET
	.p2align 4
.Lless_16bytes:
	cmpl $8,	%edx
	jb   .Lless_8bytes
	/*
	 * Move data from 8 bytes to 15 bytes.
	 */
	movq 0*8(%rsi),	%r8
	movq -1*8(%rsi, %rdx),	%r9
	movq %r8,	0*8(%rdi)
	movq %r9,	-1*8(%rdi, %rdx)
	RET
	.p2align 4
.Lless_8bytes:
	cmpl $4,	%edx
	jb   .Lless_3bytes

	/*
	 * Move data from 4 bytes to 7 bytes.
	 */
	movl (%rsi), %ecx
	movl -4(%rsi, %rdx), %r8d
	movl %ecx, (%rdi)
	movl %r8d, -4(%rdi, %rdx)
	RET
	.p2align 4
.Lless_3bytes:
	subl $1, %edx
	jb .Lend
	/*
	 * Move data from 1 bytes to 3 bytes.
	 */
	movzbl (%rsi), %ecx
	jz .Lstore_1byte
	movzbq 1(%rsi), %r8
	movzbq (%rsi, %rdx), %r9
	movb %r8b, 1(%rdi)
	movb %r9b, (%rdi, %rdx)
.Lstore_1byte:
	movb %cl, (%rdi)

.Lend:
	RET
SYM_FUNC_END(memcpy_orig)

