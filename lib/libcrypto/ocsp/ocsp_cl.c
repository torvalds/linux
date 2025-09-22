/* $OpenBSD: ocsp_cl.c,v 1.26 2025/05/10 05:54:38 tb Exp $ */
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
#include <time.h>

#include <openssl/ocsp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/posix_time.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "err_local.h"
#include "ocsp_local.h"

/* Utility functions related to sending OCSP requests and extracting
 * relevant information from the response.
 */

/* Add an OCSP_CERTID to an OCSP request. Return new OCSP_ONEREQ
 * pointer: useful if we want to add extensions.
 */
OCSP_ONEREQ *
OCSP_request_add0_id(OCSP_REQUEST *req, OCSP_CERTID *cid)
{
	OCSP_ONEREQ *one;

	if ((one = OCSP_ONEREQ_new()) == NULL)
		goto err;
	if (req != NULL) {
		if (!sk_OCSP_ONEREQ_push(req->tbsRequest->requestList, one))
			goto err;
	}
	OCSP_CERTID_free(one->reqCert);
	one->reqCert = cid;
	return one;

 err:
	OCSP_ONEREQ_free(one);
	return NULL;
}
LCRYPTO_ALIAS(OCSP_request_add0_id);

/* Set requestorName from an X509_NAME structure */
int
OCSP_request_set1_name(OCSP_REQUEST *req, X509_NAME *nm)
{
	GENERAL_NAME *gen;

	gen = GENERAL_NAME_new();
	if (gen == NULL)
		return 0;
	if (!X509_NAME_set(&gen->d.directoryName, nm)) {
		GENERAL_NAME_free(gen);
		return 0;
	}
	gen->type = GEN_DIRNAME;
	if (req->tbsRequest->requestorName)
		GENERAL_NAME_free(req->tbsRequest->requestorName);
	req->tbsRequest->requestorName = gen;
	return 1;
}
LCRYPTO_ALIAS(OCSP_request_set1_name);

/* Add a certificate to an OCSP request */
int
OCSP_request_add1_cert(OCSP_REQUEST *req, X509 *cert)
{
	OCSP_SIGNATURE *sig;

	if (!req->optionalSignature)
		req->optionalSignature = OCSP_SIGNATURE_new();
	sig = req->optionalSignature;
	if (!sig)
		return 0;
	if (!cert)
		return 1;
	if (!sig->certs && !(sig->certs = sk_X509_new_null()))
		return 0;

	if (!sk_X509_push(sig->certs, cert))
		return 0;
	X509_up_ref(cert);
	return 1;
}
LCRYPTO_ALIAS(OCSP_request_add1_cert);

/* Sign an OCSP request set the requestorName to the subject
 * name of an optional signers certificate and include one
 * or more optional certificates in the request. Behaves
 * like PKCS7_sign().
 */
