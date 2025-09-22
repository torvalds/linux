/* $OpenBSD: bn_lib.c,v 1.94 2025/05/10 05:54:38 tb Exp $ */
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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include "bn_local.h"
#include "bn_internal.h"
#include "err_local.h"

BIGNUM *
BN_new(void)
{
	BIGNUM *bn;

	if ((bn = calloc(1, sizeof(BIGNUM))) == NULL) {
		BNerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	bn->flags = BN_FLG_MALLOCED;

	return bn;
}
LCRYPTO_ALIAS(BN_new);

void
BN_init(BIGNUM *a)
{
	memset(a, 0, sizeof(BIGNUM));
}

void
BN_clear(BIGNUM *a)
{
	if (a->d != NULL)
		explicit_bzero(a->d, a->dmax * sizeof(a->d[0]));
	a->top = 0;
	a->neg = 0;
}
LCRYPTO_ALIAS(BN_clear);

void
BN_free(BIGNUM *bn)
{
	if (bn == NULL)
		return;

	if (!BN_get_flags(bn, BN_FLG_STATIC_DATA))
		freezero(bn->d, bn->dmax * sizeof(bn->d[0]));

	if (!BN_get_flags(bn, BN_FLG_MALLOCED)) {
		explicit_bzero(bn, sizeof(*bn));
		return;
	}

	freezero(bn, sizeof(*bn));
}
LCRYPTO_ALIAS(BN_free);

void
BN_clear_free(BIGNUM *bn)
{
	BN_free(bn);
}
LCRYPTO_ALIAS(BN_clear_free);

void
BN_set_flags(BIGNUM *b, int n)
{
	b->flags |= n;
}
LCRYPTO_ALIAS(BN_set_flags);

int
BN_get_flags(const BIGNUM *b, int n)
{
	return b->flags & n;
}
LCRYPTO_ALIAS(BN_get_flags);

void
BN_with_flags(BIGNUM *dest, const BIGNUM *b, int flags)
{
	int dest_flags;

	dest_flags = (dest->flags & BN_FLG_MALLOCED) |
	    (b->flags & ~BN_FLG_MALLOCED) | BN_FLG_STATIC_DATA | flags;

	*dest = *b;
	dest->flags = dest_flags;
}
LCRYPTO_ALIAS(BN_with_flags);

static const BN_ULONG bn_value_one_data = 1;
static const BIGNUM bn_value_one = {
	.d = (BN_ULONG *)&bn_value_one_data,
	.top = 1,
	.dmax = 1,
	.neg = 0,
	.flags = BN_FLG_STATIC_DATA,
};

const BIGNUM *
BN_value_one(void)
{
	return &bn_value_one;
}
LCRYPTO_ALIAS(BN_value_one);

int
BN_num_bits_word(BN_ULONG w)
{
	return BN_BITS2 - bn_clzw(w);
}
LCRYPTO_ALIAS(BN_num_bits_word);

int
BN_num_bits(const BIGNUM *bn)
{
	return bn_bitsize(bn);
}
LCRYPTO_ALIAS(BN_num_bits);

void
bn_correct_top(BIGNUM *a)
{
	while (a->top > 0 && a->d[a->top - 1] == 0)
		a->top--;
}

static int
bn_expand_internal(BIGNUM *bn, int words)
{
	BN_ULONG *d;

	if (words < 0) {
		BNerror(BN_R_BIGNUM_TOO_LONG); // XXX
		return 0;
	}

	if (words > INT_MAX / (4 * BN_BITS2)) {
		BNerror(BN_R_BIGNUM_TOO_LONG);
		return 0;
	}
	if (BN_get_flags(bn, BN_FLG_STATIC_DATA)) {
		BNerror(BN_R_EXPAND_ON_STATIC_BIGNUM_DATA);
		return 0;
	}

	d = recallocarray(bn->d, bn->dmax, words, sizeof(BN_ULONG));
	if (d == NULL) {
		BNerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	bn->d = d;
	bn->dmax = words;

	return 1;
}

int
bn_expand_bits(BIGNUM *bn, size_t bits)
{
	int words;

	if (bits > (INT_MAX - BN_BITS2 + 1))
		return 0;

	words = (bits + BN_BITS2 - 1) / BN_BITS2;

	return bn_wexpand(bn, words);
}

int
bn_expand_bytes(BIGNUM *bn, size_t bytes)
{
	int words;

	if (bytes > (INT_MAX - BN_BYTES + 1))
		return 0;

	words = (bytes + BN_BYTES - 1) / BN_BYTES;

	return bn_wexpand(bn, words);
}

int
bn_wexpand(BIGNUM *bn, int words)
{
	if (words < 0)
		return 0;

	if (words <= bn->dmax)
		return 1;

	return bn_expand_internal(bn, words);
}

BIGNUM *
BN_dup(const BIGNUM *a)
{
	BIGNUM *t;

	if (a == NULL)
		return NULL;

	t = BN_new();
	if (t == NULL)
		return NULL;
	if (!bn_copy(t, a)) {
		BN_free(t);
		return NULL;
	}
	return t;
}
LCRYPTO_ALIAS(BN_dup);

static inline void
bn_copy_words(BN_ULONG *ap, const BN_ULONG *bp, int n)
{
	while (n > 0) {
		ap[0] = bp[0];
		ap++;
		bp++;
		n--;
	}
}

BIGNUM *
BN_copy(BIGNUM *a, const BIGNUM *b)
{
	if (a == b)
		return (a);

	if (!bn_wexpand(a, b->top))
		return (NULL);

	bn_copy_words(a->d, b->d, b->top);

	/* Copy constant time flag from b, but make it sticky on a. */
	a->flags |= b->flags & BN_FLG_CONSTTIME;

	a->top = b->top;
	a->neg = b->neg;

	return (a);
}
LCRYPTO_ALIAS(BN_copy);

int
bn_copy(BIGNUM *dst, const BIGNUM *src)
{
	return BN_copy(dst, src) != NULL;
}

void
BN_swap(BIGNUM *a, BIGNUM *b)
{
	int flags_old_a, flags_old_b;
	BN_ULONG *tmp_d;
	int tmp_top, tmp_dmax, tmp_neg;


	flags_old_a = a->flags;
	flags_old_b = b->flags;

	tmp_d = a->d;
	tmp_top = a->top;
	tmp_dmax = a->dmax;
	tmp_neg = a->neg;

	a->d = b->d;
	a->top = b->top;
	a->dmax = b->dmax;
	a->neg = b->neg;

	b->d = tmp_d;
	b->top = tmp_top;
	b->dmax = tmp_dmax;
	b->neg = tmp_neg;

	a->flags = (flags_old_a & BN_FLG_MALLOCED) |
	    (flags_old_b & BN_FLG_STATIC_DATA);
	b->flags = (flags_old_b & BN_FLG_MALLOCED) |
	    (flags_old_a & BN_FLG_STATIC_DATA);
}
LCRYPTO_ALIAS(BN_swap);

BN_ULONG
BN_get_word(const BIGNUM *a)
{
	if (a->top > 1)
		return BN_MASK2;
	else if (a->top == 1)
		return a->d[0];
	/* a->top == 0 */
	return 0;
}
LCRYPTO_ALIAS(BN_get_word);

int
BN_set_word(BIGNUM *a, BN_ULONG w)
{
	if (!bn_wexpand(a, 1))
		return (0);
	a->neg = 0;
	a->d[0] = w;
	a->top = (w ? 1 : 0);
	return (1);
}
LCRYPTO_ALIAS(BN_set_word);

int
BN_ucmp(const BIGNUM *a, const BIGNUM *b)
{
	int i;

	if (a->top < b->top)
		return -1;
	if (a->top > b->top)
		return 1;

	for (i = a->top - 1; i >= 0; i--) {
		if (a->d[i] != b->d[i])
			return (a->d[i] > b->d[i] ? 1 : -1);
	}

	return 0;
}
LCRYPTO_ALIAS(BN_ucmp);

int
BN_cmp(const BIGNUM *a, const BIGNUM *b)
{
	if (a == NULL || b == NULL) {
		if (a != NULL)
			return -1;
		if (b != NULL)
			return 1;
		return 0;
	}

	if (a->neg != b->neg)
		return b->neg - a->neg;

	if (a->neg)
		return BN_ucmp(b, a);

	return BN_ucmp(a, b);
}
LCRYPTO_ALIAS(BN_cmp);

int
BN_set_bit(BIGNUM *a, int n)
{
	int i, j, k;

	if (n < 0)
		return 0;

	i = n / BN_BITS2;
	j = n % BN_BITS2;
	if (a->top <= i) {
		if (!bn_wexpand(a, i + 1))
			return (0);
		for (k = a->top; k < i + 1; k++)
			a->d[k] = 0;
		a->top = i + 1;
	}

	a->d[i] |= (((BN_ULONG)1) << j);
	return (1);
}
LCRYPTO_ALIAS(BN_set_bit);

int
BN_clear_bit(BIGNUM *a, int n)
{
	int i, j;

	if (n < 0)
		return 0;

	i = n / BN_BITS2;
	j = n % BN_BITS2;
	if (a->top <= i)
		return (0);

	a->d[i] &= (~(((BN_ULONG)1) << j));
	bn_correct_top(a);

	BN_set_negative(a, a->neg);

	return (1);
}
LCRYPTO_ALIAS(BN_clear_bit);

int
BN_is_bit_set(const BIGNUM *a, int n)
{
	int i, j;

	if (n < 0)
		return 0;
	i = n / BN_BITS2;
	j = n % BN_BITS2;
	if (a->top <= i)
		return 0;
	return (int)(((a->d[i]) >> j) & ((BN_ULONG)1));
}
LCRYPTO_ALIAS(BN_is_bit_set);

int
BN_mask_bits(BIGNUM *a, int n)
{
	int b, w;

	if (n < 0)
		return 0;

	w = n / BN_BITS2;
	b = n % BN_BITS2;
	if (w >= a->top)
		return 0;
	if (b == 0)
		a->top = w;
	else {
		a->top = w + 1;
		a->d[w] &= ~(BN_MASK2 << b);
	}
	bn_correct_top(a);

	BN_set_negative(a, a->neg);

	return (1);
}
LCRYPTO_ALIAS(BN_mask_bits);

void
BN_set_negative(BIGNUM *bn, int neg)
{
	bn->neg = ~BN_is_zero(bn) & bn_ct_ne_zero(neg);
}
LCRYPTO_ALIAS(BN_set_negative);

/*
 * Constant-time conditional swap of a and b.
 * a and b are swapped if condition is not 0.
 * The code assumes that at most one bit of condition is set.
 * nwords is the number of words to swap.
 * The code assumes that at least nwords are allocated in both a and b,
 * and that no more than nwords are used by either a or b.
 * a and b cannot be the same number
 */
void
BN_consttime_swap(BN_ULONG condition, BIGNUM *a, BIGNUM *b, int nwords)
{
	BN_ULONG t;
	int i;

	assert(a != b);
	assert((condition & (condition - 1)) == 0);
	assert(sizeof(BN_ULONG) >= sizeof(int));

	condition = ((condition - 1) >> (BN_BITS2 - 1)) - 1;

	t = (a->top^b->top) & condition;
	a->top ^= t;
	b->top ^= t;

#define BN_CONSTTIME_SWAP(ind) \
	do { \
		t = (a->d[ind] ^ b->d[ind]) & condition; \
		a->d[ind] ^= t; \
		b->d[ind] ^= t; \
	} while (0)


	switch (nwords) {
	default:
		for (i = 10; i < nwords; i++)
			BN_CONSTTIME_SWAP(i);
		/* Fallthrough */
	case 10: BN_CONSTTIME_SWAP(9); /* Fallthrough */
	case 9: BN_CONSTTIME_SWAP(8); /* Fallthrough */
	case 8: BN_CONSTTIME_SWAP(7); /* Fallthrough */
	case 7: BN_CONSTTIME_SWAP(6); /* Fallthrough */
	case 6: BN_CONSTTIME_SWAP(5); /* Fallthrough */
	case 5: BN_CONSTTIME_SWAP(4); /* Fallthrough */
	case 4: BN_CONSTTIME_SWAP(3); /* Fallthrough */
	case 3: BN_CONSTTIME_SWAP(2); /* Fallthrough */
	case 2: BN_CONSTTIME_SWAP(1); /* Fallthrough */
	case 1:
		BN_CONSTTIME_SWAP(0);
	}
#undef BN_CONSTTIME_SWAP
}
LCRYPTO_ALIAS(BN_consttime_swap);

/*
 * Constant-time conditional swap of a and b.
 * a and b are swapped if condition is not 0.
 * nwords is the number of words to swap.
 */
int
BN_swap_ct(BN_ULONG condition, BIGNUM *a, BIGNUM *b, size_t nwords)
{
	BN_ULONG t;
	int i, words;

	if (a == b)
		return 1;
	if (nwords > INT_MAX)
		return 0;
	words = (int)nwords;
	if (!bn_wexpand(a, words) || !bn_wexpand(b, words))
		return 0;
	if (a->top > words || b->top > words) {
		BNerror(BN_R_INVALID_LENGTH);
		return 0;
	}

	/* Set condition to 0 (if it was zero) or all 1s otherwise. */
	condition = ((~condition & (condition - 1)) >> (BN_BITS2 - 1)) - 1;

	/* swap top field */
	t = (a->top ^ b->top) & condition;
	a->top ^= t;
	b->top ^= t;

	/* swap neg field */
	t = (a->neg ^ b->neg) & condition;
	a->neg ^= t;
	b->neg ^= t;

	/* swap BN_FLG_CONSTTIME from flag field */
	t = ((a->flags ^ b->flags) & BN_FLG_CONSTTIME) & condition;
	a->flags ^= t;
	b->flags ^= t;

	/* swap the data */
	for (i = 0; i < words; i++) {
		t = (a->d[i] ^ b->d[i]) & condition;
		a->d[i] ^= t;
		b->d[i] ^= t;
	}

	return 1;
}

void
BN_zero(BIGNUM *a)
{
	a->neg = 0;
	a->top = 0;
}
LCRYPTO_ALIAS(BN_zero);

int
BN_one(BIGNUM *a)
{
	return BN_set_word(a, 1);
}
LCRYPTO_ALIAS(BN_one);

int
BN_abs_is_word(const BIGNUM *a, const BN_ULONG w)
{
	return (a->top == 1 && a->d[0] == w) || (w == 0 && a->top == 0);
}
LCRYPTO_ALIAS(BN_abs_is_word);

int
BN_is_zero(const BIGNUM *bn)
{
	BN_ULONG bits = 0;
	int i;

	for (i = 0; i < bn->top; i++)
		bits |= bn->d[i];

	return bits == 0;
}
LCRYPTO_ALIAS(BN_is_zero);

int
BN_is_one(const BIGNUM *a)
{
	return BN_abs_is_word(a, 1) && !a->neg;
}
LCRYPTO_ALIAS(BN_is_one);

int
BN_is_word(const BIGNUM *a, const BN_ULONG w)
{
	return BN_abs_is_word(a, w) && (w == 0 || !a->neg);
}
LCRYPTO_ALIAS(BN_is_word);

int
BN_is_odd(const BIGNUM *a)
{
	return a->top > 0 && (a->d[0] & 1);
}
LCRYPTO_ALIAS(BN_is_odd);

int
BN_is_negative(const BIGNUM *a)
{
	return a->neg != 0;
}
LCRYPTO_ALIAS(BN_is_negative);

/*
 * Bits of security, see SP800-57, section 5.6.11, table 2.
 */
int
BN_security_bits(int L, int N)
{
	int secbits, bits;

	if (L >= 15360)
		secbits = 256;
	else if (L >= 7680)
		secbits = 192;
	else if (L >= 3072)
		secbits = 128;
	else if (L >= 2048)
		secbits = 112;
	else if (L >= 1024)
		secbits = 80;
	else
		return 0;

	if (N == -1)
		return secbits;

	bits = N / 2;
	if (bits < 80)
		return 0;

	return bits >= secbits ? secbits : bits;
}
LCRYPTO_ALIAS(BN_security_bits);

BN_GENCB *
BN_GENCB_new(void)
{
	BN_GENCB *cb;

	if ((cb = calloc(1, sizeof(*cb))) == NULL)
		return NULL;

	return cb;
}
LCRYPTO_ALIAS(BN_GENCB_new);

void
BN_GENCB_free(BN_GENCB *cb)
{
	if (cb == NULL)
		return;
	free(cb);
}
LCRYPTO_ALIAS(BN_GENCB_free);

/* Populate a BN_GENCB structure with an "old"-style callback */
void
BN_GENCB_set_old(BN_GENCB *gencb, void (*cb)(int, int, void *), void *cb_arg)
{
	gencb->ver = 1;
	gencb->cb.cb_1 = cb;
	gencb->arg = cb_arg;
}
LCRYPTO_ALIAS(BN_GENCB_set_old);

/* Populate a BN_GENCB structure with a "new"-style callback */
void
BN_GENCB_set(BN_GENCB *gencb, int (*cb)(int, int, BN_GENCB *), void *cb_arg)
{
	gencb->ver = 2;
	gencb->cb.cb_2 = cb;
	gencb->arg = cb_arg;
}
LCRYPTO_ALIAS(BN_GENCB_set);

void *
BN_GENCB_get_arg(BN_GENCB *cb)
{
	return cb->arg;
}
LCRYPTO_ALIAS(BN_GENCB_get_arg);
