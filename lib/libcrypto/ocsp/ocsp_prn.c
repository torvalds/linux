/* $OpenBSD: ocsp_prn.c,v 1.12 2025/05/10 05:54:38 tb Exp $ */
/* Written by Tom Titchener <Tom_Titchener@groove.net> for the OpenSSL
 * project. */

/* History:
   This file was originally part of ocsp.c and was transfered to Richard
   Levitte from CertCo by Kathy Weinhold in mid-spring 2000 to be included
   in OpenSSL or released as a patch kit. */

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

#include <openssl/bio.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "ocsp_local.h"

static int
ocsp_certid_print(BIO *bp, OCSP_CERTID* a, int indent)
{
	const ASN1_OBJECT *aobj;

	BIO_printf(bp, "%*sCertificate ID:\n", indent, "");
	indent += 2;
	BIO_printf(bp, "%*sHash Algorithm: ", indent, "");
	X509_ALGOR_get0(&aobj, NULL, NULL, a->hashAlgorithm);
	i2a_ASN1_OBJECT(bp, aobj);
	BIO_printf(bp, "\n%*sIssuer Name Hash: ", indent, "");
	i2a_ASN1_STRING(bp, a->issuerNameHash, V_ASN1_OCTET_STRING);
	BIO_printf(bp, "\n%*sIssuer Key Hash: ", indent, "");
	i2a_ASN1_STRING(bp, a->issuerKeyHash, V_ASN1_OCTET_STRING);
	BIO_printf(bp, "\n%*sSerial Number: ", indent, "");
	i2a_ASN1_INTEGER(bp, a->serialNumber);
	BIO_printf(bp, "\n");
	return 1;
}

typedef struct {
	long t;
	const char *m;
} OCSP_TBLSTR;

static const char *
table2string(long s, const OCSP_TBLSTR *ts, int len)
{
	const OCSP_TBLSTR *p;

	for (p = ts; p < ts + len; p++)
		if (p->t == s)
			return p->m;
	return "(UNKNOWN)";
}

const char *
OCSP_response_status_str(long s)
{
	static const OCSP_TBLSTR rstat_tbl[] = {
		{ OCSP_RESPONSE_STATUS_SUCCESSFUL, "successful" },
		{ OCSP_RESPONSE_STATUS_MALFORMEDREQUEST, "malformedrequest" },
		{ OCSP_RESPONSE_STATUS_INTERNALERROR, "internalerror" },
		{ OCSP_RESPONSE_STATUS_TRYLATER, "trylater" },
		{ OCSP_RESPONSE_STATUS_SIGREQUIRED, "sigrequired" },
		{ OCSP_RESPONSE_STATUS_UNAUTHORIZED, "unauthorized" }
	};
	return table2string(s, rstat_tbl, 6);
}
LCRYPTO_ALIAS(OCSP_response_status_str);

const char *
OCSP_cert_status_str(long s)
{
	static const OCSP_TBLSTR cstat_tbl[] = {
		{ V_OCSP_CERTSTATUS_GOOD, "good" },
		{ V_OCSP_CERTSTATUS_REVOKED, "revoked" },
		{ V_OCSP_CERTSTATUS_UNKNOWN, "unknown" }
	};
	return table2string(s, cstat_tbl, 3);
}
LCRYPTO_ALIAS(OCSP_cert_status_str);

const char *
OCSP_crl_reason_str(long s)
{
	static const OCSP_TBLSTR reason_tbl[] = {
		{ OCSP_REVOKED_STATUS_UNSPECIFIED, "unspecified" },
		{ OCSP_REVOKED_STATUS_KEYCOMPROMISE, "keyCompromise" },
		{ OCSP_REVOKED_STATUS_CACOMPROMISE, "cACompromise" },
		{ OCSP_REVOKED_STATUS_AFFILIATIONCHANGED, "affiliationChanged" },
		{ OCSP_REVOKED_STATUS_SUPERSEDED, "superseded" },
		{ OCSP_REVOKED_STATUS_CESSATIONOFOPERATION, "cessationOfOperation" },
		{ OCSP_REVOKED_STATUS_CERTIFICATEHOLD, "certificateHold" },
		{ OCSP_REVOKED_STATUS_REMOVEFROMCRL, "removeFromCRL" }
	};
	return table2string(s, reason_tbl, 8);
}
LCRYPTO_ALIAS(OCSP_crl_reason_str);

int
OCSP_REQUEST_print(BIO *bp, OCSP_REQUEST* o, unsigned long flags)
{
	int i;
	long l;
	OCSP_CERTID* cid = NULL;
	OCSP_ONEREQ *one = NULL;
	OCSP_REQINFO *inf = o->tbsRequest;
	OCSP_SIGNATURE *sig = o->optionalSignature;

	if (BIO_write(bp, "OCSP Request Data:\n", 19) <= 0)
		goto err;
	l = ASN1_INTEGER_get(inf->version);
	if (BIO_printf(bp, "    Version: %lu (0x%lx)", l+1, l) <= 0)
		goto err;
	if (inf->requestorName != NULL) {
		if (BIO_write(bp, "\n    Requestor Name: ", 21) <= 0)
			goto err;
		GENERAL_NAME_print(bp, inf->requestorName);
	}
	if (BIO_write(bp, "\n    Requestor List:\n", 21) <= 0)
		goto err;
	for (i = 0; i < sk_OCSP_ONEREQ_num(inf->requestList); i++) {
		one = sk_OCSP_ONEREQ_value(inf->requestList, i);
		cid = one->reqCert;
		ocsp_certid_print(bp, cid, 8);
		if (!X509V3_extensions_print(bp, "Request Single Extensions",
		    one->singleRequestExtensions, flags, 8))
			goto err;
	}
	if (!X509V3_extensions_print(bp, "Request Extensions",
	    inf->requestExtensions, flags, 4))
		goto err;
	if (sig) {
		if (X509_signature_print(bp, sig->signatureAlgorithm,
		    sig->signature) == 0)
			goto err;
		for (i = 0; i < sk_X509_num(sig->certs); i++) {
			if (X509_print(bp, sk_X509_value(sig->certs, i)) == 0)
				goto err;
			if (PEM_write_bio_X509(bp,
			    sk_X509_value(sig->certs, i)) == 0)
				goto err;
		}
	}
	return 1;

err:
	return 0;
}
LCRYPTO_ALIAS(OCSP_REQUEST_print);

