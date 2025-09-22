/* $OpenBSD: ocsp_asn.c,v 1.12 2024/07/08 14:53:11 beck Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/ocsp.h>

#include "ocsp_local.h"

static const ASN1_TEMPLATE OCSP_SIGNATURE_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SIGNATURE, signatureAlgorithm),
		.field_name = "signatureAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SIGNATURE, signature),
		.field_name = "signature",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_SIGNATURE, certs),
		.field_name = "certs",
		.item = &X509_it,
	},
};

const ASN1_ITEM OCSP_SIGNATURE_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_SIGNATURE_seq_tt,
	.tcount = sizeof(OCSP_SIGNATURE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_SIGNATURE),
	.sname = "OCSP_SIGNATURE",
};
LCRYPTO_ALIAS(OCSP_SIGNATURE_it);


OCSP_SIGNATURE *
d2i_OCSP_SIGNATURE(OCSP_SIGNATURE **a, const unsigned char **in, long len)
{
	return (OCSP_SIGNATURE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_SIGNATURE_it);
}
LCRYPTO_ALIAS(d2i_OCSP_SIGNATURE);

int
i2d_OCSP_SIGNATURE(OCSP_SIGNATURE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_SIGNATURE_it);
}
LCRYPTO_ALIAS(i2d_OCSP_SIGNATURE);

OCSP_SIGNATURE *
OCSP_SIGNATURE_new(void)
{
	return (OCSP_SIGNATURE *)ASN1_item_new(&OCSP_SIGNATURE_it);
}
LCRYPTO_ALIAS(OCSP_SIGNATURE_new);

void
OCSP_SIGNATURE_free(OCSP_SIGNATURE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_SIGNATURE_it);
}
LCRYPTO_ALIAS(OCSP_SIGNATURE_free);

static const ASN1_TEMPLATE OCSP_CERTID_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_CERTID, hashAlgorithm),
		.field_name = "hashAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_CERTID, issuerNameHash),
		.field_name = "issuerNameHash",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_CERTID, issuerKeyHash),
		.field_name = "issuerKeyHash",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_CERTID, serialNumber),
		.field_name = "serialNumber",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM OCSP_CERTID_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_CERTID_seq_tt,
	.tcount = sizeof(OCSP_CERTID_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_CERTID),
	.sname = "OCSP_CERTID",
};
LCRYPTO_ALIAS(OCSP_CERTID_it);


OCSP_CERTID *
d2i_OCSP_CERTID(OCSP_CERTID **a, const unsigned char **in, long len)
{
	return (OCSP_CERTID *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_CERTID_it);
}
LCRYPTO_ALIAS(d2i_OCSP_CERTID);

int
i2d_OCSP_CERTID(OCSP_CERTID *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_CERTID_it);
}
LCRYPTO_ALIAS(i2d_OCSP_CERTID);

OCSP_CERTID *
OCSP_CERTID_new(void)
{
	return (OCSP_CERTID *)ASN1_item_new(&OCSP_CERTID_it);
}
LCRYPTO_ALIAS(OCSP_CERTID_new);

void
OCSP_CERTID_free(OCSP_CERTID *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_CERTID_it);
}
LCRYPTO_ALIAS(OCSP_CERTID_free);

static const ASN1_TEMPLATE OCSP_ONEREQ_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_ONEREQ, reqCert),
		.field_name = "reqCert",
		.item = &OCSP_CERTID_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_ONEREQ, singleRequestExtensions),
		.field_name = "singleRequestExtensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM OCSP_ONEREQ_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_ONEREQ_seq_tt,
	.tcount = sizeof(OCSP_ONEREQ_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_ONEREQ),
	.sname = "OCSP_ONEREQ",
};
LCRYPTO_ALIAS(OCSP_ONEREQ_it);


OCSP_ONEREQ *
d2i_OCSP_ONEREQ(OCSP_ONEREQ **a, const unsigned char **in, long len)
{
	return (OCSP_ONEREQ *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_ONEREQ_it);
}
LCRYPTO_ALIAS(d2i_OCSP_ONEREQ);

int
i2d_OCSP_ONEREQ(OCSP_ONEREQ *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_ONEREQ_it);
}
LCRYPTO_ALIAS(i2d_OCSP_ONEREQ);

OCSP_ONEREQ *
OCSP_ONEREQ_new(void)
{
	return (OCSP_ONEREQ *)ASN1_item_new(&OCSP_ONEREQ_it);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_new);

void
OCSP_ONEREQ_free(OCSP_ONEREQ *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_ONEREQ_it);
}
LCRYPTO_ALIAS(OCSP_ONEREQ_free);

static const ASN1_TEMPLATE OCSP_REQINFO_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_REQINFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(OCSP_REQINFO, requestorName),
		.field_name = "requestorName",
		.item = &GENERAL_NAME_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(OCSP_REQINFO, requestList),
		.field_name = "requestList",
		.item = &OCSP_ONEREQ_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(OCSP_REQINFO, requestExtensions),
		.field_name = "requestExtensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM OCSP_REQINFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_REQINFO_seq_tt,
	.tcount = sizeof(OCSP_REQINFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_REQINFO),
	.sname = "OCSP_REQINFO",
};
LCRYPTO_ALIAS(OCSP_REQINFO_it);


OCSP_REQINFO *
d2i_OCSP_REQINFO(OCSP_REQINFO **a, const unsigned char **in, long len)
{
	return (OCSP_REQINFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_REQINFO_it);
}
LCRYPTO_ALIAS(d2i_OCSP_REQINFO);

int
i2d_OCSP_REQINFO(OCSP_REQINFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_REQINFO_it);
}
LCRYPTO_ALIAS(i2d_OCSP_REQINFO);

OCSP_REQINFO *
OCSP_REQINFO_new(void)
{
	return (OCSP_REQINFO *)ASN1_item_new(&OCSP_REQINFO_it);
}
LCRYPTO_ALIAS(OCSP_REQINFO_new);

void
OCSP_REQINFO_free(OCSP_REQINFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_REQINFO_it);
}
LCRYPTO_ALIAS(OCSP_REQINFO_free);

static const ASN1_TEMPLATE OCSP_REQUEST_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_REQUEST, tbsRequest),
		.field_name = "tbsRequest",
		.item = &OCSP_REQINFO_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_REQUEST, optionalSignature),
		.field_name = "optionalSignature",
		.item = &OCSP_SIGNATURE_it,
	},
};

const ASN1_ITEM OCSP_REQUEST_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_REQUEST_seq_tt,
	.tcount = sizeof(OCSP_REQUEST_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_REQUEST),
	.sname = "OCSP_REQUEST",
};
LCRYPTO_ALIAS(OCSP_REQUEST_it);

OCSP_REQUEST *
d2i_OCSP_REQUEST(OCSP_REQUEST **a, const unsigned char **in, long len)
{
	return (OCSP_REQUEST *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_REQUEST_it);
}
LCRYPTO_ALIAS(d2i_OCSP_REQUEST);

int
i2d_OCSP_REQUEST(OCSP_REQUEST *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_REQUEST_it);
}
LCRYPTO_ALIAS(i2d_OCSP_REQUEST);

OCSP_REQUEST *
d2i_OCSP_REQUEST_bio(BIO *bp, OCSP_REQUEST **a)
{
	return ASN1_item_d2i_bio(&OCSP_REQUEST_it, bp, a);
}
LCRYPTO_ALIAS(d2i_OCSP_REQUEST_bio);

int
i2d_OCSP_REQUEST_bio(BIO *bp, OCSP_REQUEST *a)
{
	return ASN1_item_i2d_bio(&OCSP_REQUEST_it, bp, a);
}
LCRYPTO_ALIAS(i2d_OCSP_REQUEST_bio);

OCSP_REQUEST *
OCSP_REQUEST_new(void)
{
	return (OCSP_REQUEST *)ASN1_item_new(&OCSP_REQUEST_it);
}
LCRYPTO_ALIAS(OCSP_REQUEST_new);

void
OCSP_REQUEST_free(OCSP_REQUEST *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_REQUEST_it);
}
LCRYPTO_ALIAS(OCSP_REQUEST_free);

/* OCSP_RESPONSE templates */

