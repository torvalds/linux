/*	$OpenBSD: sm2_pmeth.c,v 1.3 2025/05/10 05:54:38 tb Exp $ */
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
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#include "err_local.h"
#include "evp_local.h"
#include "sm2_local.h"

/* SM2 pkey context structure */

typedef struct {
	/* key and paramgen group */
	EC_GROUP *gen_group;
	/* message  digest */
	const EVP_MD *md;
	EVP_MD_CTX *md_ctx;
	/* personalization string */
	uint8_t* uid;
	size_t uid_len;
} SM2_PKEY_CTX;

static int
pkey_sm2_init(EVP_PKEY_CTX *ctx)
{
	SM2_PKEY_CTX *dctx;

	if ((dctx = calloc(1, sizeof(*dctx))) == NULL) {
		SM2error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ctx->data = dctx;
	return 1;
}

static void
pkey_sm2_cleanup(EVP_PKEY_CTX *ctx)
{
	SM2_PKEY_CTX *dctx = ctx->data;

	if (ctx == NULL || ctx->data == NULL)
		return;

	EC_GROUP_free(dctx->gen_group);
	free(dctx->uid);
	free(dctx);
	ctx->data = NULL;
}

static int
pkey_sm2_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	SM2_PKEY_CTX *dctx, *sctx;

	if (!pkey_sm2_init(dst))
		return 0;
	sctx = src->data;
	dctx = dst->data;
	if (sctx->gen_group) {
		if ((dctx->gen_group = EC_GROUP_dup(sctx->gen_group)) == NULL) {
			SM2error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
	}

	if (sctx->uid != NULL) {
		if ((dctx->uid = malloc(sctx->uid_len)) == NULL) {
			SM2error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		memcpy(dctx->uid, sctx->uid, sctx->uid_len);
		dctx->uid_len = sctx->uid_len;
	}

	dctx->md = sctx->md;

	if (!EVP_MD_CTX_copy(dctx->md_ctx, sctx->md_ctx))
		goto err;

	return 1;

 err:
	pkey_sm2_cleanup(dst);
	return 0;
}

static int
pkey_sm2_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    const unsigned char *tbs, size_t tbslen)
{
	unsigned int sltmp;
	int ret, sig_sz;

	if ((sig_sz = ECDSA_size(ctx->pkey->pkey.ec)) <= 0)
		return 0;

	if (sig == NULL) {
		*siglen = sig_sz;
		return 1;
	}

	if (*siglen < (size_t)sig_sz) {
		SM2error(SM2_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if ((ret = SM2_sign(tbs, tbslen, sig, &sltmp, ctx->pkey->pkey.ec)) <= 0)
		return ret;

	*siglen = (size_t)sltmp;
	return 1;
}

static int
pkey_sm2_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	return SM2_verify(tbs, tbslen, sig, siglen, ctx->pkey->pkey.ec);
}

static int
pkey_sm2_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	SM2_PKEY_CTX *dctx = ctx->data;
	const EVP_MD *md = (dctx->md == NULL) ? EVP_sm3() : dctx->md;

	if (out == NULL) {
		if (!SM2_ciphertext_size(ctx->pkey->pkey.ec, md, inlen, outlen))
			return -1;
		else
			return 1;
	}

	return SM2_encrypt(ctx->pkey->pkey.ec, md, in, inlen, out, outlen);
}

static int
pkey_sm2_decrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	SM2_PKEY_CTX *dctx = ctx->data;
	const EVP_MD *md = (dctx->md == NULL) ? EVP_sm3() : dctx->md;

	if (out == NULL) {
		if (!SM2_plaintext_size(ctx->pkey->pkey.ec, md, inlen, outlen))
			return -1;
		else
			return 1;
	}

	return SM2_decrypt(ctx->pkey->pkey.ec, md, in, inlen, out, outlen);
}

