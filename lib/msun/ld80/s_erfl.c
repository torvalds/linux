/* @(#)s_erf.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * See s_erf.c for complete comments.
 *
 * Converted to long double by Steven G. Kargl.
 */
#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

/* XXX Prevent compilers from erroneously constant folding: */
static const volatile long double tiny = 0x1p-10000L;

static const double
half= 0.5,
one = 1,
two = 2;
/*
 * In the domain [0, 2**-34], only the first term in the power series
 * expansion of erf(x) is used.  The magnitude of the first neglected
 * terms is less than 2**-102.
 */
static const union IEEEl2bits
efxu  = LD80C(0x8375d410a6db446c, -3,  1.28379167095512573902e-1L),
efx8u = LD80C(0x8375d410a6db446c,  0,  1.02703333676410059122e+0L),
/*
 * Domain [0, 0.84375], range ~[-1.423e-22, 1.423e-22]:
 * |(erf(x) - x)/x - pp(x)/qq(x)| < 2**-72.573
 */
pp0u  = LD80C(0x8375d410a6db446c, -3,   1.28379167095512573902e-1L),
pp1u  = LD80C(0xa46c7d09ec3d0cec, -2,  -3.21140201054840180596e-1L),
pp2u  = LD80C(0x9b31e66325576f86, -5,  -3.78893851760347812082e-2L),
pp3u  = LD80C(0x804ac72c9a0b97dd, -7,  -7.83032847030604679616e-3L),
pp4u  = LD80C(0x9f42bcbc3d5a601d, -12, -3.03765663857082048459e-4L),
pp5u  = LD80C(0x9ec4ad6193470693, -16, -1.89266527398167917502e-5L),
qq1u  = LD80C(0xdb4b8eb713188d6b, -2,   4.28310832832310510579e-1L),
qq2u  = LD80C(0xa5750835b2459bd1, -4,   8.07896272074540216658e-2L),
qq3u  = LD80C(0x8b85d6bd6a90b51c, -7,   8.51579638189385354266e-3L),
qq4u  = LD80C(0x87332f82cff4ff96, -11,  5.15746855583604912827e-4L),
qq5u  = LD80C(0x83466cb6bf9dca00, -16,  1.56492109706256700009e-5L),
qq6u  = LD80C(0xf5bf98c2f996bf63, -24,  1.14435527803073879724e-7L);
#define	efx	(efxu.e)
#define	efx8	(efx8u.e)
#define	pp0	(pp0u.e)
#define	pp1	(pp1u.e)
#define	pp2	(pp2u.e)
#define	pp3	(pp3u.e)
#define	pp4	(pp4u.e)
#define	pp5	(pp5u.e)
#define	qq1	(qq1u.e)
#define	qq2	(qq2u.e)
#define	qq3	(qq3u.e)
#define	qq4	(qq4u.e)
#define	qq5	(qq5u.e)
#define	qq6	(qq6u.e)
static const union IEEEl2bits
erxu  = LD80C(0xd7bb3d0000000000, -1,  8.42700779438018798828e-1L),
/*
 * Domain [0.84375, 1.25], range ~[-8.132e-22, 8.113e-22]:
 * |(erf(x) - erx) - pa(x)/qa(x)| < 2**-71.762
 */
pa0u  = LD80C(0xe8211158da02c692, -27,  1.35116960705131296711e-8L),
pa1u  = LD80C(0xd488f89f36988618, -2,   4.15107507167065612570e-1L),
pa2u  = LD80C(0xece74f8c63fa3942, -4,  -1.15675565215949226989e-1L),
pa3u  = LD80C(0xc8d31e020727c006, -4,   9.80589241379624665791e-2L),
pa4u  = LD80C(0x985d5d5fafb0551f, -5,   3.71984145558422368847e-2L),
pa5u  = LD80C(0xa5b6c4854d2f5452, -8,  -5.05718799340957673661e-3L),
pa6u  = LD80C(0x85c8d58fe3993a47, -8,   4.08277919612202243721e-3L),
pa7u  = LD80C(0xddbfbc23677b35cf, -13,  2.11476292145347530794e-4L),
qa1u  = LD80C(0xb8a977896f5eff3f, -1,   7.21335860303380361298e-1L),
qa2u  = LD80C(0x9fcd662c3d4eac86, -1,   6.24227891731886593333e-1L),
qa3u  = LD80C(0x9d0b618eac67ba07, -2,   3.06727455774491855801e-1L),
qa4u  = LD80C(0x881a4293f6d6c92d, -3,   1.32912674218195890535e-1L),
qa5u  = LD80C(0xbab144f07dea45bf, -5,   4.55792134233613027584e-2L),
qa6u  = LD80C(0xa6c34ba438bdc900, -7,   1.01783980070527682680e-2L),
qa7u  = LD80C(0x8fa866dc20717a91, -9,   2.19204436518951438183e-3L);
#define erx	(erxu.e)
#define pa0	(pa0u.e)
#define pa1	(pa1u.e)
#define pa2	(pa2u.e)
#define pa3	(pa3u.e)
#define pa4	(pa4u.e)
#define pa5	(pa5u.e)
#define pa6	(pa6u.e)
#define pa7	(pa7u.e)
#define qa1	(qa1u.e)
#define qa2	(qa2u.e)
#define qa3	(qa3u.e)
#define qa4	(qa4u.e)
#define qa5	(qa5u.e)
#define qa6	(qa6u.e)
#define qa7	(qa7u.e)
static const union IEEEl2bits
/*
 * Domain [1.25,2.85715], range ~[-2.334e-22,2.334e-22]:
 * |log(x*erfc(x)) + x**2 + 0.5625 - ra(x)/sa(x)| < 2**-71.860
 */
