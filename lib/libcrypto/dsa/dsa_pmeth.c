/* $OpenBSD: dsa_pmeth.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "bn_local.h"
#include "dsa_local.h"
#include "err_local.h"
#include "evp_local.h"

/* DSA pkey context structure */

typedef struct {
	/* Parameter gen parameters */
	int nbits;		/* size of p in bits (default: 1024) */
	int qbits;		/* size of q in bits (default: 160)  */
	const EVP_MD *pmd;	/* MD for parameter generation */
	/* Keygen callback info */
	int gentmp[2];
	/* message digest */
	const EVP_MD *md;	/* MD for the signature */
} DSA_PKEY_CTX;

static int
pkey_dsa_init(EVP_PKEY_CTX *ctx)
{
	DSA_PKEY_CTX *dctx;

	dctx = malloc(sizeof(DSA_PKEY_CTX));
	if (!dctx)
		return 0;
	dctx->nbits = 1024;
	dctx->qbits = 160;
	dctx->pmd = NULL;
	dctx->md = NULL;

	ctx->data = dctx;
	ctx->keygen_info = dctx->gentmp;
	ctx->keygen_info_count = 2;

	return 1;
}

static int
pkey_dsa_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	DSA_PKEY_CTX *dctx, *sctx;

	if (!pkey_dsa_init(dst))
		return 0;
	sctx = src->data;
	dctx = dst->data;
	dctx->nbits = sctx->nbits;
	dctx->qbits = sctx->qbits;
	dctx->pmd = sctx->pmd;
	dctx->md  = sctx->md;
	return 1;
}

static void
pkey_dsa_cleanup(EVP_PKEY_CTX *ctx)
{
	DSA_PKEY_CTX *dctx = ctx->data;

	free(dctx);
}

static int
pkey_dsa_sign(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *out_siglen,
    const unsigned char *tbs, size_t tbslen)
{
	DSA *dsa = ctx->pkey->pkey.dsa;
	DSA_PKEY_CTX *dctx = ctx->data;
	unsigned int siglen;

	*out_siglen = 0;

	if (tbslen > INT_MAX)
		 return 0;

	if (dctx->md != NULL) {
		if (tbslen != EVP_MD_size(dctx->md))
			return 0;
	}

	if (!DSA_sign(0, tbs, tbslen, sig, &siglen, dsa))
		return 0;

	*out_siglen = siglen;

	return 1;
}

static int
pkey_dsa_verify(EVP_PKEY_CTX *ctx, const unsigned char *sig, size_t siglen,
    const unsigned char *tbs, size_t tbslen)
{
	DSA *dsa = ctx->pkey->pkey.dsa;
	DSA_PKEY_CTX *dctx = ctx->data;

	if (tbslen > INT_MAX || siglen > INT_MAX)
		 return 0;

	if (dctx->md != NULL) {
		if (tbslen != EVP_MD_size(dctx->md))
			return 0;
	}

	return DSA_verify(0, tbs, tbslen, sig, siglen, dsa);
}

static int
pkey_dsa_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	DSA_PKEY_CTX *dctx = ctx->data;

	switch (type) {
	case EVP_PKEY_CTRL_DSA_PARAMGEN_BITS:
		if (p1 < 256)
			return -2;
		dctx->nbits = p1;
		return 1;

	case EVP_PKEY_CTRL_DSA_PARAMGEN_Q_BITS:
		if (p1 != 160 && p1 != 224 && p1 && p1 != 256)
			return -2;
		dctx->qbits = p1;
		return 1;

	case EVP_PKEY_CTRL_DSA_PARAMGEN_MD:
		switch (EVP_MD_type((const EVP_MD *)p2)) {
		case NID_sha1:
		case NID_sha224:
		case NID_sha256:
			break;
		default:
			DSAerror(DSA_R_INVALID_DIGEST_TYPE);
			return 0;
		}
		dctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_MD:
		/* ANSI X9.57 and NIST CSOR. */
		switch (EVP_MD_type(p2)) {
		case NID_sha1:
		case NID_dsa:
		case NID_dsaWithSHA:
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
			DSAerror(DSA_R_INVALID_DIGEST_TYPE);
			return 0;
		}
		dctx->md = p2;
		return 1;

	case EVP_PKEY_CTRL_GET_MD:
		*(const EVP_MD **)p2 = dctx->md;
		return 1;

	case EVP_PKEY_CTRL_DIGESTINIT:
	case EVP_PKEY_CTRL_PKCS7_SIGN:
	case EVP_PKEY_CTRL_CMS_SIGN:
		return 1;

	case EVP_PKEY_CTRL_PEER_KEY:
		DSAerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return -2;
	default:
		return -2;
	}
}

