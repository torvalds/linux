/* $OpenBSD: rsa_pmeth.c,v 1.44 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "bn_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "rsa_local.h"

/* RSA pkey context structure */

typedef struct {
	/* Key gen parameters */
	int nbits;
	BIGNUM *pub_exp;
	/* Keygen callback info */
	int gentmp[2];
	/* RSA padding mode */
	int pad_mode;
	/* message digest */
	const EVP_MD *md;
	/* message digest for MGF1 */
	const EVP_MD *mgf1md;
	/* PSS salt length */
	int saltlen;
	/* Minimum salt length or -1 if no PSS parameter restriction */
	int min_saltlen;
	/* Temp buffer */
	unsigned char *tbuf;
	/* OAEP label */
	unsigned char *oaep_label;
	size_t oaep_labellen;
} RSA_PKEY_CTX;

/* True if PSS parameters are restricted */
#define rsa_pss_restricted(rctx) (rctx->min_saltlen != -1)

static int
pkey_rsa_init(EVP_PKEY_CTX *ctx)
{
	RSA_PKEY_CTX *rctx;

	if ((rctx = calloc(1, sizeof(RSA_PKEY_CTX))) == NULL)
		return 0;

	rctx->nbits = 2048;

	if (ctx->pmeth->pkey_id == EVP_PKEY_RSA_PSS)
		rctx->pad_mode = RSA_PKCS1_PSS_PADDING;
	else
		rctx->pad_mode = RSA_PKCS1_PADDING;

	/* Maximum for sign, auto for verify */
	rctx->saltlen = RSA_PSS_SALTLEN_AUTO;
	rctx->min_saltlen = -1;

	ctx->data = rctx;
	ctx->keygen_info = rctx->gentmp;
	ctx->keygen_info_count = 2;

	return 1;
}

static int
pkey_rsa_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	RSA_PKEY_CTX *dctx, *sctx;

	if (!pkey_rsa_init(dst))
		return 0;

	sctx = src->data;
	dctx = dst->data;
	dctx->nbits = sctx->nbits;
	if (sctx->pub_exp != NULL) {
		BN_free(dctx->pub_exp);
		if ((dctx->pub_exp = BN_dup(sctx->pub_exp)) == NULL)
			return 0;
	}
	dctx->pad_mode = sctx->pad_mode;
	dctx->md = sctx->md;
	dctx->mgf1md = sctx->mgf1md;
	if (sctx->oaep_label != NULL) {
		free(dctx->oaep_label);
		if ((dctx->oaep_label = calloc(1, sctx->oaep_labellen)) == NULL)
			return 0;
		memcpy(dctx->oaep_label, sctx->oaep_label, sctx->oaep_labellen);
		dctx->oaep_labellen = sctx->oaep_labellen;
	}

	return 1;
}

