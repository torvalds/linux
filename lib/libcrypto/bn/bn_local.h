/* $OpenBSD: bn_local.h,v 1.60 2025/09/07 05:21:29 jsing Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
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
 * Copyright (c) 1998-2000 The OpenSSL Project.  All rights reserved.
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

#ifndef HEADER_BN_LOCAL_H
#define HEADER_BN_LOCAL_H

#include <openssl/opensslconf.h>

#include <openssl/bn.h>

__BEGIN_HIDDEN_DECLS

struct bignum_st {
	BN_ULONG *d;	/* Pointer to an array of 'BN_BITS2' bit chunks. */
	int top;	/* Index of last used d +1. */
	/* The next are internal book keeping for bn_expand. */
	int dmax;	/* Size of the d array. */
	int neg;	/* one if the number is negative */
	int flags;
};

struct bn_mont_ctx_st {
	int ri;		/* Number of bits in R */
	BIGNUM RR;	/* Used to convert to Montgomery form */
	BIGNUM N;	/* Modulus */

	/* Least significant word(s) of Ni; R*(1/R mod N) - N*Ni = 1 */
	BN_ULONG n0[2];

	int flags;
};

typedef struct bn_recp_ctx_st BN_RECP_CTX;

/* Used for slow "generation" functions. */
struct bn_gencb_st {
	unsigned int ver;	/* To handle binary (in)compatibility */
	void *arg;		/* callback-specific data */
	union {
		/* if(ver==1) - handles old style callbacks */
		void (*cb_1)(int, int, void *);
		/* if(ver==2) - new callback style */
		int (*cb_2)(int, int, BN_GENCB *);
	} cb;
};

/*
 * BN_window_bits_for_exponent_size -- macro for sliding window mod_exp functions
 *
 *
 * For window size 'w' (w >= 2) and a random 'b' bits exponent,
 * the number of multiplications is a constant plus on average
 *
 *    2^(w-1) + (b-w)/(w+1);
 *
 * here  2^(w-1)  is for precomputing the table (we actually need
 * entries only for windows that have the lowest bit set), and
 * (b-w)/(w+1)  is an approximation for the expected number of
 * w-bit windows, not counting the first one.
 *
 * Thus we should use
 *
 *    w >= 6  if        b > 671
 *     w = 5  if  671 > b > 239
 *     w = 4  if  239 > b >  79
 *     w = 3  if   79 > b >  23
 *    w <= 2  if   23 > b
 *
 * (with draws in between).  Very small exponents are often selected
 * with low Hamming weight, so we use  w = 1  for b <= 23.
 */
#define BN_window_bits_for_exponent_size(b) \
		((b) > 671 ? 6 : \
		 (b) > 239 ? 5 : \
		 (b) >  79 ? 4 : \
		 (b) >  23 ? 3 : 1)


/* BN_mod_exp_mont_consttime is based on the assumption that the
 * L1 data cache line width of the target processor is at least
 * the following value.
 */
#define MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH	( 64 )
#define MOD_EXP_CTIME_MIN_CACHE_LINE_MASK	(MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH - 1)

/* Window sizes optimized for fixed window size modular exponentiation
 * algorithm (BN_mod_exp_mont_consttime).
 *
 * To achieve the security goals of BN_mode_exp_mont_consttime, the
 * maximum size of the window must not exceed
 * log_2(MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH).
 *
 * Window size thresholds are defined for cache line sizes of 32 and 64,
 * cache line sizes where log_2(32)=5 and log_2(64)=6 respectively. A
 * window size of 7 should only be used on processors that have a 128
 * byte or greater cache line size.
 */
#if MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH == 64

#  define BN_window_bits_for_ctime_exponent_size(b) \
		((b) > 937 ? 6 : \
		 (b) > 306 ? 5 : \
		 (b) >  89 ? 4 : \
		 (b) >  22 ? 3 : 1)
#  define BN_MAX_WINDOW_BITS_FOR_CTIME_EXPONENT_SIZE	(6)

#elif MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH == 32

#  define BN_window_bits_for_ctime_exponent_size(b) \
		((b) > 306 ? 5 : \
		 (b) >  89 ? 4 : \
		 (b) >  22 ? 3 : 1)
#  define BN_MAX_WINDOW_BITS_FOR_CTIME_EXPONENT_SIZE	(5)

#endif


/* Pentium pro 16,16,16,32,64 */
/* Alpha       16,16,16,16.64 */
#define BN_MULL_SIZE_NORMAL			(16) /* 32 */
#define BN_MUL_RECURSIVE_SIZE_NORMAL		(16) /* 32 less than */
#define BN_SQR_RECURSIVE_SIZE_NORMAL		(16) /* 32 */
#define BN_MUL_LOW_RECURSIVE_SIZE_NORMAL	(32) /* 32 */
#define BN_MONT_CTX_SET_SIZE_WORD		(64) /* 32 */

/* The least significant word of a BIGNUM. */
#define BN_lsw(n) (((n)->top == 0) ? (BN_ULONG) 0 : (n)->d[0])

BN_ULONG bn_add(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len,
    const BN_ULONG *b, int b_len);