static int
pkey_dsa_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	const char *errstr;

	if (!strcmp(type, "dsa_paramgen_bits")) {
		int nbits;

		nbits = strtonum(value, INT_MIN, INT_MAX, &errstr);
		if (errstr != NULL)
			return -2;
		return EVP_PKEY_CTX_set_dsa_paramgen_bits(ctx, nbits);
	} else if (!strcmp(type, "dsa_paramgen_q_bits")) {
		int qbits;

		qbits = strtonum(value, INT_MIN, INT_MAX, &errstr);
		if (errstr != NULL)
			return -2;
		return EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DSA,
		    EVP_PKEY_OP_PARAMGEN, EVP_PKEY_CTRL_DSA_PARAMGEN_Q_BITS,
		    qbits, NULL);
	} else if (!strcmp(type, "dsa_paramgen_md")) {
		return EVP_PKEY_CTX_ctrl(ctx, EVP_PKEY_DSA,
		    EVP_PKEY_OP_PARAMGEN, EVP_PKEY_CTRL_DSA_PARAMGEN_MD, 0,
		    (void *)EVP_get_digestbyname(value));
	}

	return -2;
}

static int
pkey_dsa_paramgen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	DSA *dsa;
	DSA_PKEY_CTX *dctx = ctx->data;
	BN_GENCB *pcb = NULL;
	BN_GENCB cb = {0};
	int ret = 0;

	if ((dsa = DSA_new()) == NULL)
		goto err;
	if (ctx->pkey_gencb != NULL) {
		pcb = &cb;
		evp_pkey_set_cb_translate(pcb, ctx);
	}
	if (!dsa_builtin_paramgen(dsa, dctx->nbits, dctx->qbits, dctx->pmd,
	    NULL, 0, NULL, NULL, NULL, pcb))
		goto err;
	if (!EVP_PKEY_assign_DSA(pkey, dsa))
		goto err;
	dsa = NULL;

	ret = 1;

 err:
	DSA_free(dsa);

	return ret;
}

static int
pkey_dsa_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	DSA *dsa = NULL;
	int ret = 0;

	if (ctx->pkey == NULL) {
		DSAerror(DSA_R_NO_PARAMETERS_SET);
		goto err;
	}
	if ((dsa = DSA_new()) == NULL)
		goto err;
	if (!EVP_PKEY_set1_DSA(pkey, dsa))
		goto err;

	if (!EVP_PKEY_copy_parameters(pkey, ctx->pkey))
		goto err;
	if (!DSA_generate_key(dsa))
		goto err;

	ret = 1;

 err:
	DSA_free(dsa);

	return ret;
}

const EVP_PKEY_METHOD dsa_pkey_meth = {
	.pkey_id = EVP_PKEY_DSA,
	.flags = EVP_PKEY_FLAG_AUTOARGLEN,

	.init = pkey_dsa_init,
	.copy = pkey_dsa_copy,
	.cleanup = pkey_dsa_cleanup,

	.paramgen = pkey_dsa_paramgen,

	.keygen = pkey_dsa_keygen,

	.sign = pkey_dsa_sign,

	.verify = pkey_dsa_verify,

	.ctrl = pkey_dsa_ctrl,
	.ctrl_str = pkey_dsa_ctrl_str
};
