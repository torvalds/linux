/*	$OpenBSD: sm2_za.c,v 1.1.1.1 2021/08/18 16:04:32 tb Exp $ */
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

#include <openssl/sm2.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <string.h>

int
sm2_compute_userid_digest(uint8_t *out, const EVP_MD *digest, uint8_t *uid,
    size_t uid_len, const EC_KEY *key)
{
	const EC_GROUP *group;
	EVP_MD_CTX *hash = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *p, *a, *b, *xG, *yG, *xA, *yA;
	uint8_t *buf = NULL;
	uint16_t entla;
	uint8_t e_byte;
	int bytes, p_bytes;
	int rc = 0;

	if ((group = EC_KEY_get0_group(key)) == NULL)
		goto err;

	if ((hash = EVP_MD_CTX_new()) == NULL)
		goto err;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	if ((p = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((a = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((xG = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((yG = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((xA = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((yA = BN_CTX_get(ctx)) == NULL)
		goto err;

	memset(out, 0, EVP_MD_size(digest));

	if (!EVP_DigestInit(hash, digest))
		goto err;

	/*
	 * ZA=H256(ENTLA || IDA || a || b || xG || yG || xA || yA)
	 */

	if (uid_len >= 8192)
		goto err;

	entla = (unsigned short)(8 * uid_len);

	e_byte = entla >> 8;
	if (!EVP_DigestUpdate(hash, &e_byte, 1))
		goto err;

	e_byte = entla & 0xFF;
	if (!EVP_DigestUpdate(hash, &e_byte, 1))
		goto err;

	if (!EVP_DigestUpdate(hash, uid, uid_len))
		goto err;

	if (!EC_GROUP_get_curve(group, p, a, b, ctx))
		goto err;

	p_bytes = BN_num_bytes(p);

	if ((buf = calloc(1, p_bytes)) == NULL)
		goto err;

	if ((bytes = BN_num_bytes(a)) > p_bytes)
		goto err;
	BN_bn2bin(a, buf + p_bytes - bytes);
	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if ((bytes = BN_num_bytes(b)) > p_bytes)
		goto err;
	memset(buf, 0, p_bytes - bytes);
	BN_bn2bin(b, buf + p_bytes - bytes);
	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if (!EC_POINT_get_affine_coordinates(group,
	    EC_GROUP_get0_generator(group), xG, yG, ctx))
		goto err;

	if ((bytes = BN_num_bytes(xG)) > p_bytes)
		goto err;
	memset(buf, 0, p_bytes - bytes);
	BN_bn2bin(xG, buf + p_bytes - bytes);

	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if ((bytes = BN_num_bytes(yG)) > p_bytes)
		goto err;
	memset(buf, 0, p_bytes - bytes);
	BN_bn2bin(yG, buf + p_bytes - bytes);

	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if (!EC_POINT_get_affine_coordinates(group,
	    EC_KEY_get0_public_key(key), xA, yA, ctx))
		goto err;

	if ((bytes = BN_num_bytes(xA)) > p_bytes)
		goto err;
	memset(buf, 0, p_bytes - bytes);
	BN_bn2bin(xA, buf + p_bytes - bytes);

	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if ((bytes = BN_num_bytes(yA)) > p_bytes)
		goto err;
	memset(buf, 0, p_bytes - bytes);
	BN_bn2bin(yA, buf + p_bytes - bytes);

	if (!EVP_DigestUpdate(hash, buf, p_bytes))
		goto err;

	if (!EVP_DigestFinal(hash, out, NULL))
		goto err;

	rc = 1;

 err:
	free(buf);
	BN_CTX_free(ctx);
	EVP_MD_CTX_free(hash);
	return rc;
}

#endif /* OPENSSL_NO_SM2 */
