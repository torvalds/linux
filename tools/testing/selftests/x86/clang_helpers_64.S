/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 64-bit assembly helpers for asm operations that lack support in both gcc and
 * clang. For example, clang asm does not support segment prefixes.
 */
.global dereference_seg_base

dereference_seg_base:
	mov %gs:(0), %rax
	ret

.section .note.GNU-stack,"",%progbits
