/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 32-bit assembly helpers for asm operations that lack support in both gcc and
 * clang. For example, clang asm does not support segment prefixes.
 */
.global dereference_seg_base
dereference_seg_base:
	mov %fs:(0), %eax
	ret

.section .note.GNU-stack,"",%progbits
