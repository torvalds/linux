/*	$OpenBSD: sm2crypttest.c,v 1.2 2022/11/26 16:08:56 tb Exp $ */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#ifdef OPENSSL_NO_SM2
int main(int argc, char *argv[])
{
    printf("No SM2 support\n");
    return (0);
}
#else
#include <openssl/sm2.h>
#include "sm2_local.h"

static EC_GROUP *
create_EC_group(const char *p_hex, const char *a_hex, const char *b_hex,
    const char *x_hex, const char *y_hex, const char *order_hex,
    const char *cof_hex)
{
	BIGNUM *p = NULL;
	BIGNUM *a = NULL;
	BIGNUM *b = NULL;
	BIGNUM *g_x = NULL;
	BIGNUM *g_y = NULL;
	BIGNUM *order = NULL;
	BIGNUM *cof = NULL;
	EC_POINT *generator = NULL;
	EC_GROUP *group = NULL;

	BN_hex2bn(&p, p_hex);
	BN_hex2bn(&a, a_hex);
	BN_hex2bn(&b, b_hex);

	group = EC_GROUP_new_curve_GFp(p, a, b, NULL);
	BN_free(p);
	BN_free(a);
	BN_free(b);

	if (group == NULL)
		return NULL;

	generator = EC_POINT_new(group);
	if (generator == NULL)
		return NULL;

	BN_hex2bn(&g_x, x_hex);
	BN_hex2bn(&g_y, y_hex);

	if (EC_POINT_set_affine_coordinates(group, generator, g_x, g_y,
	    NULL) == 0)
		return NULL;

	BN_free(g_x);
	BN_free(g_y);

	BN_hex2bn(&order, order_hex);
	BN_hex2bn(&cof, cof_hex);

	if (EC_GROUP_set_generator(group, generator, order, cof) == 0)
		return NULL;

	EC_POINT_free(generator);
	BN_free(order);
	BN_free(cof);

	return group;
}

static int
test_sm2(const EC_GROUP *group, const EVP_MD *digest, const char *privkey_hex,
    const char *message)
{
	const size_t msg_len = strlen(message);

	BIGNUM *priv = NULL;
	EC_KEY *key = NULL;
	EC_POINT *pt = NULL;
	size_t ctext_len = 0;
	uint8_t *ctext = NULL;
	uint8_t *recovered = NULL;
	size_t recovered_len = msg_len;
	int rc = 0;

	BN_hex2bn(&priv, privkey_hex);

	key = EC_KEY_new();
	EC_KEY_set_group(key, group);
	EC_KEY_set_private_key(key, priv);

	pt = EC_POINT_new(group);
	EC_POINT_mul(group, pt, priv, NULL, NULL, NULL);

	EC_KEY_set_public_key(key, pt);
	BN_free(priv);
	EC_POINT_free(pt);

	if (!SM2_ciphertext_size(key, digest, msg_len, &ctext_len))
		goto done;
	ctext = calloc(1, ctext_len);
	if (ctext == NULL)
		goto done;

	rc = SM2_encrypt(key, digest, (const uint8_t *)message, msg_len, ctext, &ctext_len);

	if (rc == 0)
		goto done;

	recovered = calloc(1, msg_len);
	if (recovered == NULL)
		goto done;
	rc = SM2_decrypt(key, digest, ctext, ctext_len, recovered, &recovered_len);

	if (rc == 0)
		goto done;
	if (recovered_len != msg_len)
		goto done;
	if (memcmp(recovered, message, msg_len) != 0)
		goto done;

	rc = 1;
 done:

	free(ctext);
	free(recovered);
	EC_KEY_free(key);
	return rc;
}

int
main(int argc, char **argv)
{
	int rc;
	EC_GROUP *test_group =
		create_EC_group
		("8542D69E4C044F18E8B92435BF6FF7DE457283915C45517D722EDB8B08F1DFC3",
		 "787968B4FA32C3FD2417842E73BBFEFF2F3C848B6831D7E0EC65228B3937E498",
		 "63E4C6D3B23B0C849CF84241484BFE48F61D59A5B16BA06E6E12D1DA27C5249A",
		 "421DEBD61B62EAB6746434EBC3CC315E32220B3BADD50BDC4C4E6C147FEDD43D",
		 "0680512BCBB42C07D47349D2153B70C4E5D7FDFCBFA36EA1A85841B9E46E09A2",
		 "8542D69E4C044F18E8B92435BF6FF7DD297720630485628D5AE74EE7C32E79B7",
		 "1");

	if (test_group == NULL)
		return 1;

	rc = test_sm2(test_group, EVP_sm3(),
	    "1649AB77A00637BD5E2EFE283FBF353534AA7F7CB89463F208DDBC2920BB0DA0",
	    "encryption standard");

	if (rc == 0)
		return 1;

	/* Same test as above except using SHA-256 instead of SM3 */
	rc = test_sm2(test_group, EVP_sha256(),
	    "1649AB77A00637BD5E2EFE283FBF353534AA7F7CB89463F208DDBC2920BB0DA0",
	    "encryption standard");
	if (rc == 0)
		return 1;

	EC_GROUP_free(test_group);

	printf("SUCCESS\n");

	return 0;
}

#endif