static int
setup_tbuf(RSA_PKEY_CTX *ctx, EVP_PKEY_CTX *pk)
{
	if (ctx->tbuf != NULL)
		return 1;
	if ((ctx->tbuf = calloc(1, EVP_PKEY_size(pk->pkey))) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	return 1;
}

static void
pkey_rsa_cleanup(EVP_PKEY_CTX *ctx)
{
	RSA_PKEY_CTX *rctx = ctx->data;

	if (rctx) {
		BN_free(rctx->pub_exp);
		free(rctx->tbuf);
		free(rctx->oaep_label);
		free(rctx);
	}
}

static int
pkey_rsa_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
    const unsigned char *tbs, size_t tbslen)
{
	int ret;
	RSA_PKEY_CTX *rctx = ctx->data;
	RSA *rsa = ctx->pkey->pkey.rsa;

	if (rctx->md) {
		if (tbslen != (size_t)EVP_MD_size(rctx->md)) {
			RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
			return -1;
		}

		if (rctx->pad_mode == RSA_X931_PADDING) {
			if ((size_t)EVP_PKEY_size(ctx->pkey) < tbslen + 1) {
				RSAerror(RSA_R_KEY_SIZE_TOO_SMALL);
				return -1;
			}
			if (!setup_tbuf(rctx, ctx)) {
				RSAerror(ERR_R_MALLOC_FAILURE);
				return -1;
			}
			memcpy(rctx->tbuf, tbs, tbslen);
			rctx->tbuf[tbslen] =
			    RSA_X931_hash_id(EVP_MD_type(rctx->md));
			ret = RSA_private_encrypt(tbslen + 1, rctx->tbuf, sig,
			    rsa, RSA_X931_PADDING);
		} else if (rctx->pad_mode == RSA_PKCS1_PADDING) {
			unsigned int sltmp;

			ret = RSA_sign(EVP_MD_type(rctx->md), tbs, tbslen, sig,
			    &sltmp, rsa);
			if (ret <= 0)
				return ret;
			ret = sltmp;
		} else if (rctx->pad_mode == RSA_PKCS1_PSS_PADDING) {
			if (!setup_tbuf(rctx, ctx))
				return -1;
			if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa, rctx->tbuf,
			    tbs, rctx->md, rctx->mgf1md, rctx->saltlen))
				return -1;
			ret = RSA_private_encrypt(RSA_size(rsa), rctx->tbuf,
			    sig, rsa, RSA_NO_PADDING);
		} else {
			return -1;
		}
	} else {
		ret = RSA_private_encrypt(tbslen, tbs, sig, ctx->pkey->pkey.rsa,
		    rctx->pad_mode);
	}
	if (ret < 0)
		return ret;
	*siglen = ret;
	return 1;
}

static int
pkey_rsa_verifyrecover(EVP_PKEY_CTX *ctx, unsigned char *rout, size_t *routlen,
    const unsigned char *sig, size_t siglen)
{
	int ret;
	RSA_PKEY_CTX *rctx = ctx->data;

	if (rctx->md) {
		if (rctx->pad_mode == RSA_X931_PADDING) {
			if (!setup_tbuf(rctx, ctx))
				return -1;
			ret = RSA_public_decrypt(siglen, sig, rctx->tbuf,
			    ctx->pkey->pkey.rsa, RSA_X931_PADDING);
			if (ret < 1)
				return 0;
			ret--;
			if (rctx->tbuf[ret] !=
			    RSA_X931_hash_id(EVP_MD_type(rctx->md))) {
				RSAerror(RSA_R_ALGORITHM_MISMATCH);
				return 0;
			}
			if (ret != EVP_MD_size(rctx->md)) {
				RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
				return 0;
			}
			if (rout)
				memcpy(rout, rctx->tbuf, ret);
		} else if (rctx->pad_mode == RSA_PKCS1_PADDING) {
			size_t sltmp;

			ret = int_rsa_verify(EVP_MD_type(rctx->md), NULL, 0,
			    rout, &sltmp, sig, siglen, ctx->pkey->pkey.rsa);
			if (ret <= 0)
				return 0;
			ret = sltmp;
		} else {
			return -1;
		}
	} else {
		ret = RSA_public_decrypt(siglen, sig, rout, ctx->pkey->pkey.rsa,
		    rctx->pad_mode);
	}
	if (ret < 0)
		return ret;
	*routlen = ret;
	return 1;
}

static int
pkey_rsa_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	RSA_PKEY_CTX *rctx = ctx->data;
	RSA *rsa = ctx->pkey->pkey.rsa;
	size_t rslen;

	if (rctx->md) {
		if (rctx->pad_mode == RSA_PKCS1_PADDING)
			return RSA_verify(EVP_MD_type(rctx->md), tbs, tbslen,
			    sig, siglen, rsa);
		if (tbslen != (size_t)EVP_MD_size(rctx->md)) {
			RSAerror(RSA_R_INVALID_DIGEST_LENGTH);
			return -1;
		}
		if (rctx->pad_mode == RSA_X931_PADDING) {
			if (pkey_rsa_verifyrecover(ctx, NULL, &rslen, sig,
			    siglen) <= 0)
				return 0;
		} else if (rctx->pad_mode == RSA_PKCS1_PSS_PADDING) {
			int ret;

			if (!setup_tbuf(rctx, ctx))
				return -1;
			ret = RSA_public_decrypt(siglen, sig, rctx->tbuf,
			    rsa, RSA_NO_PADDING);
			if (ret <= 0)
				return 0;
			ret = RSA_verify_PKCS1_PSS_mgf1(rsa, tbs, rctx->md,
			    rctx->mgf1md, rctx->tbuf, rctx->saltlen);
			if (ret <= 0)
				return 0;
			return 1;
		} else {
			return -1;
		}
	} else {
		int ret;

		if (!setup_tbuf(rctx, ctx))
			return -1;

		if ((ret = RSA_public_decrypt(siglen, sig, rctx->tbuf, rsa,
		    rctx->pad_mode)) <= 0)
			return 0;

		rslen = ret;
	}

	if (rslen != tbslen || timingsafe_bcmp(tbs, rctx->tbuf, rslen))
		return 0;

	return 1;
}