static const ASN1_TEMPLATE OCSP_RESPBYTES_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_RESPBYTES, responseType),
		.field_name = "responseType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_RESPBYTES, response),
		.field_name = "response",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM OCSP_RESPBYTES_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_RESPBYTES_seq_tt,
	.tcount = sizeof(OCSP_RESPBYTES_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_RESPBYTES),
	.sname = "OCSP_RESPBYTES",
};
LCRYPTO_ALIAS(OCSP_RESPBYTES_it);


OCSP_RESPBYTES *
d2i_OCSP_RESPBYTES(OCSP_RESPBYTES **a, const unsigned char **in, long len)
{
	return (OCSP_RESPBYTES *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_RESPBYTES_it);
}
LCRYPTO_ALIAS(d2i_OCSP_RESPBYTES);

int
i2d_OCSP_RESPBYTES(OCSP_RESPBYTES *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_RESPBYTES_it);
}
LCRYPTO_ALIAS(i2d_OCSP_RESPBYTES);

OCSP_RESPBYTES *
OCSP_RESPBYTES_new(void)
{
	return (OCSP_RESPBYTES *)ASN1_item_new(&OCSP_RESPBYTES_it);
}
LCRYPTO_ALIAS(OCSP_RESPBYTES_new);

void
OCSP_RESPBYTES_free(OCSP_RESPBYTES *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_RESPBYTES_it);
}
LCRYPTO_ALIAS(OCSP_RESPBYTES_free);

