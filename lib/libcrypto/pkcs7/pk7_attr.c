/* $OpenBSD: pk7_attr.c,v 1.22 2025/07/31 02:24:21 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 2001-2004 The OpenSSL Project.  All rights reserved.
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

#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "x509_local.h"

int
PKCS7_add_attrib_smimecap(PKCS7_SIGNER_INFO *si, STACK_OF(X509_ALGOR) *cap)
{
	ASN1_STRING *seq = NULL;
	unsigned char *data = NULL;
	int len = 0;
	int ret = 0;

	if ((len = i2d_X509_ALGORS(cap, &data)) <= 0) {
		len = 0;
		goto err;
	}

	if ((seq = ASN1_STRING_new()) == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	ASN1_STRING_set0(seq, data, len);
	data = NULL;
	len = 0;

	if (!PKCS7_add_signed_attribute(si, NID_SMIMECapabilities,
	    V_ASN1_SEQUENCE, seq))
		goto err;
	seq = NULL;

	ret = 1;

 err:
	ASN1_STRING_free(seq);
	freezero(data, len);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_add_attrib_smimecap);

STACK_OF(X509_ALGOR) *
PKCS7_get_smimecap(PKCS7_SIGNER_INFO *si)
{
	ASN1_TYPE *cap;
	const unsigned char *p;
	int len;

	if ((cap = PKCS7_get_signed_attribute(si, NID_SMIMECapabilities)) == NULL)
		return NULL;
	if (cap->type != V_ASN1_SEQUENCE)
		return NULL;

	p = cap->value.sequence->data;
	len = cap->value.sequence->length;

	return d2i_X509_ALGORS(NULL, &p, len);
}
LCRYPTO_ALIAS(PKCS7_get_smimecap);

/*
 * Add AlgorithmIdentifier OID of type |nid| to the SMIMECapability attribute
 * set |sk| (see RFC 3851, section 2.5.2). If keysize > 0, the OID has an
 * integer parameter of value |keysize|, otherwise parameters are omitted.
 *
 * See also CMS_add_simple_smimecap().
 */
int
PKCS7_simple_smimecap(STACK_OF(X509_ALGOR) *sk, int nid, int keysize)
{
	X509_ALGOR *alg = NULL;
	ASN1_INTEGER *parameter = NULL;
	int parameter_type = V_ASN1_UNDEF;
	int ret = 0;

	if (keysize > 0) {
		if ((parameter = ASN1_INTEGER_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(parameter, keysize))
			goto err;
		parameter_type = V_ASN1_INTEGER;
	}

	if ((alg = X509_ALGOR_new()) == NULL)
		goto err;
	if (!X509_ALGOR_set0_by_nid(alg, nid, parameter_type, parameter))
		goto err;
	parameter = NULL;

	if (sk_X509_ALGOR_push(sk, alg) <= 0)
		goto err;
	alg = NULL;

	ret = 1;

 err:
	X509_ALGOR_free(alg);
	ASN1_INTEGER_free(parameter);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_simple_smimecap);

int
PKCS7_add_attrib_content_type(PKCS7_SIGNER_INFO *si, ASN1_OBJECT *coid)
{
	if (PKCS7_get_signed_attribute(si, NID_pkcs9_contentType))
		return 0;
	if (!coid)
		coid = OBJ_nid2obj(NID_pkcs7_data);
	return PKCS7_add_signed_attribute(si, NID_pkcs9_contentType,
	    V_ASN1_OBJECT, coid);
}
LCRYPTO_ALIAS(PKCS7_add_attrib_content_type);

int
PKCS7_add0_attrib_signing_time(PKCS7_SIGNER_INFO *si, ASN1_TIME *t)
{
	ASN1_TIME *tm;
	int ret = 0;

	if ((tm = t) == NULL)
		tm = X509_gmtime_adj(NULL, 0);
	if (tm == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* RFC 5652, section 11.3 - UTCTime for the years 1950-2049. */
	if (ASN1_time_parse(tm->data, tm->length, NULL, tm->type) == -1)
		goto err;
	if (!PKCS7_add_signed_attribute(si, NID_pkcs9_signingTime, tm->type, tm))
		goto err;
	tm = NULL;

	ret = 1;

 err:
	if (tm != t)
		ASN1_TIME_free(tm);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_add0_attrib_signing_time);

int
PKCS7_add1_attrib_digest(PKCS7_SIGNER_INFO *si, const unsigned char *md,
    int md_len)
{
	ASN1_OCTET_STRING *os;
	int ret = 0;

	if ((os = ASN1_OCTET_STRING_new()) == NULL)
		goto err;
	if (!ASN1_STRING_set(os, md, md_len))
		goto err;
	if (!PKCS7_add_signed_attribute(si, NID_pkcs9_messageDigest,
	    V_ASN1_OCTET_STRING, os))
		goto err;
	os = NULL;

	ret = 1;

 err:
	ASN1_OCTET_STRING_free(os);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_add1_attrib_digest);
