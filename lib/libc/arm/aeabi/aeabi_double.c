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

flag __unorddf2(float64, float64);

/* These are written in asm and are only called from this file */
int __aeabi_dcmpeq_vfp(float64, float64);
int __aeabi_dcmplt_vfp(float64, float64);
int __aeabi_dcmple_vfp(float64, float64);
int __aeabi_dcmpgt_vfp(float64, float64);
int __aeabi_dcmpge_vfp(float64, float64);
int __aeabi_dcmpun_vfp(float64, float64);
int __aeabi_d2iz_vfp(float64);
float32 __aeabi_d2f_vfp(float64);
float64 __aeabi_i2d_vfp(int);
float64 __aeabi_dadd_vfp(float64, float64);
float64 __aeabi_ddiv_vfp(float64, float64);
float64 __aeabi_dmul_vfp(float64, float64);
float64 __aeabi_dsub_vfp(float64, float64);

/*
 * Depending on the target these will:
 *  On armv6 with a vfp call the above function, or
 *  Call the softfloat function in the 3rd argument.
 */
int AEABI_FUNC2(dcmpeq, float64, float64_eq)
int AEABI_FUNC2(dcmplt, float64, float64_lt)
int AEABI_FUNC2(dcmple, float64, float64_le)
int AEABI_FUNC2_REV(dcmpge, float64, float64_le)
int AEABI_FUNC2_REV(dcmpgt, float64, float64_lt)
int AEABI_FUNC2(dcmpun, float64, __unorddf2)

int AEABI_FUNC(d2iz, float64, float64_to_int32_round_to_zero)
float32 AEABI_FUNC(d2f, float64, float64_to_float32)
float64 AEABI_FUNC(i2d, int, int32_to_float64)

float64 AEABI_FUNC2(dadd, float64, float64_add)
float64 AEABI_FUNC2(ddiv, float64, float64_div)
float64 AEABI_FUNC2(dmul, float64, float64_mul)
float64 AEABI_FUNC2(dsub, float64, float64_sub)

int
__aeabi_cdcmpeq_helper(float64 a, float64 b)
{
	int quiet = 0;

	/* Check if a is a NaN */
	if ((a << 1) > 0xffe0000000000000ull) {
		/* If it's a signalling NaN we will always signal */
		if ((a & 0x0008000000000000ull) == 0)
			return (0);

		quiet = 1;
	}

	/* Check if b is a NaN */
	if ((b << 1) > 0xffe0000000000000ull) {
		/* If it's a signalling NaN we will always signal */
		if ((b & 0x0008000000000000ull) == 0)
			return (0);

		quiet = 1;
	}

	return (quiet);
}
