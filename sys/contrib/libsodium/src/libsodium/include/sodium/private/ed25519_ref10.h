#ifndef ed25519_ref10_H
#define ed25519_ref10_H

#include <stddef.h>
#include <stdint.h>

/*
 fe means field element.
 Here the field is \Z/(2^255-19).
 */

#ifdef HAVE_TI_MODE
typedef uint64_t fe25519[5];
#else
typedef int32_t fe25519[10];
#endif

void fe25519_invert(fe25519 out, const fe25519 z);
void fe25519_frombytes(fe25519 h, const unsigned char *s);
void fe25519_tobytes(unsigned char *s, const fe25519 h);

#ifdef HAVE_TI_MODE
# include "ed25519_ref10_fe_51.h"
#else
# include "ed25519_ref10_fe_25_5.h"
#endif


/*
 ge means group element.

 Here the group is the set of pairs (x,y) of field elements
 satisfying -x^2 + y^2 = 1 + d x^2y^2
 where d = -121665/121666.

 Representations:
 ge25519_p2 (projective): (X:Y:Z) satisfying x=X/Z, y=Y/Z
 ge25519_p3 (extended): (X:Y:Z:T) satisfying x=X/Z, y=Y/Z, XY=ZT
 ge25519_p1p1 (completed): ((X:Z),(Y:T)) satisfying x=X/Z, y=Y/T
 ge25519_precomp (Duif): (y+x,y-x,2dxy)
 */

typedef struct {
    fe25519 X;
    fe25519 Y;
    fe25519 Z;
} ge25519_p2;

typedef struct {
    fe25519 X;
    fe25519 Y;
    fe25519 Z;
    fe25519 T;
} ge25519_p3;

typedef struct {
    fe25519 X;
    fe25519 Y;
    fe25519 Z;
    fe25519 T;
} ge25519_p1p1;

typedef struct {
    fe25519 yplusx;
    fe25519 yminusx;
    fe25519 xy2d;
} ge25519_precomp;

typedef struct {
    fe25519 YplusX;
    fe25519 YminusX;
    fe25519 Z;
    fe25519 T2d;
} ge25519_cached;

void ge25519_tobytes(unsigned char *s, const ge25519_p2 *h);

void ge25519_p3_tobytes(unsigned char *s, const ge25519_p3 *h);

int ge25519_frombytes(ge25519_p3 *h, const unsigned char *s);

int ge25519_frombytes_negate_vartime(ge25519_p3 *h, const unsigned char *s);

void ge25519_p3_to_cached(ge25519_cached *r, const ge25519_p3 *p);

void ge25519_p1p1_to_p2(ge25519_p2 *r, const ge25519_p1p1 *p);

void ge25519_p1p1_to_p3(ge25519_p3 *r, const ge25519_p1p1 *p);

void ge25519_add(ge25519_p1p1 *r, const ge25519_p3 *p, const ge25519_cached *q);

void ge25519_sub(ge25519_p1p1 *r, const ge25519_p3 *p, const ge25519_cached *q);

void ge25519_scalarmult_base(ge25519_p3 *h, const unsigned char *a);

void ge25519_double_scalarmult_vartime(ge25519_p2 *r, const unsigned char *a,
                                       const ge25519_p3 *A,
                                       const unsigned char *b);

void ge25519_scalarmult(ge25519_p3 *h, const unsigned char *a,
                        const ge25519_p3 *p);

int ge25519_is_canonical(const unsigned char *s);

int ge25519_is_on_curve(const ge25519_p3 *p);

int ge25519_is_on_main_subgroup(const ge25519_p3 *p);

int ge25519_has_small_order(const unsigned char s[32]);

void ge25519_from_uniform(unsigned char s[32], const unsigned char r[32]);

/*
 The set of scalars is \Z/l
 where l = 2^252 + 27742317777372353535851937790883648493.
 */

void sc25519_reduce(unsigned char *s);

void sc25519_muladd(unsigned char *s, const unsigned char *a,
                    const unsigned char *b, const unsigned char *c);

int sc25519_is_canonical(const unsigned char *s);

#endif