ra0u  = LD80C(0xa1a091e0fb4f335a, -7, -9.86494298915814308249e-3L),
ra1u  = LD80C(0xc2b0d045ae37df6b, -1, -7.60510460864878271275e-1L),
ra2u  = LD80C(0xf2cec3ee7da636c5, 3,  -1.51754798236892278250e+1L),
ra3u  = LD80C(0x813cc205395adc7d, 7,  -1.29237335516455333420e+2L),
ra4u  = LD80C(0x8737c8b7b4062c2f, 9,  -5.40871625829510494776e+2L),
ra5u  = LD80C(0x8ffe5383c08d4943, 10, -1.15194769466026108551e+3L),
ra6u  = LD80C(0x983573e64d5015a9, 10, -1.21767039790249025544e+3L),
ra7u  = LD80C(0x92a794e763a6d4db, 9,  -5.86618463370624636688e+2L),
ra8u  = LD80C(0xd5ad1fae77c3d9a3, 6,  -1.06838132335777049840e+2L),
ra9u  = LD80C(0x934c1a247807bb9c, 2,  -4.60303980944467334806e+0L),
sa1u  = LD80C(0xd342f90012bb1189, 4,   2.64077014928547064865e+1L),
sa2u  = LD80C(0x839be13d9d5da883, 8,   2.63217811300123973067e+2L),
sa3u  = LD80C(0x9f8cba6d1ae1b24b, 10,  1.27639775710344617587e+3L),
sa4u  = LD80C(0xcaa83f403713e33e, 11,  3.24251544209971162003e+3L),
sa5u  = LD80C(0x8796aff2f3c47968, 12,  4.33883591261332837874e+3L),
sa6u  = LD80C(0xb6ef97f9c753157b, 11,  2.92697460344182158454e+3L),
sa7u  = LD80C(0xe02aee5f83773d1c, 9,   8.96670799139389559818e+2L),
sa8u  = LD80C(0xc82b83855b88e07e, 6,   1.00084987800048510018e+2L),
sa9u  = LD80C(0x92f030aefadf28ad, 1,   2.29591004455459083843e+0L);
#define ra0	(ra0u.e)
#define ra1	(ra1u.e)
#define ra2	(ra2u.e)
#define ra3	(ra3u.e)
#define ra4	(ra4u.e)
#define ra5	(ra5u.e)
#define ra6	(ra6u.e)
#define ra7	(ra7u.e)
#define ra8	(ra8u.e)
#define ra9	(ra9u.e)
#define sa1	(sa1u.e)
#define sa2	(sa2u.e)
#define sa3	(sa3u.e)
#define sa4	(sa4u.e)
#define sa5	(sa5u.e)
#define sa6	(sa6u.e)
#define sa7	(sa7u.e)
#define sa8	(sa8u.e)
#define sa9	(sa9u.e)
/*
 * Domain [2.85715,7], range ~[-8.323e-22,8.390e-22]:
 * |log(x*erfc(x)) + x**2 + 0.5625 - rb(x)/sb(x)| < 2**-70.326
 */
