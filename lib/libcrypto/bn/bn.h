/* $OpenBSD: bn.h,v 1.81 2025/09/07 06:28:03 jsing Exp $ */
/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the Eric Young open source
 * license provided above.
 *
 * The binary polynomial arithmetic software is originally written by
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems Laboratories.
 *
 */

#ifndef HEADER_BN_H
#define HEADER_BN_H

#include <stdio.h>
#include <stdlib.h>

#include <openssl/opensslconf.h>

#include <openssl/ossl_typ.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef _LP64
#undef	BN_LLONG
#define BN_ULONG	unsigned long
#define BN_LONG		long
#define BN_BITS		128
#define BN_BYTES	8
#define BN_BITS2	64
#define BN_BITS4	32
#define BN_MASK2	(0xffffffffffffffffL)
#define BN_MASK2l	(0xffffffffL)
#define BN_MASK2h	(0xffffffff00000000L)
#define BN_MASK2h1	(0xffffffff80000000L)
#define BN_TBIT		(0x8000000000000000L)
#define BN_DEC_CONV	(10000000000000000000UL)
#define BN_DEC_FMT1	"%lu"
#define BN_DEC_FMT2	"%019lu"
#define BN_DEC_NUM	19
#define BN_HEX_FMT1	"%lX"
#define BN_HEX_FMT2	"%016lX"
#else
#define BN_ULLONG	unsigned long long
#define	BN_LLONG
#define BN_ULONG	unsigned int
#define BN_LONG		int
#define BN_BITS		64
#define BN_BYTES	4
#define BN_BITS2	32
#define BN_BITS4	16
#define BN_MASK		(0xffffffffffffffffLL)
#define BN_MASK2	(0xffffffffL)
#define BN_MASK2l	(0xffff)
#define BN_MASK2h1	(0xffff8000L)
#define BN_MASK2h	(0xffff0000L)
#define BN_TBIT		(0x80000000L)
#define BN_DEC_CONV	(1000000000L)
#define BN_DEC_FMT1	"%u"
#define BN_DEC_FMT2	"%09u"
#define BN_DEC_NUM	9
#define BN_HEX_FMT1	"%X"
#define BN_HEX_FMT2	"%08X"
#endif

#define BN_FLG_MALLOCED		0x01
#define BN_FLG_STATIC_DATA	0x02
#define BN_FLG_CONSTTIME	0x04 /* avoid leaking exponent information through timing,
                                      * BN_mod_exp_mont() will call BN_mod_exp_mont_consttime,
                                      * BN_div() will call BN_div_no_branch,
                                      * BN_mod_inverse() will call BN_mod_inverse_no_branch.
                                      */

void BN_set_flags(BIGNUM *b, int n);
int BN_get_flags(const BIGNUM *b, int n);
void BN_with_flags(BIGNUM *dest, const BIGNUM *src, int flags);

/* Values for |top| in BN_rand() */
#define BN_RAND_TOP_ANY    -1
#define BN_RAND_TOP_ONE     0
#define BN_RAND_TOP_TWO     1

/* Values for |bottom| in BN_rand() */
#define BN_RAND_BOTTOM_ANY  0
#define BN_RAND_BOTTOM_ODD  1

BN_GENCB *BN_GENCB_new(void);
void BN_GENCB_free(BN_GENCB *cb);

/* Wrapper function to make using BN_GENCB easier,  */
int BN_GENCB_call(BN_GENCB *cb, int a, int b);

/* Populate a BN_GENCB structure with an "old"-style callback */
void BN_GENCB_set_old(BN_GENCB *gencb, void (*callback)(int, int, void *),
    void *cb_arg);

/* Populate a BN_GENCB structure with a "new"-style callback */
void BN_GENCB_set(BN_GENCB *gencb, int (*callback)(int, int, BN_GENCB *),
    void *cb_arg);

void *BN_GENCB_get_arg(BN_GENCB *cb);

#define BN_prime_checks 0 /* default: select number of iterations
			     based on the size of the number */

