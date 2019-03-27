/* @(#)e_lgamma_r.c 1.3 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * See e_lgamma_r.c for complete comments.
 *
 * Converted to long double by Steven G. Kargl.
 */

#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

static const volatile double vzero = 0;

static const double
zero=  0,
half=  0.5,
one =  1;

static const union IEEEl2bits
piu = LD80C(0xc90fdaa22168c235, 1,  3.14159265358979323851e+00L);
#define	pi	(piu.e)
/*
 * Domain y in [0x1p-70, 0.27], range ~[-4.5264e-22, 4.5264e-22]:
 * |(lgamma(2 - y) + y / 2) / y - a(y)| < 2**-70.9
 */
static const union IEEEl2bits
a0u = LD80C(0x9e233f1bed863d26, -4,  7.72156649015328606028e-02L),
a1u = LD80C(0xa51a6625307d3249, -2,  3.22467033424113218889e-01L),
a2u = LD80C(0x89f000d2abafda8c, -4,  6.73523010531979398946e-02L),
a3u = LD80C(0xa8991563eca75f26, -6,  2.05808084277991211934e-02L),
a4u = LD80C(0xf2027e10634ce6b6, -8,  7.38555102796070454026e-03L),
a5u = LD80C(0xbd6eb76dd22187f4, -9,  2.89051035162703932972e-03L),
a6u = LD80C(0x9c562ab05e0458ed, -10,  1.19275351624639999297e-03L),
a7u = LD80C(0x859baed93ee48e46, -11,  5.09674593842117925320e-04L),
a8u = LD80C(0xe9f28a4432949af2, -13,  2.23109648015769155122e-04L),
a9u = LD80C(0xd12ad0d9b93c6bb0, -14,  9.97387167479808509830e-05L),
a10u= LD80C(0xb7522643c78a219b, -15,  4.37071076331030136818e-05L),
a11u= LD80C(0xca024dcdece2cb79, -16,  2.40813493372040143061e-05L),
a12u= LD80C(0xbb90fb6968ebdbf9, -19,  2.79495621083634031729e-06L),
a13u= LD80C(0xba1c9ffeeae07b37, -17,  1.10931287015513924136e-05L);
#define	a0	(a0u.e)
#define	a1	(a1u.e)
#define	a2	(a2u.e)
#define	a3	(a3u.e)
#define	a4	(a4u.e)
#define	a5	(a5u.e)
#define	a6	(a6u.e)
#define	a7	(a7u.e)
#define	a8	(a8u.e)
#define	a9	(a9u.e)
#define	a10	(a10u.e)
#define	a11	(a11u.e)
#define	a12	(a12u.e)
#define	a13	(a13u.e)
/*
 * Domain x in [tc-0.24, tc+0.28], range ~[-6.1205e-22, 6.1205e-22]:
 * |(lgamma(x) - tf) -  t(x - tc)| < 2**-70.5
 */