static int
pkey_sm2_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	SM2_PKEY_CTX *dctx = ctx->data;
	EC_GROUP *group = NULL;

	switch (type) {
	case EVP_PKEY_CTRL_DIGESTINIT:
		dctx->md_ctx = p2;
		return 1;

	case EVP_PKEY_CTRL_EC_PARAMGEN_CURVE_NID:
		if ((group = EC_GROUP_new_by_curve_name(p1)) == NULL) {
			SM2error(SM2_R_INVALID_CURVE);
			return 0;
		}
		EC_GROUP_free(dctx->gen_group);
		dctx->gen_group = group;
		return 1;

	case EVP_PKEY_CTRL_SM2_SET_UID:
		if ((p1 < 0) || ((p1 == 0) && (p2 != NULL)))  {
			SM2error(SM2_R_INVALID_ARGUMENT);
			return 0;
		}
		if ((p1 > 0) && (p2 == NULL)) {
			SM2error(ERR_R_PASSED_NULL_PARAMETER);
			return 0;
		}
		free(dctx->uid);
		if (p2 == NULL) {
			dctx->uid = NULL;
			dctx->uid_len = 0;
			return 1;
		}

		if ((dctx->uid = malloc(p1)) == NULL) {
			SM2error(ERR_R_MALLOC_FAILURE);
			return 1;
		}
		memcpy(dctx->uid, p2, p1);
		dctx->uid_len = p1;
		return 1;

	case EVP_PKEY_CTRL_SM2_HASH_UID:
	    {
		const EVP_MD* md;
		uint8_t za[EVP_MAX_MD_SIZE] = {0};
		int md_len;

		if (dctx->uid == NULL) {
			SM2error(SM2_R_INVALID_ARGUMENT);
			return 0;
		}

		if ((md = EVP_MD_CTX_md(dctx->md_ctx)) == NULL) {
			SM2error(ERR_R_EVP_LIB);
			return 0;
		}

		if ((md_len = EVP_MD_size(md)) < 0) {
			SM2error(SM2_R_INVALID_DIGEST);
			return 0;
		}

		if (sm2_compute_userid_digest(za, md, dctx->uid, dctx->uid_len,
		    ctx->pkey->pkey.ec) != 1) {
			SM2error(SM2_R_DIGEST_FAILURE);
			return 0;
		}
		return EVP_DigestUpdate(dctx->md_ctx, za, md_len);
	    }

	case EVP_PKEY_CTRL_SM2_GET_UID_LEN:
		if (p2 == NULL) {
			SM2error(ERR_R_PASSED_NULL_PARAMETER);
			return 0;
		}
		*(size_t *)p2 = dctx->uid_len;
		return 1;

	case EVP_PKEY_CTRL_SM2_GET_UID:
		if (p2 == NULL) {
			SM2error(ERR_R_PASSED_NULL_PARAMETER);
			return 0;
		}
		if (dctx->uid_len == 0) {
			return 1;
		}
		memcpy(p2, dctx->uid, dctx->uid_len);
		return 1;

	case EVP_PKEY_CTRL_MD:
		dctx->md = p2;
		return 1;

	default:
		return -2;
	}
}

static int
pkey_sm2_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	int nid;

	if (strcmp(type, "ec_paramgen_curve") == 0) {
		if (((nid = EC_curve_nist2nid(value)) == NID_undef) &&
		    ((nid = OBJ_sn2nid(value)) == NID_undef) &&
		    ((nid = OBJ_ln2nid(value)) == NID_undef)) {
			SM2error(SM2_R_INVALID_CURVE);
			return 0;
		}
		return EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, nid);
	} else if (strcmp(type, "sm2_uid") == 0) {
		return EVP_PKEY_CTX_set_sm2_uid(ctx, (void*) value,
		    (int)strlen(value));
	}

	return -2;
}

const EVP_PKEY_METHOD sm2_pkey_meth = {
	.pkey_id = EVP_PKEY_SM2,
	.init = pkey_sm2_init,
	.copy = pkey_sm2_copy,
	.cleanup = pkey_sm2_cleanup,

	.sign = pkey_sm2_sign,

	.verify = pkey_sm2_verify,

	.encrypt = pkey_sm2_encrypt,

	.decrypt = pkey_sm2_decrypt,

	.ctrl = pkey_sm2_ctrl,
	.ctrl_str = pkey_sm2_ctrl_str
};

#endif /* OPENSSL_NO_SM2 */