static const ASN1_TEMPLATE OCSP_RESPONSE_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_RESPONSE, responseStatus),
		.field_name = "responseStatus",
		.item = &ASN1_ENUMERATED_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_RESPONSE, responseBytes),
		.field_name = "responseBytes",
		.item = &OCSP_RESPBYTES_it,
	},
};

const ASN1_ITEM OCSP_RESPONSE_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_RESPONSE_seq_tt,
	.tcount = sizeof(OCSP_RESPONSE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_RESPONSE),
	.sname = "OCSP_RESPONSE",
};
LCRYPTO_ALIAS(OCSP_RESPONSE_it);


OCSP_RESPONSE *
d2i_OCSP_RESPONSE(OCSP_RESPONSE **a, const unsigned char **in, long len)
{
	return (OCSP_RESPONSE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_RESPONSE_it);
}
LCRYPTO_ALIAS(d2i_OCSP_RESPONSE);

int
i2d_OCSP_RESPONSE(OCSP_RESPONSE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_RESPONSE_it);
}
LCRYPTO_ALIAS(i2d_OCSP_RESPONSE);

OCSP_RESPONSE *
d2i_OCSP_RESPONSE_bio(BIO *bp, OCSP_RESPONSE **a)
{
	return ASN1_item_d2i_bio(&OCSP_RESPONSE_it, bp, a);
}
LCRYPTO_ALIAS(d2i_OCSP_RESPONSE_bio);

int
i2d_OCSP_RESPONSE_bio(BIO *bp, OCSP_RESPONSE *a)
{
	return ASN1_item_i2d_bio(&OCSP_RESPONSE_it, bp, a);
}
LCRYPTO_ALIAS(i2d_OCSP_RESPONSE_bio);

OCSP_RESPONSE *
OCSP_RESPONSE_new(void)
{
	return (OCSP_RESPONSE *)ASN1_item_new(&OCSP_RESPONSE_it);
}
LCRYPTO_ALIAS(OCSP_RESPONSE_new);

void
OCSP_RESPONSE_free(OCSP_RESPONSE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_RESPONSE_it);
}
LCRYPTO_ALIAS(OCSP_RESPONSE_free);

static const ASN1_TEMPLATE OCSP_RESPID_ch_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = 1,
		.offset = offsetof(OCSP_RESPID, value.byName),
		.field_name = "value.byName",
		.item = &X509_NAME_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = 2,
		.offset = offsetof(OCSP_RESPID, value.byKey),
		.field_name = "value.byKey",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM OCSP_RESPID_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(OCSP_RESPID, type),
	.templates = OCSP_RESPID_ch_tt,
	.tcount = sizeof(OCSP_RESPID_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_RESPID),
	.sname = "OCSP_RESPID",
};
LCRYPTO_ALIAS(OCSP_RESPID_it);


