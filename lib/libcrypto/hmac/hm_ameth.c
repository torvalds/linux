/* $OpenBSD: hm_ameth.c,v 1.20 2024/01/04 17:01:26 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2007.
 */
/* ====================================================================
 * Copyright (c) 2007 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "evp_local.h"
#include "hmac_local.h"

static int
hmac_pkey_public_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	/* The ameth pub_cmp must return 1 on match, 0 on mismatch. */
	return ASN1_OCTET_STRING_cmp(a->pkey.ptr, b->pkey.ptr) == 0;
}

static int
hmac_size(const EVP_PKEY *pkey)
{
	return EVP_MAX_MD_SIZE;
}

static void
hmac_key_free(EVP_PKEY *pkey)
{
	ASN1_OCTET_STRING *os;

	if ((os = pkey->pkey.ptr) == NULL)
		return;

	if (os->data != NULL)
		explicit_bzero(os->data, os->length);

	ASN1_OCTET_STRING_free(os);
}

static int
hmac_pkey_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
	switch (op) {
	case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
		*(int *)arg2 = NID_sha1;
		return 1;
	default:
		return -2;
	}
}

static int
hmac_set_priv_key(EVP_PKEY *pkey, const unsigned char *priv, size_t len)
{
	ASN1_OCTET_STRING *os = NULL;

	if (pkey->pkey.ptr != NULL)
		goto err;

	if (len > INT_MAX)
		goto err;

	if ((os = ASN1_OCTET_STRING_new()) == NULL)
		goto err;

	if (!ASN1_OCTET_STRING_set(os, priv, len))
		goto err;

	pkey->pkey.ptr = os;

	return 1;

 err:
	ASN1_OCTET_STRING_free(os);

	return 0;
}

static int
hmac_get_priv_key(const EVP_PKEY *pkey, unsigned char *priv, size_t *len)
{
	ASN1_OCTET_STRING *os;
	CBS cbs;

	if ((os = pkey->pkey.ptr) == NULL)
		return 0;

	if (priv == NULL) {
		*len = os->length;
		return 1;
	}

	CBS_init(&cbs, os->data, os->length);
	return CBS_write_bytes(&cbs, priv, *len, len);
}

const EVP_PKEY_ASN1_METHOD hmac_asn1_meth = {
	.base_method = &hmac_asn1_meth,
	.pkey_id = EVP_PKEY_HMAC,

	.pem_str = "HMAC",
	.info = "OpenSSL HMAC method",

	.pub_cmp = hmac_pkey_public_cmp,

	.pkey_size = hmac_size,

	.pkey_free = hmac_key_free,
	.pkey_ctrl = hmac_pkey_ctrl,

	.set_priv_key = hmac_set_priv_key,
	.get_priv_key = hmac_get_priv_key,
};
