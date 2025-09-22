/*	$OpenBSD: sm2_crypt.c,v 1.4 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/sm2.h>

#include "err_local.h"
#include "sm2_local.h"

typedef struct SM2_Ciphertext_st SM2_Ciphertext;

SM2_Ciphertext *SM2_Ciphertext_new(void);
void SM2_Ciphertext_free(SM2_Ciphertext *a);
SM2_Ciphertext *d2i_SM2_Ciphertext(SM2_Ciphertext **a, const unsigned char **in,
    long len);
int i2d_SM2_Ciphertext(SM2_Ciphertext *a, unsigned char **out);

struct SM2_Ciphertext_st {
	BIGNUM *C1x;
	BIGNUM *C1y;
	ASN1_OCTET_STRING *C3;
	ASN1_OCTET_STRING *C2;
};

static const ASN1_TEMPLATE SM2_Ciphertext_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(SM2_Ciphertext, C1x),
		.field_name = "C1x",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(SM2_Ciphertext, C1y),
		.field_name = "C1y",
		.item = &BIGNUM_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(SM2_Ciphertext, C3),
		.field_name = "C3",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(SM2_Ciphertext, C2),
		.field_name = "C2",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM SM2_Ciphertext_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = SM2_Ciphertext_seq_tt,
	.tcount = sizeof(SM2_Ciphertext_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(SM2_Ciphertext),
	.sname = "SM2_Ciphertext",
};

SM2_Ciphertext *
d2i_SM2_Ciphertext(SM2_Ciphertext **a, const unsigned char **in, long len)
{
	return (SM2_Ciphertext *) ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &SM2_Ciphertext_it);
}

int
i2d_SM2_Ciphertext(SM2_Ciphertext *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &SM2_Ciphertext_it);
}

SM2_Ciphertext *
SM2_Ciphertext_new(void)
{
	return (SM2_Ciphertext *)ASN1_item_new(&SM2_Ciphertext_it);
}

void
SM2_Ciphertext_free(SM2_Ciphertext *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &SM2_Ciphertext_it);
}

static size_t
ec_field_size(const EC_GROUP *group)
{
	/* Is there some simpler way to do this? */
	BIGNUM *p;
	size_t field_size = 0;

	if ((p = BN_new()) == NULL)
		goto err;
	if (!EC_GROUP_get_curve(group, p, NULL, NULL, NULL))
		goto err;
	field_size = BN_num_bytes(p);
 err:
	BN_free(p);
	return field_size;
}