int
OCSP_RESPONSE_print(BIO *bp, OCSP_RESPONSE* o, unsigned long flags)
{
	int i, ret = 0;
	long l;
	OCSP_CERTID *cid = NULL;
	OCSP_BASICRESP *br = NULL;
	OCSP_RESPID *rid = NULL;
	OCSP_RESPDATA  *rd = NULL;
	OCSP_CERTSTATUS *cst = NULL;
	OCSP_REVOKEDINFO *rev = NULL;
	OCSP_SINGLERESP *single = NULL;
	OCSP_RESPBYTES *rb = o->responseBytes;

	if (BIO_puts(bp, "OCSP Response Data:\n") <= 0)
		goto err;
	l = ASN1_ENUMERATED_get(o->responseStatus);
	if (BIO_printf(bp, "    OCSP Response Status: %s (0x%lx)\n",
	    OCSP_response_status_str(l), l) <= 0)
		goto err;
	if (rb == NULL)
		return 1;
	if (BIO_puts(bp, "    Response Type: ") <= 0)
		goto err;
	if (i2a_ASN1_OBJECT(bp, rb->responseType) <= 0)
		goto err;
	if (OBJ_obj2nid(rb->responseType) != NID_id_pkix_OCSP_basic) {
		BIO_puts(bp, " (unknown response type)\n");
		return 1;
	}

	i = ASN1_STRING_length(rb->response);
	if (!(br = OCSP_response_get1_basic(o)))
		goto err;
	rd = br->tbsResponseData;
	l = ASN1_INTEGER_get(rd->version);
	if (BIO_printf(bp, "\n    Version: %lu (0x%lx)\n", l+1, l) <= 0)
		goto err;
	if (BIO_puts(bp, "    Responder Id: ") <= 0)
		goto err;

	rid = rd->responderId;
	switch (rid->type) {
	case V_OCSP_RESPID_NAME:
		X509_NAME_print_ex(bp, rid->value.byName, 0, XN_FLAG_ONELINE);
		break;
	case V_OCSP_RESPID_KEY:
		i2a_ASN1_STRING(bp, rid->value.byKey, V_ASN1_OCTET_STRING);
		break;
	}

	if (BIO_printf(bp, "\n    Produced At: ")<=0)
		goto err;
	if (!ASN1_GENERALIZEDTIME_print(bp, rd->producedAt))
		goto err;
	if (BIO_printf(bp, "\n    Responses:\n") <= 0)
		goto err;
	for (i = 0; i < sk_OCSP_SINGLERESP_num(rd->responses); i++) {
		if (! sk_OCSP_SINGLERESP_value(rd->responses, i))
			continue;
		single = sk_OCSP_SINGLERESP_value(rd->responses, i);
		cid = single->certId;
		if (ocsp_certid_print(bp, cid, 4) <= 0)
			goto err;
		cst = single->certStatus;
		if (BIO_printf(bp, "    Cert Status: %s",
		    OCSP_cert_status_str(cst->type)) <= 0)
			goto err;
		if (cst->type == V_OCSP_CERTSTATUS_REVOKED) {
			rev = cst->value.revoked;
			if (BIO_printf(bp, "\n    Revocation Time: ") <= 0)
				goto err;
			if (!ASN1_GENERALIZEDTIME_print(bp,
			    rev->revocationTime))
				goto err;
			if (rev->revocationReason) {
				l = ASN1_ENUMERATED_get(rev->revocationReason);
				if (BIO_printf(bp,
				    "\n    Revocation Reason: %s (0x%lx)",
				    OCSP_crl_reason_str(l), l) <= 0)
					goto err;
			}
		}
		if (BIO_printf(bp, "\n    This Update: ") <= 0)
			goto err;
		if (!ASN1_GENERALIZEDTIME_print(bp, single->thisUpdate))
			goto err;
		if (single->nextUpdate) {
			if (BIO_printf(bp, "\n    Next Update: ") <= 0)
				goto err;
			if (!ASN1_GENERALIZEDTIME_print(bp, single->nextUpdate))
				goto err;
		}
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
		if (!X509V3_extensions_print(bp, "Response Single Extensions",
		    single->singleExtensions, flags, 8))
			goto err;
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
	}
	if (!X509V3_extensions_print(bp, "Response Extensions",
	    rd->responseExtensions, flags, 4))
		goto err;
	if (X509_signature_print(bp, br->signatureAlgorithm, br->signature) <=
	    0)
		goto err;

	for (i = 0; i < sk_X509_num(br->certs); i++) {
		X509_print(bp, sk_X509_value(br->certs, i));
		PEM_write_bio_X509(bp, sk_X509_value(br->certs, i));
	}

	ret = 1;

err:
	OCSP_BASICRESP_free(br);
	return ret;
}
LCRYPTO_ALIAS(OCSP_RESPONSE_print);
