/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2013 Bruce D. Evans
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * Implementation of the natural logarithm of x for Intel 80-bit format.
 *
 * First decompose x into its base 2 representation:
 *
 *    log(x) = log(X * 2**k), where X is in [1, 2)
 *           = log(X) + k * log(2).
 *
 * Let X = X_i + e, where X_i is the center of one of the intervals
 * [-1.0/256, 1.0/256), [1.0/256, 3.0/256), .... [2.0-1.0/256, 2.0+1.0/256)
 * and X is in this interval.  Then
 *
 *    log(X) = log(X_i + e)
 *           = log(X_i * (1 + e / X_i))
 *           = log(X_i) + log(1 + e / X_i).
 *
 * The values log(X_i) are tabulated below.  Let d = e / X_i and use
 *
 *    log(1 + d) = p(d)
 *
 * where p(d) = d - 0.5*d*d + ... is a special minimax polynomial of
 * suitably high degree.
 *
 * To get sufficiently small roundoff errors, k * log(2), log(X_i), and
 * sometimes (if |k| is not large) the first term in p(d) must be evaluated
 * and added up in extra precision.  Extra precision is not needed for the
 * rest of p(d).  In the worst case when k = 0 and log(X_i) is 0, the final
 * error is controlled mainly by the error in the second term in p(d).  The
 * error in this term itself is at most 0.5 ulps from the d*d operation in
 * it.  The error in this term relative to the first term is thus at most
 * 0.5 * |-0.5| * |d| < 1.0/1024 ulps.  We aim for an accumulated error of
 * at most twice this at the point of the final rounding step.  Thus the
 * final error should be at most 0.5 + 1.0/512 = 0.5020 ulps.  Exhaustive
 * testing of a float variant of this function showed a maximum final error
 * of 0.5008 ulps.  Non-exhaustive testing of a double variant of this
 * function showed a maximum final error of 0.5078 ulps (near 1+1.0/256).
 *
 * We made the maximum of |d| (and thus the total relative error and the
 * degree of p(d)) small by using a large number of intervals.  Using
 * centers of intervals instead of endpoints reduces this maximum by a
 * factor of 2 for a given number of intervals.  p(d) is special only
 * in beginning with the Taylor coefficients 0 + 1*d, which tends to happen
 * naturally.  The most accurate minimax polynomial of a given degree might
 * be different, but then we wouldn't want it since we would have to do
 * extra work to avoid roundoff error (especially for P0*d instead of d).
 */

#ifdef DEBUG
#include <assert.h>
#include <fenv.h>
#endif

#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#define	i386_SSE_GOOD
#ifndef NO_STRUCT_RETURN
#define	STRUCT_RETURN
#endif
#include "math_private.h"

#if !defined(NO_UTAB) && !defined(NO_UTABL)
#define	USE_UTAB
#endif

/*
 * Domain [-0.005280, 0.004838], range ~[-5.1736e-22, 5.1738e-22]:
 * |log(1 + d)/d - p(d)| < 2**-70.7
 */
static const double
P2 = -0.5,
P3 =  3.3333333333333359e-1,		/*  0x1555555555555a.0p-54 */
P4 = -2.5000000000004424e-1,		/* -0x1000000000031d.0p-54 */
P5 =  1.9999999992970016e-1,		/*  0x1999999972f3c7.0p-55 */
P6 = -1.6666666072191585e-1,		/* -0x15555548912c09.0p-55 */
P7 =  1.4286227413310518e-1,		/*  0x12494f9d9def91.0p-55 */
P8 = -1.2518388626763144e-1;		/* -0x1006068cc0b97c.0p-55 */

static volatile const double zero = 0;

#define	INTERVALS	128
#define	LOG2_INTERVALS	7
#define	TSIZE		(INTERVALS + 1)
#define	G(i)		(T[(i)].G)
#define	F_hi(i)		(T[(i)].F_hi)
#define	F_lo(i)		(T[(i)].F_lo)
#define	ln2_hi		F_hi(TSIZE - 1)
#define	ln2_lo		F_lo(TSIZE - 1)
#define	E(i)		(U[(i)].E)
#define	H(i)		(U[(i)].H)