static const union IEEEl2bits
tcu  = LD80C(0xbb16c31ab5f1fb71, 0,  1.46163214496836234128e+00L),
tfu  = LD80C(0xf8cdcde61c520e0f, -4, -1.21486290535849608093e-01L),
ttu  = LD80C(0xd46ee54b27d4de99, -69, -2.81152980996018785880e-21L),
t0u  = LD80C(0x80b9406556a62a6b, -68,  3.40728634996055147231e-21L),
t1u  = LD80C(0xc7e9c6f6df3f8c39, -67, -1.05833162742737073665e-20L),
t2u  = LD80C(0xf7b95e4771c55d51, -2,  4.83836122723810583532e-01L),
t3u  = LD80C(0x97213c6e35e119ff, -3, -1.47587722994530691476e-01L),
t4u  = LD80C(0x845a14a6a81dc94b, -4,  6.46249402389135358063e-02L),
t5u  = LD80C(0x864d46fa89997796, -5, -3.27885410884846056084e-02L),
t6u  = LD80C(0x93373cbd00297438, -6,  1.79706751150707171293e-02L),
t7u  = LD80C(0xa8fcfca7eddc8d1d, -7, -1.03142230361450732547e-02L),
t8u  = LD80C(0xc7e7015ff4bc45af, -8,  6.10053603296546099193e-03L),
t9u  = LD80C(0xf178d2247adc5093, -9, -3.68456964904901200152e-03L),
t10u = LD80C(0x94188d58f12e5e9f, -9,  2.25976420273774583089e-03L),
t11u = LD80C(0xb7cbaef14e1406f1, -10, -1.40224943666225639823e-03L),
t12u = LD80C(0xe63a671e6704ea4d, -11,  8.78250640744776944887e-04L),
t13u = LD80C(0x914b6c9cae61783e, -11, -5.54255012657716808811e-04L),
t14u = LD80C(0xb858f5bdb79276fe, -12,  3.51614951536825927370e-04L),
t15u = LD80C(0xea73e744c34b9591, -13, -2.23591563824520112236e-04L),
t16u = LD80C(0x99aeabb0d67ba835, -13,  1.46562869351659194136e-04L),
t17u = LD80C(0xd7c6938325db2024, -14, -1.02889866046435680588e-04L),
t18u = LD80C(0xe24cb1e3b0474775, -15,  5.39540265505221957652e-05L);
#define	tc	(tcu.e)
#define	tf	(tfu.e)
#define	tt	(ttu.e)
#define	t0	(t0u.e)
#define	t1	(t1u.e)
#define	t2	(t2u.e)
#define	t3	(t3u.e)
#define	t4	(t4u.e)
#define	t5	(t5u.e)
#define	t6	(t6u.e)
#define	t7	(t7u.e)
#define	t8	(t8u.e)
#define	t9	(t9u.e)
#define	t10	(t10u.e)
#define	t11	(t11u.e)
#define	t12	(t12u.e)
#define	t13	(t13u.e)
#define	t14	(t14u.e)
#define	t15	(t15u.e)
#define	t16	(t16u.e)
#define	t17	(t17u.e)
#define	t18	(t18u.e)
/*
 * Domain y in [-0.1, 0.232], range ~[-8.1938e-22, 8.3815e-22]:
 * |(lgamma(1 + y) + 0.5 * y) / y - u(y) / v(y)| < 2**-71.2
 */
static const union IEEEl2bits
u0u = LD80C(0x9e233f1bed863d27, -4, -7.72156649015328606095e-02L),
u1u = LD80C(0x98280ee45e4ddd3d, -1,  5.94361239198682739769e-01L),
u2u = LD80C(0xe330c8ead4130733, 0,  1.77492629495841234275e+00L),
u3u = LD80C(0xd4a213f1a002ec52, 0,  1.66119622514818078064e+00L),
u4u = LD80C(0xa5a9ca6f5bc62163, -1,  6.47122051417476492989e-01L),
u5u = LD80C(0xc980e49cd5b019e6, -4,  9.83903751718671509455e-02L),
u6u = LD80C(0xff636a8bdce7025b, -9,  3.89691687802305743450e-03L),
v1u = LD80C(0xbd109c533a19fbf5, 1,  2.95413883330948556544e+00L),
v2u = LD80C(0xd295cbf96f31f099, 1,  3.29039286955665403176e+00L),
v3u = LD80C(0xdab8bcfee40496cb, 0,  1.70876276441416471410e+00L),
v4u = LD80C(0xd2f2dc3638567e9f, -2,  4.12009126299534668571e-01L),
v5u = LD80C(0xa07d9b0851070f41, -5,  3.91822868305682491442e-02L),
v6u = LD80C(0xe3cd8318f7adb2c4, -11,  8.68998648222144351114e-04L);
#define	u0	(u0u.e)
#define	u1	(u1u.e)
#define	u2	(u2u.e)
#define	u3	(u3u.e)
#define	u4	(u4u.e)
#define	u5	(u5u.e)
#define	u6	(u6u.e)
#define	v1	(v1u.e)
#define	v2	(v2u.e)
#define	v3	(v3u.e)
#define	v4	(v4u.e)
#define	v5	(v5u.e)
#define	v6	(v6u.e)
/*
 * Domain x in (2, 3], range ~[-3.3648e-22, 3.4416e-22]:
 * |(lgamma(y+2) - 0.5 * y) / y - s(y)/r(y)| < 2**-72.3
 * with y = x - 2.
 */