static int
pkey_rsa_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	RSA_PKEY_CTX *rctx = ctx->data;
	int ret;

	if (rctx->pad_mode == RSA_PKCS1_OAEP_PADDING) {
		int klen = RSA_size(ctx->pkey->pkey.rsa);
		if (!setup_tbuf(rctx, ctx))
			return -1;
		if (!RSA_padding_add_PKCS1_OAEP_mgf1(rctx->tbuf, klen,
		    in, inlen, rctx->oaep_label, rctx->oaep_labellen,
		    rctx->md, rctx->mgf1md))
			return -1;
		ret = RSA_public_encrypt(klen, rctx->tbuf, out,
		    ctx->pkey->pkey.rsa, RSA_NO_PADDING);
	} else {
		ret = RSA_public_encrypt(inlen, in, out, ctx->pkey->pkey.rsa,
		    rctx->pad_mode);
	}
	if (ret < 0)
		return ret;
	*outlen = ret;
	return 1;
}

static int
pkey_rsa_decrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
    const unsigned char *in, size_t inlen)
{
	int ret;
	RSA_PKEY_CTX *rctx = ctx->data;

	if (rctx->pad_mode == RSA_PKCS1_OAEP_PADDING) {
		if (!setup_tbuf(rctx, ctx))
			return -1;
		ret = RSA_private_decrypt(inlen, in, rctx->tbuf,
		    ctx->pkey->pkey.rsa, RSA_NO_PADDING);
		if (ret <= 0)
			return ret;
		ret = RSA_padding_check_PKCS1_OAEP_mgf1(out, ret, rctx->tbuf,
		    ret, ret, rctx->oaep_label, rctx->oaep_labellen, rctx->md,
		    rctx->mgf1md);
	} else {
		ret = RSA_private_decrypt(inlen, in, out, ctx->pkey->pkey.rsa,
		    rctx->pad_mode);
	}
	if (ret < 0)
		return ret;
	*outlen = ret;
	return 1;
}

static int
check_padding_md(const EVP_MD *md, int padding)
{
	if (md == NULL)
		return 1;

	if (padding == RSA_NO_PADDING) {
		RSAerror(RSA_R_INVALID_PADDING_MODE);
		return 0;
	}

	if (padding == RSA_X931_PADDING) {
		if (RSA_X931_hash_id(EVP_MD_type(md)) == -1) {
			RSAerror(RSA_R_INVALID_X931_DIGEST);
			return 0;
		}
	} else {
		/* List of all supported RSA digests. */
		/* RFC 8017 and NIST CSOR. */
		switch(EVP_MD_type(md)) {
		case NID_sha1:
		case NID_sha224:
		case NID_sha256:
		case NID_sha384:
		case NID_sha512:
		case NID_sha512_224:
		case NID_sha512_256:
		case NID_sha3_224:
		case NID_sha3_256:
		case NID_sha3_384:
		case NID_sha3_512:
		case NID_md5:
		case NID_md5_sha1:
		case NID_md4:
		case NID_ripemd160:
			return 1;

		default:
			RSAerror(RSA_R_INVALID_DIGEST);
			return 0;
		}
	}

	return 1;
}

