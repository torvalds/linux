/* $OpenBSD: ocsp_lib.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
/* Written by Tom Titchener <Tom_Titchener@groove.net> for the OpenSSL
 * project. */

/* History:
   This file was transfered to Richard Levitte from CertCo by Kathy
   Weinhold in mid-spring 2000 to be included in OpenSSL or released
   as a patch kit. */

/* ====================================================================
 * Copyright (c) 1998-2000 The OpenSSL Project.  All rights reserved.
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

#include <stdio.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1t.h>
#include <openssl/objects.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "ocsp_local.h"
#include "x509_local.h"

/* Convert a certificate and its issuer to an OCSP_CERTID */

OCSP_CERTID *
OCSP_cert_to_id(const EVP_MD *dgst, const X509 *subject, const X509 *issuer)
{
	X509_NAME *iname;
	const ASN1_INTEGER *serial;
	ASN1_BIT_STRING *ikey;

#ifndef OPENSSL_NO_SHA1
	if (!dgst)
		dgst = EVP_sha1();
#endif
	if (subject) {
		iname = X509_get_issuer_name(subject);
		serial = X509_get0_serialNumber(subject);
	} else {
		iname = X509_get_subject_name(issuer);
		serial = NULL;
	}
	if ((ikey = X509_get0_pubkey_bitstr(issuer)) == NULL)
		return NULL;

	return OCSP_cert_id_new(dgst, iname, ikey, serial);
}
LCRYPTO_ALIAS(OCSP_cert_to_id);

OCSP_CERTID *
OCSP_cert_id_new(const EVP_MD *dgst, const X509_NAME *issuerName,
    const ASN1_BIT_STRING *issuerKey, const ASN1_INTEGER *serialNumber)
{
	int nid;
	unsigned int i;
	OCSP_CERTID *cid = NULL;
	unsigned char md[EVP_MAX_MD_SIZE];

	if ((cid = OCSP_CERTID_new()) == NULL)
		goto err;

	if ((nid = EVP_MD_type(dgst)) == NID_undef) {
		OCSPerror(OCSP_R_UNKNOWN_NID);
		goto err;
	}
	if (!X509_ALGOR_set0_by_nid(cid->hashAlgorithm, nid, V_ASN1_NULL, NULL))
		goto err;

	if (!X509_NAME_digest(issuerName, dgst, md, &i)) {
		OCSPerror(OCSP_R_DIGEST_ERR);
		goto err;
	}
	if (!ASN1_OCTET_STRING_set(cid->issuerNameHash, md, i))
		goto err;

	/* Calculate the issuerKey hash, excluding tag and length */
	if (!EVP_Digest(issuerKey->data, issuerKey->length, md, &i, dgst, NULL))
		goto err;

	if (!ASN1_OCTET_STRING_set(cid->issuerKeyHash, md, i))
		goto err;

	if (serialNumber != NULL) {
		ASN1_INTEGER_free(cid->serialNumber);
		if ((cid->serialNumber = ASN1_INTEGER_dup(serialNumber)) == NULL)
			goto err;
	}

	return cid;

 err:
	OCSP_CERTID_free(cid);

	return NULL;
}
LCRYPTO_ALIAS(OCSP_cert_id_new);

int
OCSP_id_issuer_cmp(OCSP_CERTID *a, OCSP_CERTID *b)
{
	int ret;

	/*
	 * XXX - should we really ignore parameters here? We probably need to
	 * consider omitted parameters and explicit ASN.1 NULL as equal for
	 * the SHAs, so don't blindly switch to X509_ALGOR_cmp().
	 */
	ret = OBJ_cmp(a->hashAlgorithm->algorithm, b->hashAlgorithm->algorithm);
	if (ret)
		return ret;
	ret = ASN1_OCTET_STRING_cmp(a->issuerNameHash, b->issuerNameHash);
	if (ret)
		return ret;
	return ASN1_OCTET_STRING_cmp(a->issuerKeyHash, b->issuerKeyHash);
}
LCRYPTO_ALIAS(OCSP_id_issuer_cmp);

int
OCSP_id_cmp(OCSP_CERTID *a, OCSP_CERTID *b)
{
	int ret;

	ret = OCSP_id_issuer_cmp(a, b);
	if (ret)
		return ret;
	return ASN1_INTEGER_cmp(a->serialNumber, b->serialNumber);
}
LCRYPTO_ALIAS(OCSP_id_cmp);

/* Parse a URL and split it up into host, port and path components and whether
 * it is SSL.
 */
int
OCSP_parse_url(const char *url, char **phost, char **pport, char **ppath,
    int *pssl)
{
	char *host, *path, *port, *tmp;

	*phost = *pport = *ppath = NULL;
	*pssl = 0;

	if (strncmp(url, "https://", 8) == 0) {
		*pssl = 1;
		host = strdup(url + 8);
	} else if (strncmp(url, "http://", 7) == 0)
		host = strdup(url + 7);
	else {
		OCSPerror(OCSP_R_ERROR_PARSING_URL);
		return 0;
	}
	if (host == NULL) {
		OCSPerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	if ((tmp = strchr(host, '/')) != NULL) {
		path = strdup(tmp);
		*tmp = '\0';
	} else
		path = strdup("/");

	if ((tmp = strchr(host, ':')) != NULL ) {
		port = strdup(tmp + 1);
		*tmp = '\0';
	} else {
		if (*pssl)
			port = strdup("443");
		else
			port = strdup("80");
	}

	if (path == NULL || port == NULL) {
		free(host);
		free(path);
		free(port);
		OCSPerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	*phost = host;
	*ppath = path;
	*pport = port;
	return 1;
}
LCRYPTO_ALIAS(OCSP_parse_url);

OCSP_CERTID *
OCSP_CERTID_dup(OCSP_CERTID *x)
{
	return ASN1_item_dup(&OCSP_CERTID_it, x);
}
LCRYPTO_ALIAS(OCSP_CERTID_dup);
