/* $OpenBSD: ec_pmeth.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2006.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1t.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "bn_local.h"
#include "ec_local.h"
#include "err_local.h"
#include "evp_local.h"

/* EC pkey context structure */

typedef struct {
	/* Key and paramgen group */
	EC_GROUP *gen_group;
	/* message digest */
	const EVP_MD *md;
	/* Duplicate key if custom cofactor needed */
	EC_KEY *co_key;
	/* Cofactor mode */
	signed char cofactor_mode;
	/* KDF (if any) to use for ECDH */
	char kdf_type;
	/* Message digest to use for key derivation */
	const EVP_MD *kdf_md;
	/* User key material */
	unsigned char *kdf_ukm;
	size_t kdf_ukmlen;
	/* KDF output length */
	size_t kdf_outlen;
} EC_PKEY_CTX;

static int
pkey_ec_init(EVP_PKEY_CTX *ctx)
{
	EC_PKEY_CTX *dctx;

	if ((dctx = calloc(1, sizeof(EC_PKEY_CTX))) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	dctx->cofactor_mode = -1;
	dctx->kdf_type = EVP_PKEY_ECDH_KDF_NONE;

	ctx->data = dctx;

	return 1;
}

static int
pkey_ec_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	EC_PKEY_CTX *dctx, *sctx;
	if (!pkey_ec_init(dst))
		return 0;
	sctx = src->data;
	dctx = dst->data;
	if (sctx->gen_group) {
		dctx->gen_group = EC_GROUP_dup(sctx->gen_group);
		if (!dctx->gen_group)
			return 0;
	}
	dctx->md = sctx->md;

	if (sctx->co_key) {
		dctx->co_key = EC_KEY_dup(sctx->co_key);
		if (!dctx->co_key)
			return 0;
	}
	dctx->kdf_type = sctx->kdf_type;
	dctx->kdf_md = sctx->kdf_md;
	dctx->kdf_outlen = sctx->kdf_outlen;
	if (sctx->kdf_ukm) {
		if ((dctx->kdf_ukm = calloc(1, sctx->kdf_ukmlen)) == NULL)
			return 0;
		memcpy(dctx->kdf_ukm, sctx->kdf_ukm, sctx->kdf_ukmlen);
	} else
		dctx->kdf_ukm = NULL;

	dctx->kdf_ukmlen = sctx->kdf_ukmlen;

	return 1;
}

static void
pkey_ec_cleanup(EVP_PKEY_CTX *ctx)
{
	EC_PKEY_CTX *dctx = ctx->data;

	if (dctx != NULL) {
		EC_GROUP_free(dctx->gen_group);
		EC_KEY_free(dctx->co_key);
		free(dctx->kdf_ukm);
		free(dctx);
		ctx->data = NULL;
	}
}

static int
pkey_ec_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    const unsigned char *tbs, size_t tbslen)
{
	int ret, type;
	unsigned int sltmp;
	EC_PKEY_CTX *dctx = ctx->data;
	EC_KEY *ec = ctx->pkey->pkey.ec;

	if (!sig) {
		*siglen = ECDSA_size(ec);
		return 1;
	} else if (*siglen < (size_t) ECDSA_size(ec)) {
		ECerror(EC_R_BUFFER_TOO_SMALL);
		return 0;
	}
	if (dctx->md)
		type = EVP_MD_type(dctx->md);
	else
		type = NID_sha1;

	ret = ECDSA_sign(type, tbs, tbslen, sig, &sltmp, ec);
	if (ret <= 0)
		return ret;
	*siglen = (size_t) sltmp;
	return 1;
}

static int
pkey_ec_verify(EVP_PKEY_CTX *ctx,
    const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	int ret, type;
	EC_PKEY_CTX *dctx = ctx->data;
	EC_KEY *ec = ctx->pkey->pkey.ec;

	if (dctx->md)
		type = EVP_MD_type(dctx->md);
	else
		type = NID_sha1;

	ret = ECDSA_verify(type, tbs, tbslen, sig, siglen, ec);

	return ret;
}

static int
pkey_ec_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *keylen)
{
	int ret;
	size_t outlen;
	const EC_POINT *pubkey = NULL;
	EC_KEY *eckey;
	EC_PKEY_CTX *dctx = ctx->data;

	if (!ctx->pkey || !ctx->peerkey) {
		ECerror(EC_R_KEYS_NOT_SET);
		return 0;
	}

	eckey = dctx->co_key ? dctx->co_key : ctx->pkey->pkey.ec;
	if (key == NULL) {
		*keylen = BN_num_bytes(eckey->group->p);
		return 1;
	}
	pubkey = EC_KEY_get0_public_key(ctx->peerkey->pkey.ec);

	/*
	 * NB: unlike PKCS#3 DH, if *outlen is less than maximum size this is
	 * not an error, the result is truncated.
	 */

	outlen = *keylen;

	ret = ECDH_compute_key(key, outlen, pubkey, eckey, NULL);
	if (ret <= 0)
		return 0;

	*keylen = ret;

	return 1;
}

