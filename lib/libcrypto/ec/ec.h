/* $OpenBSD: ec.h,v 1.55 2025/03/10 08:38:11 tb Exp $ */
/*
 * Originally written by Bodo Moeller for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 1998-2005 The OpenSSL Project.  All rights reserved.
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
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * The elliptic curve binary polynomial software is originally written by
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems Laboratories.
 */

#ifndef HEADER_EC_H
#define HEADER_EC_H

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef OPENSSL_ECC_MAX_FIELD_BITS
#define OPENSSL_ECC_MAX_FIELD_BITS 661
#endif

/* Elliptic point conversion form as per X9.62, page 4 and section 4.4.2. */
typedef enum {
	POINT_CONVERSION_COMPRESSED = 2,
	POINT_CONVERSION_UNCOMPRESSED = 4,
	POINT_CONVERSION_HYBRID = 6
} point_conversion_form_t;

typedef struct ec_group_st EC_GROUP;
typedef struct ec_point_st EC_POINT;

void EC_GROUP_free(EC_GROUP *group);
void EC_GROUP_clear_free(EC_GROUP *group);

EC_GROUP *EC_GROUP_dup(const EC_GROUP *src);

int EC_GROUP_set_generator(EC_GROUP *group, const EC_POINT *generator,
    const BIGNUM *order, const BIGNUM *cofactor);
const EC_POINT *EC_GROUP_get0_generator(const EC_GROUP *group);

int EC_GROUP_get_order(const EC_GROUP *group, BIGNUM *order, BN_CTX *ctx);
int EC_GROUP_order_bits(const EC_GROUP *group);
int EC_GROUP_get_cofactor(const EC_GROUP *group, BIGNUM *cofactor, BN_CTX *ctx);

void EC_GROUP_set_curve_name(EC_GROUP *group, int nid);
int EC_GROUP_get_curve_name(const EC_GROUP *group);

void EC_GROUP_set_asn1_flag(EC_GROUP *group, int flag);
int EC_GROUP_get_asn1_flag(const EC_GROUP *group);

void EC_GROUP_set_point_conversion_form(EC_GROUP *group,
    point_conversion_form_t form);
point_conversion_form_t EC_GROUP_get_point_conversion_form(const EC_GROUP *);

unsigned char *EC_GROUP_get0_seed(const EC_GROUP *x);
size_t EC_GROUP_get_seed_len(const EC_GROUP *);
size_t EC_GROUP_set_seed(EC_GROUP *, const unsigned char *, size_t len);

int EC_GROUP_set_curve(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx);
int EC_GROUP_get_curve(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b,
    BN_CTX *ctx);

int EC_GROUP_set_curve_GFp(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx);
int EC_GROUP_get_curve_GFp(const EC_GROUP *group, BIGNUM *p, BIGNUM *a,
    BIGNUM *b, BN_CTX *ctx);

int EC_GROUP_get_degree(const EC_GROUP *group);

int EC_GROUP_check(const EC_GROUP *group, BN_CTX *ctx);
int EC_GROUP_check_discriminant(const EC_GROUP *group, BN_CTX *ctx);

/* Compare two EC_GROUPs. Returns 0 if both groups are equal, 1 otherwise. */
int EC_GROUP_cmp(const EC_GROUP *a, const EC_GROUP *b, BN_CTX *ctx);

EC_GROUP *EC_GROUP_new_curve_GFp(const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx);
EC_GROUP *EC_GROUP_new_by_curve_name(int nid);

typedef struct {
	int nid;
	const char *comment;
} EC_builtin_curve;

size_t EC_get_builtin_curves(EC_builtin_curve *r, size_t nitems);

const char *EC_curve_nid2nist(int nid);
int EC_curve_nist2nid(const char *name);

EC_POINT *EC_POINT_new(const EC_GROUP *group);
void EC_POINT_free(EC_POINT *point);
void EC_POINT_clear_free(EC_POINT *point);
int EC_POINT_copy(EC_POINT *dst, const EC_POINT *src);
EC_POINT *EC_POINT_dup(const EC_POINT *src, const EC_GROUP *group);

int EC_POINT_set_to_infinity(const EC_GROUP *group, EC_POINT *point);

int EC_POINT_set_affine_coordinates(const EC_GROUP *group, EC_POINT *p,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx);
int EC_POINT_get_affine_coordinates(const EC_GROUP *group, const EC_POINT *p,
    BIGNUM *x, BIGNUM *y, BN_CTX *ctx);
int EC_POINT_set_compressed_coordinates(const EC_GROUP *group, EC_POINT *p,
    const BIGNUM *x, int y_bit, BN_CTX *ctx);

