/*
   This file is adapted from amd64-51/fe25519_invert.c:
   Loops of squares are replaced by nsquares for better performance.
*/

#include "fe51.h"

#ifdef HAVE_AVX_ASM

#define fe51_square(x, y) fe51_nsquare(x, y, 1)

void
fe51_invert(fe51 *r, const fe51 *x)
{
    fe51 z2;
    fe51 z9;
    fe51 z11;
    fe51 z2_5_0;
    fe51 z2_10_0;
    fe51 z2_20_0;
    fe51 z2_50_0;
    fe51 z2_100_0;
    fe51 t;

    /* 2 */ fe51_square(&z2,x);
    /* 4 */ fe51_square(&t,&z2);
    /* 8 */ fe51_square(&t,&t);
    /* 9 */ fe51_mul(&z9,&t,x);
    /* 11 */ fe51_mul(&z11,&z9,&z2);
    /* 22 */ fe51_square(&t,&z11);
    /* 2^5 - 2^0 = 31 */ fe51_mul(&z2_5_0,&t,&z9);

    /* 2^10 - 2^5 */ fe51_nsquare(&t,&z2_5_0, 5);
    /* 2^10 - 2^0 */ fe51_mul(&z2_10_0,&t,&z2_5_0);

    /* 2^20 - 2^10 */ fe51_nsquare(&t,&z2_10_0, 10);
    /* 2^20 - 2^0 */ fe51_mul(&z2_20_0,&t,&z2_10_0);

    /* 2^40 - 2^20 */ fe51_nsquare(&t,&z2_20_0, 20);
    /* 2^40 - 2^0 */ fe51_mul(&t,&t,&z2_20_0);

    /* 2^50 - 2^10 */ fe51_nsquare(&t,&t,10);
    /* 2^50 - 2^0 */ fe51_mul(&z2_50_0,&t,&z2_10_0);

    /* 2^100 - 2^50 */ fe51_nsquare(&t,&z2_50_0, 50);
    /* 2^100 - 2^0 */ fe51_mul(&z2_100_0,&t,&z2_50_0);

    /* 2^200 - 2^100 */ fe51_nsquare(&t,&z2_100_0, 100);
    /* 2^200 - 2^0 */ fe51_mul(&t,&t,&z2_100_0);

    /* 2^250 - 2^50 */ fe51_nsquare(&t,&t, 50);
    /* 2^250 - 2^0 */ fe51_mul(&t,&t,&z2_50_0);

    /* 2^255 - 2^5 */ fe51_nsquare(&t,&t,5);
    /* 2^255 - 21 */ fe51_mul(r,&t,&z11);
}

#endif