static const struct {
	float	G;			/* 1/(1 + i/128) rounded to 8/9 bits */
	float	F_hi;			/* log(1 / G_i) rounded (see below) */
	double	F_lo;			/* next 53 bits for log(1 / G_i) */
} T[TSIZE] = {
	/*
	 * ln2_hi and each F_hi(i) are rounded to a number of bits that
	 * makes F_hi(i) + dk*ln2_hi exact for all i and all dk.
	 *
	 * The last entry (for X just below 2) is used to define ln2_hi
	 * and ln2_lo, to ensure that F_hi(i) and F_lo(i) cancel exactly
	 * with dk*ln2_hi and dk*ln2_lo, respectively, when dk = -1.
	 * This is needed for accuracy when x is just below 1.  (To avoid
	 * special cases, such x are "reduced" strangely to X just below
	 * 2 and dk = -1, and then the exact cancellation is needed
	 * because any the error from any non-exactness would be too
	 * large).
	 *
	 * We want to share this table between double precision and ld80,
	 * so the relevant range of dk is the larger one of ld80
	 * ([-16445, 16383]) and the relevant exactness requirement is
	 * the stricter one of double precision.  The maximum number of
	 * bits in F_hi(i) that works is very dependent on i but has
	 * a minimum of 33.  We only need about 12 bits in F_hi(i) for
	 * it to provide enough extra precision in double precision (11
	 * more than that are required for ld80).
	 *
	 * We round F_hi(i) to 24 bits so that it can have type float,
	 * mainly to minimize the size of the table.  Using all 24 bits
	 * in a float for it automatically satisfies the above constraints.
	 */
	 { 0x800000.0p-23,  0,               0 },
	 { 0xfe0000.0p-24,  0x8080ac.0p-30, -0x14ee431dae6675.0p-84 },
	 { 0xfc0000.0p-24,  0x8102b3.0p-29, -0x1db29ee2d83718.0p-84 },
	 { 0xfa0000.0p-24,  0xc24929.0p-29,  0x1191957d173698.0p-83 },
	 { 0xf80000.0p-24,  0x820aec.0p-28,  0x13ce8888e02e79.0p-82 },
	 { 0xf60000.0p-24,  0xa33577.0p-28, -0x17a4382ce6eb7c.0p-82 },
	 { 0xf48000.0p-24,  0xbc42cb.0p-28, -0x172a21161a1076.0p-83 },
	 { 0xf30000.0p-24,  0xd57797.0p-28, -0x1e09de07cb9589.0p-82 },
	 { 0xf10000.0p-24,  0xf7518e.0p-28,  0x1ae1eec1b036c5.0p-91 },
	 { 0xef0000.0p-24,  0x8cb9df.0p-27, -0x1d7355325d560e.0p-81 },
	 { 0xed8000.0p-24,  0x999ec0.0p-27, -0x1f9f02d256d503.0p-82 },
	 { 0xec0000.0p-24,  0xa6988b.0p-27, -0x16fc0a9d12c17a.0p-83 },
	 { 0xea0000.0p-24,  0xb80698.0p-27,  0x15d581c1e8da9a.0p-81 },
	 { 0xe80000.0p-24,  0xc99af3.0p-27, -0x1535b3ba8f150b.0p-83 },
	 { 0xe70000.0p-24,  0xd273b2.0p-27,  0x163786f5251af0.0p-85 },
	 { 0xe50000.0p-24,  0xe442c0.0p-27,  0x1bc4b2368e32d5.0p-84 },
	 { 0xe38000.0p-24,  0xf1b83f.0p-27,  0x1c6090f684e676.0p-81 },
	 { 0xe20000.0p-24,  0xff448a.0p-27, -0x1890aa69ac9f42.0p-82 },
	 { 0xe08000.0p-24,  0x8673f6.0p-26,  0x1b9985194b6b00.0p-80 },
	 { 0xdf0000.0p-24,  0x8d515c.0p-26, -0x1dc08d61c6ef1e.0p-83 },
	 { 0xdd8000.0p-24,  0x943a9e.0p-26, -0x1f72a2dac729b4.0p-82 },
	 { 0xdc0000.0p-24,  0x9b2fe6.0p-26, -0x1fd4dfd3a0afb9.0p-80 },
	 { 0xda8000.0p-24,  0xa2315d.0p-26, -0x11b26121629c47.0p-82 },
	 { 0xd90000.0p-24,  0xa93f2f.0p-26,  0x1286d633e8e569.0p-81 },
	 { 0xd78000.0p-24,  0xb05988.0p-26,  0x16128eba936770.0p-84 },
	 { 0xd60000.0p-24,  0xb78094.0p-26,  0x16ead577390d32.0p-80 },
	 { 0xd50000.0p-24,  0xbc4c6c.0p-26,  0x151131ccf7c7b7.0p-81 },
	 { 0xd38000.0p-24,  0xc3890a.0p-26, -0x115e2cd714bd06.0p-80 },
	 { 0xd20000.0p-24,  0xcad2d7.0p-26, -0x1847f406ebd3b0.0p-82 },
	 { 0xd10000.0p-24,  0xcfb620.0p-26,  0x1c2259904d6866.0p-81 },
	 { 0xcf8000.0p-24,  0xd71653.0p-26,  0x1ece57a8d5ae55.0p-80 },
	 { 0xce0000.0p-24,  0xde843a.0p-26, -0x1f109d4bc45954.0p-81 },
	 { 0xcd0000.0p-24,  0xe37fde.0p-26,  0x1bc03dc271a74d.0p-81 },
	 { 0xcb8000.0p-24,  0xeb050c.0p-26, -0x1bf2badc0df842.0p-85 },
	 { 0xca0000.0p-24,  0xf29878.0p-26, -0x18efededd89fbe.0p-87 },
	 { 0xc90000.0p-24,  0xf7ad6f.0p-26,  0x1373ff977baa69.0p-81 },
	 { 0xc80000.0p-24,  0xfcc8e3.0p-26,  0x196766f2fb3283.0p-80 },
	 { 0xc68000.0p-24,  0x823f30.0p-25,  0x19bd076f7c434e.0p-79 },
	 { 0xc58000.0p-24,  0x84d52c.0p-25, -0x1a327257af0f46.0p-79 },
	 { 0xc40000.0p-24,  0x88bc74.0p-25,  0x113f23def19c5a.0p-81 },
	 { 0xc30000.0p-24,  0x8b5ae6.0p-25,  0x1759f6e6b37de9.0p-79 },
	 { 0xc20000.0p-24,  0x8dfccb.0p-25,  0x1ad35ca6ed5148.0p-81 },
	 { 0xc10000.0p-24,  0x90a22b.0p-25,  0x1a1d71a87deba4.0p-79 },
	 { 0xbf8000.0p-24,  0x94a0d8.0p-25, -0x139e5210c2b731.0p-80 },
	 { 0xbe8000.0p-24,  0x974f16.0p-25, -0x18f6ebcff3ed73.0p-81 },
	 { 0xbd8000.0p-24,  0x9a00f1.0p-25, -0x1aa268be39aab7.0p-79 },
	 { 0xbc8000.0p-24,  0x9cb672.0p-25, -0x14c8815839c566.0p-79 },
	 { 0xbb0000.0p-24,  0xa0cda1.0p-25,  0x1eaf46390dbb24.0p-81 },
	 { 0xba0000.0p-24,  0xa38c6e.0p-25,  0x138e20d831f698.0p-81 },
	 { 0xb90000.0p-24,  0xa64f05.0p-25, -0x1e8d3c41123616.0p-82 },
	 { 0xb80000.0p-24,  0xa91570.0p-25,  0x1ce28f5f3840b2.0p-80 },
	 { 0xb70000.0p-24,  0xabdfbb.0p-25, -0x186e5c0a424234.0p-79 },
	 { 0xb60000.0p-24,  0xaeadef.0p-25, -0x14d41a0b2a08a4.0p-83 },
	 { 0xb50000.0p-24,  0xb18018.0p-25,  0x16755892770634.0p-79 },
	 { 0xb40000.0p-24,  0xb45642.0p-25, -0x16395ebe59b152.0p-82 },
	 { 0xb30000.0p-24,  0xb73077.0p-25,  0x1abc65c8595f09.0p-80 },
	 { 0xb20000.0p-24,  0xba0ec4.0p-25, -0x1273089d3dad89.0p-79 },
	 { 0xb10000.0p-24,  0xbcf133.0p-25,  0x10f9f67b1f4bbf.0p-79 },
	 { 0xb00000.0p-24,  0xbfd7d2.0p-25, -0x109fab90486409.0p-80 },
	 { 0xaf0000.0p-24,  0xc2c2ac.0p-25, -0x1124680aa43333.0p-79 },
	 { 0xae8000.0p-24,  0xc439b3.0p-25, -0x1f360cc4710fc0.0p-80 },
	 { 0xad8000.0p-24,  0xc72afd.0p-25, -0x132d91f21d89c9.0p-80 },
	 { 0xac8000.0p-24,  0xca20a2.0p-25, -0x16bf9b4d1f8da8.0p-79 },
	 { 0xab8000.0p-24,  0xcd1aae.0p-25,  0x19deb5ce6a6a87.0p-81 },
	 { 0xaa8000.0p-24,  0xd0192f.0p-25,  0x1a29fb48f7d3cb.0p-79 },
	 { 0xaa0000.0p-24,  0xd19a20.0p-25,  0x1127d3c6457f9d.0p-81 },
	 { 0xa90000.0p-24,  0xd49f6a.0p-25, -0x1ba930e486a0ac.0p-81 },
	 { 0xa80000.0p-24,  0xd7a94b.0p-25, -0x1b6e645f31549e.0p-79 },
	 { 0xa70000.0p-24,  0xdab7d0.0p-25,  0x1118a425494b61.0p-80 },
	 { 0xa68000.0p-24,  0xdc40d5.0p-25,  0x1966f24d29d3a3.0p-80 },
	 { 0xa58000.0p-24,  0xdf566d.0p-25, -0x1d8e52eb2248f1.0p-82 },
	 { 0xa48000.0p-24,  0xe270ce.0p-25, -0x1ee370f96e6b68.0p-80 },
	 { 0xa40000.0p-24,  0xe3ffce.0p-25,  0x1d155324911f57.0p-80 },
	 { 0xa30000.0p-24,  0xe72179.0p-25, -0x1fe6e2f2f867d9.0p-80 },
	 { 0xa20000.0p-24,  0xea4812.0p-25,  0x1b7be9add7f4d4.0p-80 },
	 { 0xa18000.0p-24,  0xebdd3d.0p-25,  0x1b3cfb3f7511dd.0p-79 },
	 { 0xa08000.0p-24,  0xef0b5b.0p-25, -0x1220de1f730190.0p-79 },
	 { 0xa00000.0p-24,  0xf0a451.0p-25, -0x176364c9ac81cd.0p-80 },
	 { 0x9f0000.0p-24,  0xf3da16.0p-25,  0x1eed6b9aafac8d.0p-81 },
	 { 0x9e8000.0p-24,  0xf576e9.0p-25,  0x1d593218675af2.0p-79 },
	 { 0x9d8000.0p-24,  0xf8b47c.0p-25, -0x13e8eb7da053e0.0p-84 },
	 { 0x9d0000.0p-24,  0xfa553f.0p-25,  0x1c063259bcade0.0p-79 },
	 { 0x9c0000.0p-24,  0xfd9ac5.0p-25,  0x1ef491085fa3c1.0p-79 },
	 { 0x9b8000.0p-24,  0xff3f8c.0p-25,  0x1d607a7c2b8c53.0p-79 },
	 { 0x9a8000.0p-24,  0x814697.0p-24, -0x12ad3817004f3f.0p-78 },
	 { 0x9a0000.0p-24,  0x821b06.0p-24, -0x189fc53117f9e5.0p-81 },
	 { 0x990000.0p-24,  0x83c5f8.0p-24,  0x14cf15a048907b.0p-79 },
	 { 0x988000.0p-24,  0x849c7d.0p-24,  0x1cbb1d35fb8287.0p-78 },
	 { 0x978000.0p-24,  0x864ba6.0p-24,  0x1128639b814f9c.0p-78 },
	 { 0x970000.0p-24,  0x87244c.0p-24,  0x184733853300f0.0p-79 },
	 { 0x968000.0p-24,  0x87fdaa.0p-24,  0x109d23aef77dd6.0p-80 },
	 { 0x958000.0p-24,  0x89b293.0p-24, -0x1a81ef367a59de.0p-78 },
	 { 0x950000.0p-24,  0x8a8e20.0p-24, -0x121ad3dbb2f452.0p-78 },
	 { 0x948000.0p-24,  0x8b6a6a.0p-24, -0x1cfb981628af72.0p-79 },
	 { 0x938000.0p-24,  0x8d253a.0p-24, -0x1d21730ea76cfe.0p-79 },
	 { 0x930000.0p-24,  0x8e03c2.0p-24,  0x135cc00e566f77.0p-78 },
	 { 0x928000.0p-24,  0x8ee30d.0p-24, -0x10fcb5df257a26.0p-80 },
	 { 0x918000.0p-24,  0x90a3ee.0p-24, -0x16e171b15433d7.0p-79 },
	 { 0x910000.0p-24,  0x918587.0p-24, -0x1d050da07f3237.0p-79 },
	 { 0x908000.0p-24,  0x9267e7.0p-24,  0x1be03669a5268d.0p-79 },
	 { 0x8f8000.0p-24,  0x942f04.0p-24,  0x10b28e0e26c337.0p-79 },
	 { 0x8f0000.0p-24,  0x9513c3.0p-24,  0x1a1d820da57cf3.0p-78 },
	 { 0x8e8000.0p-24,  0x95f950.0p-24, -0x19ef8f13ae3cf1.0p-79 },
	 { 0x8e0000.0p-24,  0x96dfab.0p-24, -0x109e417a6e507c.0p-78 },
	 { 0x8d0000.0p-24,  0x98aed2.0p-24,  0x10d01a2c5b0e98.0p-79 },
	 { 0x8c8000.0p-24,  0x9997a2.0p-24, -0x1d6a50d4b61ea7.0p-78 },
	 { 0x8c0000.0p-24,  0x9a8145.0p-24,  0x1b3b190b83f952.0p-78 },
	 { 0x8b8000.0p-24,  0x9b6bbf.0p-24,  0x13a69fad7e7abe.0p-78 },
	 { 0x8b0000.0p-24,  0x9c5711.0p-24, -0x11cd12316f576b.0p-78 },
	 { 0x8a8000.0p-24,  0x9d433b.0p-24,  0x1c95c444b807a2.0p-79 },
	 { 0x898000.0p-24,  0x9f1e22.0p-24, -0x1b9c224ea698c3.0p-79 },
	 { 0x890000.0p-24,  0xa00ce1.0p-24,  0x125ca93186cf0f.0p-81 },
	 { 0x888000.0p-24,  0xa0fc80.0p-24, -0x1ee38a7bc228b3.0p-79 },
	 { 0x880000.0p-24,  0xa1ed00.0p-24, -0x1a0db876613d20.0p-78 },
	 { 0x878000.0p-24,  0xa2de62.0p-24,  0x193224e8516c01.0p-79 },
	 { 0x870000.0p-24,  0xa3d0a9.0p-24,  0x1fa28b4d2541ad.0p-79 },
	 { 0x868000.0p-24,  0xa4c3d6.0p-24,  0x1c1b5760fb4572.0p-78 },
	 { 0x858000.0p-24,  0xa6acea.0p-24,  0x1fed5d0f65949c.0p-80 },
	 { 0x850000.0p-24,  0xa7a2d4.0p-24,  0x1ad270c9d74936.0p-80 },
	 { 0x848000.0p-24,  0xa899ab.0p-24,  0x199ff15ce53266.0p-79 },
	 { 0x840000.0p-24,  0xa99171.0p-24,  0x1a19e15ccc45d2.0p-79 },
	 { 0x838000.0p-24,  0xaa8a28.0p-24, -0x121a14ec532b36.0p-80 },
	 { 0x830000.0p-24,  0xab83d1.0p-24,  0x1aee319980bff3.0p-79 },
	 { 0x828000.0p-24,  0xac7e6f.0p-24, -0x18ffd9e3900346.0p-80 },
	 { 0x820000.0p-24,  0xad7a03.0p-24, -0x1e4db102ce29f8.0p-80 },
	 { 0x818000.0p-24,  0xae768f.0p-24,  0x17c35c55a04a83.0p-81 },
	 { 0x810000.0p-24,  0xaf7415.0p-24,  0x1448324047019b.0p-78 },
	 { 0x808000.0p-24,  0xb07298.0p-24, -0x1750ee3915a198.0p-78 },
	 { 0x800000.0p-24,  0xb17218.0p-24, -0x105c610ca86c39.0p-81 },
};