static int
pkey_ec_kdf_derive(EVP_PKEY_CTX *ctx, unsigned char *key, size_t *keylen)
{
	EC_PKEY_CTX *dctx = ctx->data;
	unsigned char *ktmp = NULL;
	size_t ktmplen;
	int rv = 0;

	if (dctx->kdf_type == EVP_PKEY_ECDH_KDF_NONE)
		return pkey_ec_derive(ctx, key, keylen);

	if (!key) {
		*keylen = dctx->kdf_outlen;
		return 1;
	}
	if (*keylen != dctx->kdf_outlen)
		return 0;
	if (!pkey_ec_derive(ctx, NULL, &ktmplen))
		return 0;
	if ((ktmp = calloc(1, ktmplen)) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	if (!pkey_ec_derive(ctx, ktmp, &ktmplen))
		goto err;
	/* Do KDF stuff */
	if (!ecdh_KDF_X9_63(key, *keylen, ktmp, ktmplen, dctx->kdf_ukm,
	    dctx->kdf_ukmlen, dctx->kdf_md))
		goto err;
	rv = 1;

 err:
	freezero(ktmp, ktmplen);

	return rv;
}

static int
pkey_ec_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	EC_PKEY_CTX *dctx = ctx->data;
	EC_GROUP *group;

	switch (type) {
	case EVP_PKEY_CTRL_EC_PARAMGEN_CURVE_NID:
		group = EC_GROUP_new_by_curve_name(p1);
		if (group == NULL) {
			ECerror(EC_R_INVALID_CURVE);
			return 0;
		}
		EC_GROUP_free(dctx->gen_group);
		dctx->gen_group = group;
		return 1;

	case EVP_PKEY_CTRL_EC_PARAM_ENC:
		if (!dctx->gen_group) {
			ECerror(EC_R_NO_PARAMETERS_SET);
			return 0;
		}
		EC_GROUP_set_asn1_flag(dctx->gen_group, p1);
		return 1;

	case EVP_PKEY_CTRL_EC_ECDH_COFACTOR:
		if (p1 == -2) {
			if (dctx->cofactor_mode != -1)
				return dctx->cofactor_mode;
			else {
				EC_KEY *ec_key = ctx->pkey->pkey.ec;
				return EC_KEY_get_flags(ec_key) & EC_FLAG_COFACTOR_ECDH ? 1 : 0;
			}
		} else if (p1 < -1 || p1 > 1)
			return -2;
		dctx->cofactor_mode = p1;
		if (p1 != -1) {
			EC_KEY *ec_key = ctx->pkey->pkey.ec;
			if (!ec_key->group)
				return -2;
			/* If cofactor is 1 cofactor mode does nothing */
			if (BN_is_one(ec_key->group->cofactor))
				return 1;
			if (!dctx->co_key) {
				dctx->co_key = EC_KEY_dup(ec_key);
				if (!dctx->co_key)
					return 0;
			}
			if (p1)
				EC_KEY_set_flags(dctx->co_key, EC_FLAG_COFACTOR_ECDH);
			else
				EC_KEY_clear_flags(dctx->co_key, EC_FLAG_COFACTOR_ECDH);
		} else {
			EC_KEY_free(dctx->co_key);
			dctx->co_key = NULL;
		}
		return 1;

	case EVP_PKEY_CTRL_EC_KDF_TYPE:
		if (p1 == -2)
			return dctx->kdf_type;
		if (p1 != EVP_PKEY_ECDH_KDF_NONE && p1 != EVP_PKEY_ECDH_KDF_X9_63)
			return -2;
		dctx->kdf_type = p1;
		return 1;

	case EVP_PKEY_CTRL_EC_KDF_MD:
		dctx->kdf_md = p2;
		return 1;

	case EVP_PKEY_CTRL_GET_EC_KDF_MD:
		*(const EVP_MD **)p2 = dctx->kdf_md;
		return 1;

	case EVP_PKEY_CTRL_EC_KDF_OUTLEN:
		if (p1 <= 0)
			return -2;
		dctx->kdf_outlen = (size_t)p1;
		return 1;

	case EVP_PKEY_CTRL_GET_EC_KDF_OUTLEN:
		*(int *)p2 = dctx->kdf_outlen;
		return 1;

	case EVP_PKEY_CTRL_EC_KDF_UKM:
		free(dctx->kdf_ukm);
		dctx->kdf_ukm = p2;
		if (p2)
			dctx->kdf_ukmlen = p1;
		else
			dctx->kdf_ukmlen = 0;
		return 1;

	case EVP_PKEY_CTRL_GET_EC_KDF_UKM:
		*(unsigned char **)p2 = dctx->kdf_ukm;
		return dctx->kdf_ukmlen;

	case EVP_PKEY_CTRL_MD:
		/* RFC 3279, RFC 5758 and NIST CSOR. */
		switch (EVP_MD_type(p2)) {
		case NID_sha1:
		case NID_ecdsa_with_SHA1:
		case NID_sha224:
		case NID_sha256:
		case NID_sha384:
		case NID_sha512:
		case NID_sha3_224:
		case NID_sha3_256:
		case NID_sha3_384:
		case NID_sha3_512:
			break;
		default:
			ECerror(EC_R_INVALID_DIGEST_TYPE);
			return 0;
		}
		dctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_GET_MD:
		*(const EVP_MD **)p2 = dctx->md;
		return 1;

	case EVP_PKEY_CTRL_PEER_KEY:
		/* Default behaviour is OK */
	case EVP_PKEY_CTRL_DIGESTINIT:
	case EVP_PKEY_CTRL_PKCS7_SIGN:
	case EVP_PKEY_CTRL_CMS_SIGN:
		return 1;

	default:
		return -2;

	}
}

