/*	$OpenBSD: ct_b64.c,v 1.8 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <string.h>

#include <openssl/ct.h>
#include <openssl/evp.h>

#include "bytestring.h"
#include "ct_local.h"
#include "err_local.h"

/*
 * Decodes the base64 string |in| into |out|.
 * A new string will be malloc'd and assigned to |out|. This will be owned by
 * the caller. Do not provide a pre-allocated string in |out|.
 */
static int
ct_base64_decode(const char *in, unsigned char **out)
{
	size_t inlen = strlen(in);
	int outlen, i;
	unsigned char *outbuf = NULL;

	if (inlen == 0) {
		*out = NULL;
		return 0;
	}

	outlen = (inlen / 4) * 3;
	outbuf = malloc(outlen);
	if (outbuf == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	outlen = EVP_DecodeBlock(outbuf, (unsigned char *)in, inlen);
	if (outlen < 0) {
		CTerror(CT_R_BASE64_DECODE_ERROR);
		goto err;
	}

	/*
	 * Subtract padding bytes from |outlen|.
	 * Any more than 2 is malformed.
	 */
	i = 0;
	while (in[--inlen] == '=') {
		--outlen;
		if (++i > 2)
			goto err;
	}

	*out = outbuf;
	return outlen;
 err:
	free(outbuf);
	return -1;
}

SCT *
SCT_new_from_base64(unsigned char version, const char *logid_base64,
    ct_log_entry_type_t entry_type, uint64_t timestamp,
    const char *extensions_base64, const char *signature_base64)
{
	unsigned char *dec = NULL;
	int declen;
	SCT *sct;
	CBS cbs;

	if ((sct = SCT_new()) == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	/*
	 * RFC6962 section 4.1 says we "MUST NOT expect this to be 0", but we
	 * can only construct SCT versions that have been defined.
	 */
	if (!SCT_set_version(sct, version)) {
		CTerror(CT_R_SCT_UNSUPPORTED_VERSION);
		goto err;
	}

	declen = ct_base64_decode(logid_base64, &dec);
	if (declen < 0) {
		CTerror(X509_R_BASE64_DECODE_ERROR);
		goto err;
	}
	if (!SCT_set0_log_id(sct, dec, declen))
		goto err;
	dec = NULL;

	declen = ct_base64_decode(extensions_base64, &dec);
	if (declen < 0) {
		CTerror(X509_R_BASE64_DECODE_ERROR);
		goto err;
	}
	SCT_set0_extensions(sct, dec, declen);
	dec = NULL;

	declen = ct_base64_decode(signature_base64, &dec);
	if (declen < 0) {
		CTerror(X509_R_BASE64_DECODE_ERROR);
		goto err;
	}

	CBS_init(&cbs, dec, declen);
	if (!o2i_SCT_signature(sct, &cbs))
		goto err;
	free(dec);
	dec = NULL;

	SCT_set_timestamp(sct, timestamp);

	if (!SCT_set_log_entry_type(sct, entry_type))
		goto err;

	return sct;

 err:
	free(dec);
	SCT_free(sct);
	return NULL;
}
LCRYPTO_ALIAS(SCT_new_from_base64);

/*
 * Allocate, build and returns a new |ct_log| from input |pkey_base64|
 * It returns 1 on success,
 * 0 on decoding failure, or invalid parameter if any
 * -1 on internal (malloc) failure
 */
int
CTLOG_new_from_base64(CTLOG **ct_log, const char *pkey_base64, const char *name)
{
	unsigned char *pkey_der = NULL;
	int pkey_der_len;
	const unsigned char *p;
	EVP_PKEY *pkey = NULL;

	if (ct_log == NULL) {
	        CTerror(ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}

	pkey_der_len = ct_base64_decode(pkey_base64, &pkey_der);
	if (pkey_der_len < 0) {
		CTerror(CT_R_LOG_CONF_INVALID_KEY);
		return 0;
	}

	p = pkey_der;
	pkey = d2i_PUBKEY(NULL, &p, pkey_der_len);
	free(pkey_der);
	if (pkey == NULL) {
		CTerror(CT_R_LOG_CONF_INVALID_KEY);
		return 0;
	}

	*ct_log = CTLOG_new(pkey, name);
	if (*ct_log == NULL) {
		EVP_PKEY_free(pkey);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(CTLOG_new_from_base64);