#ifdef USE_UTAB
static const struct {
	float	H;			/* 1 + i/INTERVALS (exact) */
	float	E;			/* H(i) * G(i) - 1 (exact) */
} U[TSIZE] = {
	 { 0x800000.0p-23,  0 },
	 { 0x810000.0p-23, -0x800000.0p-37 },
	 { 0x820000.0p-23, -0x800000.0p-35 },
	 { 0x830000.0p-23, -0x900000.0p-34 },
	 { 0x840000.0p-23, -0x800000.0p-33 },
	 { 0x850000.0p-23, -0xc80000.0p-33 },
	 { 0x860000.0p-23, -0xa00000.0p-36 },
	 { 0x870000.0p-23,  0x940000.0p-33 },
	 { 0x880000.0p-23,  0x800000.0p-35 },
	 { 0x890000.0p-23, -0xc80000.0p-34 },
	 { 0x8a0000.0p-23,  0xe00000.0p-36 },
	 { 0x8b0000.0p-23,  0x900000.0p-33 },
	 { 0x8c0000.0p-23, -0x800000.0p-35 },
	 { 0x8d0000.0p-23, -0xe00000.0p-33 },
	 { 0x8e0000.0p-23,  0x880000.0p-33 },
	 { 0x8f0000.0p-23, -0xa80000.0p-34 },
	 { 0x900000.0p-23, -0x800000.0p-35 },
	 { 0x910000.0p-23,  0x800000.0p-37 },
	 { 0x920000.0p-23,  0x900000.0p-35 },
	 { 0x930000.0p-23,  0xd00000.0p-35 },
	 { 0x940000.0p-23,  0xe00000.0p-35 },
	 { 0x950000.0p-23,  0xc00000.0p-35 },
	 { 0x960000.0p-23,  0xe00000.0p-36 },
	 { 0x970000.0p-23, -0x800000.0p-38 },
	 { 0x980000.0p-23, -0xc00000.0p-35 },
	 { 0x990000.0p-23, -0xd00000.0p-34 },
	 { 0x9a0000.0p-23,  0x880000.0p-33 },
	 { 0x9b0000.0p-23,  0xe80000.0p-35 },
	 { 0x9c0000.0p-23, -0x800000.0p-35 },
	 { 0x9d0000.0p-23,  0xb40000.0p-33 },
	 { 0x9e0000.0p-23,  0x880000.0p-34 },
	 { 0x9f0000.0p-23, -0xe00000.0p-35 },
	 { 0xa00000.0p-23,  0x800000.0p-33 },
	 { 0xa10000.0p-23, -0x900000.0p-36 },
	 { 0xa20000.0p-23, -0xb00000.0p-33 },
	 { 0xa30000.0p-23, -0xa00000.0p-36 },
	 { 0xa40000.0p-23,  0x800000.0p-33 },
	 { 0xa50000.0p-23, -0xf80000.0p-35 },
	 { 0xa60000.0p-23,  0x880000.0p-34 },
	 { 0xa70000.0p-23, -0x900000.0p-33 },
	 { 0xa80000.0p-23, -0x800000.0p-35 },
	 { 0xa90000.0p-23,  0x900000.0p-34 },
	 { 0xaa0000.0p-23,  0xa80000.0p-33 },
	 { 0xab0000.0p-23, -0xac0000.0p-34 },
	 { 0xac0000.0p-23, -0x800000.0p-37 },
	 { 0xad0000.0p-23,  0xf80000.0p-35 },
	 { 0xae0000.0p-23,  0xf80000.0p-34 },
	 { 0xaf0000.0p-23, -0xac0000.0p-33 },
	 { 0xb00000.0p-23, -0x800000.0p-33 },
	 { 0xb10000.0p-23, -0xb80000.0p-34 },
	 { 0xb20000.0p-23, -0x800000.0p-34 },
	 { 0xb30000.0p-23, -0xb00000.0p-35 },
	 { 0xb40000.0p-23, -0x800000.0p-35 },
	 { 0xb50000.0p-23, -0xe00000.0p-36 },
	 { 0xb60000.0p-23, -0x800000.0p-35 },
	 { 0xb70000.0p-23, -0xb00000.0p-35 },
	 { 0xb80000.0p-23, -0x800000.0p-34 },
	 { 0xb90000.0p-23, -0xb80000.0p-34 },
	 { 0xba0000.0p-23, -0x800000.0p-33 },
	 { 0xbb0000.0p-23, -0xac0000.0p-33 },
	 { 0xbc0000.0p-23,  0x980000.0p-33 },
	 { 0xbd0000.0p-23,  0xbc0000.0p-34 },
	 { 0xbe0000.0p-23,  0xe00000.0p-36 },
	 { 0xbf0000.0p-23, -0xb80000.0p-35 },
	 { 0xc00000.0p-23, -0x800000.0p-33 },
	 { 0xc10000.0p-23,  0xa80000.0p-33 },
	 { 0xc20000.0p-23,  0x900000.0p-34 },
	 { 0xc30000.0p-23, -0x800000.0p-35 },
	 { 0xc40000.0p-23, -0x900000.0p-33 },
	 { 0xc50000.0p-23,  0x820000.0p-33 },
	 { 0xc60000.0p-23,  0x800000.0p-38 },
	 { 0xc70000.0p-23, -0x820000.0p-33 },
	 { 0xc80000.0p-23,  0x800000.0p-33 },
	 { 0xc90000.0p-23, -0xa00000.0p-36 },
	 { 0xca0000.0p-23, -0xb00000.0p-33 },
	 { 0xcb0000.0p-23,  0x840000.0p-34 },
	 { 0xcc0000.0p-23, -0xd00000.0p-34 },
	 { 0xcd0000.0p-23,  0x800000.0p-33 },
	 { 0xce0000.0p-23, -0xe00000.0p-35 },
	 { 0xcf0000.0p-23,  0xa60000.0p-33 },
	 { 0xd00000.0p-23, -0x800000.0p-35 },
	 { 0xd10000.0p-23,  0xb40000.0p-33 },
	 { 0xd20000.0p-23, -0x800000.0p-35 },
	 { 0xd30000.0p-23,  0xaa0000.0p-33 },
	 { 0xd40000.0p-23, -0xe00000.0p-35 },
	 { 0xd50000.0p-23,  0x880000.0p-33 },
	 { 0xd60000.0p-23, -0xd00000.0p-34 },
	 { 0xd70000.0p-23,  0x9c0000.0p-34 },
	 { 0xd80000.0p-23, -0xb00000.0p-33 },
	 { 0xd90000.0p-23, -0x800000.0p-38 },
	 { 0xda0000.0p-23,  0xa40000.0p-33 },
	 { 0xdb0000.0p-23, -0xdc0000.0p-34 },
	 { 0xdc0000.0p-23,  0xc00000.0p-35 },
	 { 0xdd0000.0p-23,  0xca0000.0p-33 },
	 { 0xde0000.0p-23, -0xb80000.0p-34 },
	 { 0xdf0000.0p-23,  0xd00000.0p-35 },
	 { 0xe00000.0p-23,  0xc00000.0p-33 },
	 { 0xe10000.0p-23, -0xf40000.0p-34 },
	 { 0xe20000.0p-23,  0x800000.0p-37 },
	 { 0xe30000.0p-23,  0x860000.0p-33 },
	 { 0xe40000.0p-23, -0xc80000.0p-33 },
	 { 0xe50000.0p-23, -0xa80000.0p-34 },
	 { 0xe60000.0p-23,  0xe00000.0p-36 },
	 { 0xe70000.0p-23,  0x880000.0p-33 },
	 { 0xe80000.0p-23, -0xe00000.0p-33 },
	 { 0xe90000.0p-23, -0xfc0000.0p-34 },
	 { 0xea0000.0p-23, -0x800000.0p-35 },
	 { 0xeb0000.0p-23,  0xe80000.0p-35 },
	 { 0xec0000.0p-23,  0x900000.0p-33 },
	 { 0xed0000.0p-23,  0xe20000.0p-33 },
	 { 0xee0000.0p-23, -0xac0000.0p-33 },
	 { 0xef0000.0p-23, -0xc80000.0p-34 },
	 { 0xf00000.0p-23, -0x800000.0p-35 },
	 { 0xf10000.0p-23,  0x800000.0p-35 },
	 { 0xf20000.0p-23,  0xb80000.0p-34 },
	 { 0xf30000.0p-23,  0x940000.0p-33 },
	 { 0xf40000.0p-23,  0xc80000.0p-33 },
	 { 0xf50000.0p-23, -0xf20000.0p-33 },
	 { 0xf60000.0p-23, -0xc80000.0p-33 },
	 { 0xf70000.0p-23, -0xa20000.0p-33 },
	 { 0xf80000.0p-23, -0x800000.0p-33 },
	 { 0xf90000.0p-23, -0xc40000.0p-34 },
	 { 0xfa0000.0p-23, -0x900000.0p-34 },
	 { 0xfb0000.0p-23, -0xc80000.0p-35 },
	 { 0xfc0000.0p-23, -0x800000.0p-35 },
	 { 0xfd0000.0p-23, -0x900000.0p-36 },
	 { 0xfe0000.0p-23, -0x800000.0p-37 },
	 { 0xff0000.0p-23, -0x800000.0p-39 },
	 { 0x800000.0p-22,  0 },
};
#endif /* USE_UTAB */

