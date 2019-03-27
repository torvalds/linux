/* $FreeBSD$ */

#ifndef _MP_H_
#define _MP_H_

#ifndef HEADER_BN_H_
#include <openssl/bn.h>
#endif

typedef struct _mint {
	BIGNUM *bn;
} MINT;

void mp_gcd(const MINT *, const MINT *, MINT *);
MINT *mp_itom(short);
void mp_madd(const MINT *, const MINT *, MINT *);
int mp_mcmp(const MINT *, const MINT *);
void mp_mdiv(const MINT *, const MINT *, MINT *, MINT *);
void mp_mfree(MINT *);
void mp_min(MINT *);
void mp_mout(const MINT *);
void mp_move(const MINT *, MINT *);
void mp_msqrt(const MINT *, MINT *, MINT *);
void mp_msub(const MINT *, const MINT *, MINT *);
char *mp_mtox(const MINT *);
void mp_mult(const MINT *, const MINT *, MINT *);
void mp_pow(const MINT *, const MINT *, const MINT *, MINT *);
void mp_rpow(const MINT *, short, MINT *);
void mp_sdiv(const MINT *, short, MINT *, short *);
MINT *mp_xtom(const char *);

#endif /* !_MP_H_ */
