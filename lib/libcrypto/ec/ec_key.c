/* $OpenBSD: ec_key.c,v 1.52 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Nils Larsch for the OpenSSL project.
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
 * Portions originally developed by SUN MICROSYSTEMS, INC., and
 * contributed to the OpenSSL project.
 */

#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/ec.h>

#include "bn_local.h"
#include "ec_local.h"
#include "ecdsa_local.h"
#include "err_local.h"

EC_KEY *
EC_KEY_new(void)
{
	return EC_KEY_new_method(NULL);
}
LCRYPTO_ALIAS(EC_KEY_new);

EC_KEY *
EC_KEY_new_by_curve_name(int nid)
{
	EC_KEY *ec_key;

	if ((ec_key = EC_KEY_new()) == NULL)
		goto err;

	if ((ec_key->group = EC_GROUP_new_by_curve_name(nid)) == NULL)
		goto err;

	/* XXX - do we want an ec_key_set0_group()? */
	if (ec_key->meth->set_group != NULL) {
		if (!ec_key->meth->set_group(ec_key, ec_key->group))
			goto err;
	}

	return ec_key;

 err:
	EC_KEY_free(ec_key);

	return NULL;
}
LCRYPTO_ALIAS(EC_KEY_new_by_curve_name);

void
EC_KEY_free(EC_KEY *ec_key)
{
	if (ec_key == NULL)
		return;

	if (CRYPTO_add(&ec_key->references, -1, CRYPTO_LOCK_EC) > 0)
		return;

	if (ec_key->meth != NULL && ec_key->meth->finish != NULL)
		ec_key->meth->finish(ec_key);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_EC_KEY, ec_key, &ec_key->ex_data);

	EC_GROUP_free(ec_key->group);
	EC_POINT_free(ec_key->pub_key);
	BN_free(ec_key->priv_key);

	freezero(ec_key, sizeof(*ec_key));
}
LCRYPTO_ALIAS(EC_KEY_free);

EC_KEY *
EC_KEY_copy(EC_KEY *dest, const EC_KEY *src)
{
	if (dest == NULL || src == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return NULL;
	}

	if (src->meth != dest->meth) {
		if (dest->meth != NULL && dest->meth->finish != NULL)
			dest->meth->finish(dest);
	}

	if (src->group != NULL) {
		EC_GROUP_free(dest->group);
		if ((dest->group = EC_GROUP_dup(src->group)) == NULL)
			return NULL;
		if (src->pub_key != NULL) {
			EC_POINT_free(dest->pub_key);
			if ((dest->pub_key = EC_POINT_dup(src->pub_key,
			    src->group)) == NULL)
				return NULL;
		}
	}

	BN_free(dest->priv_key);
	dest->priv_key = NULL;
	if (src->priv_key != NULL) {
		if ((dest->priv_key = BN_dup(src->priv_key)) == NULL)
			return NULL;
	}

	dest->enc_flag = src->enc_flag;
	dest->conv_form = src->conv_form;
	dest->version = src->version;
	dest->flags = src->flags;

	/*
	 * The fun part about being a toolkit implementer is that the rest of
	 * the world gets to live with your terrible API design choices for
	 * eternity. (To be fair: the signature was changed in OpenSSL 3).
	 */
	if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_EC_KEY, &dest->ex_data,
	    &((EC_KEY *)src)->ex_data))	/* XXX const */
		return NULL;

	dest->meth = src->meth;

	if (src->meth != NULL && src->meth->copy != NULL) {
		if (!src->meth->copy(dest, src))
			return NULL;
	}

	return dest;
}
LCRYPTO_ALIAS(EC_KEY_copy);

EC_KEY *
EC_KEY_dup(const EC_KEY *in_ec_key)
{
	EC_KEY *ec_key;

	/* XXX - Pass NULL - so we're perhaps not running the right init()? */
	if ((ec_key = EC_KEY_new_method(NULL)) == NULL)
		goto err;
	if (EC_KEY_copy(ec_key, in_ec_key) == NULL)
		goto err;

	return ec_key;

 err:
	EC_KEY_free(ec_key);

	return NULL;
}
LCRYPTO_ALIAS(EC_KEY_dup);

int
EC_KEY_up_ref(EC_KEY *r)
{
	return CRYPTO_add(&r->references, 1, CRYPTO_LOCK_EC) > 1;
}
LCRYPTO_ALIAS(EC_KEY_up_ref);