BN_ULONG bn_sub(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len,
    const BN_ULONG *b, int b_len);

void bn_mul_comba4(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b);
void bn_mul_comba6(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b);
void bn_mul_comba8(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b);
void bn_mul_words(BN_ULONG *r, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len);

void bn_sqr_comba4(BN_ULONG *r, const BN_ULONG *a);
void bn_sqr_comba6(BN_ULONG *r, const BN_ULONG *a);
void bn_sqr_comba8(BN_ULONG *r, const BN_ULONG *a);
void bn_sqr_words(BN_ULONG *r, const BN_ULONG *a, int a_len);

int bn_mul_mont(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
    const BN_ULONG *np, const BN_ULONG *n0, int num);

void bn_correct_top(BIGNUM *a);
int bn_expand_bits(BIGNUM *a, size_t bits);
int bn_expand_bytes(BIGNUM *a, size_t bytes);
int bn_wexpand(BIGNUM *a, int words);

BN_ULONG bn_mulw_add_words(BN_ULONG *rp, const BN_ULONG *ap, int num, BN_ULONG w);
BN_ULONG bn_mulw_words(BN_ULONG *rp, const BN_ULONG *ap, int num, BN_ULONG w);
BN_ULONG bn_div_words(BN_ULONG h, BN_ULONG l, BN_ULONG d);
void bn_div_rem_words(BN_ULONG h, BN_ULONG l, BN_ULONG d, BN_ULONG *out_q,
    BN_ULONG *out_r);

int BN_bntest_rand(BIGNUM *rnd, int bits, int top, int bottom);
int bn_rand_in_range(BIGNUM *rnd, const BIGNUM *lower_inc, const BIGNUM *upper_exc);
int bn_rand_interval(BIGNUM *rnd, BN_ULONG lower_word, const BIGNUM *upper_exc);

void	BN_init(BIGNUM *);

BN_MONT_CTX *BN_MONT_CTX_create(const BIGNUM *bn, BN_CTX *ctx);

BN_RECP_CTX *BN_RECP_CTX_create(const BIGNUM *N);
void BN_RECP_CTX_free(BN_RECP_CTX *recp);
int	BN_div_reciprocal(BIGNUM *dv, BIGNUM *rem, const BIGNUM *m,
    BN_RECP_CTX *recp, BN_CTX *ctx);
int	BN_mod_mul_reciprocal(BIGNUM *r, const BIGNUM *x, const BIGNUM *y,
    BN_RECP_CTX *recp, BN_CTX *ctx);
int	BN_mod_sqr_reciprocal(BIGNUM *r, const BIGNUM *x, BN_RECP_CTX *recp,
    BN_CTX *ctx);
int	BN_mod_exp_reciprocal(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx);

/* Explicitly const time / non-const time versions for internal use */
int BN_mod_exp_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx);
int BN_mod_exp_nonct(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx);
int BN_mod_exp_mont_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
int BN_mod_exp_mont_nonct(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
int BN_div_nonct(BIGNUM *q, BIGNUM *r, const BIGNUM *n, const BIGNUM *d,
    BN_CTX *ctx);
int BN_div_ct(BIGNUM *q, BIGNUM *r, const BIGNUM *n, const BIGNUM *d,
    BN_CTX *ctx);
int BN_mod_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_nonct(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);

int BN_mod_exp_mont_word(BIGNUM *r, BN_ULONG a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
int BN_mod_exp2_mont(BIGNUM *r, const BIGNUM *a1, const BIGNUM *p1,
    const BIGNUM *a2, const BIGNUM *p2, const BIGNUM *m,
    BN_CTX *ctx, BN_MONT_CTX *m_ctx);

int BN_mod_exp_simple(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx);

BIGNUM *BN_mod_inverse_ct(BIGNUM *ret, const BIGNUM *a, const BIGNUM *n,
    BN_CTX *ctx);
BIGNUM *BN_mod_inverse_nonct(BIGNUM *ret, const BIGNUM *a, const BIGNUM *n,
    BN_CTX *ctx);
int	BN_gcd_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);

int	BN_swap_ct(BN_ULONG swap, BIGNUM *a, BIGNUM *b, size_t nwords);

int bn_copy(BIGNUM *dst, const BIGNUM *src);

int bn_isqrt(BIGNUM *out_sqrt, int *out_perfect, const BIGNUM *n, BN_CTX *ctx);
int bn_is_perfect_square(int *out_perfect, const BIGNUM *n, BN_CTX *ctx);

int bn_is_prime_bpsw(int *is_prime, const BIGNUM *n, BN_CTX *ctx, size_t rounds);

int bn_printf(BIO *bio, const BIGNUM *bn, int indent, const char *fmt, ...)
    __attribute__((__format__ (printf, 4, 5)))
    __attribute__((__nonnull__ (4)));

int bn_bn2hex_nosign(const BIGNUM *bn, char **out, size_t *out_len);
int bn_bn2hex_nibbles(const BIGNUM *bn, char **out, size_t *out_len);

__END_HIDDEN_DECLS
#endif /* !HEADER_BN_LOCAL_H */