int
OCSP_request_sign(OCSP_REQUEST *req, X509 *signer, EVP_PKEY *key,
    const EVP_MD *dgst, STACK_OF(X509) *certs, unsigned long flags)
{
	int i;
	OCSP_SIGNATURE *sig;
	X509 *x;

	if (!OCSP_request_set1_name(req, X509_get_subject_name(signer)))
		goto err;

	if (!(req->optionalSignature = sig = OCSP_SIGNATURE_new()))
		goto err;
	if (key) {
		if (!X509_check_private_key(signer, key)) {
			OCSPerror(OCSP_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
			goto err;
		}
		if (!OCSP_REQUEST_sign(req, key, dgst))
			goto err;
	}

	if (!(flags & OCSP_NOCERTS)) {
		if (!OCSP_request_add1_cert(req, signer))
			goto err;
		for (i = 0; i < sk_X509_num(certs); i++) {
			x = sk_X509_value(certs, i);
			if (!OCSP_request_add1_cert(req, x))
				goto err;
		}
	}

	return 1;

err:
	OCSP_SIGNATURE_free(req->optionalSignature);
	req->optionalSignature = NULL;
	return 0;
}
LCRYPTO_ALIAS(OCSP_request_sign);

/* Get response status */
int
OCSP_response_status(OCSP_RESPONSE *resp)
{
	return ASN1_ENUMERATED_get(resp->responseStatus);
}
LCRYPTO_ALIAS(OCSP_response_status);

/* Extract basic response from OCSP_RESPONSE or NULL if
 * no basic response present.
 */
OCSP_BASICRESP *
OCSP_response_get1_basic(OCSP_RESPONSE *resp)
{
	OCSP_RESPBYTES *rb;

	rb = resp->responseBytes;
	if (!rb) {
		OCSPerror(OCSP_R_NO_RESPONSE_DATA);
		return NULL;
	}
	if (OBJ_obj2nid(rb->responseType) != NID_id_pkix_OCSP_basic) {
		OCSPerror(OCSP_R_NOT_BASIC_RESPONSE);
		return NULL;
	}

	return ASN1_item_unpack(rb->response, &OCSP_BASICRESP_it);
}
LCRYPTO_ALIAS(OCSP_response_get1_basic);

/* Return number of OCSP_SINGLERESP responses present in
 * a basic response.
 */
int
OCSP_resp_count(OCSP_BASICRESP *bs)
{
	if (!bs)
		return -1;
	return sk_OCSP_SINGLERESP_num(bs->tbsResponseData->responses);
}
LCRYPTO_ALIAS(OCSP_resp_count);

/* Extract an OCSP_SINGLERESP response with a given index */
OCSP_SINGLERESP *
OCSP_resp_get0(OCSP_BASICRESP *bs, int idx)
{
	if (!bs)
		return NULL;
	return sk_OCSP_SINGLERESP_value(bs->tbsResponseData->responses, idx);
}
LCRYPTO_ALIAS(OCSP_resp_get0);

const ASN1_GENERALIZEDTIME *
OCSP_resp_get0_produced_at(const OCSP_BASICRESP *bs)
{
	return bs->tbsResponseData->producedAt;
}
LCRYPTO_ALIAS(OCSP_resp_get0_produced_at);

const STACK_OF(X509) *
OCSP_resp_get0_certs(const OCSP_BASICRESP *bs)
{
	return bs->certs;
}
LCRYPTO_ALIAS(OCSP_resp_get0_certs);

int
OCSP_resp_get0_id(const OCSP_BASICRESP *bs, const ASN1_OCTET_STRING **pid,
    const X509_NAME **pname)
{
	const OCSP_RESPID *rid = bs->tbsResponseData->responderId;

	if (rid->type == V_OCSP_RESPID_NAME) {
		*pname = rid->value.byName;
		*pid = NULL;
	} else if (rid->type == V_OCSP_RESPID_KEY) {
		*pid = rid->value.byKey;
		*pname = NULL;
	} else {
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(OCSP_resp_get0_id);

const ASN1_OCTET_STRING *
OCSP_resp_get0_signature(const OCSP_BASICRESP *bs)
{
	return bs->signature;
}
LCRYPTO_ALIAS(OCSP_resp_get0_signature);

const X509_ALGOR *
OCSP_resp_get0_tbs_sigalg(const OCSP_BASICRESP *bs)
{
	return bs->signatureAlgorithm;
}
LCRYPTO_ALIAS(OCSP_resp_get0_tbs_sigalg);

const OCSP_RESPDATA *
OCSP_resp_get0_respdata(const OCSP_BASICRESP *bs)
{
	return bs->tbsResponseData;
}
LCRYPTO_ALIAS(OCSP_resp_get0_respdata);

/* Look single response matching a given certificate ID */
int
OCSP_resp_find(OCSP_BASICRESP *bs, OCSP_CERTID *id, int last)
{
	int i;
	STACK_OF(OCSP_SINGLERESP) *sresp;
	OCSP_SINGLERESP *single;

	if (!bs)
		return -1;
	if (last < 0)
		last = 0;
	else
		last++;
	sresp = bs->tbsResponseData->responses;
	for (i = last; i < sk_OCSP_SINGLERESP_num(sresp); i++) {
		single = sk_OCSP_SINGLERESP_value(sresp, i);
		if (!OCSP_id_cmp(id, single->certId))
			return i;
	}
	return -1;
}
LCRYPTO_ALIAS(OCSP_resp_find);

/* Extract status information from an OCSP_SINGLERESP structure.
 * Note: the revtime and reason values are only set if the
 * certificate status is revoked. Returns numerical value of
 * status.
 */
int
OCSP_single_get0_status(OCSP_SINGLERESP *single, int *reason,
    ASN1_GENERALIZEDTIME **revtime, ASN1_GENERALIZEDTIME **thisupd,
    ASN1_GENERALIZEDTIME **nextupd)
{
	int ret;
	OCSP_CERTSTATUS *cst;

	if (!single)
		return -1;
	cst = single->certStatus;
	ret = cst->type;
	if (ret == V_OCSP_CERTSTATUS_REVOKED) {
		OCSP_REVOKEDINFO *rev = cst->value.revoked;

		if (revtime)
			*revtime = rev->revocationTime;
		if (reason) {
			if (rev->revocationReason)
				*reason = ASN1_ENUMERATED_get(
				    rev->revocationReason);
			else
				*reason = -1;
		}
	}
	if (thisupd)
		*thisupd = single->thisUpdate;
	if (nextupd)
		*nextupd = single->nextUpdate;
	return ret;
}
LCRYPTO_ALIAS(OCSP_single_get0_status);

/* This function combines the previous ones: look up a certificate ID and
 * if found extract status information. Return 0 is successful.
 */
int
OCSP_resp_find_status(OCSP_BASICRESP *bs, OCSP_CERTID *id, int *status,
    int *reason, ASN1_GENERALIZEDTIME **revtime, ASN1_GENERALIZEDTIME **thisupd,
    ASN1_GENERALIZEDTIME **nextupd)
{
	int i;
	OCSP_SINGLERESP *single;

	i = OCSP_resp_find(bs, id, -1);
	/* Maybe check for multiple responses and give an error? */
	if (i < 0)
		return 0;
	single = OCSP_resp_get0(bs, i);
	i = OCSP_single_get0_status(single, reason, revtime, thisupd, nextupd);
	if (status)
		*status = i;
	return 1;
}
LCRYPTO_ALIAS(OCSP_resp_find_status);

/* Check validity of thisUpdate and nextUpdate fields. It is possible that the request will
 * take a few seconds to process and/or the time wont be totally accurate. Therefore to avoid
 * rejecting otherwise valid time we allow the times to be within 'nsec' of the current time.
 * Also to avoid accepting very old responses without a nextUpdate field an optional maxage
 * parameter specifies the maximum age the thisUpdate field can be.
 */
int
OCSP_check_validity(ASN1_GENERALIZEDTIME *thisupd,
    ASN1_GENERALIZEDTIME *nextupd, long nsec, long maxsec)
{
	int64_t posix_next, posix_this, posix_now;
	struct tm tm_this, tm_next;

	/* Negative values of nsec make no sense */
	if (nsec < 0)
		return 0;

	posix_now = time(NULL);

	/*
	 * Times must explicitly be a GENERALIZEDTIME as per section
	 * 4.2.2.1 of RFC 6960 - It is invalid to accept other times
	 * (such as UTCTIME permitted/required by RFC 5280 for certificates)
	 */
	/* Check that thisUpdate is valid. */
	if (ASN1_time_parse(thisupd->data, thisupd->length, &tm_this,
	    V_ASN1_GENERALIZEDTIME) != V_ASN1_GENERALIZEDTIME) {
		OCSPerror(OCSP_R_ERROR_IN_THISUPDATE_FIELD);
		return 0;
	}
	if (!OPENSSL_tm_to_posix(&tm_this, &posix_this))
		return 0;
	/* thisUpdate must not be more than nsec in the future. */
	if (posix_this - nsec > posix_now) {
		OCSPerror(OCSP_R_STATUS_NOT_YET_VALID);
		return 0;
	}
	/* thisUpdate must not be more than maxsec seconds in the past. */
	if (maxsec >= 0 && posix_this < posix_now - maxsec) {
		OCSPerror(OCSP_R_STATUS_TOO_OLD);
		return 0;
	}

	/* RFC 6960 section 4.2.2.1 allows for servers to not set nextUpdate */
	if (nextupd == NULL)
		return 1;

	/* Check that nextUpdate is valid. */
	if (ASN1_time_parse(nextupd->data, nextupd->length, &tm_next,
	    V_ASN1_GENERALIZEDTIME) != V_ASN1_GENERALIZEDTIME) {
		OCSPerror(OCSP_R_ERROR_IN_NEXTUPDATE_FIELD);
		return 0;
	}
	if (!OPENSSL_tm_to_posix(&tm_next, &posix_next))
		return 0;
	/* Don't allow nextUpdate to precede thisUpdate. */
	if (posix_next < posix_this) {
		OCSPerror(OCSP_R_NEXTUPDATE_BEFORE_THISUPDATE);
		return 0;
	}
	/* nextUpdate must not be more than nsec seconds in the past. */
	if (posix_next + nsec  < posix_now) {
		OCSPerror(OCSP_R_STATUS_EXPIRED);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(OCSP_check_validity);

const OCSP_CERTID *
OCSP_SINGLERESP_get0_id(const OCSP_SINGLERESP *single)
{
	return single->certId;
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_get0_id);
