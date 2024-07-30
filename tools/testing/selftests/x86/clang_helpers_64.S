/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 64-bit assembly helpers for asm operations that lack support in both gcc and
 * clang. For example, clang asm does not support segment prefixes.
 */
.global dereference_seg_base

dereference_seg_base:
	mov %gs:(0), %rax
	ret

.global test_page
.global test_syscall_insn

.pushsection ".text", "ax"
.balign 4096
test_page: .globl test_page
	.fill 4094,1,0xcc

test_syscall_insn:
	syscall

.ifne . - test_page - 4096
	.error "test page is not one page long"
.endif
.popsection

.section .note.GNU-stack,"",%progbits