int EC_POINT_set_affine_coordinates_GFp(const EC_GROUP *group, EC_POINT *p,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx);
int EC_POINT_get_affine_coordinates_GFp(const EC_GROUP *group,
    const EC_POINT *p, BIGNUM *x, BIGNUM *y, BN_CTX *ctx);
int EC_POINT_set_compressed_coordinates_GFp(const EC_GROUP *group, EC_POINT *p,
    const BIGNUM *x, int y_bit, BN_CTX *ctx);
size_t EC_POINT_point2oct(const EC_GROUP *group, const EC_POINT *p,
    point_conversion_form_t form, unsigned char *buf, size_t len, BN_CTX *ctx);
int EC_POINT_oct2point(const EC_GROUP *group, EC_POINT *p,
    const unsigned char *buf, size_t len, BN_CTX *ctx);

BIGNUM *EC_POINT_point2bn(const EC_GROUP *, const EC_POINT *,
    point_conversion_form_t form, BIGNUM *, BN_CTX *);
EC_POINT *EC_POINT_bn2point(const EC_GROUP *, const BIGNUM *, EC_POINT *,
    BN_CTX *);
char *EC_POINT_point2hex(const EC_GROUP *, const EC_POINT *,
    point_conversion_form_t form, BN_CTX *);
EC_POINT *EC_POINT_hex2point(const EC_GROUP *, const char *, EC_POINT *,
    BN_CTX *);

int EC_POINT_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, BN_CTX *ctx);
int EC_POINT_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    BN_CTX *ctx);
int EC_POINT_invert(const EC_GROUP *group, EC_POINT *a, BN_CTX *ctx);
int EC_POINT_is_at_infinity(const EC_GROUP *group, const EC_POINT *p);
int EC_POINT_is_on_curve(const EC_GROUP *group, const EC_POINT *point,
    BN_CTX *ctx);
int EC_POINT_cmp(const EC_GROUP *group, const EC_POINT *a, const EC_POINT *b,
    BN_CTX *ctx);

int EC_POINT_make_affine(const EC_GROUP *group, EC_POINT *point, BN_CTX *ctx);
int EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *n,
    const EC_POINT *q, const BIGNUM *m, BN_CTX *ctx);

int EC_GROUP_get_basis_type(const EC_GROUP *);

#define OPENSSL_EC_EXPLICIT_CURVE	0x000
#define OPENSSL_EC_NAMED_CURVE		0x001

EC_GROUP *d2i_ECPKParameters(EC_GROUP **, const unsigned char **in, long len);
int i2d_ECPKParameters(const EC_GROUP *, unsigned char **out);

#define d2i_ECPKParameters_bio(bp,x) ASN1_d2i_bio_of(EC_GROUP,NULL,d2i_ECPKParameters,bp,x)
#define i2d_ECPKParameters_bio(bp,x) ASN1_i2d_bio_of_const(EC_GROUP,i2d_ECPKParameters,bp,x)
#define d2i_ECPKParameters_fp(fp,x) (EC_GROUP *)ASN1_d2i_fp(NULL, \
                (char *(*)())d2i_ECPKParameters,(fp),(unsigned char **)(x))
#define i2d_ECPKParameters_fp(fp,x) ASN1_i2d_fp(i2d_ECPKParameters,(fp), \
		(unsigned char *)(x))

#ifndef OPENSSL_NO_BIO
int ECPKParameters_print(BIO *bp, const EC_GROUP *x, int off);
#endif
int ECPKParameters_print_fp(FILE *fp, const EC_GROUP *x, int off);

#define EC_PKEY_NO_PARAMETERS	0x001
#define EC_PKEY_NO_PUBKEY	0x002

#define EC_FLAG_NON_FIPS_ALLOW	0x1
#define EC_FLAG_FIPS_CHECKED	0x2
#define EC_FLAG_COFACTOR_ECDH	0x1000

EC_KEY *EC_KEY_new(void);
int EC_KEY_get_flags(const EC_KEY *key);
void EC_KEY_set_flags(EC_KEY *key, int flags);
void EC_KEY_clear_flags(EC_KEY *key, int flags);
EC_KEY *EC_KEY_new_by_curve_name(int nid);
void EC_KEY_free(EC_KEY *key);
EC_KEY *EC_KEY_copy(EC_KEY *dst, const EC_KEY *src);
EC_KEY *EC_KEY_dup(const EC_KEY *src);
int EC_KEY_up_ref(EC_KEY *key);

