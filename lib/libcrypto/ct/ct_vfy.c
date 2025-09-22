/*	$OpenBSD: ct_vfy.c,v 1.7 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Rob Stradling (rob@comodo.com) and Stephen Henson
 * (steve@openssl.org) for the OpenSSL project 2014.
 */
/* ====================================================================
 * Copyright (c) 2014 The OpenSSL Project.  All rights reserved.
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

#include <string.h>

#include <openssl/ct.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "ct_local.h"
#include "err_local.h"

typedef enum sct_signature_type_t {
	SIGNATURE_TYPE_NOT_SET = -1,
	SIGNATURE_TYPE_CERT_TIMESTAMP,
	SIGNATURE_TYPE_TREE_HASH
} SCT_SIGNATURE_TYPE;

/*
 * Update encoding for SCT signature verification/generation to supplied
 * EVP_MD_CTX.
 */
static int
sct_ctx_update(EVP_MD_CTX *ctx, const SCT_CTX *sctx, const SCT *sct)
{
	CBB cbb, entry, extensions;
	uint8_t *data = NULL;
	size_t data_len;
	int ret = 0;

	memset(&cbb, 0, sizeof(cbb));

	if (sct->entry_type == CT_LOG_ENTRY_TYPE_NOT_SET)
		goto err;
	if (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT && sctx->ihash == NULL)
		goto err;

	if (!CBB_init(&cbb, 0))
		goto err;

	/*
	 * Build the digitally-signed struct per RFC 6962 section 3.2.
	 */
	if (!CBB_add_u8(&cbb, sct->version))
		goto err;
	if (!CBB_add_u8(&cbb, SIGNATURE_TYPE_CERT_TIMESTAMP))
		goto err;
	if (!CBB_add_u64(&cbb, sct->timestamp))
		goto err;
	if (!CBB_add_u16(&cbb, sct->entry_type))
		goto err;

	if (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT) {
		if (!CBB_add_bytes(&cbb, sctx->ihash, sctx->ihashlen))
			goto err;
	}

	if (!CBB_add_u24_length_prefixed(&cbb, &entry))
		goto err;
	if (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT) {
		if (sctx->preder == NULL)
			goto err;
		if (!CBB_add_bytes(&entry, sctx->preder, sctx->prederlen))
			goto err;
	} else {
		if (sctx->certder == NULL)
			goto err;
		if (!CBB_add_bytes(&entry, sctx->certder, sctx->certderlen))
			goto err;
	}

	if (!CBB_add_u16_length_prefixed(&cbb, &extensions))
		goto err;
	if (sct->ext_len > 0) {
		if (!CBB_add_bytes(&extensions, sct->ext, sct->ext_len))
			goto err;
	}

	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	if (!EVP_DigestUpdate(ctx, data, data_len))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);
	free(data);

	return ret;
}

int
SCT_CTX_verify(const SCT_CTX *sctx, const SCT *sct)
{
	EVP_MD_CTX *ctx = NULL;
	int ret = 0;

	if (!SCT_is_complete(sct) || sctx->pkey == NULL ||
	    sct->entry_type == CT_LOG_ENTRY_TYPE_NOT_SET ||
	    (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT &&
	    sctx->ihash == NULL)) {
		CTerror(CT_R_SCT_NOT_SET);
		return 0;
	}
	if (sct->version != SCT_VERSION_V1) {
		CTerror(CT_R_SCT_UNSUPPORTED_VERSION);
		return 0;
	}
	if (sct->log_id_len != sctx->pkeyhashlen ||
	    memcmp(sct->log_id, sctx->pkeyhash, sctx->pkeyhashlen) != 0) {
		CTerror(CT_R_SCT_LOG_ID_MISMATCH);
		return 0;
	}
	if (sct->timestamp > sctx->epoch_time_in_ms) {
		CTerror(CT_R_SCT_FUTURE_TIMESTAMP);
		return 0;
	}

	if ((ctx = EVP_MD_CTX_new()) == NULL)
		goto end;

	if (!EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, sctx->pkey))
		goto end;

	if (!sct_ctx_update(ctx, sctx, sct))
		goto end;

	/* Verify signature */
	/* If ret < 0 some other error: fall through without setting error */
	if ((ret = EVP_DigestVerifyFinal(ctx, sct->sig, sct->sig_len)) == 0)
		CTerror(CT_R_SCT_INVALID_SIGNATURE);

 end:
	EVP_MD_CTX_free(ctx);

	return ret;
}