static const union IEEEl2bits
s0u = LD80C(0x9e233f1bed863d27, -4, -7.72156649015328606095e-02L),
s1u = LD80C(0xd3ff0dcc7fa91f94, -3,  2.07027640921219389860e-01L),
s2u = LD80C(0xb2bb62782478ef31, -2,  3.49085881391362090549e-01L),
s3u = LD80C(0xb49f7438c4611a74, -3,  1.76389518704213357954e-01L),
s4u = LD80C(0x9a957008fa27ecf9, -5,  3.77401710862930008071e-02L),
s5u = LD80C(0xda9b389a6ca7a7ac, -9,  3.33566791452943399399e-03L),
s6u = LD80C(0xbc7a2263faf59c14, -14,  8.98728786745638844395e-05L),
r1u = LD80C(0xbf5cff5b11477d4d, 0,  1.49502555796294337722e+00L),
r2u = LD80C(0xd9aec89de08e3da6, -1,  8.50323236984473285866e-01L),
r3u = LD80C(0xeab7ae5057c443f9, -3,  2.29216312078225806131e-01L),
r4u = LD80C(0xf29707d9bd2b1e37, -6,  2.96130326586640089145e-02L),
r5u = LD80C(0xd376c2f09736c5a3, -10,  1.61334161411590662495e-03L),
r6u = LD80C(0xc985983d0cd34e3d, -16,  2.40232770710953450636e-05L),
r7u = LD80C(0xe5c7a4f7fc2ef13d, -25, -5.34997929289167573510e-08L);
#define	s0	(s0u.e)
#define	s1	(s1u.e)
#define	s2	(s2u.e)
#define	s3	(s3u.e)
#define	s4	(s4u.e)
#define	s5	(s5u.e)
#define	s6	(s6u.e)
#define	r1	(r1u.e)
#define	r2	(r2u.e)
#define	r3	(r3u.e)
#define	r4	(r4u.e)
#define	r5	(r5u.e)
#define	r6	(r6u.e)
#define	r7	(r7u.e)
/*
 * Domain z in [8, 0x1p70], range ~[-3.0235e-22, 3.0563e-22]:
 * |lgamma(x) - (x - 0.5) * (log(x) - 1) - w(1/x)| < 2**-71.7
 */
static const union IEEEl2bits
w0u = LD80C(0xd67f1c864beb4a69, -2,  4.18938533204672741776e-01L),
w1u = LD80C(0xaaaaaaaaaaaaaaa1, -4,  8.33333333333333332678e-02L),
w2u = LD80C(0xb60b60b60b5491c9, -9, -2.77777777777760927870e-03L),
w3u = LD80C(0xd00d00cf58aede4c, -11,  7.93650793490637233668e-04L),
w4u = LD80C(0x9c09bf626783d4a5, -11, -5.95238023926039051268e-04L),
w5u = LD80C(0xdca7cadc5baa517b, -11,  8.41733700408000822962e-04L),
w6u = LD80C(0xfb060e361e1ffd07, -10, -1.91515849570245136604e-03L),
w7u = LD80C(0xcbd5101bb58d1f2b, -8,  6.22046743903262649294e-03L),
w8u = LD80C(0xad27a668d32c821b, -6, -2.11370706734662081843e-02L);
#define	w0	(w0u.e)
#define	w1	(w1u.e)
#define	w2	(w2u.e)
#define	w3	(w3u.e)
#define	w4	(w4u.e)
#define	w5	(w5u.e)
#define	w6	(w6u.e)
#define	w7	(w7u.e)
#define	w8	(w8u.e)

static long double
sin_pil(long double x)
{
	volatile long double vz;
	long double y,z;
	uint64_t n;
	uint16_t hx;

	y = -x;

	vz = y+0x1p63;
	z = vz-0x1p63;
	if (z == y)
	    return zero;

	vz = y+0x1p61;
	EXTRACT_LDBL80_WORDS(hx,n,vz);
	z = vz-0x1p61;
	if (z > y) {
	    z -= 0.25;			/* adjust to round down */
	    n--;
	}
	n &= 7;				/* octant of y mod 2 */
	y = y - z + n * 0.25;		/* y mod 2 */

	switch (n) {
	    case 0:   y =  __kernel_sinl(pi*y,zero,0); break;
	    case 1:
	    case 2:   y =  __kernel_cosl(pi*(0.5-y),zero); break;
	    case 3:
	    case 4:   y =  __kernel_sinl(pi*(one-y),zero,0); break;
	    case 5:
	    case 6:   y = -__kernel_cosl(pi*(y-1.5),zero); break;
	    default:  y =  __kernel_sinl(pi*(y-2.0),zero,0); break;
	    }
	return -y;
}