OCSP_RESPID *
d2i_OCSP_RESPID(OCSP_RESPID **a, const unsigned char **in, long len)
{
	return (OCSP_RESPID *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_RESPID_it);
}
LCRYPTO_ALIAS(d2i_OCSP_RESPID);

int
i2d_OCSP_RESPID(OCSP_RESPID *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_RESPID_it);
}
LCRYPTO_ALIAS(i2d_OCSP_RESPID);

OCSP_RESPID *
OCSP_RESPID_new(void)
{
	return (OCSP_RESPID *)ASN1_item_new(&OCSP_RESPID_it);
}
LCRYPTO_ALIAS(OCSP_RESPID_new);

void
OCSP_RESPID_free(OCSP_RESPID *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_RESPID_it);
}
LCRYPTO_ALIAS(OCSP_RESPID_free);

static const ASN1_TEMPLATE OCSP_REVOKEDINFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_REVOKEDINFO, revocationTime),
		.field_name = "revocationTime",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_REVOKEDINFO, revocationReason),
		.field_name = "revocationReason",
		.item = &ASN1_ENUMERATED_it,
	},
};

const ASN1_ITEM OCSP_REVOKEDINFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_REVOKEDINFO_seq_tt,
	.tcount = sizeof(OCSP_REVOKEDINFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_REVOKEDINFO),
	.sname = "OCSP_REVOKEDINFO",
};
LCRYPTO_ALIAS(OCSP_REVOKEDINFO_it);


OCSP_REVOKEDINFO *
d2i_OCSP_REVOKEDINFO(OCSP_REVOKEDINFO **a, const unsigned char **in, long len)
{
	return (OCSP_REVOKEDINFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_REVOKEDINFO_it);
}
LCRYPTO_ALIAS(d2i_OCSP_REVOKEDINFO);

int
i2d_OCSP_REVOKEDINFO(OCSP_REVOKEDINFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_REVOKEDINFO_it);
}
LCRYPTO_ALIAS(i2d_OCSP_REVOKEDINFO);

OCSP_REVOKEDINFO *
OCSP_REVOKEDINFO_new(void)
{
	return (OCSP_REVOKEDINFO *)ASN1_item_new(&OCSP_REVOKEDINFO_it);
}
LCRYPTO_ALIAS(OCSP_REVOKEDINFO_new);

void
OCSP_REVOKEDINFO_free(OCSP_REVOKEDINFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_REVOKEDINFO_it);
}
LCRYPTO_ALIAS(OCSP_REVOKEDINFO_free);

static const ASN1_TEMPLATE OCSP_CERTSTATUS_ch_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(OCSP_CERTSTATUS, value.good),
		.field_name = "value.good",
		.item = &ASN1_NULL_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(OCSP_CERTSTATUS, value.revoked),
		.field_name = "value.revoked",
		.item = &OCSP_REVOKEDINFO_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 2,
		.offset = offsetof(OCSP_CERTSTATUS, value.unknown),
		.field_name = "value.unknown",
		.item = &ASN1_NULL_it,
	},
};

const ASN1_ITEM OCSP_CERTSTATUS_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(OCSP_CERTSTATUS, type),
	.templates = OCSP_CERTSTATUS_ch_tt,
	.tcount = sizeof(OCSP_CERTSTATUS_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_CERTSTATUS),
	.sname = "OCSP_CERTSTATUS",
};
LCRYPTO_ALIAS(OCSP_CERTSTATUS_it);


OCSP_CERTSTATUS *
d2i_OCSP_CERTSTATUS(OCSP_CERTSTATUS **a, const unsigned char **in, long len)
{
	return (OCSP_CERTSTATUS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_CERTSTATUS_it);
}
LCRYPTO_ALIAS(d2i_OCSP_CERTSTATUS);

int
i2d_OCSP_CERTSTATUS(OCSP_CERTSTATUS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_CERTSTATUS_it);
}
LCRYPTO_ALIAS(i2d_OCSP_CERTSTATUS);

OCSP_CERTSTATUS *
OCSP_CERTSTATUS_new(void)
{
	return (OCSP_CERTSTATUS *)ASN1_item_new(&OCSP_CERTSTATUS_it);
}
LCRYPTO_ALIAS(OCSP_CERTSTATUS_new);