#ifdef STRUCT_RETURN
#define	RETURN1(rp, v) do {	\
	(rp)->hi = (v);		\
	(rp)->lo_set = 0;	\
	return;			\
} while (0)

#define	RETURN2(rp, h, l) do {	\
	(rp)->hi = (h);		\
	(rp)->lo = (l);		\
	(rp)->lo_set = 1;	\
	return;			\
} while (0)

struct ld {
	long double hi;
	long double lo;
	int	lo_set;
};
#else
#define	RETURN1(rp, v)	RETURNF(v)
#define	RETURN2(rp, h, l)	RETURNI((h) + (l))
#endif

#ifdef STRUCT_RETURN
static inline __always_inline void
k_logl(long double x, struct ld *rp)
#else
long double
logl(long double x)
#endif
{
	long double d, dk, val_hi, val_lo, z;
	uint64_t ix, lx;
	int i, k;
	uint16_t hx;

	EXTRACT_LDBL80_WORDS(hx, lx, x);
	k = -16383;
#if 0 /* Hard to do efficiently.  Don't do it until we support all modes. */
	if (x == 1)
		RETURN1(rp, 0);		/* log(1) = +0 in all rounding modes */
#endif
	if (hx == 0 || hx >= 0x8000) {	/* zero, negative or subnormal? */
		if (((hx & 0x7fff) | lx) == 0)
			RETURN1(rp, -1 / zero);	/* log(+-0) = -Inf */
		if (hx != 0)
			/* log(neg or [pseudo-]NaN) = qNaN: */
			RETURN1(rp, (x - x) / zero);
		x *= 0x1.0p65;		/* subnormal; scale up x */
					/* including pseudo-subnormals */
		EXTRACT_LDBL80_WORDS(hx, lx, x);
		k = -16383 - 65;
	} else if (hx >= 0x7fff || (lx & 0x8000000000000000ULL) == 0)
		RETURN1(rp, x + x);	/* log(Inf or NaN) = Inf or qNaN */
					/* log(pseudo-Inf) = qNaN */
					/* log(pseudo-NaN) = qNaN */
					/* log(unnormal) = qNaN */
#ifndef STRUCT_RETURN
	ENTERI();
#endif
	k += hx;
	ix = lx & 0x7fffffffffffffffULL;
	dk = k;

	/* Scale x to be in [1, 2). */
	SET_LDBL_EXPSIGN(x, 0x3fff);

	/* 0 <= i <= INTERVALS: */
#define	L2I	(64 - LOG2_INTERVALS)
	i = (ix + (1LL << (L2I - 2))) >> (L2I - 1);

	/*
	 * -0.005280 < d < 0.004838.  In particular, the infinite-
	 * precision |d| is <= 2**-7.  Rounding of G(i) to 8 bits
	 * ensures that d is representable without extra precision for
	 * this bound on |d| (since when this calculation is expressed
	 * as x*G(i)-1, the multiplication needs as many extra bits as
	 * G(i) has and the subtraction cancels 8 bits).  But for
	 * most i (107 cases out of 129), the infinite-precision |d|
	 * is <= 2**-8.  G(i) is rounded to 9 bits for such i to give
	 * better accuracy (this works by improving the bound on |d|,
	 * which in turn allows rounding to 9 bits in more cases).
	 * This is only important when the original x is near 1 -- it
	 * lets us avoid using a special method to give the desired
	 * accuracy for such x.
	 */
	if (0)
		d = x * G(i) - 1;
	else {
#ifdef USE_UTAB
		d = (x - H(i)) * G(i) + E(i);
#else
		long double x_hi, x_lo;
		float fx_hi;

		/*
		 * Split x into x_hi + x_lo to calculate x*G(i)-1 exactly.
		 * G(i) has at most 9 bits, so the splitting point is not
		 * critical.
		 */
		SET_FLOAT_WORD(fx_hi, (lx >> 40) | 0x3f800000);
		x_hi = fx_hi;
		x_lo = x - x_hi;
		d = x_hi * G(i) - 1 + x_lo * G(i);
#endif
	}

	/*
	 * Our algorithm depends on exact cancellation of F_lo(i) and
	 * F_hi(i) with dk*ln_2_lo and dk*ln2_hi when k is -1 and i is
	 * at the end of the table.  This and other technical complications
	 * make it difficult to avoid the double scaling in (dk*ln2) *
	 * log(base) for base != e without losing more accuracy and/or
	 * efficiency than is gained.
	 */
	z = d * d;
	val_lo = z * d * z * (z * (d * P8 + P7) + (d * P6 + P5)) +
	    (F_lo(i) + dk * ln2_lo + z * d * (d * P4 + P3)) + z * P2;
	val_hi = d;
#ifdef DEBUG
	if (fetestexcept(FE_UNDERFLOW))
		breakpoint();
#endif

	_3sumF(val_hi, val_lo, F_hi(i) + dk * ln2_hi);
	RETURN2(rp, val_hi, val_lo);
}