const EC_GROUP *EC_KEY_get0_group(const EC_KEY *key);
int EC_KEY_set_group(EC_KEY *key, const EC_GROUP *group);
const BIGNUM *EC_KEY_get0_private_key(const EC_KEY *key);
int EC_KEY_set_private_key(EC_KEY *key, const BIGNUM *prv);
const EC_POINT *EC_KEY_get0_public_key(const EC_KEY *key);
int EC_KEY_set_public_key(EC_KEY *key, const EC_POINT *pub);

unsigned EC_KEY_get_enc_flags(const EC_KEY *key);
void EC_KEY_set_enc_flags(EC_KEY *eckey, unsigned int flags);
point_conversion_form_t EC_KEY_get_conv_form(const EC_KEY *key);
void EC_KEY_set_conv_form(EC_KEY *eckey, point_conversion_form_t cform);

void EC_KEY_set_asn1_flag(EC_KEY *eckey, int asn1_flag);
int EC_KEY_precompute_mult(EC_KEY *key, BN_CTX *ctx);
int EC_KEY_generate_key(EC_KEY *key);
int EC_KEY_check_key(const EC_KEY *key);
int EC_KEY_set_public_key_affine_coordinates(EC_KEY *key, BIGNUM *x, BIGNUM *y);

EC_KEY *d2i_ECPrivateKey(EC_KEY **key, const unsigned char **in, long len);
int i2d_ECPrivateKey(EC_KEY *key, unsigned char **out);
EC_KEY *d2i_ECParameters(EC_KEY **key, const unsigned char **in, long len);
int i2d_ECParameters(EC_KEY *key, unsigned char **out);

EC_KEY *o2i_ECPublicKey(EC_KEY **key, const unsigned char **in, long len);
int i2o_ECPublicKey(const EC_KEY *key, unsigned char **out);

#ifndef OPENSSL_NO_BIO
int ECParameters_print(BIO *bp, const EC_KEY *key);
int EC_KEY_print(BIO *bp, const EC_KEY *key, int off);
#endif
int ECParameters_print_fp(FILE *fp, const EC_KEY *key);
int EC_KEY_print_fp(FILE *fp, const EC_KEY *key, int off);

#define EC_KEY_get_ex_new_index(l, p, newf, dupf, freef) \
    CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_EC_KEY, l, p, newf, dupf, freef)
int EC_KEY_set_ex_data(EC_KEY *key, int idx, void *arg);
void *EC_KEY_get_ex_data(const EC_KEY *key, int idx);

const EC_KEY_METHOD *EC_KEY_OpenSSL(void);
const EC_KEY_METHOD *EC_KEY_get_default_method(void);
void EC_KEY_set_default_method(const EC_KEY_METHOD *meth);
const EC_KEY_METHOD *EC_KEY_get_method(const EC_KEY *key);
int EC_KEY_set_method(EC_KEY *key, const EC_KEY_METHOD *meth);
EC_KEY *EC_KEY_new_method(ENGINE *engine);

int ECDH_size(const EC_KEY *ecdh);
int ECDH_compute_key(void *out, size_t outlen, const EC_POINT *pub_key,
    EC_KEY *ecdh,
    void *(*KDF)(const void *in, size_t inlen, void *out, size_t *outlen));

typedef struct ECDSA_SIG_st ECDSA_SIG;

ECDSA_SIG *ECDSA_SIG_new(void);
void ECDSA_SIG_free(ECDSA_SIG *sig);
int i2d_ECDSA_SIG(const ECDSA_SIG *sig, unsigned char **pp);
ECDSA_SIG *d2i_ECDSA_SIG(ECDSA_SIG **sig, const unsigned char **pp, long len);

const BIGNUM *ECDSA_SIG_get0_r(const ECDSA_SIG *sig);
const BIGNUM *ECDSA_SIG_get0_s(const ECDSA_SIG *sig);
void ECDSA_SIG_get0(const ECDSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps);
int ECDSA_SIG_set0(ECDSA_SIG *sig, BIGNUM *r, BIGNUM *s);

int ECDSA_size(const EC_KEY *eckey);

ECDSA_SIG *ECDSA_do_sign(const unsigned char *digest, int digest_len,
    EC_KEY *eckey);
int ECDSA_do_verify(const unsigned char *digest, int digest_len,
    const ECDSA_SIG *sig, EC_KEY *eckey);

int ECDSA_sign(int type, const unsigned char *digest, int digest_len,
    unsigned char *signature, unsigned int *signature_len, EC_KEY *eckey);
int ECDSA_verify(int type, const unsigned char *digest, int digest_len,
    const unsigned char *signature, int signature_len, EC_KEY *eckey);