static int
pkey_rsa_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	RSA_PKEY_CTX *rctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_RSA_PADDING:
		if (p1 >= RSA_PKCS1_PADDING && p1 <= RSA_PKCS1_PSS_PADDING) {
			if (!check_padding_md(rctx->md, p1))
				return 0;
			if (p1 == RSA_PKCS1_PSS_PADDING) {
				if (!(ctx->operation &
				    (EVP_PKEY_OP_SIGN | EVP_PKEY_OP_VERIFY)))
					goto bad_pad;
				if (!rctx->md)
					rctx->md = EVP_sha1();
			} else if (ctx->pmeth->pkey_id == EVP_PKEY_RSA_PSS) {
				goto bad_pad;
			}
			if (p1 == RSA_PKCS1_OAEP_PADDING) {
				if (!(ctx->operation & EVP_PKEY_OP_TYPE_CRYPT))
					goto bad_pad;
				if (!rctx->md)
					rctx->md = EVP_sha1();
			}
			rctx->pad_mode = p1;
			return 1;
		}
 bad_pad:
		RSAerror(RSA_R_ILLEGAL_OR_UNSUPPORTED_PADDING_MODE);
		return -2;

	case EVP_PKEY_CTRL_GET_RSA_PADDING:
		*(int *)p2 = rctx->pad_mode;
		return 1;

	case EVP_PKEY_CTRL_RSA_PSS_SALTLEN:
	case EVP_PKEY_CTRL_GET_RSA_PSS_SALTLEN:
		if (rctx->pad_mode != RSA_PKCS1_PSS_PADDING) {
			RSAerror(RSA_R_INVALID_PSS_SALTLEN);
			return -2;
		}
		if (type == EVP_PKEY_CTRL_GET_RSA_PSS_SALTLEN) {
			*(int *)p2 = rctx->saltlen;
		} else {
			if (p1 < RSA_PSS_SALTLEN_MAX)
				return -2;
			if (rsa_pss_restricted(rctx)) {
				if (p1 == RSA_PSS_SALTLEN_AUTO &&
				    ctx->operation == EVP_PKEY_OP_VERIFY) {
					RSAerror(RSA_R_INVALID_PSS_SALTLEN);
					return -2;
				}
				if ((p1 == RSA_PSS_SALTLEN_DIGEST &&
				    rctx->min_saltlen > EVP_MD_size(rctx->md)) ||
				    (p1 >= 0 && p1 < rctx->min_saltlen)) {
					RSAerror(RSA_R_PSS_SALTLEN_TOO_SMALL);
					return 0;
				}
			}
			rctx->saltlen = p1;
		}
		return 1;

	case EVP_PKEY_CTRL_RSA_KEYGEN_BITS:
		if (p1 < RSA_MIN_MODULUS_BITS) {
			RSAerror(RSA_R_KEY_SIZE_TOO_SMALL);
			return -2;
		}
		rctx->nbits = p1;
		return 1;

	case EVP_PKEY_CTRL_RSA_KEYGEN_PUBEXP:
		if (p2 == NULL || !BN_is_odd((BIGNUM *)p2) ||
		    BN_is_one((BIGNUM *)p2)) {
			RSAerror(RSA_R_BAD_E_VALUE);
			return -2;
		}
		BN_free(rctx->pub_exp);
		rctx->pub_exp = p2;
		return 1;

	case EVP_PKEY_CTRL_RSA_OAEP_MD:
	case EVP_PKEY_CTRL_GET_RSA_OAEP_MD:
		if (rctx->pad_mode != RSA_PKCS1_OAEP_PADDING) {
			RSAerror(RSA_R_INVALID_PADDING_MODE);
			return -2;
		}
		if (type == EVP_PKEY_CTRL_GET_RSA_OAEP_MD)
			*(const EVP_MD **)p2 = rctx->md;
		else
			rctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_MD:
		if (!check_padding_md(p2, rctx->pad_mode))
			return 0;
		if (rsa_pss_restricted(rctx)) {
			if (EVP_MD_type(rctx->md) == EVP_MD_type(p2))
				return 1;
			RSAerror(RSA_R_DIGEST_NOT_ALLOWED);
			return 0;
		}
		rctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_GET_MD:
		*(const EVP_MD **)p2 = rctx->md;
		return 1;

	case EVP_PKEY_CTRL_RSA_MGF1_MD:
	case EVP_PKEY_CTRL_GET_RSA_MGF1_MD:
		if (rctx->pad_mode != RSA_PKCS1_PSS_PADDING &&
		    rctx->pad_mode != RSA_PKCS1_OAEP_PADDING) {
			RSAerror(RSA_R_INVALID_MGF1_MD);
			return -2;
		}
		if (type == EVP_PKEY_CTRL_GET_RSA_MGF1_MD) {
			if (rctx->mgf1md)
				*(const EVP_MD **)p2 = rctx->mgf1md;
			else
				*(const EVP_MD **)p2 = rctx->md;
		} else {
			if (rsa_pss_restricted(rctx)) {
				if (EVP_MD_type(rctx->mgf1md) == EVP_MD_type(p2))
					return 1;
				RSAerror(RSA_R_MGF1_DIGEST_NOT_ALLOWED);
				return 0;
			}
			rctx->mgf1md = p2;
		}
		return 1;

	case EVP_PKEY_CTRL_RSA_OAEP_LABEL:
		if (rctx->pad_mode != RSA_PKCS1_OAEP_PADDING) {
			RSAerror(RSA_R_INVALID_PADDING_MODE);
			return -2;
		}
		free(rctx->oaep_label);
		if (p2 != NULL && p1 > 0) {
			rctx->oaep_label = p2;
			rctx->oaep_labellen = p1;
		} else {
			rctx->oaep_label = NULL;
			rctx->oaep_labellen = 0;
		}
		return 1;

	case EVP_PKEY_CTRL_GET_RSA_OAEP_LABEL:
		if (rctx->pad_mode != RSA_PKCS1_OAEP_PADDING) {
			RSAerror(RSA_R_INVALID_PADDING_MODE);
			return -2;
		}
		*(unsigned char **)p2 = rctx->oaep_label;
		return rctx->oaep_labellen;

	case EVP_PKEY_CTRL_DIGESTINIT:
	case EVP_PKEY_CTRL_PKCS7_SIGN:
