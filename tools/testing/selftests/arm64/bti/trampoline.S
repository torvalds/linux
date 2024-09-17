/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#include "assembler.h"

startfn call_using_br_x0
	bti	c
	br	x0
endfn

startfn call_using_br_x16
	bti	c
	mov	x16, x0
	br	x16
endfn

startfn call_using_blr
	paciasp
	stp	x29, x30, [sp, #-16]!
	blr	x0
	ldp	x29, x30, [sp], #16
	autiasp
	ret
endfn

emit_aarch64_feature_1_and