EC_KEY_METHOD *EC_KEY_METHOD_new(const EC_KEY_METHOD *meth);
void EC_KEY_METHOD_free(EC_KEY_METHOD *meth);
void EC_KEY_METHOD_set_init(EC_KEY_METHOD *meth,
    int (*init)(EC_KEY *key),
    void (*finish)(EC_KEY *key),
    int (*copy)(EC_KEY *dest, const EC_KEY *src),
    int (*set_group)(EC_KEY *key, const EC_GROUP *grp),
    int (*set_private)(EC_KEY *key, const BIGNUM *priv_key),
    int (*set_public)(EC_KEY *key, const EC_POINT *pub_key));
void EC_KEY_METHOD_set_keygen(EC_KEY_METHOD *meth,
    int (*keygen)(EC_KEY *key));
void EC_KEY_METHOD_set_compute_key(EC_KEY_METHOD *meth,
    int (*ckey)(unsigned char **out, size_t *out_len, const EC_POINT *pub_key,
        const EC_KEY *ecdh));
void EC_KEY_METHOD_set_sign(EC_KEY_METHOD *meth,
    int (*sign)(int type, const unsigned char *digest, int digest_len,
	unsigned char *signature, unsigned int *signature_len,
	const BIGNUM *kinv, const BIGNUM *r, EC_KEY *eckey),
    int (*sign_setup)(EC_KEY *eckey, BN_CTX *ctx_in, BIGNUM **kinvp, BIGNUM **rp),
    ECDSA_SIG *(*sign_sig)(const unsigned char *digest, int digest_len,
        const BIGNUM *in_kinv, const BIGNUM *in_r, EC_KEY *eckey));
void EC_KEY_METHOD_set_verify(EC_KEY_METHOD *meth,
    int (*verify)(int type, const unsigned char *digest, int digest_len,
	const unsigned char *signature, int signature_len, EC_KEY *eckey),
    int (*verify_sig)(const unsigned char *digest, int digest_len,
	const ECDSA_SIG *sig, EC_KEY *eckey));
void EC_KEY_METHOD_get_init(const EC_KEY_METHOD *meth,
    int (**pinit)(EC_KEY *key),
    void (**pfinish)(EC_KEY *key),
    int (**pcopy)(EC_KEY *dest, const EC_KEY *src),
    int (**pset_group)(EC_KEY *key, const EC_GROUP *grp),
    int (**pset_private)(EC_KEY *key, const BIGNUM *priv_key),
    int (**pset_public)(EC_KEY *key, const EC_POINT *pub_key));
void EC_KEY_METHOD_get_keygen(const EC_KEY_METHOD *meth,
    int (**pkeygen)(EC_KEY *key));
void EC_KEY_METHOD_get_compute_key(const EC_KEY_METHOD *meth,
    int (**pck)(unsigned char **out, size_t *out_len, const EC_POINT *pub_key,
        const EC_KEY *ecdh));
void EC_KEY_METHOD_get_sign(const EC_KEY_METHOD *meth,
    int (**psign)(int type, const unsigned char *digest, int digest_len,
        unsigned char *signature, unsigned int *signature_len,
	const BIGNUM *kinv, const BIGNUM *r, EC_KEY *eckey),
    int (**psign_setup)(EC_KEY *eckey, BN_CTX *ctx_in, BIGNUM **kinvp, BIGNUM **rp),
    ECDSA_SIG *(**psign_sig)(const unsigned char *digest, int digest_len,
        const BIGNUM *in_kinv, const BIGNUM *in_r, EC_KEY *eckey));
void EC_KEY_METHOD_get_verify(const EC_KEY_METHOD *meth,
    int (**pverify)(int type, const unsigned char *digest, int digest_len,
	const unsigned char *signature, int signature_len, EC_KEY *eckey),
    int (**pverify_sig)(const unsigned char *digest, int digest_len,
	const ECDSA_SIG *sig, EC_KEY *eckey));

EC_KEY *ECParameters_dup(EC_KEY *key);

#define EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_PARAMGEN|EVP_PKEY_OP_KEYGEN, \
	    EVP_PKEY_CTRL_EC_PARAMGEN_CURVE_NID, nid, NULL)

#define EVP_PKEY_CTX_set_ec_param_enc(ctx, flag) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_PARAMGEN|EVP_PKEY_OP_KEYGEN, \
	    EVP_PKEY_CTRL_EC_PARAM_ENC, flag, NULL)

#define EVP_PKEY_CTX_set_ecdh_cofactor_mode(ctx, flag) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_ECDH_COFACTOR, flag, NULL)

#define EVP_PKEY_CTX_get_ecdh_cofactor_mode(ctx) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_ECDH_COFACTOR, -2, NULL)

#define EVP_PKEY_CTX_set_ecdh_kdf_type(ctx, kdf) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_KDF_TYPE, kdf, NULL)

