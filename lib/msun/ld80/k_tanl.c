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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * ld80 version of k_tan.c.  See ../src/k_tan.c for most comments.
 */

#include "math.h"
#include "math_private.h"

/*
 * Domain [-0.67434, 0.67434], range ~[-2.25e-22, 1.921e-22]
 * |tan(x)/x - t(x)| < 2**-71.9
 *
 * See k_cosl.c for more details about the polynomial.
 */
#if defined(__amd64__) || defined(__i386__)
/* Long double constants are slow on these arches, and broken on i386. */
static const volatile double
T3hi =  0.33333333333333331,		/*  0x15555555555555.0p-54 */
T3lo =  1.8350121769317163e-17,		/*  0x15280000000000.0p-108 */
T5hi =  0.13333333333333336,		/*  0x11111111111112.0p-55 */
T5lo =  1.3051083651294260e-17,		/*  0x1e180000000000.0p-109 */
T7hi =  0.053968253968250494,		/*  0x1ba1ba1ba1b827.0p-57 */
T7lo =  3.1509625637859973e-18,		/*  0x1d100000000000.0p-111 */
pio4_hi =  0.78539816339744828,		/*  0x1921fb54442d18.0p-53 */
pio4_lo =  3.0628711372715500e-17,	/*  0x11a80000000000.0p-107 */
pio4lo_hi = -1.2541394031670831e-20,	/* -0x1d9cceba3f91f2.0p-119 */
pio4lo_lo =  6.1493048227390915e-37;	/*  0x1a280000000000.0p-173 */
#define	T3	((long double)T3hi + T3lo)
#define	T5	((long double)T5hi + T5lo)
#define	T7	((long double)T7hi + T7lo)
#define	pio4	((long double)pio4_hi + pio4_lo)
#define	pio4lo	((long double)pio4lo_hi + pio4lo_lo)
#else
static const long double
T3 =   0.333333333333333333180L,	/*  0xaaaaaaaaaaaaaaa5.0p-65 */
T5 =   0.133333333333333372290L,	/*  0x88888888888893c3.0p-66 */
T7 =   0.0539682539682504975744L,	/*  0xdd0dd0dd0dc13ba2.0p-68 */
pio4 = 0.785398163397448309628L,	/*  0xc90fdaa22168c235.0p-64 */
pio4lo = -1.25413940316708300586e-20L;	/* -0xece675d1fc8f8cbb.0p-130 */
#endif

static const double
T9  =  0.021869488536312216,		/*  0x1664f4882cc1c2.0p-58 */
T11 =  0.0088632355256619590,		/*  0x1226e355c17612.0p-59 */
T13 =  0.0035921281113786528,		/*  0x1d6d3d185d7ff8.0p-61 */
T15 =  0.0014558334756312418,		/*  0x17da354aa3f96b.0p-62 */
T17 =  0.00059003538700862256,		/*  0x13559358685b83.0p-63 */
T19 =  0.00023907843576635544,		/*  0x1f56242026b5be.0p-65 */
T21 =  0.000097154625656538905,		/*  0x1977efc26806f4.0p-66 */
T23 =  0.000038440165747303162,		/*  0x14275a09b3ceac.0p-67 */
T25 =  0.000018082171885432524,		/*  0x12f5e563e5487e.0p-68 */
T27 =  0.0000024196006108814377,	/*  0x144c0d80cc6896.0p-71 */
T29 =  0.0000078293456938132840,	/*  0x106b59141a6cb3.0p-69 */
T31 = -0.0000032609076735050182,	/* -0x1b5abef3ba4b59.0p-71 */
T33 =  0.0000023261313142559411;	/*  0x13835436c0c87f.0p-71 */

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
	    w * (T25 + w * (T29 + w * T33))))));
	v = z * (T7 + w * (T11 + w * (T15 + w * (T19 + w * (T23 +
	    w * (T27 + w * T31))))));
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