void
OCSP_CERTSTATUS_free(OCSP_CERTSTATUS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_CERTSTATUS_it);
}
LCRYPTO_ALIAS(OCSP_CERTSTATUS_free);

static const ASN1_TEMPLATE OCSP_SINGLERESP_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SINGLERESP, certId),
		.field_name = "certId",
		.item = &OCSP_CERTID_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SINGLERESP, certStatus),
		.field_name = "certStatus",
		.item = &OCSP_CERTSTATUS_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SINGLERESP, thisUpdate),
		.field_name = "thisUpdate",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_SINGLERESP, nextUpdate),
		.field_name = "nextUpdate",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(OCSP_SINGLERESP, singleExtensions),
		.field_name = "singleExtensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM OCSP_SINGLERESP_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_SINGLERESP_seq_tt,
	.tcount = sizeof(OCSP_SINGLERESP_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_SINGLERESP),
	.sname = "OCSP_SINGLERESP",
};
LCRYPTO_ALIAS(OCSP_SINGLERESP_it);


OCSP_SINGLERESP *
d2i_OCSP_SINGLERESP(OCSP_SINGLERESP **a, const unsigned char **in, long len)
{
	return (OCSP_SINGLERESP *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_SINGLERESP_it);
}
LCRYPTO_ALIAS(d2i_OCSP_SINGLERESP);

int
i2d_OCSP_SINGLERESP(OCSP_SINGLERESP *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_SINGLERESP_it);
}
LCRYPTO_ALIAS(i2d_OCSP_SINGLERESP);

OCSP_SINGLERESP *
OCSP_SINGLERESP_new(void)
{
	return (OCSP_SINGLERESP *)ASN1_item_new(&OCSP_SINGLERESP_it);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_new);

void
OCSP_SINGLERESP_free(OCSP_SINGLERESP *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_SINGLERESP_it);
}
LCRYPTO_ALIAS(OCSP_SINGLERESP_free);

static const ASN1_TEMPLATE OCSP_RESPDATA_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_RESPDATA, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_RESPDATA, responderId),
		.field_name = "responderId",
		.item = &OCSP_RESPID_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_RESPDATA, producedAt),
		.field_name = "producedAt",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(OCSP_RESPDATA, responses),
		.field_name = "responses",
		.item = &OCSP_SINGLERESP_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(OCSP_RESPDATA, responseExtensions),
		.field_name = "responseExtensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM OCSP_RESPDATA_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_RESPDATA_seq_tt,
	.tcount = sizeof(OCSP_RESPDATA_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_RESPDATA),
	.sname = "OCSP_RESPDATA",
};
LCRYPTO_ALIAS(OCSP_RESPDATA_it);


OCSP_RESPDATA *
d2i_OCSP_RESPDATA(OCSP_RESPDATA **a, const unsigned char **in, long len)
{
	return (OCSP_RESPDATA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_RESPDATA_it);
}
LCRYPTO_ALIAS(d2i_OCSP_RESPDATA);

int
i2d_OCSP_RESPDATA(OCSP_RESPDATA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_RESPDATA_it);
}
LCRYPTO_ALIAS(i2d_OCSP_RESPDATA);

OCSP_RESPDATA *
OCSP_RESPDATA_new(void)
{
	return (OCSP_RESPDATA *)ASN1_item_new(&OCSP_RESPDATA_it);
}
LCRYPTO_ALIAS(OCSP_RESPDATA_new);

void
OCSP_RESPDATA_free(OCSP_RESPDATA *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_RESPDATA_it);
}
LCRYPTO_ALIAS(OCSP_RESPDATA_free);

static const ASN1_TEMPLATE OCSP_BASICRESP_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_BASICRESP, tbsResponseData),
		.field_name = "tbsResponseData",
		.item = &OCSP_RESPDATA_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_BASICRESP, signatureAlgorithm),
		.field_name = "signatureAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_BASICRESP, signature),
		.field_name = "signature",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_BASICRESP, certs),
		.field_name = "certs",
		.item = &X509_it,
	},
};

