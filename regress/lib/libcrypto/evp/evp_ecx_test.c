/*	$OpenBSD: evp_ecx_test.c,v 1.5 2023/03/02 20:04:42 tb Exp $ */
/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "curve25519_internal.h"

static const uint8_t ed25519_priv_key_1[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MC4CAQAwBQYDK2VwBCIEIIkDg89yB70IpUXsAZieCcCDE2ig9nin9JJWpDQoCup8\n"
    "-----END PRIVATE KEY-----\n";

const uint8_t ed25519_raw_priv_key_1[] = {
	0x89, 0x03, 0x83, 0xcf, 0x72, 0x07, 0xbd, 0x08,
	0xa5, 0x45, 0xec, 0x01, 0x98, 0x9e, 0x09, 0xc0,
	0x83, 0x13, 0x68, 0xa0, 0xf6, 0x78, 0xa7, 0xf4,
	0x92, 0x56, 0xa4, 0x34, 0x28, 0x0a, 0xea, 0x7c,
};

static const uint8_t ed25519_pub_key_1[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MCowBQYDK2VwAyEA1vxPpbnoC7G8vFmRjYVXUU2aln3hUZEgfW1atlTHF/o=\n"
    "-----END PUBLIC KEY-----\n";

const uint8_t ed25519_raw_pub_key_1[] = {
	0xd6, 0xfc, 0x4f, 0xa5, 0xb9, 0xe8, 0x0b, 0xb1,
	0xbc, 0xbc, 0x59, 0x91, 0x8d, 0x85, 0x57, 0x51,
	0x4d, 0x9a, 0x96, 0x7d, 0xe1, 0x51, 0x91, 0x20,
	0x7d, 0x6d, 0x5a, 0xb6, 0x54, 0xc7, 0x17, 0xfa,
};

static const uint8_t message_1[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t signature_1[] = {
	0x1c, 0xba, 0x71, 0x5a, 0xbc, 0x7f, 0x3b, 0x6b,
	0xc1, 0x61, 0x04, 0x02, 0xb6, 0x37, 0x9e, 0xe1,
	0xa6, 0x7c, 0xfe, 0xcd, 0xdd, 0x68, 0x59, 0xb5,
	0xc8, 0x09, 0xa5, 0x36, 0x66, 0xfb, 0xad, 0xc5,
	0x68, 0x31, 0xd1, 0x7a, 0x48, 0x44, 0xaa, 0xa9,
	0x9c, 0xf1, 0x1a, 0xbb, 0xd5, 0x49, 0xd5, 0xe8,
	0x63, 0xe2, 0x94, 0x77, 0x16, 0x1a, 0x52, 0xfa,
	0x33, 0x6b, 0xf3, 0x57, 0x93, 0xd4, 0xc1, 0x07,
};

static const uint8_t x25519_priv_key_1[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MC4CAQAwBQYDK2VuBCIEICi6rzFFJb02mi6sopELeshEi2vr68ul4bzEHPOz+K1o\n"
    "-----END PRIVATE KEY-----\n";

const uint8_t x25519_raw_priv_key_1[] = {
	0x28, 0xba, 0xaf, 0x31, 0x45, 0x25, 0xbd, 0x36,
	0x9a, 0x2e, 0xac, 0xa2, 0x91, 0x0b, 0x7a, 0xc8,
	0x44, 0x8b, 0x6b, 0xeb, 0xeb, 0xcb, 0xa5, 0xe1,
	0xbc, 0xc4, 0x1c, 0xf3, 0xb3, 0xf8, 0xad, 0x68,
};

static const uint8_t x25519_pub_key_1[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MCowBQYDK2VuAyEAu4WHXnAQL2YfonJhuoEO9PM2WwXjveApPmCXSiDnf1M=\n"
    "-----END PUBLIC KEY-----\n";

static const uint8_t x25519_raw_pub_key_1[] = {
	0xbb, 0x85, 0x87, 0x5e, 0x70, 0x10, 0x2f, 0x66,
	0x1f, 0xa2, 0x72, 0x61, 0xba, 0x81, 0x0e, 0xf4,
	0xf3, 0x36, 0x5b, 0x05, 0xe3, 0xbd, 0xe0, 0x29,
	0x3e, 0x60, 0x97, 0x4a, 0x20, 0xe7, 0x7f, 0x53,
};

static const uint8_t x25519_priv_key_2[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MC4CAQAwBQYDK2VuBCIEIAg9Jbp/Ma0TO4r179WGGiv+VnGxGNRh4VNrHUij7Ql/\n"
    "-----END PRIVATE KEY-----\n";

static const uint8_t x25519_raw_priv_key_2[] = {
	0x08, 0x3d, 0x25, 0xba, 0x7f, 0x31, 0xad, 0x13,
	0x3b, 0x8a, 0xf5, 0xef, 0xd5, 0x86, 0x1a, 0x2b,
	0xfe, 0x56, 0x71, 0xb1, 0x18, 0xd4, 0x61, 0xe1,
	0x53, 0x6b, 0x1d, 0x48, 0xa3, 0xed, 0x09, 0x7f,
};

static const uint8_t x25519_pub_key_2[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MCowBQYDK2VuAyEABvksGQRgsUXEK5CaniVZ59pPvDoABgBSdAM+EF0Q9Cw=\n"
    "-----END PUBLIC KEY-----\n";

static const uint8_t x25519_raw_pub_key_2[] = {
	0x06, 0xf9, 0x2c, 0x19, 0x04, 0x60, 0xb1, 0x45,
	0xc4, 0x2b, 0x90, 0x9a, 0x9e, 0x25, 0x59, 0xe7,
	0xda, 0x4f, 0xbc, 0x3a, 0x00, 0x06, 0x00, 0x52,
	0x74, 0x03, 0x3e, 0x10, 0x5d, 0x10, 0xf4, 0x2c,
};

static const uint8_t shared_key_1[] = {
	0xa2, 0x61, 0xf5, 0x91, 0x2e, 0x82, 0xbc, 0x98,
	0x6c, 0x85, 0xb6, 0x51, 0x1f, 0x69, 0xdb, 0xfa,
	0x88, 0x6c, 0x4b, 0x9e, 0x3b, 0xb0, 0x71, 0xd1,
	0xf3, 0xea, 0x2a, 0xd0, 0xef, 0xf6, 0xa5, 0x5a,
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
ecx_ed25519_keygen_test(void)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	EVP_PKEY *pkey = NULL;
	BIO *bio = NULL;
	int failed = 1;

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to create ED25519 context\n");
		goto failure;
	}

	if (EVP_PKEY_keygen_init(pkey_ctx) <= 0) {
		fprintf(stderr, "FAIL: failed to init keygen for ED25519\n");
		goto failure;
	}
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
		fprintf(stderr, "FAIL: failed to generate ED25519 key\n");
		goto failure;
	}

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
		fprintf(stderr, "FAIL: failed to write ED25519 to PEM\n");
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_PKEY_CTX_free(pkey_ctx);
	EVP_PKEY_free(pkey);

	return failed;
}

static int
ecx_ed25519_raw_key_test(void)
{
	EVP_PKEY *pkey = NULL;
	uint8_t *priv_key = NULL;
	size_t priv_key_len = 0;
	uint8_t *pub_key = NULL;
	size_t pub_key_len = 0;
	const uint8_t *pp;
	BIO *bio = NULL;
	int failed = 1;

	/*
	 * Decode private key from PEM and check raw private and raw public.
	 */

	if ((bio = BIO_new_mem_buf(ed25519_priv_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read private key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (!EVP_PKEY_get_raw_private_key(pkey, NULL, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw private key len\n");
		goto failure;
	}
	if (priv_key_len != sizeof(ed25519_raw_priv_key_1)) {
		fprintf(stderr, "FAIL: raw private key length differs "
		    "(%zu != %zu)\n", priv_key_len,
		    sizeof(ed25519_raw_priv_key_1));
		goto failure;
	}
	if ((priv_key = malloc(priv_key_len)) == NULL)
		errx(1, "failed to malloc priv key");
	if (!EVP_PKEY_get_raw_private_key(pkey, priv_key, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw private key len\n");
		goto failure;
	}
	if (memcmp(priv_key, ed25519_raw_priv_key_1, priv_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw private key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(priv_key, priv_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(ed25519_raw_priv_key_1, sizeof(ed25519_raw_priv_key_1));
		goto failure;
	}

	if (!EVP_PKEY_get_raw_public_key(pkey, NULL, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (pub_key_len != sizeof(ed25519_raw_pub_key_1)) {
		fprintf(stderr, "FAIL: raw public key length differs "
		    "(%zu != %zu)\n", pub_key_len,
		    sizeof(ed25519_raw_pub_key_1));
		goto failure;
	}
	if ((pub_key = malloc(pub_key_len)) == NULL)
		errx(1, "failed to malloc private key");
	if (!EVP_PKEY_get_raw_public_key(pkey, pub_key, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (memcmp(pub_key, ed25519_raw_pub_key_1, pub_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw public key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(pub_key, pub_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(ed25519_raw_pub_key_1, sizeof(ed25519_raw_pub_key_1));
		goto failure;
	}

	BIO_free(bio);
	bio = NULL;

	EVP_PKEY_free(pkey);
	pkey = NULL;

	freezero(priv_key, priv_key_len);
	priv_key = NULL;
	priv_key_len = 0;

	freezero(pub_key, pub_key_len);
	pub_key = NULL;
	pub_key_len = 0;

	/*
	 * Decode public key from PEM and check raw private and raw public.
	 */

	if ((bio = BIO_new_mem_buf(ed25519_pub_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read public key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	/*
	 * Yet another astounding API design - we cannot tell if the private key
	 * is not present, or if some other failure occurred.
	 */
	if (!EVP_PKEY_get_raw_private_key(pkey, NULL, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw priv key len\n");
		goto failure;
	}
	if ((priv_key = malloc(priv_key_len)) == NULL)
		errx(1, "failed to malloc priv key");
	if (EVP_PKEY_get_raw_private_key(pkey, priv_key, &priv_key_len)) {
		fprintf(stderr, "FAIL: got raw private key, should fail\n");
		goto failure;
	}

	if (!EVP_PKEY_get_raw_public_key(pkey, NULL, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (pub_key_len != sizeof(ed25519_raw_pub_key_1)) {
		fprintf(stderr, "FAIL: raw public key length differs "
		    "(%zu != %zu)\n", pub_key_len,
		    sizeof(ed25519_raw_pub_key_1));
		goto failure;
	}
	if ((pub_key = malloc(pub_key_len)) == NULL)
		errx(1, "failed to malloc private key");
	if (!EVP_PKEY_get_raw_public_key(pkey, pub_key, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (memcmp(pub_key, ed25519_raw_pub_key_1, pub_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw public key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(pub_key, pub_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(ed25519_raw_pub_key_1, sizeof(ed25519_raw_pub_key_1));
		goto failure;
	}

	BIO_free(bio);
	bio = NULL;

	EVP_PKEY_free(pkey);
	pkey = NULL;

	/*
	 * Create PKEY from raw private, check PEM encoded private and public.
	 */
	if ((pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL,
	    ed25519_raw_priv_key_1, sizeof(ed25519_raw_priv_key_1))) == NULL) {
		fprintf(stderr, "FAIL: PKEY from raw private key failed");
		goto failure;
	}
	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
		fprintf(stderr, "FAIL: failed to write ED25519 private to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(ed25519_priv_key_1, pp) != 0) {
		fprintf(stderr, "FAIL: resulting private key PEM differs\n");
		goto failure;
	}

	(void)BIO_reset(bio);
	if (!PEM_write_bio_PUBKEY(bio, pkey)) {
		fprintf(stderr, "FAIL: failed to write ED25519 public to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(ed25519_pub_key_1, pp) != 0) {
		fprintf(stderr, "FAIL: resulting public key PEM differs\n");
		fprintf(stderr, "%s\n", ed25519_pub_key_1);
		fprintf(stderr, "%s\n", pp);
		//goto failure;
	}

	EVP_PKEY_free(pkey);
	pkey = NULL;

	/*
	 * Create PKEY from raw public, check public key PEM.
	 */
	if ((pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL,
	    ed25519_raw_pub_key_1, sizeof(ed25519_raw_pub_key_1))) == NULL) {
		fprintf(stderr, "FAIL: PKEY from raw public key failed");
		goto failure;
	}
	(void)BIO_reset(bio);
	if (!PEM_write_bio_PUBKEY(bio, pkey)) {
		fprintf(stderr, "FAIL: failed to write ED25519 public to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(ed25519_pub_key_1, pp) != 0) {
		fprintf(stderr, "FAIL: resulting public key PEM differs\n");
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_PKEY_free(pkey);
	freezero(priv_key, priv_key_len);
	freezero(pub_key, pub_key_len);

	return failed;
}

static int
ecx_ed25519_sign_test(void)
{
	EVP_MD_CTX *md_ctx = NULL;
	EVP_PKEY *pkey = NULL;
	uint8_t *signature = NULL;
	size_t signature_len = 0;
	BIO *bio = NULL;
	int failed = 1;

	if ((bio = BIO_new_mem_buf(ed25519_priv_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read private key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		errx(1, "failed to create MD_CTX");

	if (!EVP_DigestSignInit(md_ctx, NULL, NULL, NULL, pkey)) {
		fprintf(stderr, "FAIL: failed to init digest sign\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	if (!EVP_DigestSign(md_ctx, NULL, &signature_len, NULL, 0)) {
		fprintf(stderr, "FAIL: failed to digest sign update\n");
		goto failure;
	}
	if ((signature = calloc(1, signature_len)) == NULL)
		errx(1, "failed to allocate signature");
	if (!EVP_DigestSign(md_ctx, signature, &signature_len, message_1,
	    sizeof(message_1))) {
		fprintf(stderr, "FAIL: failed to digest sign update\n");
		goto failure;
	}

	if (signature_len != sizeof(signature_1)) {
		fprintf(stderr, "FAIL: signature length differs (%zu != %zu)\n",
		    signature_len, sizeof(signature_1));
		goto failure;
	}

	if (memcmp(signature, signature_1, signature_len) != 0) {
		fprintf(stderr, "FAIL: Ed25519 sign failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(signature, signature_len);
		fprintf(stderr, "Want:\n");
		hexdump(signature_1, sizeof(signature_1));
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_MD_CTX_free(md_ctx);
	EVP_PKEY_free(pkey);
	free(signature);

	return failed;
}

static int
ecx_ed25519_verify_test(void)
{
	EVP_MD_CTX *md_ctx = NULL;
	EVP_PKEY *pkey = NULL;
	BIO *bio = NULL;
	int failed = 1;

	if ((bio = BIO_new_mem_buf(ed25519_pub_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read public key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		errx(1, "failed to create MD_CTX");

	if (!EVP_DigestVerifyInit(md_ctx, NULL, NULL, NULL, pkey)) {
		fprintf(stderr, "FAIL: failed to init digest verify\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	if (!EVP_DigestVerify(md_ctx, signature_1, sizeof(signature_1),
	    message_1, sizeof(message_1))) {
		fprintf(stderr, "FAIL: failed to digest verify update\n");
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_MD_CTX_free(md_ctx);
	EVP_PKEY_free(pkey);

	return failed;
}

static int
ecx_x25519_keygen_test(void)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	EVP_PKEY *pkey = NULL;
	BIO *bio = NULL;
	int failed = 1;

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to create X25519 context\n");
		goto failure;
	}

	if (EVP_PKEY_keygen_init(pkey_ctx) <= 0) {
		fprintf(stderr, "FAIL: failed to init keygen for X25519\n");
		goto failure;
	}
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
		fprintf(stderr, "FAIL: failed to generate X25519 key\n");
		goto failure;
	}

	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
		fprintf(stderr, "FAIL: failed to write X25519 to PEM\n");
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_PKEY_CTX_free(pkey_ctx);
	EVP_PKEY_free(pkey);

	return failed;
}

static int
ecx_x25519_derive_test(void)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	EVP_PKEY *pkey = NULL, *pkey_peer = NULL;
	uint8_t *shared_key = NULL;
	size_t shared_key_len = 0;
	BIO *bio = NULL;
	int failed = 1;

	if ((bio = BIO_new_mem_buf(x25519_priv_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read private key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	BIO_free(bio);
	if ((bio = BIO_new_mem_buf(x25519_pub_key_2, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey_peer = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read peer public key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if ((pkey_ctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to create X25519 context\n");
		goto failure;
	}
	if (EVP_PKEY_derive_init(pkey_ctx) <= 0) {
		fprintf(stderr, "FAIL: failed to init derive for X25519\n");
		goto failure;
	}
	if (EVP_PKEY_derive_set_peer(pkey_ctx, pkey_peer) <= 0) {
		fprintf(stderr, "FAIL: failed to set peer key for X25519\n");
		goto failure;
	}
	if (EVP_PKEY_derive(pkey_ctx, NULL, &shared_key_len) <= 0) {
		fprintf(stderr, "FAIL: failed to derive X25519 key length\n");
		goto failure;
	}
	if ((shared_key = malloc(shared_key_len)) == NULL)
		errx(1, "failed to malloc shared key");
	if (EVP_PKEY_derive(pkey_ctx, shared_key, &shared_key_len) <= 0) {
		fprintf(stderr, "FAIL: failed to derive X25519 key\n");
		goto failure;
	}

	if (shared_key_len != sizeof(shared_key_1)) {
		fprintf(stderr, "FAIL: shared key length differs (%zu != %zu)\n",
		    shared_key_len, sizeof(shared_key_1));
		goto failure;
	}

	if (memcmp(shared_key, shared_key_1, shared_key_len) != 0) {
		fprintf(stderr, "FAIL: X25519 derive failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(shared_key, shared_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(shared_key_1, sizeof(shared_key_1));
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_PKEY_CTX_free(pkey_ctx);
	EVP_PKEY_free(pkey_peer);
	EVP_PKEY_free(pkey);
	freezero(shared_key, shared_key_len);

	return failed;
}

static int
ecx_x25519_raw_key_test(void)
{
	EVP_PKEY *pkey = NULL;
	uint8_t *priv_key = NULL;
	size_t priv_key_len = 0;
	uint8_t *pub_key = NULL;
	size_t pub_key_len = 0;
	const uint8_t *pp;
	BIO *bio = NULL;
	int failed = 1;

	/*
	 * Decode private key from PEM and check raw private and raw public.
	 */

	if ((bio = BIO_new_mem_buf(x25519_priv_key_2, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read private key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (!EVP_PKEY_get_raw_private_key(pkey, NULL, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw private key len\n");
		goto failure;
	}
	if (priv_key_len != sizeof(x25519_raw_priv_key_2)) {
		fprintf(stderr, "FAIL: raw private key length differs "
		    "(%zu != %zu)\n", priv_key_len,
		    sizeof(x25519_raw_priv_key_2));
		goto failure;
	}
	if ((priv_key = malloc(priv_key_len)) == NULL)
		errx(1, "failed to malloc priv key");
	if (!EVP_PKEY_get_raw_private_key(pkey, priv_key, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw private key len\n");
		goto failure;
	}
	if (memcmp(priv_key, x25519_raw_priv_key_2, priv_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw private key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(priv_key, priv_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(x25519_raw_priv_key_2, sizeof(x25519_raw_priv_key_2));
		goto failure;
	}

	if (!EVP_PKEY_get_raw_public_key(pkey, NULL, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (pub_key_len != sizeof(x25519_raw_pub_key_2)) {
		fprintf(stderr, "FAIL: raw public key length differs "
		    "(%zu != %zu)\n", pub_key_len,
		    sizeof(x25519_raw_pub_key_2));
		goto failure;
	}
	if ((pub_key = malloc(pub_key_len)) == NULL)
		errx(1, "failed to malloc private key");
	if (!EVP_PKEY_get_raw_public_key(pkey, pub_key, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (memcmp(pub_key, x25519_raw_pub_key_2, pub_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw public key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(pub_key, pub_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(x25519_raw_pub_key_2, sizeof(x25519_raw_pub_key_2));
		goto failure;
	}

	BIO_free(bio);
	bio = NULL;

	EVP_PKEY_free(pkey);
	pkey = NULL;

	freezero(priv_key, priv_key_len);
	priv_key = NULL;
	priv_key_len = 0;

	freezero(pub_key, pub_key_len);
	pub_key = NULL;
	pub_key_len = 0;

	/*
	 * Decode public key from PEM and check raw private and raw public.
	 */

	if ((bio = BIO_new_mem_buf(x25519_pub_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to read public key\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	/*
	 * Yet another astounding API design - we cannot tell if the private key
	 * is not present, or if some other failure occurred.
	 */
	if (!EVP_PKEY_get_raw_private_key(pkey, NULL, &priv_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw priv key len\n");
		goto failure;
	}
	if ((priv_key = malloc(priv_key_len)) == NULL)
		errx(1, "failed to malloc priv key");
	if (EVP_PKEY_get_raw_private_key(pkey, priv_key, &priv_key_len)) {
		fprintf(stderr, "FAIL: got raw private key, should fail\n");
		goto failure;
	}

	if (!EVP_PKEY_get_raw_public_key(pkey, NULL, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (pub_key_len != sizeof(x25519_raw_pub_key_1)) {
		fprintf(stderr, "FAIL: raw public key length differs "
		    "(%zu != %zu)\n", pub_key_len,
		    sizeof(x25519_raw_pub_key_1));
		goto failure;
	}
	if ((pub_key = malloc(pub_key_len)) == NULL)
		errx(1, "failed to malloc private key");
	if (!EVP_PKEY_get_raw_public_key(pkey, pub_key, &pub_key_len)) {
		fprintf(stderr, "FAIL: failed to get raw pub key len\n");
		goto failure;
	}
	if (memcmp(pub_key, x25519_raw_pub_key_1, pub_key_len) != 0) {
		fprintf(stderr, "FAIL: get raw public key failed\n");
		fprintf(stderr, "Got:\n");
		hexdump(pub_key, pub_key_len);
		fprintf(stderr, "Want:\n");
		hexdump(x25519_raw_pub_key_1, sizeof(x25519_raw_pub_key_1));
		goto failure;
	}

	BIO_free(bio);
	bio = NULL;

	EVP_PKEY_free(pkey);
	pkey = NULL;

	/*
	 * Create PKEY from raw private, check PEM encoded private and public.
	 */
	if ((pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL,
	    x25519_raw_priv_key_2, sizeof(x25519_raw_priv_key_2))) == NULL) {
		fprintf(stderr, "FAIL: PKEY from raw private key failed");
		goto failure;
	}
	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
		fprintf(stderr, "FAIL: failed to write X25519 private to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(x25519_priv_key_2, pp) != 0) {
		fprintf(stderr, "FAIL: resulting private key PEM differs\n");
		goto failure;
	}

	(void)BIO_reset(bio);
	if (!PEM_write_bio_PUBKEY(bio, pkey)) {
		fprintf(stderr, "FAIL: failed to write X25519 public to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(x25519_pub_key_2, pp) != 0) {
		fprintf(stderr, "FAIL: resulting public key PEM differs\n");
		goto failure;
	}

	EVP_PKEY_free(pkey);
	pkey = NULL;

	/*
	 * Create PKEY from raw public, check public key PEM.
	 */
	if ((pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL,
	    x25519_raw_pub_key_1, sizeof(x25519_raw_pub_key_1))) == NULL) {
		fprintf(stderr, "FAIL: PKEY from raw public key failed");
		goto failure;
	}
	(void)BIO_reset(bio);
	if (!PEM_write_bio_PUBKEY(bio, pkey)) {
		fprintf(stderr, "FAIL: failed to write X25519 public to PEM\n");
		goto failure;
	}
	BIO_get_mem_data(bio, &pp);
	if (strcmp(x25519_pub_key_1, pp) != 0) {
		fprintf(stderr, "FAIL: resulting public key PEM differs\n");
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EVP_PKEY_free(pkey);
	freezero(priv_key, priv_key_len);
	freezero(pub_key, pub_key_len);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= ecx_ed25519_raw_key_test();
	failed |= ecx_ed25519_keygen_test();
	failed |= ecx_ed25519_sign_test();
	failed |= ecx_ed25519_verify_test();

	failed |= ecx_x25519_keygen_test();
	failed |= ecx_x25519_derive_test();
	failed |= ecx_x25519_raw_key_test();

	return failed;
}
