/*	$OpenBSD: ecdhtest.c,v 1.22 2024/12/24 18:32:31 tb Exp $ */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * The Elliptic Curve Public-Key Crypto Library (ECC Code) included
 * herein is developed by SUN MICROSYSTEMS, INC., and is contributed
 * to the OpenSSL project.
 *
 * The ECC Code is licensed pursuant to the OpenSSL open source
 * license provided below.
 *
 * The ECDH software is originally written by Douglas Stebila of
 * Sun Microsystems Laboratories.
 *
 */
/* ====================================================================
 * Copyright (c) 1998-2003 The OpenSSL Project.  All rights reserved.
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include <openssl/ec.h>
#include <openssl/ecdh.h>

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stdout, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stdout, "\n");
}

static void *
KDF1_SHA1(const void *in, size_t inlen, void *out, size_t *outlen)
{
#ifdef OPENSSL_NO_SHA
	return NULL;
#else
	if (*outlen < SHA_DIGEST_LENGTH)
		return NULL;
	*outlen = SHA_DIGEST_LENGTH;
	return SHA1(in, inlen, out);
#endif
}

static int
ecdh_keygen_test(int nid)
{
	EC_KEY *keya = NULL, *keyb = NULL;
	const EC_POINT *puba, *pubb;
	unsigned char *abuf = NULL, *bbuf = NULL;
	int len = SHA_DIGEST_LENGTH;
	int failed = 1;

	if ((keya = EC_KEY_new_by_curve_name(nid)) == NULL)
		goto err;
	if (!EC_KEY_generate_key(keya))
		goto err;
	if ((puba = EC_KEY_get0_public_key(keya)) == NULL)
		goto err;

	if ((keyb = EC_KEY_new_by_curve_name(nid)) == NULL)
		goto err;
	if (!EC_KEY_generate_key(keyb))
		goto err;
	if ((pubb = EC_KEY_get0_public_key(keyb)) == NULL)
		goto err;

	if ((abuf = calloc(1, len)) == NULL)
		goto err;
	if ((bbuf = calloc(1, len)) == NULL)
		goto err;

	if (ECDH_compute_key(abuf, len, pubb, keya, KDF1_SHA1) != len)
		goto err;
	if (ECDH_compute_key(bbuf, len, puba, keyb, KDF1_SHA1) != len)
		goto err;

	if (memcmp(abuf, bbuf, len) != 0) {
		printf("key generation with %s failed\n", OBJ_nid2sn(nid));

		EC_KEY_print_fp(stdout, keya, 1);
		printf(" shared secret:\n");
		hexdump(abuf, len);

		EC_KEY_print_fp(stdout, keyb, 1);
		printf(" shared secret:\n");
		hexdump(bbuf, len);

		fprintf(stderr, "Error in ECDH routines\n");

		goto err;
	}

	failed = 0;

 err:
	ERR_print_errors_fp(stderr);

	EC_KEY_free(keya);
	EC_KEY_free(keyb);
	freezero(abuf, len);
	freezero(bbuf, len);

	return failed;
}

static const struct ecdh_kat_test {
    const int nid;
    const char *keya;
    const char *keyb;
    const char *want;
} ecdh_kat_tests[] = {
	/* Keys and shared secrets from RFC 5114 */
	{
		.nid =	NID_secp224r1,
		.keya =	"b558eb6c288da707bbb4f8fbae2ab9e9cb62e3bc5c7573e2"
			"2e26d37f",
		.keyb =	"ac3b1add3d9770e6f6a708ee9f3b8e0ab3b480e9f27f85c8"
			"8b5e6d18",
		.want =	"52272f50f46f4edc9151569092f46df2d96ecc3b6dc1714a"
			"4ea949fa",
	},
	{
		.nid =	NID_X9_62_prime256v1,
		.keya =	"814264145f2f56f2e96a8e337a1284993faf432a5abce59e"
			"867b7291d507a3af",
		.keyb =	"2ce1788ec197e096db95a200cc0ab26a19ce6bccad562b8e"
			"ee1b593761cf7f41",
		.want =	"dd0f5396219d1ea393310412d19a08f1f5811e9dc8ec8eea"
			"7f80d21c820c2788",
	},
	{
		.nid =	NID_secp384r1,
		.keya =	"d27335ea71664af244dd14e9fd1260715dfd8a7965571c48"
			"d709ee7a7962a156d706a90cbcb5df2986f05feadb9376f1",
		.keyb =	"52d1791fdb4b70f89c0f00d456c2f7023b6125262c36a7df"
			"1f80231121cce3d39be52e00c194a4132c4a6c768bcd94d2",
		.want =	"5ea1fc4af7256d2055981b110575e0a8cae53160137d904c"
			"59d926eb1b8456e427aa8a4540884c37de159a58028abc0e",
	},
	{
		.nid =	NID_secp521r1,
		.keya =	"0113f82da825735e3d97276683b2b74277bad27335ea7166"
			"4af2430cc4f33459b9669ee78b3ffb9b8683015d344dcbfe"
			"f6fb9af4c6c470be254516cd3c1a1fb47362",
		.keyb =	"00cee3480d8645a17d249f2776d28bae616952d1791fdb4b"
			"70f7c3378732aa1b22928448bcd1dc2496d435b01048066e"
			"be4f72903c361b1a9dc1193dc2c9d0891b96",
		.want =	"00cdea89621cfa46b132f9e4cfe2261cde2d4368eb565663"
			"4c7cc98c7a00cde54ed1866a0dd3e6126c9d2f845daff82c"
			"eb1da08f5d87521bb0ebeca77911169c20cc",
	},
	/* Keys and shared secrets from RFC 5903 */
	{
		.nid =	NID_X9_62_prime256v1,
		.keya =	"c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049"
			"c62a9c57862d1433",
		.keyb =	"c6ef9c5d78ae012a011164acb397ce2088685d8f06bf9be0"
			"b283ab46476bee53",
		.want =	"d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03"
			"812464d04b9442de",
	},
	{
		.nid =	NID_secp384r1,
		.keya =	"099f3c7034d4a2c699884d73a375a67f7624ef7c6b3c0f16"
			"0647b67414dce655e35b538041e649ee3faef896783ab194",
		.keyb =	"41cb0779b4bdb85d47846725fbec3c9430fab46cc8dc5060"
			"855cc9bda0aa2942e0308312916b8ed2960e4bd55a7448fc",
		.want =	"11187331c279962d93d604243fd592cb9d0a926f422e4718"
			"7521287e7156c5c4d603135569b9e9d09cf5d4a270f59746",
	},
	{
		.nid =	NID_secp521r1,
		.keya =	"0037ade9319a89f4dabdb3ef411aaccca5123c61acab57b5"
			"393dce47608172a095aa85a30fe1c2952c6771d937ba9777"
			"f5957b2639bab072462f68c27a57382d"
			"4a52",
		.keyb =	"0145ba99a847af43793fdd0e872e7cdfa16be30fdc780f97"
			"bccc3f078380201e9c677d600b343757a3bdbf2a3163e4c2"
			"f869cca7458aa4a4effc311f5cb151685eb9",
		.want =	"01144c7d79ae6956bc8edb8e7c787c4521cb086fa64407f9"
			"7894e5e6b2d79b04d1427e73ca4baa240a34786859810c06"
			"b3c715a3a8cc3151f2bee417996d19f3ddea",
	},
	/* Keys and shared secrets from RFC 7027 */
	{
		.nid =	NID_brainpoolP256r1,
		.keya =	"81db1ee100150ff2ea338d708271be38300cb54241d79950"
			"f77b063039804f1d",
		.keyb =	"55e40bc41e37e3e2ad25c3c6654511ffa8474a91a0032087"
			"593852d3e7d76bd3",
		.want =	"89afc39d41d3b327814b80940b042590f96556ec91e6ae79"
			"39bce31f3a18bf2b",
	},
	{
		.nid =	NID_brainpoolP384r1,
		.keya =	"1e20f5e048a5886f1f157c74e91bde2b98c8b52d58e5003d"
			"57053fc4b0bd65d6f15eb5d1ee1610df870795143627d042",
		.keyb =	"032640bc6003c59260f7250c3db58ce647f98e1260acce4a"
			"cda3dd869f74e01f8ba5e0324309db6a9831497abac96670",
		.want =	"0bd9d3a7ea0b3d519d09d8e48d0785fb744a6b355e6304bc"
			"51c229fbbce239bbadf6403715c35d4fb2a5444f575d4f42",
	},
	{
		.nid =	NID_brainpoolP512r1,
		.keya =	"16302ff0dbbb5a8d733dab7141c1b45acbc8715939677f6a"
			"56850a38bd87bd59b09e80279609ff333eb9d4c061231fb2"
			"6f92eeb04982a5f1d1764cad57665422",
		.keyb =	"230e18e1bcc88a362fa54e4ea3902009292f7f8033624fd4"
			"71b5d8ace49d12cfabbc19963dab8e2f1eba00bffb29e4d7"
			"2d13f2224562f405cb80503666b25429",
		.want =	"a7927098655f1f9976fa50a9d566865dc530331846381c87"
			"256baf3226244b76d36403c024d7bbf0aa0803eaff405d3d"
			"24f11a9b5c0bef679fe1454b21c4cd1f",
	},
};

