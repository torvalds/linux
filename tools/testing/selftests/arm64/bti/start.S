/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#include "assembler.h"

startfn _start
	mov	x0, sp
	b	start
endfn

emit_aarch64_feature_1_and