#ifndef OPENSSL_NO_CMS
	case EVP_PKEY_CTRL_CMS_SIGN:
#endif
		return 1;

	case EVP_PKEY_CTRL_PKCS7_ENCRYPT:
	case EVP_PKEY_CTRL_PKCS7_DECRYPT:
#ifndef OPENSSL_NO_CMS
	case EVP_PKEY_CTRL_CMS_DECRYPT:
	case EVP_PKEY_CTRL_CMS_ENCRYPT:
#endif
		if (ctx->pmeth->pkey_id != EVP_PKEY_RSA_PSS)
			return 1;

	/* fall through */
	case EVP_PKEY_CTRL_PEER_KEY:
		RSAerror(RSA_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;

	default:
		return -2;

	}
}

static int
pkey_rsa_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	const char *errstr;

	if (!value) {
		RSAerror(RSA_R_VALUE_MISSING);
		return 0;
	}
	if (strcmp(type, "rsa_padding_mode") == 0) {
		int pm;
		if (strcmp(value, "pkcs1") == 0)
			pm = RSA_PKCS1_PADDING;
		else if (strcmp(value, "none") == 0)
			pm = RSA_NO_PADDING;
		else if (strcmp(value, "oaep") == 0 || strcmp(value, "oeap") == 0)
			pm = RSA_PKCS1_OAEP_PADDING;
		else if (strcmp(value, "x931") == 0)
			pm = RSA_X931_PADDING;
		else if (strcmp(value, "pss") == 0)
			pm = RSA_PKCS1_PSS_PADDING;
		else {
			RSAerror(RSA_R_UNKNOWN_PADDING_TYPE);
			return -2;
		}
		return EVP_PKEY_CTX_set_rsa_padding(ctx, pm);
	}

	if (strcmp(type, "rsa_pss_saltlen") == 0) {
		int saltlen;

		if (strcmp(value, "digest") == 0)
			saltlen = RSA_PSS_SALTLEN_DIGEST;
		else if (strcmp(value, "max") == 0)
			saltlen = RSA_PSS_SALTLEN_MAX;
		else if (strcmp(value, "auto") == 0)
			saltlen = RSA_PSS_SALTLEN_AUTO;
		else {
			/*
			 * Accept the special values -1, -2, -3 since that's
			 * what atoi() historically did. Lower values are later
			 * rejected in EVP_PKEY_CTRL_RSA_PSS_SALTLEN anyway.
			 */
			saltlen = strtonum(value, -3, INT_MAX, &errstr);
			if (errstr != NULL) {
				RSAerror(RSA_R_INVALID_PSS_SALTLEN);
				return -2;
			}
		}
		return EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, saltlen);
	}

	if (strcmp(type, "rsa_keygen_bits") == 0) {
		int nbits;

		nbits = strtonum(value, 0, INT_MAX, &errstr);
		if (errstr != NULL) {
			RSAerror(RSA_R_INVALID_KEYBITS);
			return -2;
		}

		return EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, nbits);
	}

	if (strcmp(type, "rsa_keygen_pubexp") == 0) {
		BIGNUM *pubexp = NULL;
		int ret;

		if (!BN_asc2bn(&pubexp, value))
			return 0;
		ret = EVP_PKEY_CTX_set_rsa_keygen_pubexp(ctx, pubexp);
		if (ret <= 0)
			BN_free(pubexp);
		return ret;
	}

	if (strcmp(type, "rsa_mgf1_md") == 0)
		return EVP_PKEY_CTX_md(ctx,
		    EVP_PKEY_OP_TYPE_SIG | EVP_PKEY_OP_TYPE_CRYPT,
		    EVP_PKEY_CTRL_RSA_MGF1_MD, value);

	if (ctx->pmeth->pkey_id == EVP_PKEY_RSA_PSS) {
		if (strcmp(type, "rsa_pss_keygen_mgf1_md") == 0)
			return EVP_PKEY_CTX_md(ctx, EVP_PKEY_OP_KEYGEN,
			    EVP_PKEY_CTRL_RSA_MGF1_MD, value);

		if (strcmp(type, "rsa_pss_keygen_md") == 0)
			return EVP_PKEY_CTX_md(ctx, EVP_PKEY_OP_KEYGEN,
			    EVP_PKEY_CTRL_MD, value);

		if (strcmp(type, "rsa_pss_keygen_saltlen") == 0) {
			int saltlen;

			/*
			 * Accept the special values -1, -2, -3 since that's
			 * what atoi() historically did. Lower values are later
			 * rejected in EVP_PKEY_CTRL_RSA_PSS_SALTLEN anyway.
			 */
			saltlen = strtonum(value, -3, INT_MAX, &errstr);
			if (errstr != NULL) {
				RSAerror(RSA_R_INVALID_PSS_SALTLEN);
				return -2;
			}

			return EVP_PKEY_CTX_set_rsa_pss_keygen_saltlen(ctx, saltlen);
		}
	}

	if (strcmp(type, "rsa_oaep_md") == 0)
		return EVP_PKEY_CTX_md(ctx, EVP_PKEY_OP_TYPE_CRYPT,
		    EVP_PKEY_CTRL_RSA_OAEP_MD, value);

	if (strcmp(type, "rsa_oaep_label") == 0) {
		unsigned char *lab;
		long lablen;
		int ret;

		if ((lab = string_to_hex(value, &lablen)) == NULL)
			return 0;
		ret = EVP_PKEY_CTX_set0_rsa_oaep_label(ctx, lab, lablen);
		if (ret <= 0)
			free(lab);

		return ret;
	}

	return -2;
}

