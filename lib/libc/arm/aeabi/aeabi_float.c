/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "softfloat-for-gcc.h"
#include "milieu.h"
#include "softfloat.h"

#include "aeabi_vfp.h"

extern int _libc_arm_fpu_present;

flag __unordsf2(float32, float32);

/* These are written in asm and are only called from this file */
int __aeabi_fcmpeq_vfp(float32, float32);
int __aeabi_fcmplt_vfp(float32, float32);
int __aeabi_fcmple_vfp(float32, float32);
int __aeabi_fcmpgt_vfp(float32, float32);
int __aeabi_fcmpge_vfp(float32, float32);
int __aeabi_fcmpun_vfp(float32, float32);
int __aeabi_f2iz_vfp(float32);
float64 __aeabi_f2d_vfp(float32);
float32 __aeabi_i2f_vfp(int);
float32 __aeabi_fadd_vfp(float32, float32);
float32 __aeabi_fdiv_vfp(float32, float32);
float32 __aeabi_fmul_vfp(float32, float32);
float32 __aeabi_fsub_vfp(float32, float32);

/*
 * Depending on the target these will:
 *  On armv6 with a vfp call the above function, or
 *  Call the softfloat function in the 3rd argument.
 */
int AEABI_FUNC2(fcmpeq, float32, float32_eq)
int AEABI_FUNC2(fcmplt, float32, float32_lt)
int AEABI_FUNC2(fcmple, float32, float32_le)
int AEABI_FUNC2_REV(fcmpge, float32, float32_le)
int AEABI_FUNC2_REV(fcmpgt, float32, float32_lt)
int AEABI_FUNC2(fcmpun, float32, __unordsf2)

int AEABI_FUNC(f2iz, float32, float32_to_int32_round_to_zero)
float64 AEABI_FUNC(f2d, float32, float32_to_float64)
float32 AEABI_FUNC(i2f, int, int32_to_float32)

float32 AEABI_FUNC2(fadd, float32, float32_add)
float32 AEABI_FUNC2(fdiv, float32, float32_div)
float32 AEABI_FUNC2(fmul, float32, float32_mul)
float32 AEABI_FUNC2(fsub, float32, float32_sub)

int
__aeabi_cfcmpeq_helper(float32 a, float32 b)
{
	int quiet = 0;

	/* Check if a is a NaN */
	if ((a << 1) > 0xff000000u) {
		/* If it's a signalling NaN we will always signal */
		if ((a & 0x00400000u) == 0)
			return (0);

		quiet = 1;
	}

	/* Check if b is a NaN */
	if ((b << 1) > 0xff000000u) {
		/* If it's a signalling NaN we will always signal */
		if ((b & 0x00400000u) == 0)
			return (0);

		quiet = 1;
	}

	return (quiet);
}
