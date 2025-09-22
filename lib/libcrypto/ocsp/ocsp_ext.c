/* $OpenBSD: ocsp_ext.c,v 1.23 2023/07/08 10:44:00 beck Exp $ */
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
#include <stdlib.h>
#include <string.h>

#include <openssl/objects.h>
#include <openssl/ocsp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "ocsp_local.h"
#include "x509_local.h"

/* Standard wrapper functions for extensions */

/* OCSP request extensions */

int
OCSP_REQUEST_get_ext_count(OCSP_REQUEST *x)
{
	return X509v3_get_ext_count(x->tbsRequest->requestExtensions);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get_ext_count);

int
OCSP_REQUEST_get_ext_by_NID(OCSP_REQUEST *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->tbsRequest->requestExtensions, nid,
	    lastpos);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get_ext_by_NID);

int
OCSP_REQUEST_get_ext_by_OBJ(OCSP_REQUEST *x, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->tbsRequest->requestExtensions, obj,
	    lastpos);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get_ext_by_OBJ);

int
OCSP_REQUEST_get_ext_by_critical(OCSP_REQUEST *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->tbsRequest->requestExtensions,
	    crit, lastpos);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get_ext_by_critical);

X509_EXTENSION *
OCSP_REQUEST_get_ext(OCSP_REQUEST *x, int loc)
{
	return X509v3_get_ext(x->tbsRequest->requestExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get_ext);

X509_EXTENSION *
OCSP_REQUEST_delete_ext(OCSP_REQUEST *x, int loc)
{
	return X509v3_delete_ext(x->tbsRequest->requestExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_REQUEST_delete_ext);

void *
OCSP_REQUEST_get1_ext_d2i(OCSP_REQUEST *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->tbsRequest->requestExtensions, nid, crit, idx);
}
LCRYPTO_ALIAS(OCSP_REQUEST_get1_ext_d2i);

int
OCSP_REQUEST_add1_ext_i2d(OCSP_REQUEST *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->tbsRequest->requestExtensions, nid, value,
	    crit, flags);
}
LCRYPTO_ALIAS(OCSP_REQUEST_add1_ext_i2d);

int
OCSP_REQUEST_add_ext(OCSP_REQUEST *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&(x->tbsRequest->requestExtensions), ex,
	    loc) != NULL;
}
LCRYPTO_ALIAS(OCSP_REQUEST_add_ext);

/* Single extensions */

int
OCSP_ONEREQ_get_ext_count(OCSP_ONEREQ *x)
{
	return X509v3_get_ext_count(x->singleRequestExtensions);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get_ext_count);

int
OCSP_ONEREQ_get_ext_by_NID(OCSP_ONEREQ *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->singleRequestExtensions, nid, lastpos);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get_ext_by_NID);

int
OCSP_ONEREQ_get_ext_by_OBJ(OCSP_ONEREQ *x, const ASN1_OBJECT *obj, int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->singleRequestExtensions, obj, lastpos);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get_ext_by_OBJ);

int
OCSP_ONEREQ_get_ext_by_critical(OCSP_ONEREQ *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->singleRequestExtensions, crit,
	    lastpos);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get_ext_by_critical);

X509_EXTENSION *
OCSP_ONEREQ_get_ext(OCSP_ONEREQ *x, int loc)
{
	return X509v3_get_ext(x->singleRequestExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get_ext);

X509_EXTENSION *
OCSP_ONEREQ_delete_ext(OCSP_ONEREQ *x, int loc)
{
	return X509v3_delete_ext(x->singleRequestExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_delete_ext);

void *
OCSP_ONEREQ_get1_ext_d2i(OCSP_ONEREQ *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->singleRequestExtensions, nid, crit, idx);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_get1_ext_d2i);

int
OCSP_ONEREQ_add1_ext_i2d(OCSP_ONEREQ *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->singleRequestExtensions, nid, value, crit,
	    flags);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_add1_ext_i2d);

int
OCSP_ONEREQ_add_ext(OCSP_ONEREQ *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&(x->singleRequestExtensions), ex, loc) != NULL;
}
LCRYPTO_ALIAS(OCSP_ONEREQ_add_ext);

/* OCSP Basic response */

int
OCSP_BASICRESP_get_ext_count(OCSP_BASICRESP *x)
{
	return X509v3_get_ext_count(x->tbsResponseData->responseExtensions);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get_ext_count);