long double
log1pl(long double x)
{
	long double d, d_hi, d_lo, dk, f_lo, val_hi, val_lo, z;
	long double f_hi, twopminusk;
	uint64_t ix, lx;
	int i, k;
	int16_t ax, hx;

	DOPRINT_START(&x);
	EXTRACT_LDBL80_WORDS(hx, lx, x);
	if (hx < 0x3fff) {		/* x < 1, or x neg NaN */
		ax = hx & 0x7fff;
		if (ax >= 0x3fff) {	/* x <= -1, or x neg NaN */
			if (ax == 0x3fff && lx == 0x8000000000000000ULL)
				RETURNP(-1 / zero);	/* log1p(-1) = -Inf */
			/* log1p(x < 1, or x [pseudo-]NaN) = qNaN: */
			RETURNP((x - x) / (x - x));
		}
		if (ax <= 0x3fbe) {	/* |x| < 2**-64 */
			if ((int)x == 0)
				RETURNP(x);	/* x with inexact if x != 0 */
		}
		f_hi = 1;
		f_lo = x;
	} else if (hx >= 0x7fff) {	/* x +Inf or non-neg NaN */
		RETURNP(x + x);		/* log1p(Inf or NaN) = Inf or qNaN */
					/* log1p(pseudo-Inf) = qNaN */
					/* log1p(pseudo-NaN) = qNaN */
					/* log1p(unnormal) = qNaN */
	} else if (hx < 0x407f) {	/* 1 <= x < 2**128 */
		f_hi = x;
		f_lo = 1;
	} else {			/* 2**128 <= x < +Inf */
		f_hi = x;
		f_lo = 0;		/* avoid underflow of the P5 term */
	}
	ENTERI();
	x = f_hi + f_lo;
	f_lo = (f_hi - x) + f_lo;

	EXTRACT_LDBL80_WORDS(hx, lx, x);
	k = -16383;

	k += hx;
	ix = lx & 0x7fffffffffffffffULL;
	dk = k;

	SET_LDBL_EXPSIGN(x, 0x3fff);
	twopminusk = 1;
	SET_LDBL_EXPSIGN(twopminusk, 0x7ffe - (hx & 0x7fff));
	f_lo *= twopminusk;

	i = (ix + (1LL << (L2I - 2))) >> (L2I - 1);

	/*
	 * x*G(i)-1 (with a reduced x) can be represented exactly, as
	 * above, but now we need to evaluate the polynomial on d =
	 * (x+f_lo)*G(i)-1 and extra precision is needed for that.
	 * Since x+x_lo is a hi+lo decomposition and subtracting 1
	 * doesn't lose too many bits, an inexact calculation for
	 * f_lo*G(i) is good enough.
	 */
	if (0)
		d_hi = x * G(i) - 1;
	else {
#ifdef USE_UTAB
		d_hi = (x - H(i)) * G(i) + E(i);
#else
		long double x_hi, x_lo;
		float fx_hi;

		SET_FLOAT_WORD(fx_hi, (lx >> 40) | 0x3f800000);
		x_hi = fx_hi;
		x_lo = x - x_hi;
		d_hi = x_hi * G(i) - 1 + x_lo * G(i);
#endif
	}
	d_lo = f_lo * G(i);

	/*
	 * This is _2sumF(d_hi, d_lo) inlined.  The condition
	 * (d_hi == 0 || |d_hi| >= |d_lo|) for using _2sumF() is not
	 * always satisifed, so it is not clear that this works, but
	 * it works in practice.  It works even if it gives a wrong
	 * normalized d_lo, since |d_lo| > |d_hi| implies that i is
	 * nonzero and d is tiny, so the F(i) term dominates d_lo.
	 * In float precision:
	 * (By exhaustive testing, the worst case is d_hi = 0x1.bp-25.
	 * And if d is only a little tinier than that, we would have
	 * another underflow problem for the P3 term; this is also ruled
	 * out by exhaustive testing.)
	 */
	d = d_hi + d_lo;
	d_lo = d_hi - d + d_lo;
	d_hi = d;

	z = d * d;
	val_lo = z * d * z * (z * (d * P8 + P7) + (d * P6 + P5)) +
	    (F_lo(i) + dk * ln2_lo + d_lo + z * d * (d * P4 + P3)) + z * P2;
	val_hi = d_hi;
#ifdef DEBUG
	if (fetestexcept(FE_UNDERFLOW))
		breakpoint();
#endif

	_3sumF(val_hi, val_lo, F_hi(i) + dk * ln2_hi);
	RETURN2PI(val_hi, val_lo);
}