int
EC_KEY_set_ex_data(EC_KEY *r, int idx, void *arg)
{
	return CRYPTO_set_ex_data(&r->ex_data, idx, arg);
}
LCRYPTO_ALIAS(EC_KEY_set_ex_data);

void *
EC_KEY_get_ex_data(const EC_KEY *r, int idx)
{
	return CRYPTO_get_ex_data(&r->ex_data, idx);
}
LCRYPTO_ALIAS(EC_KEY_get_ex_data);

int
EC_KEY_generate_key(EC_KEY *eckey)
{
	if (eckey->meth->keygen != NULL)
		return eckey->meth->keygen(eckey);
	ECerror(EC_R_NOT_IMPLEMENTED);
	return 0;
}
LCRYPTO_ALIAS(EC_KEY_generate_key);

static int
ec_key_gen(EC_KEY *eckey)
{
	BIGNUM *priv_key = NULL;
	EC_POINT *pub_key = NULL;
	const BIGNUM *order;
	int ret = 0;

	if (eckey == NULL || eckey->group == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if ((priv_key = BN_new()) == NULL)
		goto err;
	if ((pub_key = EC_POINT_new(eckey->group)) == NULL)
		goto err;

	if ((order = EC_GROUP_get0_order(eckey->group)) == NULL)
		goto err;
	if (!bn_rand_interval(priv_key, 1, order))
		goto err;
	if (!EC_POINT_mul(eckey->group, pub_key, priv_key, NULL, NULL, NULL))
		goto err;

	BN_free(eckey->priv_key);
	eckey->priv_key = priv_key;
	priv_key = NULL;

	EC_POINT_free(eckey->pub_key);
	eckey->pub_key = pub_key;
	pub_key = NULL;

	ret = 1;

 err:
	EC_POINT_free(pub_key);
	BN_free(priv_key);

	return ret;
}

int
EC_KEY_check_key(const EC_KEY *eckey)
{
	BN_CTX *ctx = NULL;
	EC_POINT *point = NULL;
	const BIGNUM *order;
	int ret = 0;

	if (eckey == NULL || eckey->group == NULL || eckey->pub_key == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if (EC_POINT_is_at_infinity(eckey->group, eckey->pub_key)) {
		ECerror(EC_R_POINT_AT_INFINITY);
		goto err;
	}

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	if ((point = EC_POINT_new(eckey->group)) == NULL)
		goto err;

	/* Ensure public key is on the elliptic curve. */
	if (EC_POINT_is_on_curve(eckey->group, eckey->pub_key, ctx) <= 0) {
		ECerror(EC_R_POINT_IS_NOT_ON_CURVE);
		goto err;
	}

	/* Ensure public key multiplied by the order is the point at infinity. */
	if ((order = EC_GROUP_get0_order(eckey->group)) == NULL) {
		ECerror(EC_R_INVALID_GROUP_ORDER);
		goto err;
	}
	if (!EC_POINT_mul(eckey->group, point, NULL, eckey->pub_key, order, ctx)) {
		ECerror(ERR_R_EC_LIB);
		goto err;
	}
	if (!EC_POINT_is_at_infinity(eckey->group, point)) {
		ECerror(EC_R_WRONG_ORDER);
		goto err;
	}

	/*
	 * If the private key is present, ensure that the private key multiplied
	 * by the generator matches the public key.
	 */
	if (eckey->priv_key != NULL) {
		if (BN_cmp(eckey->priv_key, order) >= 0) {
			ECerror(EC_R_WRONG_ORDER);
			goto err;
		}
		if (!EC_POINT_mul(eckey->group, point, eckey->priv_key, NULL,
		    NULL, ctx)) {
			ECerror(ERR_R_EC_LIB);
			goto err;
		}
		if (EC_POINT_cmp(eckey->group, point, eckey->pub_key,
		    ctx) != 0) {
			ECerror(EC_R_INVALID_PRIVATE_KEY);
			goto err;
		}
	}

	ret = 1;

 err:
	BN_CTX_free(ctx);
	EC_POINT_free(point);

	return ret;
}
LCRYPTO_ALIAS(EC_KEY_check_key);

int
EC_KEY_set_public_key_affine_coordinates(EC_KEY *key, BIGNUM *x, BIGNUM *y)
{
	BN_CTX *ctx = NULL;
	EC_POINT *point = NULL;
	BIGNUM *tx, *ty;
	int ret = 0;

	if (key == NULL || key->group == NULL || x == NULL || y == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((tx = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((ty = BN_CTX_get(ctx)) == NULL)
		goto err;

	if ((point = EC_POINT_new(key->group)) == NULL)
		goto err;

	if (!EC_POINT_set_affine_coordinates(key->group, point, x, y, ctx))
		goto err;
	if (!EC_POINT_get_affine_coordinates(key->group, point, tx, ty, ctx))
		goto err;

	/*
	 * Check if retrieved coordinates match originals: if not values are
	 * out of range.
	 */
	if (BN_cmp(x, tx) != 0 || BN_cmp(y, ty) != 0) {
		ECerror(EC_R_COORDINATES_OUT_OF_RANGE);
		goto err;
	}
	if (!EC_KEY_set_public_key(key, point))
		goto err;
	if (EC_KEY_check_key(key) == 0)
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	EC_POINT_free(point);

	return ret;
}
LCRYPTO_ALIAS(EC_KEY_set_public_key_affine_coordinates);

const EC_GROUP *
EC_KEY_get0_group(const EC_KEY *key)
{
	return key->group;
}
LCRYPTO_ALIAS(EC_KEY_get0_group);

int
EC_KEY_set_group(EC_KEY *key, const EC_GROUP *group)
{
	if (key->meth->set_group != NULL &&
	    key->meth->set_group(key, group) == 0)
		return 0;
	EC_GROUP_free(key->group);
	key->group = EC_GROUP_dup(group);
	return (key->group == NULL) ? 0 : 1;
}
LCRYPTO_ALIAS(EC_KEY_set_group);

const BIGNUM *
EC_KEY_get0_private_key(const EC_KEY *key)
{
	return key->priv_key;
}
LCRYPTO_ALIAS(EC_KEY_get0_private_key);

int
EC_KEY_set_private_key(EC_KEY *key, const BIGNUM *priv_key)
{
	if (key->meth->set_private != NULL &&
	    key->meth->set_private(key, priv_key) == 0)
		return 0;

	BN_free(key->priv_key);
	if ((key->priv_key = BN_dup(priv_key)) == NULL)
		return 0;

	return 1;
}
LCRYPTO_ALIAS(EC_KEY_set_private_key);

const EC_POINT *
EC_KEY_get0_public_key(const EC_KEY *key)
{
	return key->pub_key;
}
LCRYPTO_ALIAS(EC_KEY_get0_public_key);

int
EC_KEY_set_public_key(EC_KEY *key, const EC_POINT *pub_key)
{
	if (key->meth->set_public != NULL &&
	    key->meth->set_public(key, pub_key) == 0)
		return 0;

	EC_POINT_free(key->pub_key);
	if ((key->pub_key = EC_POINT_dup(pub_key, key->group)) == NULL)
		return 0;

	return 1;
}
LCRYPTO_ALIAS(EC_KEY_set_public_key);

unsigned int
EC_KEY_get_enc_flags(const EC_KEY *key)
{
	return key->enc_flag;
}
LCRYPTO_ALIAS(EC_KEY_get_enc_flags);

void
EC_KEY_set_enc_flags(EC_KEY *key, unsigned int flags)
{
	key->enc_flag = flags;
}
LCRYPTO_ALIAS(EC_KEY_set_enc_flags);

point_conversion_form_t
EC_KEY_get_conv_form(const EC_KEY *key)
{
	return key->conv_form;
}
LCRYPTO_ALIAS(EC_KEY_get_conv_form);

void
EC_KEY_set_conv_form(EC_KEY *key, point_conversion_form_t cform)
{
	key->conv_form = cform;
	if (key->group != NULL)
		EC_GROUP_set_point_conversion_form(key->group, cform);
}
LCRYPTO_ALIAS(EC_KEY_set_conv_form);

void
EC_KEY_set_asn1_flag(EC_KEY *key, int flag)
{
	if (key->group != NULL)
		EC_GROUP_set_asn1_flag(key->group, flag);
}
LCRYPTO_ALIAS(EC_KEY_set_asn1_flag);

int
EC_KEY_precompute_mult(EC_KEY *key, BN_CTX *ctx)
{
	if (key->group == NULL)
		return 0;
	return 1;
}
LCRYPTO_ALIAS(EC_KEY_precompute_mult);

int
EC_KEY_get_flags(const EC_KEY *key)
{
	return key->flags;
}
LCRYPTO_ALIAS(EC_KEY_get_flags);

void
EC_KEY_set_flags(EC_KEY *key, int flags)
{
	key->flags |= flags;
}
LCRYPTO_ALIAS(EC_KEY_set_flags);

void
EC_KEY_clear_flags(EC_KEY *key, int flags)
{
	key->flags &= ~flags;
}
LCRYPTO_ALIAS(EC_KEY_clear_flags);

const EC_KEY_METHOD *
EC_KEY_get_method(const EC_KEY *key)
{
	return key->meth;
}
LCRYPTO_ALIAS(EC_KEY_get_method);

int
EC_KEY_set_method(EC_KEY *key, const EC_KEY_METHOD *meth)
{
	void (*finish)(EC_KEY *key) = key->meth->finish;

	if (finish != NULL)
		finish(key);

	key->meth = meth;
	if (meth->init != NULL)
		return meth->init(key);
	return 1;
}
LCRYPTO_ALIAS(EC_KEY_set_method);

EC_KEY *
EC_KEY_new_method(ENGINE *engine)
{
	EC_KEY *ret;

	if ((ret = calloc(1, sizeof(EC_KEY))) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	ret->meth = EC_KEY_get_default_method();
	ret->version = 1;
	ret->flags = 0;
	ret->group = NULL;
	ret->pub_key = NULL;
	ret->priv_key = NULL;
	ret->enc_flag = 0;
	ret->conv_form = POINT_CONVERSION_UNCOMPRESSED;
	ret->references = 1;

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_EC_KEY, ret, &ret->ex_data))
		goto err;
	if (ret->meth->init != NULL && ret->meth->init(ret) == 0)
		goto err;

	return ret;

 err:
	EC_KEY_free(ret);
	return NULL;
}
LCRYPTO_ALIAS(EC_KEY_new_method);

#define EC_KEY_METHOD_DYNAMIC   1

EC_KEY_METHOD *
EC_KEY_METHOD_new(const EC_KEY_METHOD *meth)
{
	EC_KEY_METHOD *ret;

	if ((ret = calloc(1, sizeof(*meth))) == NULL)
		return NULL;
	if (meth != NULL)
		*ret = *meth;
	ret->flags |= EC_KEY_METHOD_DYNAMIC;
	return ret;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_new);

void
EC_KEY_METHOD_free(EC_KEY_METHOD *meth)
{
	if (meth == NULL)
		return;
	if (meth->flags & EC_KEY_METHOD_DYNAMIC)
		free(meth);
}
LCRYPTO_ALIAS(EC_KEY_METHOD_free);

void
EC_KEY_METHOD_set_init(EC_KEY_METHOD *meth,
    int (*init)(EC_KEY *key),
    void (*finish)(EC_KEY *key),
    int (*copy)(EC_KEY *dest, const EC_KEY *src),
    int (*set_group)(EC_KEY *key, const EC_GROUP *grp),
    int (*set_private)(EC_KEY *key, const BIGNUM *priv_key),
    int (*set_public)(EC_KEY *key, const EC_POINT *pub_key))
{
	meth->init = init;
	meth->finish = finish;
	meth->copy = copy;
	meth->set_group = set_group;
	meth->set_private = set_private;
	meth->set_public = set_public;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_set_init);

void
EC_KEY_METHOD_set_keygen(EC_KEY_METHOD *meth, int (*keygen)(EC_KEY *key))
{
	meth->keygen = keygen;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_set_keygen);

void
EC_KEY_METHOD_set_compute_key(EC_KEY_METHOD *meth,
    int (*ckey)(unsigned char **out, size_t *out_len, const EC_POINT *pub_key,
        const EC_KEY *ecdh))
{
	meth->compute_key = ckey;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_set_compute_key);

void
EC_KEY_METHOD_set_sign(EC_KEY_METHOD *meth,
    int (*sign)(int type, const unsigned char *dgst,
	int dlen, unsigned char *sig, unsigned int *siglen,
	const BIGNUM *kinv, const BIGNUM *r, EC_KEY *eckey),
    int (*sign_setup)(EC_KEY *eckey, BN_CTX *ctx_in,
	BIGNUM **kinvp, BIGNUM **rp),
    ECDSA_SIG *(*sign_sig)(const unsigned char *dgst,
	int dgst_len, const BIGNUM *in_kinv,
	const BIGNUM *in_r, EC_KEY *eckey))
{
	meth->sign = sign;
	meth->sign_setup = sign_setup;
	meth->sign_sig = sign_sig;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_set_sign);

void
EC_KEY_METHOD_set_verify(EC_KEY_METHOD *meth,
    int (*verify)(int type, const unsigned char *dgst, int dgst_len,
	const unsigned char *sigbuf, int sig_len, EC_KEY *eckey),
    int (*verify_sig)(const unsigned char *dgst, int dgst_len,
	const ECDSA_SIG *sig, EC_KEY *eckey))
{
	meth->verify = verify;
	meth->verify_sig = verify_sig;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_set_verify);


void
EC_KEY_METHOD_get_init(const EC_KEY_METHOD *meth,
    int (**pinit)(EC_KEY *key),
    void (**pfinish)(EC_KEY *key),
    int (**pcopy)(EC_KEY *dest, const EC_KEY *src),
    int (**pset_group)(EC_KEY *key, const EC_GROUP *grp),
    int (**pset_private)(EC_KEY *key, const BIGNUM *priv_key),
    int (**pset_public)(EC_KEY *key, const EC_POINT *pub_key))
{
	if (pinit != NULL)
		*pinit = meth->init;
	if (pfinish != NULL)
		*pfinish = meth->finish;
	if (pcopy != NULL)
		*pcopy = meth->copy;
	if (pset_group != NULL)
		*pset_group = meth->set_group;
	if (pset_private != NULL)
		*pset_private = meth->set_private;
	if (pset_public != NULL)
		*pset_public = meth->set_public;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_get_init);

void
EC_KEY_METHOD_get_keygen(const EC_KEY_METHOD *meth,
    int (**pkeygen)(EC_KEY *key))
{
	if (pkeygen != NULL)
		*pkeygen = meth->keygen;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_get_keygen);

void
EC_KEY_METHOD_get_compute_key(const EC_KEY_METHOD *meth,
    int (**pck)(unsigned char **out, size_t *out_len, const EC_POINT *pub_key,
        const EC_KEY *ecdh))
{
	if (pck != NULL)
		*pck = meth->compute_key;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_get_compute_key);

void
EC_KEY_METHOD_get_sign(const EC_KEY_METHOD *meth,
    int (**psign)(int type, const unsigned char *dgst,
	int dlen, unsigned char *sig, unsigned int *siglen,
	const BIGNUM *kinv, const BIGNUM *r, EC_KEY *eckey),
    int (**psign_setup)(EC_KEY *eckey, BN_CTX *ctx_in,
	BIGNUM **kinvp, BIGNUM **rp),
    ECDSA_SIG *(**psign_sig)(const unsigned char *dgst,
	int dgst_len, const BIGNUM *in_kinv, const BIGNUM *in_r,
	EC_KEY *eckey))
{
	if (psign != NULL)
		*psign = meth->sign;
	if (psign_setup != NULL)
		*psign_setup = meth->sign_setup;
	if (psign_sig != NULL)
		*psign_sig = meth->sign_sig;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_get_sign);

void
EC_KEY_METHOD_get_verify(const EC_KEY_METHOD *meth,
    int (**pverify)(int type, const unsigned char *dgst, int dgst_len,
	const unsigned char *sigbuf, int sig_len, EC_KEY *eckey),
    int (**pverify_sig)(const unsigned char *dgst, int dgst_len,
	const ECDSA_SIG *sig, EC_KEY *eckey))
{
	if (pverify != NULL)
		*pverify = meth->verify;
	if (pverify_sig != NULL)
		*pverify_sig = meth->verify_sig;
}
LCRYPTO_ALIAS(EC_KEY_METHOD_get_verify);

static const EC_KEY_METHOD openssl_ec_key_method = {
	.name = "OpenSSL EC_KEY method",
	.flags = 0,

	.init = NULL,
	.finish = NULL,
	.copy = NULL,

	.set_group = NULL,
	.set_private = NULL,
	.set_public = NULL,

	.keygen = ec_key_gen,
	.compute_key = ecdh_compute_key,

	.sign = ecdsa_sign,
	.sign_setup = ecdsa_sign_setup,
	.sign_sig = ecdsa_sign_sig,

	.verify = ecdsa_verify,
	.verify_sig = ecdsa_verify_sig,
};

const EC_KEY_METHOD *
EC_KEY_OpenSSL(void)
{
	return &openssl_ec_key_method;
}
LCRYPTO_ALIAS(EC_KEY_OpenSSL);

const EC_KEY_METHOD *default_ec_key_meth = &openssl_ec_key_method;

const EC_KEY_METHOD *
EC_KEY_get_default_method(void)
{
	return default_ec_key_meth;
}
LCRYPTO_ALIAS(EC_KEY_get_default_method);

void
EC_KEY_set_default_method(const EC_KEY_METHOD *meth)
{
	if (meth == NULL)
		default_ec_key_meth = &openssl_ec_key_method;
	else
		default_ec_key_meth = meth;
}
LCRYPTO_ALIAS(EC_KEY_set_default_method);