int
SM2_plaintext_size(const EC_KEY *key, const EVP_MD *digest, size_t msg_len,
    size_t *pl_size)
{
	size_t field_size, overhead;
	int md_size;

	if ((field_size = ec_field_size(EC_KEY_get0_group(key))) == 0) {
		SM2error(SM2_R_INVALID_FIELD);
		return 0;
	}

	if ((md_size = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		return 0;
	}

	overhead = 10 + 2 * field_size + md_size;
	if (msg_len <= overhead) {
		SM2error(SM2_R_INVALID_ARGUMENT);
		return 0;
	}

	*pl_size = msg_len - overhead;
	return 1;
}

int
SM2_ciphertext_size(const EC_KEY *key, const EVP_MD *digest, size_t msg_len,
    size_t *c_size)
{
	size_t asn_size, field_size;
	int md_size;

	if ((field_size = ec_field_size(EC_KEY_get0_group(key))) == 0) {
		SM2error(SM2_R_INVALID_FIELD);
		return 0;
	}

	if ((md_size = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		return 0;
	}

	asn_size = 2 * ASN1_object_size(0, field_size + 1, V_ASN1_INTEGER) +
	    ASN1_object_size(0, md_size, V_ASN1_OCTET_STRING) +
	    ASN1_object_size(0, msg_len, V_ASN1_OCTET_STRING);

	*c_size = ASN1_object_size(1, asn_size, V_ASN1_SEQUENCE);
	return 1;
}

int
sm2_kdf(uint8_t *key, size_t key_len, uint8_t *secret, size_t secret_len,
    const EVP_MD *digest)
{
	EVP_MD_CTX *hash;
	uint8_t *hash_buf = NULL;
	uint32_t ctr = 1;
	uint8_t ctr_buf[4] = {0};
	size_t hadd, hlen;
	int rc = 0;

	if ((hash = EVP_MD_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((hlen = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		goto err;
	}
	if ((hash_buf = malloc(hlen)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	while ((key_len > 0) && (ctr != 0)) {
		if (!EVP_DigestInit_ex(hash, digest, NULL)) {
			SM2error(ERR_R_EVP_LIB);
			goto err;
		}
		if (!EVP_DigestUpdate(hash, secret, secret_len)) {
			SM2error(ERR_R_EVP_LIB);
			goto err;
		}

		/* big-endian counter representation */
		ctr_buf[0] = (ctr >> 24) & 0xff;
		ctr_buf[1] = (ctr >> 16) & 0xff;
		ctr_buf[2] = (ctr >> 8) & 0xff;
		ctr_buf[3] = ctr & 0xff;
		ctr++;

		if (!EVP_DigestUpdate(hash, ctr_buf, 4)) {
			SM2error(ERR_R_EVP_LIB);
			goto err;
		}
		if (!EVP_DigestFinal(hash, hash_buf, NULL)) {
			SM2error(ERR_R_EVP_LIB);
			goto err;
		}

		hadd = key_len > hlen ? hlen : key_len;
		memcpy(key, hash_buf, hadd);
		memset(hash_buf, 0, hlen);
		key_len -= hadd;
		key += hadd;
	}

	rc = 1;
 err:
	free(hash_buf);
	EVP_MD_CTX_free(hash);
	return rc;
}

int
SM2_encrypt(const EC_KEY *key, const EVP_MD *digest, const uint8_t *msg,
    size_t msg_len, uint8_t *ciphertext_buf, size_t *ciphertext_len)
{
	SM2_Ciphertext ctext_struct;
	EVP_MD_CTX *hash = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *order = NULL;
	BIGNUM *k, *x1, *y1, *x2, *y2;
	const EC_GROUP *group;
	const EC_POINT *P;
	EC_POINT *kG = NULL, *kP = NULL;
	uint8_t *msg_mask = NULL, *x2y2 = NULL, *C3 = NULL;
	size_t C3_size, field_size, i, x2size, y2size;
	int rc = 0;
	int clen;

	ctext_struct.C2 = NULL;
	ctext_struct.C3 = NULL;

	if ((hash = EVP_MD_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((group = EC_KEY_get0_group(key)) == NULL) {
		SM2error(SM2_R_INVALID_KEY);
		goto err;
	}

	if ((order = BN_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_GROUP_get_order(group, order, NULL)) {
		SM2error(SM2_R_INVALID_GROUP_ORDER);
		goto err;
	}

	if ((P = EC_KEY_get0_public_key(key)) == NULL) {
		SM2error(SM2_R_INVALID_KEY);
		goto err;
	}

	if ((field_size = ec_field_size(group)) == 0) {
		SM2error(SM2_R_INVALID_FIELD);
		goto err;
	}

	if ((C3_size = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		goto err;
	}

	if ((kG = EC_POINT_new(group)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((kP = EC_POINT_new(group)) == NULL) {
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
	if ((x1 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((x2 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((y1 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((y2 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}

	if ((x2y2 = calloc(2, field_size)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((C3 = calloc(1, C3_size)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	memset(ciphertext_buf, 0, *ciphertext_len);

	if (!BN_rand_range(k, order)) {
		SM2error(SM2_R_RANDOM_NUMBER_GENERATION_FAILED);
		goto err;
	}

	if (!EC_POINT_mul(group, kG, k, NULL, NULL, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_get_affine_coordinates(group, kG, x1, y1, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_mul(group, kP, NULL, P, k, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_get_affine_coordinates(group, kP, x2, y2, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if ((x2size = BN_num_bytes(x2)) > field_size ||
	    (y2size = BN_num_bytes(y2)) > field_size) {
		SM2error(SM2_R_BIGNUM_OUT_OF_RANGE);
		goto err;
	}

	BN_bn2bin(x2, x2y2 + field_size - x2size);
	BN_bn2bin(y2, x2y2 + 2 * field_size - y2size);

	if ((msg_mask = calloc(1, msg_len)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!sm2_kdf(msg_mask, msg_len, x2y2, 2 * field_size, digest)) {
		SM2error(SM2_R_KDF_FAILURE);
		goto err;
	}

	for (i = 0; i != msg_len; i++)
		msg_mask[i] ^= msg[i];

	if (!EVP_DigestInit(hash, digest)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, x2y2, field_size)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, msg, msg_len)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, x2y2 + field_size, field_size)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestFinal(hash, C3, NULL)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	ctext_struct.C1x = x1;
	ctext_struct.C1y = y1;
	if ((ctext_struct.C3 = ASN1_OCTET_STRING_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((ctext_struct.C2 = ASN1_OCTET_STRING_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!ASN1_OCTET_STRING_set(ctext_struct.C3, C3, C3_size)) {
		SM2error(ERR_R_INTERNAL_ERROR);
		goto err;
	}
	if (!ASN1_OCTET_STRING_set(ctext_struct.C2, msg_mask, msg_len)) {
		SM2error(ERR_R_INTERNAL_ERROR);
		goto err;
	}

	if ((clen = i2d_SM2_Ciphertext(&ctext_struct, &ciphertext_buf)) < 0) {
		SM2error(ERR_R_INTERNAL_ERROR);
		goto err;
	}

	*ciphertext_len = clen;
	rc = 1;

 err:
	ASN1_OCTET_STRING_free(ctext_struct.C2);
	ASN1_OCTET_STRING_free(ctext_struct.C3);
	free(msg_mask);
	free(x2y2);
	free(C3);
	EVP_MD_CTX_free(hash);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	EC_POINT_free(kG);
	EC_POINT_free(kP);
	BN_free(order);
	return rc;
}

int
SM2_decrypt(const EC_KEY *key, const EVP_MD *digest, const uint8_t *ciphertext,
    size_t ciphertext_len, uint8_t *ptext_buf, size_t *ptext_len)
{
	SM2_Ciphertext *sm2_ctext = NULL;
	EVP_MD_CTX *hash = NULL;
	BN_CTX *ctx = NULL;
	BIGNUM *x2, *y2;
	const EC_GROUP *group;
	EC_POINT *C1 = NULL;
	const uint8_t *C2, *C3;
	uint8_t *computed_C3 = NULL, *msg_mask = NULL, *x2y2 = NULL;
	size_t field_size, x2size, y2size;
	int msg_len = 0, rc = 0;
	int hash_size, i;

	if ((group = EC_KEY_get0_group(key)) == NULL) {
		SM2error(SM2_R_INVALID_KEY);
		goto err;
	}

	if ((field_size = ec_field_size(group)) == 0) {
		SM2error(SM2_R_INVALID_FIELD);
		goto err;
	}

	if ((hash_size = EVP_MD_size(digest)) < 0) {
		SM2error(SM2_R_INVALID_DIGEST);
		goto err;
	}

	memset(ptext_buf, 0xFF, *ptext_len);

	if ((sm2_ctext = d2i_SM2_Ciphertext(NULL, &ciphertext,
	    ciphertext_len)) == NULL) {
		SM2error(SM2_R_ASN1_ERROR);
		goto err;
	}

	if (sm2_ctext->C3->length != hash_size) {
		SM2error(SM2_R_INVALID_ENCODING);
		goto err;
	}

	C2 = sm2_ctext->C2->data;
	C3 = sm2_ctext->C3->data;
	msg_len = sm2_ctext->C2->length;

	if ((ctx = BN_CTX_new()) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	BN_CTX_start(ctx);
	if ((x2 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}
	if ((y2 = BN_CTX_get(ctx)) == NULL) {
		SM2error(ERR_R_BN_LIB);
		goto err;
	}

	if ((msg_mask = calloc(1, msg_len)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((x2y2 = calloc(2, field_size)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((computed_C3 = calloc(1, hash_size)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((C1 = EC_POINT_new(group)) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!EC_POINT_set_affine_coordinates(group, C1, sm2_ctext->C1x,
	    sm2_ctext->C1y, ctx))
	{
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_mul(group, C1, NULL, C1, EC_KEY_get0_private_key(key),
	    ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if (!EC_POINT_get_affine_coordinates(group, C1, x2, y2, ctx)) {
		SM2error(ERR_R_EC_LIB);
		goto err;
	}

	if ((x2size = BN_num_bytes(x2)) > field_size ||
	    (y2size = BN_num_bytes(y2)) > field_size) {
		SM2error(SM2_R_BIGNUM_OUT_OF_RANGE);
		goto err;
	}

	BN_bn2bin(x2, x2y2 + field_size - x2size);
	BN_bn2bin(y2, x2y2 + 2 * field_size - y2size);

	if (!sm2_kdf(msg_mask, msg_len, x2y2, 2 * field_size, digest)) {
		SM2error(SM2_R_KDF_FAILURE);
		goto err;
	}

	for (i = 0; i != msg_len; ++i)
		ptext_buf[i] = C2[i] ^ msg_mask[i];

	if ((hash = EVP_MD_CTX_new()) == NULL) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestInit(hash, digest)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, x2y2, field_size)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, ptext_buf, msg_len)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestUpdate(hash, x2y2 + field_size, field_size)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (!EVP_DigestFinal(hash, computed_C3, NULL)) {
		SM2error(ERR_R_EVP_LIB);
		goto err;
	}

	if (memcmp(computed_C3, C3, hash_size) != 0)
		goto err;

	rc = 1;
	*ptext_len = msg_len;

 err:
	if (rc == 0)
		memset(ptext_buf, 0, *ptext_len);

	free(msg_mask);
	free(x2y2);
	free(computed_C3);
	EC_POINT_free(C1);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	SM2_Ciphertext_free(sm2_ctext);
	EVP_MD_CTX_free(hash);

	return rc;
}

#endif /* OPENSSL_NO_SM2 */