#define N_KATS (sizeof(ecdh_kat_tests) / sizeof(ecdh_kat_tests[0]))

/* Given private value and NID, create EC_KEY structure */

static EC_KEY *
mk_eckey(int nid, const char *priv_str)
{
	EC_KEY *key = NULL;
	BIGNUM *priv = NULL;
	EC_POINT *pub = NULL;
	const EC_GROUP *group;
	EC_KEY *ret = NULL;

	if ((key = EC_KEY_new_by_curve_name(nid)) == NULL)
		goto err;
	if (!BN_hex2bn(&priv, priv_str))
		goto err;
	if (!EC_KEY_set_private_key(key, priv))
		goto err;
	if ((group = EC_KEY_get0_group(key)) == NULL)
		goto err;
	if ((pub = EC_POINT_new(group)) == NULL)
		goto err;
	if (!EC_POINT_mul(group, pub, priv, NULL, NULL, NULL))
		goto err;
	if (!EC_KEY_set_public_key(key, pub))
		goto err;

	ret = key;
	key = NULL;

 err:
	EC_KEY_free(key);
	BN_free(priv);
	EC_POINT_free(pub);

	return ret;
}

/*
 * Known answer test: compute shared secret and check it matches expected value.
 */
static int
ecdh_kat(const struct ecdh_kat_test *kat)
{
	EC_KEY *keya = NULL, *keyb = NULL;
	const EC_POINT *puba, *pubb;
	BIGNUM *z = NULL;
	unsigned char *want = NULL, *got = NULL;
	int len = 0;
	int failed = 1;

	if ((keya = mk_eckey(kat->nid, kat->keya)) == NULL)
		goto err;
	if ((puba = EC_KEY_get0_public_key(keya)) == NULL)
		goto err;
	if ((keyb = mk_eckey(kat->nid, kat->keyb)) == NULL)
		goto err;
	if ((pubb = EC_KEY_get0_public_key(keyb)) == NULL)
		goto err;

	if ((len = ECDH_size(keya)) != ECDH_size(keyb))
		goto err;

	if ((want = calloc(1, len)) == NULL)
		goto err;
	if ((got = calloc(1, len)) == NULL)
		goto err;

	if (!BN_hex2bn(&z, kat->want))
		goto err;
	if (BN_num_bytes(z) > len)
		goto err;
	if (BN_bn2binpad(z, want, len) != len)
		goto err;

	if (ECDH_compute_key(got, len, pubb, keya, NULL) != len)
		goto err;
	if (memcmp(got, want, len) != 0)
		goto err;

	memset(got, 0, len);

	if (ECDH_compute_key(got, len, puba, keyb, NULL) != len)
		goto err;
	if (memcmp(got, want, len) != 0)
		goto err;

	failed = 0;

 err:
	if (failed) {
		printf("shared secret with %s failed", OBJ_nid2sn(kat->nid));

		fprintf(stderr, "Error in ECDH routines\n");
		ERR_print_errors_fp(stderr);
	}

	EC_KEY_free(keya);
	EC_KEY_free(keyb);
	BN_free(z);
	freezero(want, len);
	freezero(got, len);

	return failed;
}

int
main(void)
{
	EC_builtin_curve *curves = NULL;
	size_t i, n_curves;
	int failed = 0;

	if ((n_curves = EC_get_builtin_curves(NULL, 0)) == 0)
		errx(1, "EC_get_builtin_curves failed");
	if ((curves = calloc(n_curves, sizeof(*curves))) == NULL)
		errx(1, NULL);
	if (EC_get_builtin_curves(curves, n_curves) != n_curves)
		errx(1, "EC_get_builtin_curves failed");

	for (i = 0; i < n_curves; i++)
		failed |= ecdh_keygen_test(curves[i].nid);

	for (i = 0; i < N_KATS; i++)
		failed |= ecdh_kat(&ecdh_kat_tests[i]);

	free(curves);
	ERR_print_errors_fp(stderr);

	return failed;
}