#ifdef STRUCT_RETURN

long double
logl(long double x)
{
	struct ld r;

	ENTERI();
	DOPRINT_START(&x);
	k_logl(x, &r);
	RETURNSPI(&r);
}

static const double
invln10_hi =  4.3429448190317999e-1,		/*  0x1bcb7b1526e000.0p-54 */
invln10_lo =  7.1842412889749798e-14,		/*  0x1438ca9aadd558.0p-96 */
invln2_hi =  1.4426950408887933e0,		/*  0x171547652b8000.0p-52 */
invln2_lo =  1.7010652264631490e-13;		/*  0x17f0bbbe87fed0.0p-95 */

long double
log10l(long double x)
{
	struct ld r;
	long double hi, lo;

	ENTERI();
	DOPRINT_START(&x);
	k_logl(x, &r);
	if (!r.lo_set)
		RETURNPI(r.hi);
	_2sumF(r.hi, r.lo);
	hi = (float)r.hi;
	lo = r.lo + (r.hi - hi);
	RETURN2PI(invln10_hi * hi,
	    (invln10_lo + invln10_hi) * lo + invln10_lo * hi);
}

long double
log2l(long double x)
{
	struct ld r;
	long double hi, lo;

	ENTERI();
	DOPRINT_START(&x);
	k_logl(x, &r);
	if (!r.lo_set)
		RETURNPI(r.hi);
	_2sumF(r.hi, r.lo);
	hi = (float)r.hi;
	lo = r.lo + (r.hi - hi);
	RETURN2PI(invln2_hi * hi,
	    (invln2_lo + invln2_hi) * lo + invln2_lo * hi);
}

#endif /* STRUCT_RETURN */