/* Set PSS parameters when generating a key, if necessary. */
static int
rsa_set_pss_param(RSA *rsa, EVP_PKEY_CTX *ctx)
{
	RSA_PKEY_CTX *rctx = ctx->data;

	if (ctx->pmeth->pkey_id != EVP_PKEY_RSA_PSS)
		return 1;

	/* If all parameters are default values then do not set PSS. */
	if (rctx->md == NULL && rctx->mgf1md == NULL &&
	    rctx->saltlen == RSA_PSS_SALTLEN_AUTO)
		return 1;

	rsa->pss = rsa_pss_params_create(rctx->md, rctx->mgf1md,
	    rctx->saltlen == RSA_PSS_SALTLEN_AUTO ? 0 : rctx->saltlen);
	if (rsa->pss == NULL)
		return 0;

	return 1;
}

static int
pkey_rsa_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	RSA *rsa = NULL;
	RSA_PKEY_CTX *rctx = ctx->data;
	BN_GENCB *pcb = NULL;
	BN_GENCB cb = {0};
	int ret = 0;

	if (rctx->pub_exp == NULL) {
		if ((rctx->pub_exp = BN_new()) == NULL)
			goto err;
		if (!BN_set_word(rctx->pub_exp, RSA_F4))
			goto err;
	}

	if ((rsa = RSA_new()) == NULL)
		goto err;
	if (ctx->pkey_gencb != NULL) {
		pcb = &cb;
		evp_pkey_set_cb_translate(pcb, ctx);
	}
	if (!RSA_generate_key_ex(rsa, rctx->nbits, rctx->pub_exp, pcb))
		goto err;
	if (!rsa_set_pss_param(rsa, ctx))
		goto err;
	if (!EVP_PKEY_assign(pkey, ctx->pmeth->pkey_id, rsa))
		goto err;
	rsa = NULL;

	ret = 1;

 err:
	RSA_free(rsa);

	return ret;
}