#define EVP_PKEY_CTX_get_ecdh_kdf_type(ctx) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_KDF_TYPE, -2, NULL)

#define EVP_PKEY_CTX_set_ecdh_kdf_md(ctx, md) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_KDF_MD, 0, (void *)(md))

#define EVP_PKEY_CTX_get_ecdh_kdf_md(ctx, pmd) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_GET_EC_KDF_MD, 0, (void *)(pmd))

#define EVP_PKEY_CTX_set_ecdh_kdf_outlen(ctx, len) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_KDF_OUTLEN, len, NULL)

#define EVP_PKEY_CTX_get_ecdh_kdf_outlen(ctx, plen) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_GET_EC_KDF_OUTLEN, 0, \
	    (void *)(plen))

#define EVP_PKEY_CTX_set0_ecdh_kdf_ukm(ctx, p, plen) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_EC_KDF_UKM, plen, (void *)(p))

#define EVP_PKEY_CTX_get0_ecdh_kdf_ukm(ctx, p) \
	EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_EC, \
	    EVP_PKEY_OP_DERIVE, \
	    EVP_PKEY_CTRL_GET_EC_KDF_UKM, 0, (void *)(p))

/* SM2 will skip the operation check so no need to pass operation here */
#define EVP_PKEY_CTX_set1_id(ctx, id, id_len) \
	EVP_PKEY_CTX_ctrl(ctx, -1, -1, \
	    EVP_PKEY_CTRL_SET1_ID, (int)id_len, (void*)(id))

#define EVP_PKEY_CTX_get1_id(ctx, id) \
	EVP_PKEY_CTX_ctrl(ctx, -1, -1, \
	    EVP_PKEY_CTRL_GET1_ID, 0, (void*)(id))

#define EVP_PKEY_CTX_get1_id_len(ctx, id_len) \
	EVP_PKEY_CTX_ctrl(ctx, -1, -1, \
	    EVP_PKEY_CTRL_GET1_ID_LEN, 0, (void*)(id_len))

#define EVP_PKEY_CTRL_EC_PARAMGEN_CURVE_NID		(EVP_PKEY_ALG_CTRL + 1)
#define EVP_PKEY_CTRL_EC_PARAM_ENC			(EVP_PKEY_ALG_CTRL + 2)
#define EVP_PKEY_CTRL_EC_ECDH_COFACTOR			(EVP_PKEY_ALG_CTRL + 3)
#define EVP_PKEY_CTRL_EC_KDF_TYPE			(EVP_PKEY_ALG_CTRL + 4)
#define EVP_PKEY_CTRL_EC_KDF_MD				(EVP_PKEY_ALG_CTRL + 5)
#define EVP_PKEY_CTRL_GET_EC_KDF_MD			(EVP_PKEY_ALG_CTRL + 6)
#define EVP_PKEY_CTRL_EC_KDF_OUTLEN			(EVP_PKEY_ALG_CTRL + 7)
#define EVP_PKEY_CTRL_GET_EC_KDF_OUTLEN			(EVP_PKEY_ALG_CTRL + 8)
#define EVP_PKEY_CTRL_EC_KDF_UKM			(EVP_PKEY_ALG_CTRL + 9)
#define EVP_PKEY_CTRL_GET_EC_KDF_UKM			(EVP_PKEY_ALG_CTRL + 10)
#define EVP_PKEY_CTRL_SET1_ID				(EVP_PKEY_ALG_CTRL + 11)
#define EVP_PKEY_CTRL_GET1_ID				(EVP_PKEY_ALG_CTRL + 12)
#define EVP_PKEY_CTRL_GET1_ID_LEN			(EVP_PKEY_ALG_CTRL + 13)

/* KDF types */
#define EVP_PKEY_ECDH_KDF_NONE				1
#define EVP_PKEY_ECDH_KDF_X9_63				2

void ERR_load_EC_strings(void);

/* Error codes for the EC functions. */

