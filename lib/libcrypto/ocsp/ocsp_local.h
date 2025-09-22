/* $OpenBSD: ocsp_local.h,v 1.2 2022/01/14 08:32:26 tb Exp $ */
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

#ifndef HEADER_OCSP_LOCAL_H
#define HEADER_OCSP_LOCAL_H

__BEGIN_HIDDEN_DECLS

/*   CertID ::= SEQUENCE {
 *       hashAlgorithm            AlgorithmIdentifier,
 *       issuerNameHash     OCTET STRING, -- Hash of Issuer's DN
 *       issuerKeyHash      OCTET STRING, -- Hash of Issuers public key (excluding the tag & length fields)
 *       serialNumber       CertificateSerialNumber }
 */
struct ocsp_cert_id_st {
	X509_ALGOR *hashAlgorithm;
	ASN1_OCTET_STRING *issuerNameHash;
	ASN1_OCTET_STRING *issuerKeyHash;
	ASN1_INTEGER *serialNumber;
} /* OCSP_CERTID */;

/*   Request ::=     SEQUENCE {
 *       reqCert                    CertID,
 *       singleRequestExtensions    [0] EXPLICIT Extensions OPTIONAL }
 */
struct ocsp_one_request_st {
	OCSP_CERTID *reqCert;
	STACK_OF(X509_EXTENSION) *singleRequestExtensions;
} /* OCSP_ONEREQ */;

/*   TBSRequest      ::=     SEQUENCE {
 *       version             [0] EXPLICIT Version DEFAULT v1,
 *       requestorName       [1] EXPLICIT GeneralName OPTIONAL,
 *       requestList             SEQUENCE OF Request,
 *       requestExtensions   [2] EXPLICIT Extensions OPTIONAL }
 */
struct ocsp_req_info_st {
	ASN1_INTEGER *version;
	GENERAL_NAME *requestorName;
	STACK_OF(OCSP_ONEREQ) *requestList;
	STACK_OF(X509_EXTENSION) *requestExtensions;
} /* OCSP_REQINFO */;

/*   Signature       ::=     SEQUENCE {
 *       signatureAlgorithm   AlgorithmIdentifier,
 *       signature            BIT STRING,
 *       certs                [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL }
 */
struct ocsp_signature_st {
	X509_ALGOR *signatureAlgorithm;
	ASN1_BIT_STRING *signature;
	STACK_OF(X509) *certs;
} /* OCSP_SIGNATURE */;

/*   OCSPRequest     ::=     SEQUENCE {
 *       tbsRequest                  TBSRequest,
 *       optionalSignature   [0]     EXPLICIT Signature OPTIONAL }
 */
struct ocsp_request_st {
	OCSP_REQINFO *tbsRequest;
	OCSP_SIGNATURE *optionalSignature; /* OPTIONAL */
} /* OCSP_REQUEST */;

/*   OCSPResponseStatus ::= ENUMERATED {
 *       successful            (0),      --Response has valid confirmations
 *       malformedRequest      (1),      --Illegal confirmation request
 *       internalError         (2),      --Internal error in issuer
 *       tryLater              (3),      --Try again later
 *                                       --(4) is not used
 *       sigRequired           (5),      --Must sign the request
 *       unauthorized          (6)       --Request unauthorized
 *   }
 */

/*   ResponseBytes ::=       SEQUENCE {
 *       responseType   OBJECT IDENTIFIER,
 *       response       OCTET STRING }
 */
struct ocsp_resp_bytes_st {
	ASN1_OBJECT *responseType;
	ASN1_OCTET_STRING *response;
} /* OCSP_RESPBYTES */;

/*   OCSPResponse ::= SEQUENCE {
 *      responseStatus         OCSPResponseStatus,
 *      responseBytes          [0] EXPLICIT ResponseBytes OPTIONAL }
 */
struct ocsp_response_st {
	ASN1_ENUMERATED *responseStatus;
	OCSP_RESPBYTES  *responseBytes;
};

/*   ResponderID ::= CHOICE {
 *      byName   [1] Name,
 *      byKey    [2] KeyHash }
 */
struct ocsp_responder_id_st {
	int type;
	union {
		X509_NAME* byName;
		ASN1_OCTET_STRING *byKey;
	} value;
};

/*   KeyHash ::= OCTET STRING --SHA-1 hash of responder's public key
 *                            --(excluding the tag and length fields)
 */

/*   RevokedInfo ::= SEQUENCE {
 *       revocationTime              GeneralizedTime,
 *       revocationReason    [0]     EXPLICIT CRLReason OPTIONAL }
 */
struct ocsp_revoked_info_st {
	ASN1_GENERALIZEDTIME *revocationTime;
	ASN1_ENUMERATED *revocationReason;
} /* OCSP_REVOKEDINFO */;

/*   CertStatus ::= CHOICE {
 *       good                [0]     IMPLICIT NULL,
 *       revoked             [1]     IMPLICIT RevokedInfo,
 *       unknown             [2]     IMPLICIT UnknownInfo }
 */