const ASN1_ITEM OCSP_BASICRESP_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_BASICRESP_seq_tt,
	.tcount = sizeof(OCSP_BASICRESP_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_BASICRESP),
	.sname = "OCSP_BASICRESP",
};
LCRYPTO_ALIAS(OCSP_BASICRESP_it);


OCSP_BASICRESP *
d2i_OCSP_BASICRESP(OCSP_BASICRESP **a, const unsigned char **in, long len)
{
	return (OCSP_BASICRESP *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_BASICRESP_it);
}
LCRYPTO_ALIAS(d2i_OCSP_BASICRESP);

int
i2d_OCSP_BASICRESP(OCSP_BASICRESP *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_BASICRESP_it);
}
LCRYPTO_ALIAS(i2d_OCSP_BASICRESP);

OCSP_BASICRESP *
OCSP_BASICRESP_new(void)
{
	return (OCSP_BASICRESP *)ASN1_item_new(&OCSP_BASICRESP_it);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_new);

void
OCSP_BASICRESP_free(OCSP_BASICRESP *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_BASICRESP_it);
}
LCRYPTO_ALIAS(OCSP_BASICRESP_free);

static const ASN1_TEMPLATE OCSP_CRLID_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_CRLID, crlUrl),
		.field_name = "crlUrl",
		.item = &ASN1_IA5STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(OCSP_CRLID, crlNum),
		.field_name = "crlNum",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(OCSP_CRLID, crlTime),
		.field_name = "crlTime",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
};

const ASN1_ITEM OCSP_CRLID_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_CRLID_seq_tt,
	.tcount = sizeof(OCSP_CRLID_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_CRLID),
	.sname = "OCSP_CRLID",
};
LCRYPTO_ALIAS(OCSP_CRLID_it);


OCSP_CRLID *
d2i_OCSP_CRLID(OCSP_CRLID **a, const unsigned char **in, long len)
{
	return (OCSP_CRLID *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_CRLID_it);
}
LCRYPTO_ALIAS(d2i_OCSP_CRLID);

int
i2d_OCSP_CRLID(OCSP_CRLID *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_CRLID_it);
}
LCRYPTO_ALIAS(i2d_OCSP_CRLID);

OCSP_CRLID *
OCSP_CRLID_new(void)
{
	return (OCSP_CRLID *)ASN1_item_new(&OCSP_CRLID_it);
}
LCRYPTO_ALIAS(OCSP_CRLID_new);

void
OCSP_CRLID_free(OCSP_CRLID *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_CRLID_it);
}
LCRYPTO_ALIAS(OCSP_CRLID_free);

static const ASN1_TEMPLATE OCSP_SERVICELOC_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OCSP_SERVICELOC, issuer),
		.field_name = "issuer",
		.item = &X509_NAME_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(OCSP_SERVICELOC, locator),
		.field_name = "locator",
		.item = &ACCESS_DESCRIPTION_it,
	},
};

const ASN1_ITEM OCSP_SERVICELOC_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OCSP_SERVICELOC_seq_tt,
	.tcount = sizeof(OCSP_SERVICELOC_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OCSP_SERVICELOC),
	.sname = "OCSP_SERVICELOC",
};
LCRYPTO_ALIAS(OCSP_SERVICELOC_it);


OCSP_SERVICELOC *
d2i_OCSP_SERVICELOC(OCSP_SERVICELOC **a, const unsigned char **in, long len)
{
	return (OCSP_SERVICELOC *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OCSP_SERVICELOC_it);
}
LCRYPTO_ALIAS(d2i_OCSP_SERVICELOC);

int
i2d_OCSP_SERVICELOC(OCSP_SERVICELOC *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OCSP_SERVICELOC_it);
}
LCRYPTO_ALIAS(i2d_OCSP_SERVICELOC);

OCSP_SERVICELOC *
OCSP_SERVICELOC_new(void)
{
	return (OCSP_SERVICELOC *)ASN1_item_new(&OCSP_SERVICELOC_it);
}
LCRYPTO_ALIAS(OCSP_SERVICELOC_new);

void
OCSP_SERVICELOC_free(OCSP_SERVICELOC *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OCSP_SERVICELOC_it);
}
LCRYPTO_ALIAS(OCSP_SERVICELOC_free);