/* Function codes. */
#define EC_F_BN_TO_FELEM				 224
#define EC_F_COMPUTE_WNAF				 143
#define EC_F_D2I_ECPARAMETERS				 144
#define EC_F_D2I_ECPKPARAMETERS				 145
#define EC_F_D2I_ECPRIVATEKEY				 146
#define EC_F_DO_EC_KEY_PRINT				 221
#define EC_F_ECKEY_PARAM2TYPE				 223
#define EC_F_ECKEY_PARAM_DECODE				 212
#define EC_F_ECKEY_PRIV_DECODE				 213
#define EC_F_ECKEY_PRIV_ENCODE				 214
#define EC_F_ECKEY_PUB_DECODE				 215
#define EC_F_ECKEY_PUB_ENCODE				 216
#define EC_F_ECKEY_TYPE2PARAM				 220
#define EC_F_ECPARAMETERS_PRINT				 147
#define EC_F_ECPARAMETERS_PRINT_FP			 148
#define EC_F_ECPKPARAMETERS_PRINT			 149
#define EC_F_ECPKPARAMETERS_PRINT_FP			 150
#define EC_F_ECP_NIST_MOD_192				 203
#define EC_F_ECP_NIST_MOD_224				 204
#define EC_F_ECP_NIST_MOD_256				 205
#define EC_F_ECP_NIST_MOD_521				 206
#define EC_F_ECP_NISTZ256_GET_AFFINE			 240
#define EC_F_ECP_NISTZ256_MULT_PRECOMPUTE		 243
#define EC_F_ECP_NISTZ256_POINTS_MUL			 241
#define EC_F_ECP_NISTZ256_PRE_COMP_NEW			 244
#define EC_F_ECP_NISTZ256_SET_WORDS			 245
#define EC_F_ECP_NISTZ256_WINDOWED_MUL			 242
#define EC_F_EC_ASN1_GROUP2CURVE			 153
#define EC_F_EC_ASN1_GROUP2FIELDID			 154
#define EC_F_EC_ASN1_GROUP2PARAMETERS			 155
#define EC_F_EC_ASN1_GROUP2PKPARAMETERS			 156
#define EC_F_EC_ASN1_PARAMETERS2GROUP			 157
#define EC_F_EC_ASN1_PKPARAMETERS2GROUP			 158
#define EC_F_EC_EX_DATA_SET_DATA			 211
#define EC_F_EC_GF2M_MONTGOMERY_POINT_MULTIPLY		 208
#define EC_F_EC_GF2M_SIMPLE_GROUP_CHECK_DISCRIMINANT	 159
#define EC_F_EC_GF2M_SIMPLE_GROUP_SET_CURVE		 195
#define EC_F_EC_GF2M_SIMPLE_OCT2POINT			 160
#define EC_F_EC_GF2M_SIMPLE_POINT2OCT			 161
#define EC_F_EC_GF2M_SIMPLE_POINT_GET_AFFINE_COORDINATES 162
#define EC_F_EC_GF2M_SIMPLE_POINT_SET_AFFINE_COORDINATES 163
#define EC_F_EC_GF2M_SIMPLE_SET_COMPRESSED_COORDINATES	 164
#define EC_F_EC_GFP_MONT_FIELD_DECODE			 133
#define EC_F_EC_GFP_MONT_FIELD_ENCODE			 134
#define EC_F_EC_GFP_MONT_FIELD_MUL			 131
#define EC_F_EC_GFP_MONT_FIELD_SET_TO_ONE		 209
#define EC_F_EC_GFP_MONT_FIELD_SQR			 132
#define EC_F_EC_GFP_MONT_GROUP_SET_CURVE		 189
#define EC_F_EC_GFP_MONT_GROUP_SET_CURVE_GFP		 135
#define EC_F_EC_GFP_NISTP224_GROUP_SET_CURVE		 225
#define EC_F_EC_GFP_NISTP224_POINTS_MUL			 228
#define EC_F_EC_GFP_NISTP224_POINT_GET_AFFINE_COORDINATES 226
#define EC_F_EC_GFP_NISTP256_GROUP_SET_CURVE		 230
#define EC_F_EC_GFP_NISTP256_POINTS_MUL			 231
#define EC_F_EC_GFP_NISTP256_POINT_GET_AFFINE_COORDINATES 232
#define EC_F_EC_GFP_NISTP521_GROUP_SET_CURVE		 233
#define EC_F_EC_GFP_NISTP521_POINTS_MUL			 234
#define EC_F_EC_GFP_NISTP521_POINT_GET_AFFINE_COORDINATES 235
#define EC_F_EC_GFP_NIST_FIELD_MUL			 200
#define EC_F_EC_GFP_NIST_FIELD_SQR			 201
#define EC_F_EC_GFP_NIST_GROUP_SET_CURVE		 202
#define EC_F_EC_GFP_SIMPLE_GROUP_CHECK_DISCRIMINANT	 165
#define EC_F_EC_GFP_SIMPLE_GROUP_SET_CURVE		 166
#define EC_F_EC_GFP_SIMPLE_GROUP_SET_CURVE_GFP		 100
#define EC_F_EC_GFP_SIMPLE_GROUP_SET_GENERATOR		 101
#define EC_F_EC_GFP_SIMPLE_MAKE_AFFINE			 102
#define EC_F_EC_GFP_SIMPLE_OCT2POINT			 103
#define EC_F_EC_GFP_SIMPLE_POINT2OCT			 104
#define EC_F_EC_GFP_SIMPLE_POINTS_MAKE_AFFINE		 137
#define EC_F_EC_GFP_SIMPLE_POINT_GET_AFFINE_COORDINATES	 167
#define EC_F_EC_GFP_SIMPLE_POINT_GET_AFFINE_COORDINATES_GFP 105
#define EC_F_EC_GFP_SIMPLE_POINT_SET_AFFINE_COORDINATES	 168
#define EC_F_EC_GFP_SIMPLE_POINT_SET_AFFINE_COORDINATES_GFP 128
#define EC_F_EC_GFP_SIMPLE_SET_COMPRESSED_COORDINATES	 169
#define EC_F_EC_GFP_SIMPLE_SET_COMPRESSED_COORDINATES_GFP 129
#define EC_F_EC_GROUP_CHECK				 170
#define EC_F_EC_GROUP_CHECK_DISCRIMINANT		 171
#define EC_F_EC_GROUP_COPY				 106
#define EC_F_EC_GROUP_GET0_GENERATOR			 139
#define EC_F_EC_GROUP_GET_COFACTOR			 140
#define EC_F_EC_GROUP_GET_CURVE_GF2M			 172
#define EC_F_EC_GROUP_GET_CURVE_GFP			 130
#define EC_F_EC_GROUP_GET_DEGREE			 173
#define EC_F_EC_GROUP_GET_ORDER				 141
#define EC_F_EC_GROUP_GET_PENTANOMIAL_BASIS		 193
#define EC_F_EC_GROUP_GET_TRINOMIAL_BASIS		 194
#define EC_F_EC_GROUP_NEW				 108
#define EC_F_EC_GROUP_NEW_BY_CURVE_NAME			 174
#define EC_F_EC_GROUP_NEW_FROM_DATA			 175
#define EC_F_EC_GROUP_PRECOMPUTE_MULT			 142
#define EC_F_EC_GROUP_SET_CURVE_GF2M			 176
#define EC_F_EC_GROUP_SET_CURVE_GFP			 109
#define EC_F_EC_GROUP_SET_EXTRA_DATA			 110
#define EC_F_EC_GROUP_SET_GENERATOR			 111
#define EC_F_EC_KEY_CHECK_KEY				 177
#define EC_F_EC_KEY_COPY				 178
#define EC_F_EC_KEY_GENERATE_KEY			 179
#define EC_F_EC_KEY_NEW					 182
#define EC_F_EC_KEY_PRINT				 180
#define EC_F_EC_KEY_PRINT_FP				 181
#define EC_F_EC_KEY_SET_PUBLIC_KEY_AFFINE_COORDINATES	 229
#define EC_F_EC_POINTS_MAKE_AFFINE			 136
#define EC_F_EC_POINT_ADD				 112
#define EC_F_EC_POINT_CMP				 113
#define EC_F_EC_POINT_COPY				 114
#define EC_F_EC_POINT_DBL				 115
#define EC_F_EC_POINT_GET_AFFINE_COORDINATES_GF2M	 183
#define EC_F_EC_POINT_GET_AFFINE_COORDINATES_GFP	 116
#define EC_F_EC_POINT_GET_JPROJECTIVE_COORDINATES_GFP	 117
#define EC_F_EC_POINT_INVERT				 210
#define EC_F_EC_POINT_IS_AT_INFINITY			 118
#define EC_F_EC_POINT_IS_ON_CURVE			 119
#define EC_F_EC_POINT_MAKE_AFFINE			 120
#define EC_F_EC_POINT_MUL				 184
#define EC_F_EC_POINT_NEW				 121
#define EC_F_EC_POINT_OCT2POINT				 122
#define EC_F_EC_POINT_POINT2OCT				 123
#define EC_F_EC_POINT_SET_AFFINE_COORDINATES_GF2M	 185
#define EC_F_EC_POINT_SET_AFFINE_COORDINATES_GFP	 124
#define EC_F_EC_POINT_SET_COMPRESSED_COORDINATES_GF2M	 186
#define EC_F_EC_POINT_SET_COMPRESSED_COORDINATES_GFP	 125
#define EC_F_EC_POINT_SET_JPROJECTIVE_COORDINATES_GFP	 126
#define EC_F_EC_POINT_SET_TO_INFINITY			 127
#define EC_F_EC_PRE_COMP_DUP				 207
#define EC_F_EC_PRE_COMP_NEW				 196
#define EC_F_EC_WNAF_MUL				 187
#define EC_F_EC_WNAF_PRECOMPUTE_MULT			 188
#define EC_F_I2D_ECPARAMETERS				 190
#define EC_F_I2D_ECPKPARAMETERS				 191
#define EC_F_I2D_ECPRIVATEKEY				 192
#define EC_F_I2O_ECPUBLICKEY				 151
#define EC_F_NISTP224_PRE_COMP_NEW			 227
#define EC_F_NISTP256_PRE_COMP_NEW			 236
#define EC_F_NISTP521_PRE_COMP_NEW			 237
#define EC_F_O2I_ECPUBLICKEY				 152
#define EC_F_OLD_EC_PRIV_DECODE				 222
#define EC_F_PKEY_EC_CTRL				 197
#define EC_F_PKEY_EC_CTRL_STR				 198
#define EC_F_PKEY_EC_DERIVE				 217
#define EC_F_PKEY_EC_KEYGEN				 199
#define EC_F_PKEY_EC_PARAMGEN				 219
#define EC_F_PKEY_EC_SIGN				 218