long double
lgammal_r(long double x, int *signgamp)
{
	long double nadj,p,p1,p2,q,r,t,w,y,z;
	uint64_t lx;
	int i;
	uint16_t hx,ix;

	EXTRACT_LDBL80_WORDS(hx,lx,x);

    /* purge +-Inf and NaNs */
	*signgamp = 1;
	ix = hx&0x7fff;
	if(ix==0x7fff) return x*x;

	ENTERI();

    /* purge +-0 and tiny arguments */
	*signgamp = 1-2*(hx>>15);
	if(ix<0x3fff-67) {		/* |x|<2**-(p+3), return -log(|x|) */
	    if((ix|lx)==0)
		RETURNI(one/vzero);
	    RETURNI(-logl(fabsl(x)));
	}

    /* purge negative integers and start evaluation for other x < 0 */
	if(hx&0x8000) {
	    *signgamp = 1;
	    if(ix>=0x3fff+63) 		/* |x|>=2**(p-1), must be -integer */
		RETURNI(one/vzero);
	    t = sin_pil(x);
	    if(t==zero) RETURNI(one/vzero); /* -integer */
	    nadj = logl(pi/fabsl(t*x));
	    if(t<zero) *signgamp = -1;
	    x = -x;
	}

    /* purge 1 and 2 */
	if((ix==0x3fff || ix==0x4000) && lx==0x8000000000000000ULL) r = 0;
    /* for x < 2.0 */
	else if(ix<0x4000) {
    /*
     * XXX Supposedly, one can use the following information to replace the
     * XXX FP rational expressions.  A similar approach is appropriate
     * XXX for ld128, but one (may need?) needs to consider llx, too.
     *
     * 8.9999961853027344e-01 3ffe e666600000000000
     * 7.3159980773925781e-01 3ffe bb4a200000000000
     * 2.3163998126983643e-01 3ffc ed33080000000000
     * 1.7316312789916992e+00 3fff dda6180000000000
     * 1.2316322326660156e+00 3fff 9da6200000000000
     */
	    if(x<8.9999961853027344e-01) {
		r = -logl(x);
		if(x>=7.3159980773925781e-01) {y = 1-x; i= 0;}
		else if(x>=2.3163998126983643e-01) {y= x-(tc-1); i=1;}
		else {y = x; i=2;}
	    } else {
		r = 0;
		if(x>=1.7316312789916992e+00) {y=2-x;i=0;}
		else if(x>=1.2316322326660156e+00) {y=x-tc;i=1;}
		else {y=x-1;i=2;}
	    }
	    switch(i) {
	      case 0:
		z = y*y;
		p1 = a0+z*(a2+z*(a4+z*(a6+z*(a8+z*(a10+z*a12)))));
		p2 = z*(a1+z*(a3+z*(a5+z*(a7+z*(a9+z*(a11+z*a13))))));
		p  = y*p1+p2;
		r  += p-y/2; break;
	      case 1:
		p = t0+y*t1+tt+y*y*(t2+y*(t3+y*(t4+y*(t5+y*(t6+y*(t7+y*(t8+
		    y*(t9+y*(t10+y*(t11+y*(t12+y*(t13+y*(t14+y*(t15+y*(t16+
		    y*(t17+y*t18))))))))))))))));
		r += tf + p; break;
	      case 2:
		p1 = y*(u0+y*(u1+y*(u2+y*(u3+y*(u4+y*(u5+y*u6))))));
		p2 = 1+y*(v1+y*(v2+y*(v3+y*(v4+y*(v5+y*v6)))));
		r += p1/p2-y/2;
	    }
	}
    /* x < 8.0 */
	else if(ix<0x4002) {
	    i = x;
	    y = x-i;
	    p = y*(s0+y*(s1+y*(s2+y*(s3+y*(s4+y*(s5+y*s6))))));
	    q = 1+y*(r1+y*(r2+y*(r3+y*(r4+y*(r5+y*(r6+y*r7))))));
	    r = y/2+p/q;
	    z = 1;	/* lgamma(1+s) = log(s) + lgamma(s) */
	    switch(i) {
	    case 7: z *= (y+6);		/* FALLTHRU */
	    case 6: z *= (y+5);		/* FALLTHRU */
	    case 5: z *= (y+4);		/* FALLTHRU */
	    case 4: z *= (y+3);		/* FALLTHRU */
	    case 3: z *= (y+2);		/* FALLTHRU */
		    r += logl(z); break;
	    }
    /* 8.0 <= x < 2**(p+3) */
	} else if (ix<0x3fff+67) {
	    t = logl(x);
	    z = one/x;
	    y = z*z;
	    w = w0+z*(w1+y*(w2+y*(w3+y*(w4+y*(w5+y*(w6+y*(w7+y*w8)))))));
	    r = (x-half)*(t-one)+w;
    /* 2**(p+3) <= x <= inf */
	} else 
	    r =  x*(logl(x)-1);
	if(hx&0x8000) r = nadj - r;
	RETURNI(r);
}