const EVP_PKEY_METHOD rsa_pkey_meth = {
	.pkey_id = EVP_PKEY_RSA,
	.flags = EVP_PKEY_FLAG_AUTOARGLEN,

	.init = pkey_rsa_init,
	.copy = pkey_rsa_copy,
	.cleanup = pkey_rsa_cleanup,

	.keygen = pkey_rsa_keygen,

	.sign = pkey_rsa_sign,

	.verify = pkey_rsa_verify,

	.verify_recover = pkey_rsa_verifyrecover,

	.encrypt = pkey_rsa_encrypt,

	.decrypt = pkey_rsa_decrypt,

	.ctrl = pkey_rsa_ctrl,
	.ctrl_str = pkey_rsa_ctrl_str
};

/*
 * Called for PSS sign or verify initialisation: checks PSS parameter
 * sanity and sets any restrictions on key usage.
 */

static int
pkey_pss_init(EVP_PKEY_CTX *ctx)
{
	RSA *rsa;
	RSA_PKEY_CTX *rctx = ctx->data;
	const EVP_MD *md;
	const EVP_MD *mgf1md;
	int min_saltlen, max_saltlen;

	/* Should never happen */
	if (ctx->pmeth->pkey_id != EVP_PKEY_RSA_PSS)
		return 0;
	rsa = ctx->pkey->pkey.rsa;

	/* If no restrictions just return */
	if (rsa->pss == NULL)
		return 1;

	/* Get and check parameters */
	if (!rsa_pss_get_param(rsa->pss, &md, &mgf1md, &min_saltlen))
		return 0;

	/* See if minimum salt length exceeds maximum possible */
	max_saltlen = RSA_size(rsa) - EVP_MD_size(md);
	if ((RSA_bits(rsa) & 0x7) == 1)
		max_saltlen--;
	if (min_saltlen > max_saltlen) {
		RSAerror(RSA_R_INVALID_SALT_LENGTH);
		return 0;
	}
	rctx->min_saltlen = min_saltlen;

	/*
	 * Set PSS restrictions as defaults: we can then block any attempt to
	 * use invalid values in pkey_rsa_ctrl
	 */

	rctx->md = md;
	rctx->mgf1md = mgf1md;
	rctx->saltlen = min_saltlen;

	return 1;
}

const EVP_PKEY_METHOD rsa_pss_pkey_meth = {
	.pkey_id = EVP_PKEY_RSA_PSS,
	.flags = EVP_PKEY_FLAG_AUTOARGLEN,

	.init = pkey_rsa_init,
	.copy = pkey_rsa_copy,
	.cleanup = pkey_rsa_cleanup,

	.keygen = pkey_rsa_keygen,

	.sign_init = pkey_pss_init,
	.sign = pkey_rsa_sign,

	.verify_init = pkey_pss_init,
	.verify = pkey_rsa_verify,

	.ctrl = pkey_rsa_ctrl,
	.ctrl_str = pkey_rsa_ctrl_str
};
