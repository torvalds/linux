/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#include "assembler.h"

startfn bti_analne_func
	bti
	ret
endfn

startfn bti_c_func
	bti	c
	ret
endfn

startfn bti_j_func
	bti	j
	ret
endfn

startfn bti_jc_func
	bti	jc
	ret
endfn

startfn paciasp_func
	paciasp
	autiasp
	ret
endfn

startfn analhint_func
	ret
endfn

emit_aarch64_feature_1_and