int
OCSP_BASICRESP_get_ext_by_NID(OCSP_BASICRESP *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->tbsResponseData->responseExtensions,
	    nid, lastpos);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get_ext_by_NID);

int
OCSP_BASICRESP_get_ext_by_OBJ(OCSP_BASICRESP *x, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->tbsResponseData->responseExtensions,
	    obj, lastpos);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get_ext_by_OBJ);

int
OCSP_BASICRESP_get_ext_by_critical(OCSP_BASICRESP *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(
	    x->tbsResponseData->responseExtensions, crit, lastpos);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get_ext_by_critical);

X509_EXTENSION *
OCSP_BASICRESP_get_ext(OCSP_BASICRESP *x, int loc)
{
	return X509v3_get_ext(x->tbsResponseData->responseExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get_ext);

X509_EXTENSION *
OCSP_BASICRESP_delete_ext(OCSP_BASICRESP *x, int loc)
{
	return X509v3_delete_ext(x->tbsResponseData->responseExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_delete_ext);

void *
OCSP_BASICRESP_get1_ext_d2i(OCSP_BASICRESP *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->tbsResponseData->responseExtensions, nid,
	    crit, idx);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_get1_ext_d2i);

int
OCSP_BASICRESP_add1_ext_i2d(OCSP_BASICRESP *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->tbsResponseData->responseExtensions, nid,
	    value, crit, flags);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_add1_ext_i2d);

int
OCSP_BASICRESP_add_ext(OCSP_BASICRESP *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&(x->tbsResponseData->responseExtensions), ex,
	    loc) != NULL;
}
LCRYPTO_ALIAS(OCSP_BASICRESP_add_ext);

/* OCSP single response extensions */

int
OCSP_SINGLERESP_get_ext_count(OCSP_SINGLERESP *x)
{
	return X509v3_get_ext_count(x->singleExtensions);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get_ext_count);

int
OCSP_SINGLERESP_get_ext_by_NID(OCSP_SINGLERESP *x, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(x->singleExtensions, nid, lastpos);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get_ext_by_NID);

int
OCSP_SINGLERESP_get_ext_by_OBJ(OCSP_SINGLERESP *x, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509v3_get_ext_by_OBJ(x->singleExtensions, obj, lastpos);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get_ext_by_OBJ);

int
OCSP_SINGLERESP_get_ext_by_critical(OCSP_SINGLERESP *x, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(x->singleExtensions, crit, lastpos);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get_ext_by_critical);

X509_EXTENSION *
OCSP_SINGLERESP_get_ext(OCSP_SINGLERESP *x, int loc)
{
	return X509v3_get_ext(x->singleExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get_ext);

X509_EXTENSION *
OCSP_SINGLERESP_delete_ext(OCSP_SINGLERESP *x, int loc)
{
	return X509v3_delete_ext(x->singleExtensions, loc);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_delete_ext);

void *
OCSP_SINGLERESP_get1_ext_d2i(OCSP_SINGLERESP *x, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(x->singleExtensions, nid, crit, idx);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get1_ext_d2i);

int
OCSP_SINGLERESP_add1_ext_i2d(OCSP_SINGLERESP *x, int nid, void *value, int crit,
    unsigned long flags)
{
	return X509V3_add1_i2d(&x->singleExtensions, nid, value, crit, flags);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_add1_ext_i2d);

int
OCSP_SINGLERESP_add_ext(OCSP_SINGLERESP *x, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&(x->singleExtensions), ex, loc) != NULL;
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_add_ext);

/* Nonce handling functions */

/* Add a nonce to an extension stack. A nonce can be specified or if NULL
 * a random nonce will be generated.
 * Note: OpenSSL 0.9.7d and later create an OCTET STRING containing the
 * nonce, previous versions used the raw nonce.
 */

static int
ocsp_add1_nonce(STACK_OF(X509_EXTENSION) **exts, unsigned char *val, int len)
{
	unsigned char *tmpval;
	ASN1_OCTET_STRING os;
	int ret = 0;

	if (len <= 0)
		len = OCSP_DEFAULT_NONCE_LENGTH;
	/* Create the OCTET STRING manually by writing out the header and
	 * appending the content octets. This avoids an extra memory allocation
	 * operation in some cases. Applications should *NOT* do this because
	 * it relies on library internals.
	 */
	os.length = ASN1_object_size(0, len, V_ASN1_OCTET_STRING);
	os.data = malloc(os.length);
	if (os.data == NULL)
		goto err;
	tmpval = os.data;
	ASN1_put_object(&tmpval, 0, len, V_ASN1_OCTET_STRING, V_ASN1_UNIVERSAL);
	if (val)
		memcpy(tmpval, val, len);
	else
		arc4random_buf(tmpval, len);
	if (!X509V3_add1_i2d(exts, NID_id_pkix_OCSP_Nonce, &os, 0,
	    X509V3_ADD_REPLACE))
		goto err;
	ret = 1;

err:
	free(os.data);
	return ret;
}

