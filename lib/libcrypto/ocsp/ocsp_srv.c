/* $OpenBSD: ocsp_srv.c,v 1.14 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 1998-2001 The OpenSSL Project.  All rights reserved.
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

#include <openssl/objects.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "ocsp_local.h"

/* Utility functions related to sending OCSP responses and extracting
 * relevant information from the request.
 */

int
OCSP_request_onereq_count(OCSP_REQUEST *req)
{
	return sk_OCSP_ONEREQ_num(req->tbsRequest->requestList);
}
LCRYPTO_ALIAS(OCSP_request_onereq_count);

OCSP_ONEREQ *
OCSP_request_onereq_get0(OCSP_REQUEST *req, int i)
{
	return sk_OCSP_ONEREQ_value(req->tbsRequest->requestList, i);
}
LCRYPTO_ALIAS(OCSP_request_onereq_get0);

OCSP_CERTID *
OCSP_onereq_get0_id(OCSP_ONEREQ *one)
{
	return one->reqCert;
}
LCRYPTO_ALIAS(OCSP_onereq_get0_id);

int
OCSP_id_get0_info(ASN1_OCTET_STRING **piNameHash, ASN1_OBJECT **pmd,
    ASN1_OCTET_STRING **pikeyHash, ASN1_INTEGER **pserial, OCSP_CERTID *cid)
{
	if (!cid)
		return 0;
	if (pmd)
		*pmd = cid->hashAlgorithm->algorithm;
	if (piNameHash)
		*piNameHash = cid->issuerNameHash;
	if (pikeyHash)
		*pikeyHash = cid->issuerKeyHash;
	if (pserial)
		*pserial = cid->serialNumber;
	return 1;
}
LCRYPTO_ALIAS(OCSP_id_get0_info);

int
OCSP_request_is_signed(OCSP_REQUEST *req)
{
	if (req->optionalSignature)
		return 1;
	return 0;
}
LCRYPTO_ALIAS(OCSP_request_is_signed);

/* Create an OCSP response and encode an optional basic response */
OCSP_RESPONSE *
OCSP_response_create(int status, OCSP_BASICRESP *bs)
{
	OCSP_RESPONSE *rsp = NULL;

	if (!(rsp = OCSP_RESPONSE_new()))
		goto err;
	if (!(ASN1_ENUMERATED_set(rsp->responseStatus, status)))
		goto err;
	if (!bs)
		return rsp;
	if (!(rsp->responseBytes = OCSP_RESPBYTES_new()))
		goto err;
	rsp->responseBytes->responseType = OBJ_nid2obj(NID_id_pkix_OCSP_basic);
	if (!ASN1_item_pack(bs, &OCSP_BASICRESP_it,
	    &rsp->responseBytes->response))
		goto err;
	return rsp;

err:
	if (rsp)
		OCSP_RESPONSE_free(rsp);
	return NULL;
}
LCRYPTO_ALIAS(OCSP_response_create);

OCSP_SINGLERESP *
OCSP_basic_add1_status(OCSP_BASICRESP *rsp, OCSP_CERTID *cid, int status,
    int reason, ASN1_TIME *revtime, ASN1_TIME *thisupd, ASN1_TIME *nextupd)
{
	OCSP_SINGLERESP *single = NULL;
	OCSP_CERTSTATUS *cs;
	OCSP_REVOKEDINFO *ri;

	if (!rsp->tbsResponseData->responses &&
	    !(rsp->tbsResponseData->responses = sk_OCSP_SINGLERESP_new_null()))
		goto err;

	if (!(single = OCSP_SINGLERESP_new()))
		goto err;

	if (!ASN1_TIME_to_generalizedtime(thisupd, &single->thisUpdate))
		goto err;
	if (nextupd &&
	    !ASN1_TIME_to_generalizedtime(nextupd, &single->nextUpdate))
		goto err;

	OCSP_CERTID_free(single->certId);

	if (!(single->certId = OCSP_CERTID_dup(cid)))
		goto err;

	cs = single->certStatus;
	switch (cs->type = status) {
	case V_OCSP_CERTSTATUS_REVOKED:
		if (!revtime) {
			OCSPerror(OCSP_R_NO_REVOKED_TIME);
			goto err;
		}
		if (!(cs->value.revoked = ri = OCSP_REVOKEDINFO_new()))
			goto err;
		if (!ASN1_TIME_to_generalizedtime(revtime, &ri->revocationTime))
			goto err;
		if (reason != OCSP_REVOKED_STATUS_NOSTATUS) {
			if (!(ri->revocationReason = ASN1_ENUMERATED_new()))
				goto err;
			if (!(ASN1_ENUMERATED_set(ri->revocationReason,
			    reason)))
				goto err;
		}
		break;

	case V_OCSP_CERTSTATUS_GOOD:
		cs->value.good = ASN1_NULL_new();
		break;

	case V_OCSP_CERTSTATUS_UNKNOWN:
		cs->value.unknown = ASN1_NULL_new();
		break;

	default:
		goto err;
	}
	if (!(sk_OCSP_SINGLERESP_push(rsp->tbsResponseData->responses, single)))
		goto err;
	return single;

err:
	OCSP_SINGLERESP_free(single);
	return NULL;
}
LCRYPTO_ALIAS(OCSP_basic_add1_status);

/* Add a certificate to an OCSP request */
int
OCSP_basic_add1_cert(OCSP_BASICRESP *resp, X509 *cert)
{
	if (!resp->certs && !(resp->certs = sk_X509_new_null()))
		return 0;

	if (!sk_X509_push(resp->certs, cert))
		return 0;
	X509_up_ref(cert);
	return 1;
}
LCRYPTO_ALIAS(OCSP_basic_add1_cert);

int
OCSP_basic_sign(OCSP_BASICRESP *brsp, X509 *signer, EVP_PKEY *key,
    const EVP_MD *dgst, STACK_OF(X509) *certs, unsigned long flags)
{
	int i;
	OCSP_RESPID *rid;

	if (!X509_check_private_key(signer, key)) {
		OCSPerror(OCSP_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
		goto err;
	}

	if (!(flags & OCSP_NOCERTS)) {
		if (!OCSP_basic_add1_cert(brsp, signer))
			goto err;
		for (i = 0; i < sk_X509_num(certs); i++) {
			X509 *tmpcert = sk_X509_value(certs, i);
			if (!OCSP_basic_add1_cert(brsp, tmpcert))
				goto err;
		}
	}

	rid = brsp->tbsResponseData->responderId;
	if (flags & OCSP_RESPID_KEY) {
		unsigned char md[SHA_DIGEST_LENGTH];

		X509_pubkey_digest(signer, EVP_sha1(), md, NULL);
		if (!(rid->value.byKey = ASN1_OCTET_STRING_new()))
			goto err;
		if (!(ASN1_OCTET_STRING_set(rid->value.byKey, md,
		    SHA_DIGEST_LENGTH)))
			goto err;
		rid->type = V_OCSP_RESPID_KEY;
	} else {
		if (!X509_NAME_set(&rid->value.byName,
		    X509_get_subject_name(signer)))
			goto err;
		rid->type = V_OCSP_RESPID_NAME;
	}

	if (!(flags & OCSP_NOTIME) &&
	    !ASN1_GENERALIZEDTIME_set(brsp->tbsResponseData->producedAt, time(NULL)))
		goto err;

	/* Right now, I think that not doing double hashing is the right
	   thing.	-- Richard Levitte */

	if (!OCSP_BASICRESP_sign(brsp, key, dgst, 0))
		goto err;

	return 1;

err:
	return 0;
}
LCRYPTO_ALIAS(OCSP_basic_sign);