struct ocsp_cert_status_st {
	int type;
	union {
		ASN1_NULL *good;
		OCSP_REVOKEDINFO *revoked;
		ASN1_NULL *unknown;
	} value;
} /* OCSP_CERTSTATUS */;

/*   SingleResponse ::= SEQUENCE {
 *      certID                       CertID,
 *      certStatus                   CertStatus,
 *      thisUpdate                   GeneralizedTime,
 *      nextUpdate           [0]     EXPLICIT GeneralizedTime OPTIONAL,
 *      singleExtensions     [1]     EXPLICIT Extensions OPTIONAL }
 */
struct ocsp_single_response_st {
	OCSP_CERTID *certId;
	OCSP_CERTSTATUS *certStatus;
	ASN1_GENERALIZEDTIME *thisUpdate;
	ASN1_GENERALIZEDTIME *nextUpdate;
	STACK_OF(X509_EXTENSION) *singleExtensions;
} /* OCSP_SINGLERESP */;

/*   ResponseData ::= SEQUENCE {
 *      version              [0] EXPLICIT Version DEFAULT v1,
 *      responderID              ResponderID,
 *      producedAt               GeneralizedTime,
 *      responses                SEQUENCE OF SingleResponse,
 *      responseExtensions   [1] EXPLICIT Extensions OPTIONAL }
 */
struct ocsp_response_data_st {
	ASN1_INTEGER *version;
	OCSP_RESPID  *responderId;
	ASN1_GENERALIZEDTIME *producedAt;
	STACK_OF(OCSP_SINGLERESP) *responses;
	STACK_OF(X509_EXTENSION) *responseExtensions;
} /* OCSP_RESPDATA */;

/*   BasicOCSPResponse       ::= SEQUENCE {
 *      tbsResponseData      ResponseData,
 *      signatureAlgorithm   AlgorithmIdentifier,
 *      signature            BIT STRING,
 *      certs                [0] EXPLICIT SEQUENCE OF Certificate OPTIONAL }
 */
  /* Note 1:
     The value for "signature" is specified in the OCSP rfc2560 as follows:
     "The value for the signature SHALL be computed on the hash of the DER
     encoding ResponseData."  This means that you must hash the DER-encoded
     tbsResponseData, and then run it through a crypto-signing function, which
     will (at least w/RSA) do a hash-'n'-private-encrypt operation.  This seems
     a bit odd, but that's the spec.  Also note that the data structures do not
     leave anywhere to independently specify the algorithm used for the initial
     hash. So, we look at the signature-specification algorithm, and try to do
     something intelligent.	-- Kathy Weinhold, CertCo */
  /* Note 2:
     It seems that the mentioned passage from RFC 2560 (section 4.2.1) is open
     for interpretation.  I've done tests against another responder, and found
     that it doesn't do the double hashing that the RFC seems to say one
     should.  Therefore, all relevant functions take a flag saying which
     variant should be used.	-- Richard Levitte, OpenSSL team and CeloCom */
struct ocsp_basic_response_st {
	OCSP_RESPDATA *tbsResponseData;
	X509_ALGOR *signatureAlgorithm;
	ASN1_BIT_STRING *signature;
	STACK_OF(X509) *certs;
} /* OCSP_BASICRESP */;

/* CrlID ::= SEQUENCE {
 *     crlUrl               [0]     EXPLICIT IA5String OPTIONAL,
 *     crlNum               [1]     EXPLICIT INTEGER OPTIONAL,
 *     crlTime              [2]     EXPLICIT GeneralizedTime OPTIONAL }
 */
struct ocsp_crl_id_st {
	ASN1_IA5STRING *crlUrl;
	ASN1_INTEGER *crlNum;
	ASN1_GENERALIZEDTIME *crlTime;
} /* OCSP_CRLID */;

/* ServiceLocator ::= SEQUENCE {
 *      issuer    Name,
 *      locator   AuthorityInfoAccessSyntax OPTIONAL }
 */
struct ocsp_service_locator_st {
	X509_NAME* issuer;
	STACK_OF(ACCESS_DESCRIPTION) *locator;
} /* OCSP_SERVICELOC */;

#define OCSP_REQUEST_sign(o,pkey,md) \
    ASN1_item_sign(&OCSP_REQINFO_it, \
	(o)->optionalSignature->signatureAlgorithm, NULL, \
	(o)->optionalSignature->signature,o->tbsRequest, (pkey), (md))

#define OCSP_BASICRESP_sign(o,pkey,md,d) \
    ASN1_item_sign(&OCSP_RESPDATA_it,o->signatureAlgorithm,NULL, \
	(o)->signature,(o)->tbsResponseData,(pkey),(md))

#define OCSP_REQUEST_verify(a,r) \
    ASN1_item_verify(&OCSP_REQINFO_it, \
	(a)->optionalSignature->signatureAlgorithm, \
	(a)->optionalSignature->signature, (a)->tbsRequest, (r))

#define OCSP_BASICRESP_verify(a,r,d) \
    ASN1_item_verify(&OCSP_RESPDATA_it, \
	(a)->signatureAlgorithm, (a)->signature, (a)->tbsResponseData, (r))

__END_HIDDEN_DECLS

#endif /* !HEADER_OCSP_LOCAL_H */
