/* s_erff.c -- float version of s_erf.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */

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

#include "math.h"
#include "math_private.h"

static const float
tiny	    = 1e-30,
half=  5.0000000000e-01, /* 0x3F000000 */
one =  1.0000000000e+00, /* 0x3F800000 */
two =  2.0000000000e+00, /* 0x40000000 */
	/* c = (subfloat)0.84506291151 */
erx =  8.4506291151e-01, /* 0x3f58560b */
/*
 * Coefficients for approximation to  erf on [0,0.84375]
 */
efx =  1.2837916613e-01, /* 0x3e0375d4 */
efx8=  1.0270333290e+00, /* 0x3f8375d4 */
pp0  =  1.2837916613e-01, /* 0x3e0375d4 */
pp1  = -3.2504209876e-01, /* 0xbea66beb */
pp2  = -2.8481749818e-02, /* 0xbce9528f */
pp3  = -5.7702702470e-03, /* 0xbbbd1489 */
pp4  = -2.3763017452e-05, /* 0xb7c756b1 */
qq1  =  3.9791721106e-01, /* 0x3ecbbbce */
qq2  =  6.5022252500e-02, /* 0x3d852a63 */
qq3  =  5.0813062117e-03, /* 0x3ba68116 */
qq4  =  1.3249473704e-04, /* 0x390aee49 */
qq5  = -3.9602282413e-06, /* 0xb684e21a */
/*
 * Coefficients for approximation to  erf  in [0.84375,1.25] 
 */
pa0  = -2.3621185683e-03, /* 0xbb1acdc6 */
pa1  =  4.1485610604e-01, /* 0x3ed46805 */
pa2  = -3.7220788002e-01, /* 0xbebe9208 */
pa3  =  3.1834661961e-01, /* 0x3ea2fe54 */
pa4  = -1.1089469492e-01, /* 0xbde31cc2 */
pa5  =  3.5478305072e-02, /* 0x3d1151b3 */
pa6  = -2.1663755178e-03, /* 0xbb0df9c0 */
qa1  =  1.0642088205e-01, /* 0x3dd9f331 */
qa2  =  5.4039794207e-01, /* 0x3f0a5785 */
qa3  =  7.1828655899e-02, /* 0x3d931ae7 */
qa4  =  1.2617121637e-01, /* 0x3e013307 */
qa5  =  1.3637083583e-02, /* 0x3c5f6e13 */
qa6  =  1.1984500103e-02, /* 0x3c445aa3 */
/*
 * Coefficients for approximation to  erfc in [1.25,1/0.35]
 */
ra0  = -9.8649440333e-03, /* 0xbc21a093 */
ra1  = -6.9385856390e-01, /* 0xbf31a0b7 */
ra2  = -1.0558626175e+01, /* 0xc128f022 */
ra3  = -6.2375331879e+01, /* 0xc2798057 */
ra4  = -1.6239666748e+02, /* 0xc322658c */
ra5  = -1.8460508728e+02, /* 0xc3389ae7 */
ra6  = -8.1287437439e+01, /* 0xc2a2932b */
ra7  = -9.8143291473e+00, /* 0xc11d077e */
sa1  =  1.9651271820e+01, /* 0x419d35ce */
sa2  =  1.3765776062e+02, /* 0x4309a863 */
sa3  =  4.3456588745e+02, /* 0x43d9486f */
sa4  =  6.4538726807e+02, /* 0x442158c9 */
sa5  =  4.2900814819e+02, /* 0x43d6810b */
sa6  =  1.0863500214e+02, /* 0x42d9451f */
sa7  =  6.5702495575e+00, /* 0x40d23f7c */
sa8  = -6.0424413532e-02, /* 0xbd777f97 */
/*
 * Coefficients for approximation to  erfc in [1/.35,28]
 */
rb0  = -9.8649431020e-03, /* 0xbc21a092 */
rb1  = -7.9928326607e-01, /* 0xbf4c9dd4 */
rb2  = -1.7757955551e+01, /* 0xc18e104b */
rb3  = -1.6063638306e+02, /* 0xc320a2ea */
rb4  = -6.3756646729e+02, /* 0xc41f6441 */
rb5  = -1.0250950928e+03, /* 0xc480230b */
rb6  = -4.8351919556e+02, /* 0xc3f1c275 */
sb1  =  3.0338060379e+01, /* 0x41f2b459 */
sb2  =  3.2579251099e+02, /* 0x43a2e571 */
sb3  =  1.5367296143e+03, /* 0x44c01759 */
sb4  =  3.1998581543e+03, /* 0x4547fdbb */
sb5  =  2.5530502930e+03, /* 0x451f90ce */
sb6  =  4.7452853394e+02, /* 0x43ed43a7 */
sb7  = -2.2440952301e+01; /* 0xc1b38712 */

