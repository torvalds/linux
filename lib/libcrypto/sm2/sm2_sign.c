/*	$OpenBSD: sm2_sign.c,v 1.5 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2017, 2019 Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#ifndef OPENSSL_NO_SM2

#include <string.h>

#include <openssl/sm2.h>
#include <openssl/evp.h>
#include <openssl/bn.h>

#include "bn_local.h"
#include "err_local.h"
#include "sm2_local.h"

static BIGNUM *
sm2_compute_msg_hash(const EVP_MD *digest, const EC_KEY *key,
    const uint8_t *uid, size_t uid_len, const uint8_t *msg, size_t msg_len)
{
	EVP_MD_CTX *hash;
	BIGNUM *e = NULL;
	int md_size;
	uint8_t *za = NULL;

	if ((hash = EVP_MD_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((md_size = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		goto err;
	}

	if ((za = calloc(1, md_size)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!sm2_compute_userid_digest(za, digest, uid, uid_len, key)) {
		SM2error(SM2_R_DIGEST_FAILURE);
		goto err;
	}

	if (!EVP_DigestInit(hash, digest)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, za, md_size)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, msg, msg_len)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	/* reuse za buffer to hold H(ZA || M) */
	if (!EVP_DigestFinal(hash, za, NULL)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	e = BN_bin2bn(za, md_size, NULL);

 err:
	free(za);
	EVP_MD_CTX_free(hash);
	return e;
}