static const union IEEEl2bits
rb0u = LD80C(0xa1a091cf43abcd26, -7, -9.86494292470284646962e-3L),
rb1u = LD80C(0xd19d2df1cbb8da0a, -1, -8.18804618389296662837e-1L),
rb2u = LD80C(0x9a4dd1383e5daf5b, 4,  -1.92879967111618594779e+1L),
rb3u = LD80C(0xbff0ae9fc0751de6, 7,  -1.91940164551245394969e+2L),
rb4u = LD80C(0xdde08465310b472b, 9,  -8.87508080766577324539e+2L),
rb5u = LD80C(0xe796e1d38c8c70a9, 10, -1.85271506669474503781e+3L),
rb6u = LD80C(0xbaf655a76e0ab3b5, 10, -1.49569795581333675349e+3L),
rb7u = LD80C(0x95d21e3e75503c21, 8,  -2.99641547972948019157e+2L),
sb1u = LD80C(0x814487ed823c8cbd, 5,   3.23169247732868256569e+1L),
sb2u = LD80C(0xbe4bfbb1301304be, 8,   3.80593618534539961773e+2L),
sb3u = LD80C(0x809c4ade46b927c7, 11,  2.05776827838541292848e+3L),
sb4u = LD80C(0xa55284359f3395a8, 12,  5.29031455540062116327e+3L),
sb5u = LD80C(0xbcfa72da9b820874, 12,  6.04730608102312640462e+3L),
sb6u = LD80C(0x9d09a35988934631, 11,  2.51260238030767176221e+3L),
sb7u = LD80C(0xd675bbe542c159fa, 7,   2.14459898308561015684e+2L);
#define rb0	(rb0u.e)
#define rb1	(rb1u.e)
#define rb2	(rb2u.e)
#define rb3	(rb3u.e)
#define rb4	(rb4u.e)
#define rb5	(rb5u.e)
#define rb6	(rb6u.e)
#define rb7	(rb7u.e)
#define sb1	(sb1u.e)
#define sb2	(sb2u.e)
#define sb3	(sb3u.e)
#define sb4	(sb4u.e)
#define sb5	(sb5u.e)
#define sb6	(sb6u.e)
#define sb7	(sb7u.e)
/*
 * Domain [7,108], range ~[-4.422e-22,4.422e-22]:
 * |log(x*erfc(x)) + x**2 + 0.5625 - rc(x)/sc(x)| < 2**-70.938
 */
static const union IEEEl2bits
/* err = -4.422092275318925082e-22 -70.937689 */
rc0u = LD80C(0xa1a091cf437a17ad, -7, -9.86494292470008707260e-3L),
rc1u = LD80C(0xbe79c5a978122b00, -1, -7.44045595049165939261e-1L),
rc2u = LD80C(0xdb26f9bbe31a2794, 3,  -1.36970155085888424425e+1L),
rc3u = LD80C(0xb5f69a38f5747ac8, 6,  -9.09816453742625888546e+1L),
rc4u = LD80C(0xd79676d970d0a21a, 7,  -2.15587750997584074147e+2L),
rc5u = LD80C(0xfe528153c45ec97c, 6,  -1.27161142938347796666e+2L),
sc1u = LD80C(0xc5e8cd46d5604a96, 4,   2.47386727842204312937e+1L),
sc2u = LD80C(0xc5f0f5a5484520eb, 7,   1.97941248254913378865e+2L),
sc3u = LD80C(0x964e3c7b34db9170, 9,   6.01222441484087787522e+2L),
sc4u = LD80C(0x99be1b89faa0596a, 9,   6.14970430845978077827e+2L),
sc5u = LD80C(0xf80dfcbf37ffc5ea, 6,   1.24027318931184605891e+2L);
#define rc0	(rc0u.e)
#define rc1	(rc1u.e)
#define rc2	(rc2u.e)
#define rc3	(rc3u.e)
#define rc4	(rc4u.e)
#define rc5	(rc5u.e)
#define sc1	(sc1u.e)
#define sc2	(sc2u.e)
#define sc3	(sc3u.e)
#define sc4	(sc4u.e)
#define sc5	(sc5u.e)