float
erff(float x) 
{
	int32_t hx,ix,i;
	float R,S,P,Q,s,y,z,r;
	GET_FLOAT_WORD(hx,x);
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) {		/* erf(nan)=nan */
	    i = ((u_int32_t)hx>>31)<<1;
	    return (float)(1-i)+one/x;	/* erf(+-inf)=+-1 */
	}

	if(ix < 0x3f580000) {		/* |x|<0.84375 */
	    if(ix < 0x31800000) { 	/* |x|<2**-28 */
	        if (ix < 0x04000000) 
		    /*avoid underflow */
		    return (float)0.125*((float)8.0*x+efx8*x);
		return x + efx*x;
	    }
	    z = x*x;
	    r = pp0+z*(pp1+z*(pp2+z*(pp3+z*pp4)));
	    s = one+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*qq5))));
	    y = r/s;
	    return x + x*y;
	}
	if(ix < 0x3fa00000) {		/* 0.84375 <= |x| < 1.25 */
	    s = fabsf(x)-one;
	    P = pa0+s*(pa1+s*(pa2+s*(pa3+s*(pa4+s*(pa5+s*pa6)))));
	    Q = one+s*(qa1+s*(qa2+s*(qa3+s*(qa4+s*(qa5+s*qa6)))));
	    if(hx>=0) return erx + P/Q; else return -erx - P/Q;
	}
	if (ix >= 0x40c00000) {		/* inf>|x|>=6 */
	    if(hx>=0) return one-tiny; else return tiny-one;
	}
	x = fabsf(x);
 	s = one/(x*x);
	if(ix< 0x4036DB6E) {	/* |x| < 1/0.35 */
	    R=ra0+s*(ra1+s*(ra2+s*(ra3+s*(ra4+s*(
				ra5+s*(ra6+s*ra7))))));
	    S=one+s*(sa1+s*(sa2+s*(sa3+s*(sa4+s*(
				sa5+s*(sa6+s*(sa7+s*sa8)))))));
	} else {	/* |x| >= 1/0.35 */
	    R=rb0+s*(rb1+s*(rb2+s*(rb3+s*(rb4+s*(
				rb5+s*rb6)))));
	    S=one+s*(sb1+s*(sb2+s*(sb3+s*(sb4+s*(
				sb5+s*(sb6+s*sb7))))));
	}
	GET_FLOAT_WORD(ix,x);
	SET_FLOAT_WORD(z,ix&0xfffff000);
	r  =  expf(-z*z-(float)0.5625)*expf((z-x)*(z+x)+R/S);
	if(hx>=0) return one-r/x; else return  r/x-one;
}

float
erfcf(float x) 
{
	int32_t hx,ix;
	float R,S,P,Q,s,y,z,r;
	GET_FLOAT_WORD(hx,x);
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) {			/* erfc(nan)=nan */
						/* erfc(+-inf)=0,2 */
	    return (float)(((u_int32_t)hx>>31)<<1)+one/x;
	}

	if(ix < 0x3f580000) {		/* |x|<0.84375 */
	    if(ix < 0x23800000)  	/* |x|<2**-56 */
		return one-x;
	    z = x*x;
	    r = pp0+z*(pp1+z*(pp2+z*(pp3+z*pp4)));
	    s = one+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*qq5))));
	    y = r/s;
	    if(hx < 0x3e800000) {  	/* x<1/4 */
		return one-(x+x*y);
	    } else {
		r = x*y;
		r += (x-half);
	        return half - r ;
	    }
	}
	if(ix < 0x3fa00000) {		/* 0.84375 <= |x| < 1.25 */
	    s = fabsf(x)-one;
	    P = pa0+s*(pa1+s*(pa2+s*(pa3+s*(pa4+s*(pa5+s*pa6)))));
	    Q = one+s*(qa1+s*(qa2+s*(qa3+s*(qa4+s*(qa5+s*qa6)))));
	    if(hx>=0) {
	        z  = one-erx; return z - P/Q; 
	    } else {
		z = erx+P/Q; return one+z;
	    }
	}
	if (ix < 0x41e00000) {		/* |x|<28 */
	    x = fabsf(x);
 	    s = one/(x*x);
	    if(ix< 0x4036DB6D) {	/* |x| < 1/.35 ~ 2.857143*/
	        R=ra0+s*(ra1+s*(ra2+s*(ra3+s*(ra4+s*(
				ra5+s*(ra6+s*ra7))))));
	        S=one+s*(sa1+s*(sa2+s*(sa3+s*(sa4+s*(
				sa5+s*(sa6+s*(sa7+s*sa8)))))));
	    } else {			/* |x| >= 1/.35 ~ 2.857143 */
		if(hx<0&&ix>=0x40c00000) return two-tiny;/* x < -6 */
	        R=rb0+s*(rb1+s*(rb2+s*(rb3+s*(rb4+s*(
				rb5+s*rb6)))));
	        S=one+s*(sb1+s*(sb2+s*(sb3+s*(sb4+s*(
				sb5+s*(sb6+s*sb7))))));
	    }
	    GET_FLOAT_WORD(ix,x);
	    SET_FLOAT_WORD(z,ix&0xfffff000);
	    r  =  expf(-z*z-(float)0.5625) * expf((z-x)*(z+x)+R/S);
	    if(hx>0) return r/x; else return two-r/x;
	} else {
	    if(hx>0) return tiny*tiny; else return two-tiny;
	}
}