static int
pkey_ec_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	if (!strcmp(type, "ec_paramgen_curve")) {
		int nid;
		nid = EC_curve_nist2nid(value);
		if (nid == NID_undef)
			nid = OBJ_sn2nid(value);
		if (nid == NID_undef)
			nid = OBJ_ln2nid(value);
		if (nid == NID_undef) {
			ECerror(EC_R_INVALID_CURVE);
			return 0;
		}
		return EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid);
	} else if (strcmp(type, "ec_param_enc") == 0) {
		int param_enc;
		if (strcmp(value, "explicit") == 0)
			param_enc = 0;
		else if (strcmp(value, "named_curve") == 0)
			param_enc = OPENSSL_EC_NAMED_CURVE;
		else
			return -2;
		return EVP_PKEY_CTX_set_ec_param_enc(ctx, param_enc);
	} else if (strcmp(type, "ecdh_kdf_md") == 0) {
		const EVP_MD *md;
		if ((md = EVP_get_digestbyname(value)) == NULL) {
			ECerror(EC_R_INVALID_DIGEST);
			return 0;
		}
		return EVP_PKEY_CTX_set_ecdh_kdf_md(ctx, md);
	} else if (strcmp(type, "ecdh_cofactor_mode") == 0) {
		int cofactor_mode;
		const char *errstr;

		cofactor_mode = strtonum(value, -1, 1, &errstr);
		if (errstr != NULL)
			return -2;
		return EVP_PKEY_CTX_set_ecdh_cofactor_mode(ctx, cofactor_mode);
	}

	return -2;
}

static int
pkey_ec_paramgen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	EC_KEY *ec = NULL;
	EC_PKEY_CTX *dctx = ctx->data;
	int ret = 0;

	if (dctx->gen_group == NULL) {
		ECerror(EC_R_NO_PARAMETERS_SET);
		goto err;
	}

	if ((ec = EC_KEY_new()) == NULL)
		goto err;
	if (!EC_KEY_set_group(ec, dctx->gen_group))
		goto err;
	if (!EVP_PKEY_assign_EC_KEY(pkey, ec))
		goto err;
	ec = NULL;

	ret = 1;

 err:
	EC_KEY_free(ec);

	return ret;
}

static int
pkey_ec_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	EC_KEY *ec = NULL;
	EC_PKEY_CTX *dctx = ctx->data;
	int ret = 0;

	if (ctx->pkey == NULL && dctx->gen_group == NULL) {
		ECerror(EC_R_NO_PARAMETERS_SET);
		goto err;
	}

	if ((ec = EC_KEY_new()) == NULL)
		goto err;
	if (!EVP_PKEY_set1_EC_KEY(pkey, ec))
		goto err;

	if (ctx->pkey != NULL) {
		if (!EVP_PKEY_copy_parameters(pkey, ctx->pkey))
			goto err;
	} else {
		if (!EC_KEY_set_group(ec, dctx->gen_group))
			goto err;
	}

	if (!EC_KEY_generate_key(ec))
		goto err;

	ret = 1;

 err:
	EC_KEY_free(ec);

	return ret;
}

const EVP_PKEY_METHOD ec_pkey_meth = {
	.pkey_id = EVP_PKEY_EC,

	.init = pkey_ec_init,
	.copy = pkey_ec_copy,
	.cleanup = pkey_ec_cleanup,

	.paramgen = pkey_ec_paramgen,

	.keygen = pkey_ec_keygen,

	.sign = pkey_ec_sign,

	.verify = pkey_ec_verify,

	.derive = pkey_ec_kdf_derive,

	.ctrl = pkey_ec_ctrl,
	.ctrl_str = pkey_ec_ctrl_str
};