/* Add nonce to an OCSP request */
int
OCSP_request_add1_nonce(OCSP_REQUEST *req, unsigned char *val, int len)
{
	return ocsp_add1_nonce(&req->tbsRequest->requestExtensions, val, len);
}
LCRYPTO_ALIAS(OCSP_request_add1_nonce);

/* Same as above but for a response */
int
OCSP_basic_add1_nonce(OCSP_BASICRESP *resp, unsigned char *val, int len)
{
	return ocsp_add1_nonce(&resp->tbsResponseData->responseExtensions, val,
	    len);
}
LCRYPTO_ALIAS(OCSP_basic_add1_nonce);

/* Check nonce validity in a request and response.
 * Return value reflects result:
 *  1: nonces present and equal.
 *  2: nonces both absent.
 *  3: nonce present in response only.
 *  0: nonces both present and not equal.
 * -1: nonce in request only.
 *
 *  For most responders clients can check return > 0.
 *  If responder doesn't handle nonces return != 0 may be
 *  necessary. return == 0 is always an error.
 */
int
OCSP_check_nonce(OCSP_REQUEST *req, OCSP_BASICRESP *bs)
{
	/*
	 * Since we are only interested in the presence or absence of
	 * the nonce and comparing its value there is no need to use
	 * the X509V3 routines: this way we can avoid them allocating an
	 * ASN1_OCTET_STRING structure for the value which would be
	 * freed immediately anyway.
	 */
	int req_idx, resp_idx;
	X509_EXTENSION *req_ext, *resp_ext;

	req_idx = OCSP_REQUEST_get_ext_by_NID(req, NID_id_pkix_OCSP_Nonce, -1);
	resp_idx = OCSP_BASICRESP_get_ext_by_NID(bs,
	    NID_id_pkix_OCSP_Nonce, -1);
	/* Check both absent */
	if (req_idx < 0 && resp_idx < 0)
		return 2;
	/* Check in request only */
	if (req_idx >= 0 && resp_idx < 0)
		return -1;
	/* Check in response but not request */
	if (req_idx < 0 && resp_idx >= 0)
		return 3;
	/* Otherwise nonce in request and response so retrieve the extensions */
	req_ext = OCSP_REQUEST_get_ext(req, req_idx);
	resp_ext = OCSP_BASICRESP_get_ext(bs, resp_idx);
	if (ASN1_OCTET_STRING_cmp(req_ext->value, resp_ext->value))
		return 0;
	return 1;
}
LCRYPTO_ALIAS(OCSP_check_nonce);

/* Copy the nonce value (if any) from an OCSP request to
 * a response.
 */
int
OCSP_copy_nonce(OCSP_BASICRESP *resp, OCSP_REQUEST *req)
{
	X509_EXTENSION *req_ext;
	int req_idx;

	/* Check for nonce in request */
	req_idx = OCSP_REQUEST_get_ext_by_NID(req, NID_id_pkix_OCSP_Nonce, -1);
	/* If no nonce that's OK */
	if (req_idx < 0)
		return 2;
	req_ext = OCSP_REQUEST_get_ext(req, req_idx);
	return OCSP_BASICRESP_add_ext(resp, req_ext, -1);
}
LCRYPTO_ALIAS(OCSP_copy_nonce);

X509_EXTENSION *
OCSP_crlID_new(const char *url, long *n, char *tim)
{
	X509_EXTENSION *x = NULL;
	OCSP_CRLID *cid = NULL;

	if (!(cid = OCSP_CRLID_new()))
		goto err;
	if (url) {
		if (!(cid->crlUrl = ASN1_IA5STRING_new()))
			goto err;
		if (!(ASN1_STRING_set(cid->crlUrl, url, -1)))
			goto err;
	}
	if (n) {
		if (!(cid->crlNum = ASN1_INTEGER_new()))
			goto err;
		if (!(ASN1_INTEGER_set(cid->crlNum, *n)))
			goto err;
	}
	if (tim) {
		if (!(cid->crlTime = ASN1_GENERALIZEDTIME_new()))
			goto err;
		if (!(ASN1_GENERALIZEDTIME_set_string(cid->crlTime, tim)))
			goto err;
	}
	x = X509V3_EXT_i2d(NID_id_pkix_OCSP_CrlID, 0, cid);

err:
	if (cid)
		OCSP_CRLID_free(cid);
	return x;
}
LCRYPTO_ALIAS(OCSP_crlID_new);

