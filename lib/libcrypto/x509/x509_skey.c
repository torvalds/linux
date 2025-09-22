/* $OpenBSD: x509_skey.c,v 1.7 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static ASN1_OCTET_STRING *s2i_skey_id(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, char *str);

static const X509V3_EXT_METHOD x509v3_ext_subject_key_identifier = {
	.ext_nid = NID_subject_key_identifier,
	.ext_flags = 0,
	.it = &ASN1_OCTET_STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_OCTET_STRING,
	.s2i = (X509V3_EXT_S2I)s2i_skey_id,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_subject_key_identifier(void)
{
	return &x509v3_ext_subject_key_identifier;
}

char *
i2s_ASN1_OCTET_STRING(X509V3_EXT_METHOD *method, const ASN1_OCTET_STRING *oct)
{
	return hex_to_string(oct->data, oct->length);
}
LCRYPTO_ALIAS(i2s_ASN1_OCTET_STRING);

ASN1_OCTET_STRING *
s2i_ASN1_OCTET_STRING(X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    const char *str)
{
	ASN1_OCTET_STRING *oct;
	long length;

	if (!(oct = ASN1_OCTET_STRING_new())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (!(oct->data = string_to_hex(str, &length))) {
		ASN1_OCTET_STRING_free(oct);
		return NULL;
	}

	oct->length = length;

	return oct;
}
LCRYPTO_ALIAS(s2i_ASN1_OCTET_STRING);

static ASN1_OCTET_STRING *
s2i_skey_id(X509V3_EXT_METHOD *method, X509V3_CTX *ctx, char *str)
{
	ASN1_OCTET_STRING *oct;
	ASN1_BIT_STRING *pk;
	unsigned char pkey_dig[EVP_MAX_MD_SIZE];
	unsigned int diglen;

	if (strcmp(str, "hash"))
		return s2i_ASN1_OCTET_STRING(method, ctx, str);

	if (!(oct = ASN1_OCTET_STRING_new())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (ctx && (ctx->flags == CTX_TEST))
		return oct;

	if (!ctx || (!ctx->subject_req && !ctx->subject_cert)) {
		X509V3error(X509V3_R_NO_PUBLIC_KEY);
		goto err;
	}

	if (ctx->subject_req)
		pk = ctx->subject_req->req_info->pubkey->public_key;
	else
		pk = ctx->subject_cert->cert_info->key->public_key;

	if (!pk) {
		X509V3error(X509V3_R_NO_PUBLIC_KEY);
		goto err;
	}

	if (!EVP_Digest(pk->data, pk->length, pkey_dig, &diglen,
	    EVP_sha1(), NULL))
		goto err;

	if (!ASN1_STRING_set(oct, pkey_dig, diglen)) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	return oct;

err:
	ASN1_OCTET_STRING_free(oct);
	return NULL;
}
