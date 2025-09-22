/*	$OpenBSD: k_tanl.c,v 1.1 2008/12/09 20:00:35 martynas Exp $	*/
/* From: @(#)k_tan.c 1.5 04/04/22 SMI */
/*
 * ====================================================
 * Copyright 2004 Sun Microsystems, Inc.  All Rights Reserved.
 * Copyright (c) 2008 Steven G. Kargl, David Schultz, Bruce D. Evans.
 *
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/*
 * ld128 version of k_tan.c.  See ../k_tan.c for most comments.
 */

#include <math.h>

#include "math_private.h"

/*
 * Domain [-0.67434, 0.67434], range ~[-3.37e-36, 1.982e-37]
 * |tan(x)/x - t(x)| < 2**-117.8 (XXX should be ~1e-37)
 *
 * See ../ld80/k_cosl.c for more details about the polynomial.
 */
static const long double
T3 = 0x1.5555555555555555555555555553p-2L,
T5 = 0x1.1111111111111111111111111eb5p-3L,
T7 = 0x1.ba1ba1ba1ba1ba1ba1ba1b694cd6p-5L,
T9 = 0x1.664f4882c10f9f32d6bbe09d8bcdp-6L,
T11 = 0x1.226e355e6c23c8f5b4f5762322eep-7L,
T13 = 0x1.d6d3d0e157ddfb5fed8e84e27b37p-9L,
T15 = 0x1.7da36452b75e2b5fce9ee7c2c92ep-10L,
T17 = 0x1.355824803674477dfcf726649efep-11L,
T19 = 0x1.f57d7734d1656e0aceb716f614c2p-13L,
T21 = 0x1.967e18afcb180ed942dfdc518d6cp-14L,
T23 = 0x1.497d8eea21e95bc7e2aa79b9f2cdp-15L,
T25 = 0x1.0b132d39f055c81be49eff7afd50p-16L,
T27 = 0x1.b0f72d33eff7bfa2fbc1059d90b6p-18L,
T29 = 0x1.5ef2daf21d1113df38d0fbc00267p-19L,
T31 = 0x1.1c77d6eac0234988cdaa04c96626p-20L,
T33 = 0x1.cd2a5a292b180e0bdd701057dfe3p-22L,
T35 = 0x1.75c7357d0298c01a31d0a6f7d518p-23L,
T37 = 0x1.2f3190f4718a9a520f98f50081fcp-24L,
pio4 = 0x1.921fb54442d18469898cc51701b8p-1L,
pio4lo = 0x1.cd129024e088a67cc74020bbea60p-116L;

static const double
T39 =  0.000000028443389121318352,	/*  0x1e8a7592977938.0p-78 */
T41 =  0.000000011981013102001973,	/*  0x19baa1b1223219.0p-79 */
T43 =  0.0000000038303578044958070,	/*  0x107385dfb24529.0p-80 */
T45 =  0.0000000034664378216909893,	/*  0x1dc6c702a05262.0p-81 */
T47 = -0.0000000015090641701997785,	/* -0x19ecef3569ebb6.0p-82 */
T49 =  0.0000000029449552300483952,	/*  0x194c0668da786a.0p-81 */
T51 = -0.0000000022006995706097711,	/* -0x12e763b8845268.0p-81 */
T53 =  0.0000000015468200913196612,	/*  0x1a92fc98c29554.0p-82 */
T55 = -0.00000000061311613386849674,	/* -0x151106cbc779a9.0p-83 */
T57 =  1.4912469681508012e-10;		/*  0x147edbdba6f43a.0p-85 */

long double
__kernel_tanl(long double x, long double y, int iy) {
	long double z, r, v, w, s;
	long double osign;
	int i;

	iy = (iy == 1 ? -1 : 1);	/* XXX recover original interface */
	osign = (x >= 0 ? 1.0 : -1.0);	/* XXX slow, probably wrong for -0 */
	if (fabsl(x) >= 0.67434) {
		if (x < 0) {
			x = -x;
			y = -y;
		}
		z = pio4 - x;
		w = pio4lo - y;
		x = z + w;
		y = 0.0;
		i = 1;
	} else
		i = 0;
	z = x * x;
	w = z * z;
	r = T5 + w * (T9 + w * (T13 + w * (T17 + w * (T21 +
	    w * (T25 + w * (T29 + w * (T33 +
	    w * (T37 + w * (T41 + w * (T45 + w * (T49 + w * (T53 +
	    w * T57))))))))))));
	v = z * (T7 + w * (T11 + w * (T15 + w * (T19 + w * (T23 +
	    w * (T27 + w * (T31 + w * (T35 +
	    w * (T39 + w * (T43 + w * (T47 + w * (T51 + w * T55))))))))))));
	s = z * x;
	r = y + z * (s * (r + v) + y);
	r += T3 * s;
	w = x + r;
	if (i == 1) {
		v = (long double) iy;
		return osign *
			(v - 2.0 * (x - (w * w / (w + v) - r)));
	}
	if (iy == 1)
		return w;
	else {
		/*
		 * if allow error up to 2 ulp, simply return
		 * -1.0 / (x+r) here
		 */
		/* compute -1.0 / (x+r) accurately */
		long double a, t;
		z = w;
		z = z + 0x1p32 - 0x1p32;
		v = r - (z - x);	/* z+v = r+x */
		t = a = -1.0 / w;	/* a = -1.0/w */
		t = t + 0x1p32 - 0x1p32;
		s = 1.0 + t * z;
		return t + a * (s + t * v);
	}
}
