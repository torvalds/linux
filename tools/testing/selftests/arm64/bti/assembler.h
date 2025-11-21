/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#define NT_GNU_PROPERTY_TYPE_0	5
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND	0xc0000000

/* Bits for GNU_PROPERTY_AARCH64_FEATURE_1_BTI */
#define GNU_PROPERTY_AARCH64_FEATURE_1_BTI	(1U << 0)
#define GNU_PROPERTY_AARCH64_FEATURE_1_PAC	(1U << 1)

.macro startfn name:req
	.globl \name
\name:
	.macro endfn
		.size \name, . - \name
		.type \name, @function
		.purgem endfn
	.endm
.endm

.macro emit_aarch64_feature_1_and
	.pushsection .note.gnu.property, "a"
	.align	3
	.long	2f - 1f
	.long	6f - 3f
	.long	NT_GNU_PROPERTY_TYPE_0
1:	.string	"GNU"
2:
	.align	3
3:	.long	GNU_PROPERTY_AARCH64_FEATURE_1_AND
	.long	5f - 4f
4:
#if BTI
	.long	GNU_PROPERTY_AARCH64_FEATURE_1_PAC | \
		GNU_PROPERTY_AARCH64_FEATURE_1_BTI
#else
	.long	0
#endif
5:
	.align	3
6:
	.popsection
.endm

.macro paciasp
	hint	0x19
.endm

.macro autiasp
	hint	0x1d
.endm

.macro __bti_
	hint	0x20
.endm

.macro __bti_c
	hint	0x22
.endm

.macro __bti_j
	hint	0x24
.endm

.macro __bti_jc
	hint	0x26
.endm

.macro bti what=
	__bti_\what
.endm

#endif /* ! ASSEMBLER_H */