/*   AcceptableResponses ::= SEQUENCE OF OBJECT IDENTIFIER */
X509_EXTENSION *
OCSP_accept_responses_new(char **oids)
{
	int nid;
	STACK_OF(ASN1_OBJECT) *sk = NULL;
	ASN1_OBJECT *o = NULL;
	X509_EXTENSION *x = NULL;

	if (!(sk = sk_ASN1_OBJECT_new_null()))
		return NULL;
	while (oids && *oids) {
		if ((nid = OBJ_txt2nid(*oids)) != NID_undef &&
		    (o = OBJ_nid2obj(nid)))
			if (sk_ASN1_OBJECT_push(sk, o) == 0) {
				sk_ASN1_OBJECT_pop_free(sk, ASN1_OBJECT_free);
				return NULL;
			}
		oids++;
	}
	x = X509V3_EXT_i2d(NID_id_pkix_OCSP_acceptableResponses, 0, sk);
	sk_ASN1_OBJECT_pop_free(sk, ASN1_OBJECT_free);
	return x;
}
LCRYPTO_ALIAS(OCSP_accept_responses_new);

/*  ArchiveCutoff ::= GeneralizedTime */
X509_EXTENSION *
OCSP_archive_cutoff_new(char* tim)
{
	X509_EXTENSION *x = NULL;
	ASN1_GENERALIZEDTIME *gt = NULL;

	if (!(gt = ASN1_GENERALIZEDTIME_new()))
		return NULL;
	if (!(ASN1_GENERALIZEDTIME_set_string(gt, tim)))
		goto err;
	x = X509V3_EXT_i2d(NID_id_pkix_OCSP_archiveCutoff, 0, gt);

err:
	if (gt)
		ASN1_GENERALIZEDTIME_free(gt);
	return x;
}
LCRYPTO_ALIAS(OCSP_archive_cutoff_new);

/* per ACCESS_DESCRIPTION parameter are oids, of which there are currently
 * two--NID_ad_ocsp, NID_id_ad_caIssuers--and GeneralName value.  This
 * method forces NID_ad_ocsp and uniformResourceLocator [6] IA5String.
 */
X509_EXTENSION *
OCSP_url_svcloc_new(X509_NAME* issuer, const char **urls)
{
	X509_EXTENSION *x = NULL;
	ASN1_IA5STRING *ia5 = NULL;
	OCSP_SERVICELOC *sloc = NULL;
	ACCESS_DESCRIPTION *ad = NULL;

	if (!(sloc = OCSP_SERVICELOC_new()))
		goto err;
	if (!(sloc->issuer = X509_NAME_dup(issuer)))
		goto err;
	if (urls && *urls &&
	    !(sloc->locator = sk_ACCESS_DESCRIPTION_new_null()))
		goto err;
	while (urls && *urls) {
		if (!(ad = ACCESS_DESCRIPTION_new()))
			goto err;
		if (!(ad->method = OBJ_nid2obj(NID_ad_OCSP)))
			goto err;
		if (!(ad->location = GENERAL_NAME_new()))
			goto err;
		if (!(ia5 = ASN1_IA5STRING_new()))
			goto err;
		if (!ASN1_STRING_set((ASN1_STRING*)ia5, *urls, -1))
			goto err;
		ad->location->type = GEN_URI;
		ad->location->d.ia5 = ia5;
		ia5 = NULL;
		if (!sk_ACCESS_DESCRIPTION_push(sloc->locator, ad))
			goto err;
		ad = NULL;
		urls++;
	}
	x = X509V3_EXT_i2d(NID_id_pkix_OCSP_serviceLocator, 0, sloc);

err:
	if (ia5)
		ASN1_IA5STRING_free(ia5);
	if (ad)
		ACCESS_DESCRIPTION_free(ad);
	if (sloc)
		OCSP_SERVICELOC_free(sloc);
	return x;
}
LCRYPTO_ALIAS(OCSP_url_svcloc_new);