/*
 * BN_prime_checks_for_size() returns the number of Miller-Rabin
 * iterations that will be done for checking that a random number
 * is probably prime.  The error rate for accepting a composite
 * number as prime depends on the size of the prime |b|.  The error
 * rates used are for calculating an RSA key with 2 primes, and so
 * the level is what you would expect for a key of double the size
 * of the prime.
 *
 * This table is generated using the algorithm of FIPS PUB 186-4
 * Digital Signature Standard (DSS), section F.1, page 117.
 * (https://dx.doi.org/10.6028/NIST.FIPS.186-4)
 *
 * The following magma script was used to generate the output:
 * securitybits:=125;
 * k:=1024;
 * for t:=1 to 65 do
 *   for M:=3 to Floor(2*Sqrt(k-1)-1) do
 *     S:=0;
 *     // Sum over m
 *     for m:=3 to M do
 *       s:=0;
 *       // Sum over j
 *       for j:=2 to m do
 *         s+:=(RealField(32)!2)^-(j+(k-1)/j);
 *       end for;
 *       S+:=2^(m-(m-1)*t)*s;
 *     end for;
 *     A:=2^(k-2-M*t);
 *     B:=8*(Pi(RealField(32))^2-6)/3*2^(k-2)*S;
 *     pkt:=2.00743*Log(2)*k*2^-k*(A+B);
 *     seclevel:=Floor(-Log(2,pkt));
 *     if seclevel ge securitybits then
 *       printf "k: %5o, security: %o bits  (t: %o, M: %o)\n",k,seclevel,t,M;
 *       break;
 *     end if;
 *   end for;
 *   if seclevel ge securitybits then break; end if;
 * end for;
 *
 * It can be run online at:
 * http://magma.maths.usyd.edu.au/calc
 *
 * And will output:
 * k:  1024, security: 129 bits  (t: 6, M: 23)
 *
 * k is the number of bits of the prime, securitybits is the level
 * we want to reach.
 *
 * prime length | RSA key size | # MR tests | security level
 * -------------+--------------|------------+---------------
 *  (b) >= 6394 |     >= 12788 |          3 |        256 bit
 *  (b) >= 3747 |     >=  7494 |          3 |        192 bit
 *  (b) >= 1345 |     >=  2690 |          4 |        128 bit
 *  (b) >= 1080 |     >=  2160 |          5 |        128 bit
 *  (b) >=  852 |     >=  1704 |          5 |        112 bit
 *  (b) >=  476 |     >=   952 |          5 |         80 bit
 *  (b) >=  400 |     >=   800 |          6 |         80 bit
 *  (b) >=  347 |     >=   694 |          7 |         80 bit
 *  (b) >=  308 |     >=   616 |          8 |         80 bit
 *  (b) >=   55 |     >=   110 |         27 |         64 bit
 *  (b) >=    6 |     >=    12 |         34 |         64 bit
 */

#define BN_prime_checks_for_size(b) ((b) >= 3747 ?  3 : \
				(b) >=  1345 ?  4 : \
				(b) >=  476 ?  5 : \
				(b) >=  400 ?  6 : \
				(b) >=  347 ?  7 : \
				(b) >=  308 ?  8 : \
				(b) >=  55  ? 27 : \
				/* b >= 6 */ 34)

#define BN_num_bytes(a)	((BN_num_bits(a)+7)/8)

int BN_abs_is_word(const BIGNUM *a, const BN_ULONG w);
int BN_is_zero(const BIGNUM *a);
int BN_is_one(const BIGNUM *a);
int BN_is_word(const BIGNUM *a, const BN_ULONG w);
int BN_is_odd(const BIGNUM *a);

void BN_zero(BIGNUM *a);
int BN_one(BIGNUM *a);

const BIGNUM *BN_value_one(void);
BN_CTX *BN_CTX_new(void);
void	BN_CTX_free(BN_CTX *c);
void	BN_CTX_start(BN_CTX *ctx);
BIGNUM *BN_CTX_get(BN_CTX *ctx);
void	BN_CTX_end(BN_CTX *ctx);
int     BN_rand(BIGNUM *rnd, int bits, int top, int bottom);
int     BN_pseudo_rand(BIGNUM *rnd, int bits, int top, int bottom);
int	BN_rand_range(BIGNUM *rnd, const BIGNUM *range);
int	BN_pseudo_rand_range(BIGNUM *rnd, const BIGNUM *range);
int	BN_num_bits(const BIGNUM *a);
int	BN_num_bits_word(BN_ULONG);
BIGNUM *BN_new(void);
void	BN_clear_free(BIGNUM *a);
BIGNUM *BN_copy(BIGNUM *a, const BIGNUM *b);
void	BN_swap(BIGNUM *a, BIGNUM *b);
BIGNUM *BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret);
int	BN_bn2bin(const BIGNUM *a, unsigned char *to);
int	BN_bn2binpad(const BIGNUM *a, unsigned char *to, int tolen);
BIGNUM *BN_lebin2bn(const unsigned char *s, int len, BIGNUM *ret);
int	BN_bn2lebinpad(const BIGNUM *a, unsigned char *to, int tolen);
BIGNUM *BN_mpi2bn(const unsigned char *s, int len, BIGNUM *ret);
int	BN_bn2mpi(const BIGNUM *a, unsigned char *to);
int	BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int	BN_usub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int	BN_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int	BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int	BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
int	BN_sqr(BIGNUM *r, const BIGNUM *a, BN_CTX *ctx);
void	BN_set_negative(BIGNUM *b, int n);