static ECDSA_SIG *
sm2_sig_gen(const EC_KEY *key, const BIGNUM *e)
{
	ECDSA_SIG *sig = NULL;
	const EC_GROUP *group;
	EC_POINT *kG = NULL;
	BN_CTX *ctx = NULL;
	const BIGNUM *dA;
	BIGNUM *order = NULL, *r = NULL, *s = NULL;
	BIGNUM *k, *rk, *tmp, *x1;

	if ((dA = EC_KEY_get0_private_key(key)) == NULL) {
		SM2error(SM2_R_INVALID_FIELD);
		goto err;
	}

	if ((group = EC_KEY_get0_group(key)) == NULL) {
		SM2error(SM2_R_INVALID_FIELD);
		goto err;
	}

	if ((order = BN_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_GROUP_get_order(group, order, NULL)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if ((kG = EC_POINT_new(group)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((ctx = BN_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	BN_CTX_start(ctx);

	if ((k = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((rk = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((x1 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((tmp = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}

	/* r and s are returned as part of sig, so they can't be part of ctx. */
	if ((r = BN_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((s = BN_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	for (;;) {
		if (!BN_rand_range(k, order)) {
			SM2error(SM2_R_RANDOM_NUMBER_GENERATION_FAILED);
			goto err;
		}

		if (!EC_POINT_mul(group, kG, k, NULL, NULL, ctx)) {
			SM2error(ERR_R_EC_LIB);
			goto err;
		}

		if (!EC_POINT_get_affine_coordinates(group, kG, x1, NULL,
		    ctx)) {
			SM2error(ERR_R_EC_LIB);
			goto err;
		}

		if (!BN_mod_add(r, e, x1, order, ctx)) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		/* try again if r == 0 or r + k == n */
		if (BN_is_zero(r))
			continue;

		if (!BN_add(rk, r, k)) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if (BN_cmp(rk, order) == 0)
			continue;

		if (!BN_add(s, dA, BN_value_one())) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if (BN_mod_inverse_ct(s, s, order, ctx) == NULL) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if (!BN_mod_mul(tmp, dA, r, order, ctx)) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if (!BN_sub(tmp, k, tmp)) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if (!BN_mod_mul(s, s, tmp, order, ctx)) {
			SM2error(ERR_R_BN_LIB);
			goto err;
		}

		if ((sig = ECDSA_SIG_new()) == NULL) {
			SM2error(ERR_R_MALLOC_FAILURE);
			goto err;
		}

		/* sig takes ownership of r and s */
		if (!ECDSA_SIG_set0(sig, r, s)) {
			SM2error(ERR_R_INTERNAL_ERROR);
			goto err;
		}
		break;
	}

 err:
	if (sig == NULL) {
		BN_free(r);
		BN_free(s);
	}

	BN_free(order);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	EC_POINT_free(kG);
	return sig;
}

static int
sm2_sig_verify(const EC_KEY *key, const ECDSA_SIG *sig, const BIGNUM *e)
{
	const EC_GROUP *group;
	EC_POINT *pt = NULL;
	const BIGNUM *r = NULL, *s = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *order, *t, *x1;
	int ret = 0;

	if ((group = EC_KEY_get0_group(key)) == NULL) {
		SM2error(SM2_R_INVALID_FIELD);
		goto err;
	}

	if ((ctx = BN_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	BN_CTX_start(ctx);

	if ((order = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_GROUP_get_order(group, order, NULL)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if ((pt = EC_POINT_new(group)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((t = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((x1 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/*
	 * Section 5.3.1 in https://tools.ietf.org/html/draft-shen-sm2-ecdsa-00
	 *
	 * B1: verify that r' is in [1, n-1]
	 * B2: verify that s' is in [1, n-1]
	 * B3: set M' ~= ZA || M'
	 * B4: calculate e' = Hv(M'~)
	 * B5: verify that t = r' + s' (mod n) is not zero
	 * B6: calculate the point (x1', y1') = [s']G + [t]PA
	 * B7: verify that r' == e' + x1' (mod n)
	 */

	ECDSA_SIG_get0(sig, &r, &s);

	/* B1: verify that r' is in [1, n-1] */
	if (BN_cmp(r, BN_value_one()) < 0 || BN_cmp(order, r) <= 0) {
		SM2error(SM2_R_BAD_SIGNATURE);
		goto err;
	}

	/* B2: verify that s' is in [1, n-1] */
	if (BN_cmp(s, BN_value_one()) < 0 || BN_cmp(order, s) <= 0) {
		SM2error(SM2_R_BAD_SIGNATURE);
		goto err;
	}

	/* B5: verify that t = r + s is not zero */
	if (!BN_mod_add(t, r, s, order, ctx)) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if (BN_is_zero(t)) {
		SM2error(SM2_R_BAD_SIGNATURE);
		goto err;
	}

	/* B6: calculate pt = (x1', y1') = [s']G + [t]PA */
	if (!EC_POINT_mul(group, pt, s, EC_KEY_get0_public_key(key), t, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_get_affine_coordinates(group, pt, x1, NULL, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	/* B7: verify that r' == e' + x1' (mod n) */
	if (!BN_mod_add(t, e, x1, order, ctx)) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if (BN_cmp(r, t) == 0)
		ret = 1;

 err:
	EC_POINT_free(pt);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	return ret;
}

ECDSA_SIG *
sm2_do_sign(const EC_KEY *key, const EVP_MD *digest, const uint8_t *uid,
    size_t uid_len, const uint8_t *msg, size_t msg_len)
{
	ECDSA_SIG *sig = NULL;
	BIGNUM *e;

	e = sm2_compute_msg_hash(digest, key, uid, uid_len, msg, msg_len);
	if (e == NULL) {
		SM2error(SM2_R_DIGEST_FAILURE);
		goto err;
	}

	sig = sm2_sig_gen(key, e);

 err:
	BN_free(e);
	return sig;
}

int
sm2_do_verify(const EC_KEY *key, const EVP_MD *digest, const ECDSA_SIG *sig,
    const uint8_t *uid, size_t uid_len, const uint8_t *msg, size_t msg_len)
{
	BIGNUM *e;
	int ret = -1;

	e = sm2_compute_msg_hash(digest, key, uid, uid_len, msg, msg_len);
	if (e == NULL) {
		SM2error(SM2_R_DIGEST_FAILURE);
		goto err;
	}

	ret = sm2_sig_verify(key, sig, e);

 err:
	BN_free(e);
	return ret;
}

int
SM2_sign(const unsigned char *dgst, int dgstlen, unsigned char *sig,
    unsigned int *siglen, EC_KEY *eckey)
{
	BIGNUM *e;
	ECDSA_SIG *s = NULL;
	int outlen = 0;
	int ret = -1;

	if ((e = BN_bin2bn(dgst, dgstlen, NULL)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((s = sm2_sig_gen(eckey, e)) == NULL) {
		goto err;
	}

	if ((outlen = i2d_ECDSA_SIG(s, &sig)) < 0) {
		SM2error(SM2_R_ASN1_ERROR);
		goto err;
	}

	*siglen = outlen;
	ret = 1;

 err:
	ECDSA_SIG_free(s);
	BN_free(e);
	return ret;
}

int
SM2_verify(const unsigned char *dgst, int dgstlen, const unsigned char *sig,
    int sig_len, EC_KEY *eckey)
{
	ECDSA_SIG *s;
	BIGNUM *e = NULL;
	const unsigned char *p = sig;
	unsigned char *der = NULL;
	int derlen = -1;
	int ret = -1;

	if ((s = ECDSA_SIG_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (d2i_ECDSA_SIG(&s, &p, sig_len) == NULL) {
		SM2error(SM2_R_INVALID_ENCODING);
		goto err;
	}

	/* Ensure signature uses DER and doesn't have trailing garbage */
	derlen = i2d_ECDSA_SIG(s, &der);
	if (derlen != sig_len || memcmp(sig, der, derlen) != 0) {
		SM2error(SM2_R_INVALID_ENCODING);
		goto err;
	}

	if ((e = BN_bin2bn(dgst, dgstlen, NULL)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}

	ret = sm2_sig_verify(eckey, s, e);

 err:
	free(der);
	BN_free(e);
	ECDSA_SIG_free(s);
	return ret;
}

#endif /* OPENSSL_NO_SM2 */