/* Reason codes. */
#define EC_R_ASN1_ERROR					 115
#define EC_R_ASN1_UNKNOWN_FIELD				 116
#define EC_R_BAD_SIGNATURE				 166
#define EC_R_BIGNUM_OUT_OF_RANGE			 144
#define EC_R_BUFFER_TOO_SMALL				 100
#define EC_R_COORDINATES_OUT_OF_RANGE			 146
#define EC_R_D2I_ECPKPARAMETERS_FAILURE			 117
#define EC_R_DECODE_ERROR				 142
#define EC_R_DISCRIMINANT_IS_ZERO			 118
#define EC_R_EC_GROUP_NEW_BY_NAME_FAILURE		 119
#define EC_R_FIELD_TOO_LARGE				 143
#define EC_R_GF2M_NOT_SUPPORTED				 147
#define EC_R_GROUP2PKPARAMETERS_FAILURE			 120
#define EC_R_I2D_ECPKPARAMETERS_FAILURE			 121
#define EC_R_INCOMPATIBLE_OBJECTS			 101
#define EC_R_INVALID_ARGUMENT				 112
#define EC_R_INVALID_COMPRESSED_POINT			 110
#define EC_R_INVALID_COMPRESSION_BIT			 109
#define EC_R_INVALID_CURVE				 141
#define EC_R_INVALID_DIGEST				 151
#define EC_R_INVALID_DIGEST_TYPE			 138
#define EC_R_INVALID_ENCODING				 102
#define EC_R_INVALID_FIELD				 103
#define EC_R_INVALID_FORM				 104
#define EC_R_INVALID_GROUP_ORDER			 122
#define EC_R_INVALID_KEY				 165
#define EC_R_INVALID_OUTPUT_LENGTH			 171
#define EC_R_INVALID_PEER_KEY				 152
#define EC_R_INVALID_PENTANOMIAL_BASIS			 132
#define EC_R_INVALID_PRIVATE_KEY			 123
#define EC_R_INVALID_TRINOMIAL_BASIS			 137
#define EC_R_KDF_FAILED					 167
#define EC_R_KDF_PARAMETER_ERROR			 148
#define EC_R_KEY_TRUNCATION				 168
#define EC_R_KEYS_NOT_SET				 140
#define EC_R_MISSING_PARAMETERS				 124
#define EC_R_MISSING_PRIVATE_KEY			 125
#define EC_R_NEED_NEW_SETUP_VALUES			 170
#define EC_R_NOT_A_NIST_PRIME				 135
#define EC_R_NOT_A_SUPPORTED_NIST_PRIME			 136
#define EC_R_NOT_IMPLEMENTED				 126
#define EC_R_NOT_INITIALIZED				 111
#define EC_R_NO_FIELD_MOD				 133
#define EC_R_NO_PARAMETERS_SET				 139
#define EC_R_PASSED_NULL_PARAMETER			 134
#define EC_R_PEER_KEY_ERROR				 149
#define EC_R_PKPARAMETERS2GROUP_FAILURE			 127
#define EC_R_POINT_AT_INFINITY				 106
#define EC_R_POINT_ARITHMETIC_FAILURE			 169
#define EC_R_POINT_IS_NOT_ON_CURVE			 107
#define EC_R_SHARED_INFO_ERROR				 150
#define EC_R_SLOT_FULL					 108
#define EC_R_UNDEFINED_GENERATOR			 113
#define EC_R_UNDEFINED_ORDER				 128
#define EC_R_UNKNOWN_COFACTOR				 164
#define EC_R_UNKNOWN_GROUP				 129
#define EC_R_UNKNOWN_ORDER				 114
#define EC_R_UNSUPPORTED_FIELD				 131
#define EC_R_WRONG_CURVE_PARAMETERS			 145
#define EC_R_WRONG_ORDER				 130

#ifdef  __cplusplus
}
#endif
#endif
