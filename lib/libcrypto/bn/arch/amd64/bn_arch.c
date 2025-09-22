/*	$OpenBSD: bn_arch.c,v 1.17 2025/09/01 15:33:23 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/bn.h>

#include "bn_arch.h"
#include "bn_local.h"
#include "crypto_arch.h"
#include "s2n_bignum.h"

#ifdef HAVE_BN_ADD
BN_ULONG
bn_add(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	return bignum_add(r_len, (uint64_t *)r, a_len, (const uint64_t *)a,
	    b_len, (const uint64_t *)b);
}
#endif


#ifdef HAVE_BN_ADD_WORDS
BN_ULONG
bn_add_words(BN_ULONG *rd, const BN_ULONG *ad, const BN_ULONG *bd, int n)
{
	return bignum_add(n, (uint64_t *)rd, n, (const uint64_t *)ad, n,
	    (const uint64_t *)bd);
}
#endif

#ifdef HAVE_BN_SUB
BN_ULONG
bn_sub(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	return bignum_sub(r_len, (uint64_t *)r, a_len, (const uint64_t *)a,
	    b_len, (const uint64_t *)b);
}
#endif

#ifdef HAVE_BN_SUB_WORDS
BN_ULONG
bn_sub_words(BN_ULONG *rd, const BN_ULONG *ad, const BN_ULONG *bd, int n)
{
	return bignum_sub(n, (uint64_t *)rd, n, (const uint64_t *)ad, n,
	    (const uint64_t *)bd);
}
#endif

#ifdef HAVE_BN_MOD_ADD_WORDS
void
bn_mod_add_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    const BN_ULONG *m, size_t n)
{
	bignum_modadd(n, (uint64_t *)r, (const uint64_t *)a,
	    (const uint64_t *)b, (const uint64_t *)m);
}
#endif

#ifdef HAVE_BN_MOD_SUB_WORDS
void
bn_mod_sub_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    const BN_ULONG *m, size_t n)
{
	bignum_modsub(n, (uint64_t *)r, (const uint64_t *)a,
	    (const uint64_t *)b, (const uint64_t *)m);
}
#endif

#ifdef HAVE_BN_MUL_COMBA4
void
bn_mul_comba4(BN_ULONG *rd, const BN_ULONG *ad, const BN_ULONG *bd)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_mul_4_8((uint64_t *)rd, (const uint64_t *)ad,
		    (const uint64_t *)bd);
		return;
	}

	bignum_mul_4_8_alt((uint64_t *)rd, (const uint64_t *)ad,
	    (const uint64_t *)bd);
}
#endif

#ifdef HAVE_BN_MUL_COMBA6
void
bn_mul_comba6(BN_ULONG *rd, const BN_ULONG *ad, const BN_ULONG *bd)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_mul_6_12((uint64_t *)rd, (const uint64_t *)ad,
		    (const uint64_t *)bd);
		return;
	}

	bignum_mul_6_12_alt((uint64_t *)rd, (const uint64_t *)ad,
	    (const uint64_t *)bd);
}
#endif

#ifdef HAVE_BN_MUL_COMBA8
void
bn_mul_comba8(BN_ULONG *rd, const BN_ULONG *ad, const BN_ULONG *bd)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_mul_8_16((uint64_t *)rd, (const uint64_t *)ad,
		    (const uint64_t *)bd);
		return;
	}

	bignum_mul_8_16_alt((uint64_t *)rd, (const uint64_t *)ad,
	    (const uint64_t *)bd);
}
#endif

#ifdef HAVE_BN_MUL_WORDS
void
bn_mul_words(BN_ULONG *r, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	bignum_mul(a_len + b_len, (uint64_t *)r, a_len, (const uint64_t *)a,
	    b_len, (const uint64_t *)b);
}
#endif

#ifdef HAVE_BN_MULW_ADD_WORDS
BN_ULONG
bn_mulw_add_words(BN_ULONG *rd, const BN_ULONG *ad, int num, BN_ULONG w)
{
	return bignum_cmadd(num, (uint64_t *)rd, w, num, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_MULW_WORDS
BN_ULONG
bn_mulw_words(BN_ULONG *rd, const BN_ULONG *ad, int num, BN_ULONG w)
{
	return bignum_cmul(num, (uint64_t *)rd, w, num, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_SQR_COMBA4
void
bn_sqr_comba4(BN_ULONG *rd, const BN_ULONG *ad)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_sqr_4_8((uint64_t *)rd, (const uint64_t *)ad);
		return;
	}

	bignum_sqr_4_8_alt((uint64_t *)rd, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_SQR_COMBA6
void
bn_sqr_comba6(BN_ULONG *rd, const BN_ULONG *ad)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_sqr_6_12((uint64_t *)rd, (const uint64_t *)ad);
		return;
	}

	bignum_sqr_6_12_alt((uint64_t *)rd, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_SQR_COMBA8
void
bn_sqr_comba8(BN_ULONG *rd, const BN_ULONG *ad)
{
	if ((crypto_cpu_caps_amd64 & CRYPTO_CPU_CAPS_AMD64_ADX) != 0) {
		bignum_sqr_8_16((uint64_t *)rd, (const uint64_t *)ad);
		return;
	}

	bignum_sqr_8_16_alt((uint64_t *)rd, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_SQR_WORDS
void
bn_sqr_words(BN_ULONG *rd, const BN_ULONG *ad, int a_len)
{
	bignum_sqr(a_len * 2, (uint64_t *)rd, a_len, (const uint64_t *)ad);
}
#endif

#ifdef HAVE_BN_WORD_CLZ
int
bn_word_clz(BN_ULONG w)
{
	return word_clz(w);
}
#endif