int BN_is_negative(const BIGNUM *b);

int	BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *m, const BIGNUM *d,
    BN_CTX *ctx);
#define BN_mod(rem,m,d,ctx) BN_div(NULL,(rem),(m),(d),(ctx))

int	BN_nnmod(BIGNUM *r, const BIGNUM *m, const BIGNUM *d, BN_CTX *ctx);
int	BN_mod_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_add_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m);
int	BN_mod_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_sub_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m);
int	BN_mod_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_sqr(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_lshift1(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_lshift1_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *m);
int	BN_mod_lshift(BIGNUM *r, const BIGNUM *a, int n, const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_lshift_quick(BIGNUM *r, const BIGNUM *a, int n, const BIGNUM *m);

BN_ULONG BN_mod_word(const BIGNUM *a, BN_ULONG w);
BN_ULONG BN_div_word(BIGNUM *a, BN_ULONG w);
int	BN_mul_word(BIGNUM *a, BN_ULONG w);
int	BN_add_word(BIGNUM *a, BN_ULONG w);
int	BN_sub_word(BIGNUM *a, BN_ULONG w);
int	BN_set_word(BIGNUM *a, BN_ULONG w);
BN_ULONG BN_get_word(const BIGNUM *a);

int	BN_cmp(const BIGNUM *a, const BIGNUM *b);
void	BN_free(BIGNUM *a);
int	BN_is_bit_set(const BIGNUM *a, int n);
int	BN_lshift(BIGNUM *r, const BIGNUM *a, int n);
int	BN_lshift1(BIGNUM *r, const BIGNUM *a);
int	BN_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p, BN_CTX *ctx);

int	BN_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx);
int	BN_mod_exp_mont(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
int BN_mod_exp_mont_consttime(BIGNUM *rr, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *in_mont);

int	BN_mask_bits(BIGNUM *a, int n);
int	BN_print_fp(FILE *fp, const BIGNUM *a);
int	BN_print(BIO *fp, const BIGNUM *a);
int	BN_rshift(BIGNUM *r, const BIGNUM *a, int n);
int	BN_rshift1(BIGNUM *r, const BIGNUM *a);
void	BN_clear(BIGNUM *a);
BIGNUM *BN_dup(const BIGNUM *a);
int	BN_ucmp(const BIGNUM *a, const BIGNUM *b);
int	BN_set_bit(BIGNUM *a, int n);
int	BN_clear_bit(BIGNUM *a, int n);
char *	BN_bn2hex(const BIGNUM *a);
char *	BN_bn2dec(const BIGNUM *a);
int	BN_hex2bn(BIGNUM **a, const char *str);
int	BN_dec2bn(BIGNUM **a, const char *str);
int	BN_asc2bn(BIGNUM **a, const char *str);
int	BN_gcd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
int	BN_kronecker(const BIGNUM *a,const BIGNUM *b,BN_CTX *ctx); /* returns -2 for error */
BIGNUM *BN_mod_inverse(BIGNUM *ret,
    const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx);
BIGNUM *BN_mod_sqrt(BIGNUM *ret,
    const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx);

void	BN_consttime_swap(BN_ULONG swap, BIGNUM *a, BIGNUM *b, int nwords);

int	BN_security_bits(int L, int N);

int	BN_generate_prime_ex(BIGNUM *ret, int bits, int safe, const BIGNUM *add,
    const BIGNUM *rem, BN_GENCB *cb);
int	BN_is_prime_ex(const BIGNUM *p, int nchecks, BN_CTX *ctx, BN_GENCB *cb);
int	BN_is_prime_fasttest_ex(const BIGNUM *p, int nchecks, BN_CTX *ctx,
    int do_trial_division, BN_GENCB *cb);

BN_MONT_CTX *BN_MONT_CTX_new(void);
int BN_mod_mul_montgomery(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mont, BN_CTX *ctx);
int BN_to_montgomery(BIGNUM *r, const BIGNUM *a, BN_MONT_CTX *mont,
    BN_CTX *ctx);
int BN_from_montgomery(BIGNUM *r, const BIGNUM *a,
    BN_MONT_CTX *mont, BN_CTX *ctx);
void BN_MONT_CTX_free(BN_MONT_CTX *mont);
int BN_MONT_CTX_set(BN_MONT_CTX *mont, const BIGNUM *mod, BN_CTX *ctx);
BN_MONT_CTX *BN_MONT_CTX_copy(BN_MONT_CTX *to, const BN_MONT_CTX *from);
BN_MONT_CTX *BN_MONT_CTX_set_locked(BN_MONT_CTX **pmont, int lock,
    const BIGNUM *mod, BN_CTX *ctx);

/* Primes from RFC 2409 */
BIGNUM *BN_get_rfc2409_prime_768(BIGNUM *bn);
BIGNUM *BN_get_rfc2409_prime_1024(BIGNUM *bn);

/* Primes from RFC 3526 */
BIGNUM *BN_get_rfc3526_prime_1536(BIGNUM *bn);
BIGNUM *BN_get_rfc3526_prime_2048(BIGNUM *bn);
BIGNUM *BN_get_rfc3526_prime_3072(BIGNUM *bn);
BIGNUM *BN_get_rfc3526_prime_4096(BIGNUM *bn);
BIGNUM *BN_get_rfc3526_prime_6144(BIGNUM *bn);
BIGNUM *BN_get_rfc3526_prime_8192(BIGNUM *bn);

void ERR_load_BN_strings(void);

/* Error codes for the BN functions. */

/* Function codes. */
#define BN_F_BNRAND					 127
#define BN_F_BN_BLINDING_CONVERT_EX			 100
#define BN_F_BN_BLINDING_CREATE_PARAM			 128
#define BN_F_BN_BLINDING_INVERT_EX			 101
#define BN_F_BN_BLINDING_NEW				 102
#define BN_F_BN_BLINDING_UPDATE				 103
#define BN_F_BN_BN2DEC					 104
#define BN_F_BN_BN2HEX					 105
#define BN_F_BN_CTX_GET					 116
#define BN_F_BN_CTX_NEW					 106
#define BN_F_BN_CTX_START				 129
#define BN_F_BN_DIV					 107
#define BN_F_BN_DIV_NO_BRANCH				 138
#define BN_F_BN_DIV_RECP				 130
#define BN_F_BN_EXP					 123
#define BN_F_BN_EXPAND2					 108
#define BN_F_BN_GENERATE_PRIME_EX			 140
#define BN_F_BN_EXPAND_INTERNAL				 120
#define BN_F_BN_GF2M_MOD				 131
#define BN_F_BN_GF2M_MOD_EXP				 132
#define BN_F_BN_GF2M_MOD_MUL				 133
#define BN_F_BN_GF2M_MOD_SOLVE_QUAD			 134
#define BN_F_BN_GF2M_MOD_SOLVE_QUAD_ARR			 135
#define BN_F_BN_GF2M_MOD_SQR				 136
#define BN_F_BN_GF2M_MOD_SQRT				 137
#define BN_F_BN_MOD_EXP2_MONT				 118
#define BN_F_BN_MOD_EXP_MONT				 109
#define BN_F_BN_MOD_EXP_MONT_CONSTTIME			 124
#define BN_F_BN_MOD_EXP_MONT_WORD			 117
#define BN_F_BN_MOD_EXP_RECP				 125
#define BN_F_BN_MOD_EXP_SIMPLE				 126
#define BN_F_BN_MOD_INVERSE				 110
#define BN_F_BN_MOD_INVERSE_NO_BRANCH			 139
#define BN_F_BN_MOD_LSHIFT_QUICK			 119
#define BN_F_BN_MOD_MUL_RECIPROCAL			 111
#define BN_F_BN_MOD_SQRT				 121
#define BN_F_BN_MPI2BN					 112
#define BN_F_BN_NEW					 113
#define BN_F_BN_RAND					 114
#define BN_F_BN_RAND_RANGE				 122
#define BN_F_BN_USUB					 115

/* Reason codes. */
#define BN_R_ARG2_LT_ARG3				 100
#define BN_R_BAD_RECIPROCAL				 101
#define BN_R_BIGNUM_TOO_LONG				 114
#define BN_R_BITS_TOO_SMALL				 117
#define BN_R_CALLED_WITH_EVEN_MODULUS			 102
#define BN_R_DIV_BY_ZERO				 103
#define BN_R_ENCODING_ERROR				 104
#define BN_R_EXPAND_ON_STATIC_BIGNUM_DATA		 105
#define BN_R_INPUT_NOT_REDUCED				 110
#define BN_R_INVALID_ARGUMENT				 118
#define BN_R_INVALID_LENGTH				 106
#define BN_R_INVALID_RANGE				 115
#define BN_R_NOT_A_SQUARE				 111
#define BN_R_NOT_INITIALIZED				 107
#define BN_R_NO_INVERSE					 108
#define BN_R_NO_SOLUTION				 116
#define BN_R_P_IS_NOT_PRIME				 112
#define BN_R_TOO_MANY_ITERATIONS			 113
#define BN_R_TOO_MANY_TEMPORARY_VARIABLES		 109

#ifdef  __cplusplus
}
#endif
#endif