long double
erfl(long double x)
{
	long double ax,R,S,P,Q,s,y,z,r;
	uint64_t lx;
	int32_t i;
	uint16_t hx;

	EXTRACT_LDBL80_WORDS(hx, lx, x);

	if((hx & 0x7fff) == 0x7fff) {	/* erfl(nan)=nan */
		i = (hx>>15)<<1;
		return (1-i)+one/x;	/* erfl(+-inf)=+-1 */
	}

	ENTERI();

	ax = fabsl(x);
	if(ax < 0.84375) {
	    if(ax < 0x1p-34L) {
	        if(ax < 0x1p-16373L)	
		    RETURNI((8*x+efx8*x)/8);	/* avoid spurious underflow */
		RETURNI(x + efx*x);
	    }
	    z = x*x;
	    r = pp0+z*(pp1+z*(pp2+z*(pp3+z*(pp4+z*pp5))));
	    s = one+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*(qq5+z*qq6)))));
	    y = r/s;
	    RETURNI(x + x*y);
	}
	if(ax < 1.25) {
	    s = ax-one;
	    P = pa0+s*(pa1+s*(pa2+s*(pa3+s*(pa4+s*(pa5+s*(pa6+s*pa7))))));
	    Q = one+s*(qa1+s*(qa2+s*(qa3+s*(qa4+s*(qa5+s*(qa6+s*qa7))))));
	    if(x>=0) RETURNI(erx + P/Q); else RETURNI(-erx - P/Q);
	}
	if(ax >= 7) {			/* inf>|x|>= 7 */
	    if(x>=0) RETURNI(one-tiny); else RETURNI(tiny-one);
	}
	s = one/(ax*ax);
	if(ax < 2.85715) {	/* |x| < 2.85715 */
	    R=ra0+s*(ra1+s*(ra2+s*(ra3+s*(ra4+s*(ra5+s*(ra6+s*(ra7+
		s*(ra8+s*ra9))))))));
	    S=one+s*(sa1+s*(sa2+s*(sa3+s*(sa4+s*(sa5+s*(sa6+s*(sa7+
		s*(sa8+s*sa9))))))));
	} else {	/* |x| >= 2.85715 */
	    R=rb0+s*(rb1+s*(rb2+s*(rb3+s*(rb4+s*(rb5+s*(rb6+s*rb7))))));
	    S=one+s*(sb1+s*(sb2+s*(sb3+s*(sb4+s*(sb5+s*(sb6+s*sb7))))));
	}
	z=(float)ax;
	r=expl(-z*z-0.5625)*expl((z-ax)*(z+ax)+R/S);
	if(x>=0) RETURNI(one-r/ax); else RETURNI(r/ax-one);
}

long double
erfcl(long double x)
{
	long double ax,R,S,P,Q,s,y,z,r;
	uint64_t lx;
	uint16_t hx;

	EXTRACT_LDBL80_WORDS(hx, lx, x);

	if((hx & 0x7fff) == 0x7fff) {	/* erfcl(nan)=nan */
					/* erfcl(+-inf)=0,2 */
	    return ((hx>>15)<<1)+one/x;
	}

	ENTERI();

	ax = fabsl(x);
	if(ax < 0.84375L) {
	    if(ax < 0x1p-34L)
		RETURNI(one-x);
	    z = x*x;
	    r = pp0+z*(pp1+z*(pp2+z*(pp3+z*(pp4+z*pp5))));
	    s = one+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*(qq5+z*qq6)))));
	    y = r/s;
	    if(ax < 0.25L) {  	/* x<1/4 */
		RETURNI(one-(x+x*y));
	    } else {
		r = x*y;
		r += (x-half);
	       RETURNI(half - r);
	    }
	}
	if(ax < 1.25L) {
	    s = ax-one;
	    P = pa0+s*(pa1+s*(pa2+s*(pa3+s*(pa4+s*(pa5+s*(pa6+s*pa7))))));
	    Q = one+s*(qa1+s*(qa2+s*(qa3+s*(qa4+s*(qa5+s*(qa6+s*qa7))))));
	    if(x>=0) {
	        z  = one-erx; RETURNI(z - P/Q);
	    } else {
		z = (erx+P/Q); RETURNI(one+z);
	    }
	}

	if(ax < 108) {			/* |x| < 108 */
 	    s = one/(ax*ax);
	    if(ax < 2.85715) {		/* |x| < 2.85715 */
		R=ra0+s*(ra1+s*(ra2+s*(ra3+s*(ra4+s*(ra5+s*(ra6+s*(ra7+
		    s*(ra8+s*ra9))))))));
		S=one+s*(sa1+s*(sa2+s*(sa3+s*(sa4+s*(sa5+s*(sa6+s*(sa7+
		    s*(sa8+s*sa9))))))));
	    } else if(ax < 7) {		/* | |x| < 7 */
		R=rb0+s*(rb1+s*(rb2+s*(rb3+s*(rb4+s*(rb5+s*(rb6+s*rb7))))));
		S=one+s*(sb1+s*(sb2+s*(sb3+s*(sb4+s*(sb5+s*(sb6+s*sb7))))));
	    } else {
		if(x < -7) RETURNI(two-tiny);/* x < -7 */
		R=rc0+s*(rc1+s*(rc2+s*(rc3+s*(rc4+s*rc5))));
		S=one+s*(sc1+s*(sc2+s*(sc3+s*(sc4+s*sc5))));
	    }
	    z = (float)ax;
	    r = expl(-z*z-0.5625)*expl((z-ax)*(z+ax)+R/S);
	    if(x>0) RETURNI(r/ax); else RETURNI(two-r/ax);
	} else {
	    if(x>0) RETURNI(tiny*tiny); else RETURNI(two-tiny);
	}
}
