/* $OpenBSD: cms_asn1.c,v 1.25 2024/11/01 18:53:35 tb Exp $ */
/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
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
 */

#include <stddef.h>
#include <stdlib.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "cms_local.h"

static const ASN1_TEMPLATE CMS_IssuerAndSerialNumber_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_IssuerAndSerialNumber, issuer),
		.field_name = "issuer",
		.item = &X509_NAME_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_IssuerAndSerialNumber, serialNumber),
		.field_name = "serialNumber",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM CMS_IssuerAndSerialNumber_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_IssuerAndSerialNumber_seq_tt,
	.tcount = sizeof(CMS_IssuerAndSerialNumber_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_IssuerAndSerialNumber),
	.sname = "CMS_IssuerAndSerialNumber",
};

static const ASN1_TEMPLATE CMS_OtherCertificateFormat_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OtherCertificateFormat, otherCertFormat),
		.field_name = "otherCertFormat",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_OtherCertificateFormat, otherCert),
		.field_name = "otherCert",
		.item = &ASN1_ANY_it,
	},
};

static const ASN1_ITEM CMS_OtherCertificateFormat_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OtherCertificateFormat_seq_tt,
	.tcount = sizeof(CMS_OtherCertificateFormat_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OtherCertificateFormat),
	.sname = "CMS_OtherCertificateFormat",
};

static const ASN1_TEMPLATE CMS_CertificateChoices_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_CertificateChoices, d.certificate),
		.field_name = "d.certificate",
		.item = &X509_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_CertificateChoices, d.extendedCertificate),
		.field_name = "d.extendedCertificate",
		.item = &ASN1_SEQUENCE_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(CMS_CertificateChoices, d.v1AttrCert),
		.field_name = "d.v1AttrCert",
		.item = &ASN1_SEQUENCE_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 2,
		.offset = offsetof(CMS_CertificateChoices, d.v2AttrCert),
		.field_name = "d.v2AttrCert",
		.item = &ASN1_SEQUENCE_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 3,
		.offset = offsetof(CMS_CertificateChoices, d.other),
		.field_name = "d.other",
		.item = &CMS_OtherCertificateFormat_it,
	},
};

const ASN1_ITEM CMS_CertificateChoices_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_CertificateChoices, type),
	.templates = CMS_CertificateChoices_ch_tt,
	.tcount = sizeof(CMS_CertificateChoices_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_CertificateChoices),
	.sname = "CMS_CertificateChoices",
};

static const ASN1_TEMPLATE CMS_SignerIdentifier_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerIdentifier, d.issuerAndSerialNumber),
		.field_name = "d.issuerAndSerialNumber",
		.item = &CMS_IssuerAndSerialNumber_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_SignerIdentifier, d.subjectKeyIdentifier),
		.field_name = "d.subjectKeyIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
};

static const ASN1_ITEM CMS_SignerIdentifier_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_SignerIdentifier, type),
	.templates = CMS_SignerIdentifier_ch_tt,
	.tcount = sizeof(CMS_SignerIdentifier_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_SignerIdentifier),
	.sname = "CMS_SignerIdentifier",
};

static const ASN1_TEMPLATE CMS_EncapsulatedContentInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EncapsulatedContentInfo, eContentType),
		.field_name = "eContentType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
		.tag = 0,
		.offset = offsetof(CMS_EncapsulatedContentInfo, eContent),
		.field_name = "eContent",
		.item = &ASN1_OCTET_STRING_NDEF_it,
	},
};

static const ASN1_ITEM CMS_EncapsulatedContentInfo_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_EncapsulatedContentInfo_seq_tt,
	.tcount = sizeof(CMS_EncapsulatedContentInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_EncapsulatedContentInfo),
	.sname = "CMS_EncapsulatedContentInfo",
};

/* Minor tweak to operation: free up signer key, cert */
static int
cms_si_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_FREE_POST) {
		CMS_SignerInfo *si = (CMS_SignerInfo *)*pval;
		EVP_PKEY_free(si->pkey);
		X509_free(si->signer);
		EVP_MD_CTX_free(si->mctx);
	}
	return 1;
}

static const ASN1_AUX CMS_SignerInfo_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = cms_si_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE CMS_SignerInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, sid),
		.field_name = "sid",
		.item = &CMS_SignerIdentifier_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, digestAlgorithm),
		.field_name = "digestAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, signedAttrs),
		.field_name = "signedAttrs",
		.item = &X509_ATTRIBUTE_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, signatureAlgorithm),
		.field_name = "signatureAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignerInfo, signature),
		.field_name = "signature",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_SignerInfo, unsignedAttrs),
		.field_name = "unsignedAttrs",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM CMS_SignerInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_SignerInfo_seq_tt,
	.tcount = sizeof(CMS_SignerInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &CMS_SignerInfo_aux,
	.size = sizeof(CMS_SignerInfo),
	.sname = "CMS_SignerInfo",
};

static const ASN1_TEMPLATE CMS_OtherRevocationInfoFormat_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OtherRevocationInfoFormat, otherRevInfoFormat),
		.field_name = "otherRevInfoFormat",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_OtherRevocationInfoFormat, otherRevInfo),
		.field_name = "otherRevInfo",
		.item = &ASN1_ANY_it,
	},
};

static const ASN1_ITEM CMS_OtherRevocationInfoFormat_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OtherRevocationInfoFormat_seq_tt,
	.tcount = sizeof(CMS_OtherRevocationInfoFormat_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OtherRevocationInfoFormat),
	.sname = "CMS_OtherRevocationInfoFormat",
};

static const ASN1_TEMPLATE CMS_RevocationInfoChoice_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_RevocationInfoChoice, d.crl),
		.field_name = "d.crl",
		.item = &X509_CRL_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(CMS_RevocationInfoChoice, d.other),
		.field_name = "d.other",
		.item = &CMS_OtherRevocationInfoFormat_it,
	},
};

const ASN1_ITEM CMS_RevocationInfoChoice_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_RevocationInfoChoice, type),
	.templates = CMS_RevocationInfoChoice_ch_tt,
	.tcount = sizeof(CMS_RevocationInfoChoice_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_RevocationInfoChoice),
	.sname = "CMS_RevocationInfoChoice",
};

static const ASN1_TEMPLATE CMS_SignedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(CMS_SignedData, digestAlgorithms),
		.field_name = "digestAlgorithms",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SignedData, encapContentInfo),
		.field_name = "encapContentInfo",
		.item = &CMS_EncapsulatedContentInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_SignedData, certificates),
		.field_name = "certificates",
		.item = &CMS_CertificateChoices_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_SignedData, crls),
		.field_name = "crls",
		.item = &CMS_RevocationInfoChoice_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(CMS_SignedData, signerInfos),
		.field_name = "signerInfos",
		.item = &CMS_SignerInfo_it,
	},
};

const ASN1_ITEM CMS_SignedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_SignedData_seq_tt,
	.tcount = sizeof(CMS_SignedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_SignedData),
	.sname = "CMS_SignedData",
};

static const ASN1_TEMPLATE CMS_OriginatorInfo_seq_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_OriginatorInfo, certificates),
		.field_name = "certificates",
		.item = &CMS_CertificateChoices_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_OriginatorInfo, crls),
		.field_name = "crls",
		.item = &CMS_RevocationInfoChoice_it,
	},
};

static const ASN1_ITEM CMS_OriginatorInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OriginatorInfo_seq_tt,
	.tcount = sizeof(CMS_OriginatorInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OriginatorInfo),
	.sname = "CMS_OriginatorInfo",
};

static const ASN1_TEMPLATE CMS_EncryptedContentInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EncryptedContentInfo, contentType),
		.field_name = "contentType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EncryptedContentInfo, contentEncryptionAlgorithm),
		.field_name = "contentEncryptionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_EncryptedContentInfo, encryptedContent),
		.field_name = "encryptedContent",
		.item = &ASN1_OCTET_STRING_NDEF_it,
	},
};

static const ASN1_ITEM CMS_EncryptedContentInfo_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_EncryptedContentInfo_seq_tt,
	.tcount = sizeof(CMS_EncryptedContentInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_EncryptedContentInfo),
	.sname = "CMS_EncryptedContentInfo",
};

static const ASN1_TEMPLATE CMS_KeyTransRecipientInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyTransRecipientInfo, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyTransRecipientInfo, rid),
		.field_name = "rid",
		.item = &CMS_SignerIdentifier_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyTransRecipientInfo, keyEncryptionAlgorithm),
		.field_name = "keyEncryptionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyTransRecipientInfo, encryptedKey),
		.field_name = "encryptedKey",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_KeyTransRecipientInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_KeyTransRecipientInfo_seq_tt,
	.tcount = sizeof(CMS_KeyTransRecipientInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_KeyTransRecipientInfo),
	.sname = "CMS_KeyTransRecipientInfo",
};

static const ASN1_TEMPLATE CMS_OtherKeyAttribute_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OtherKeyAttribute, keyAttrId),
		.field_name = "keyAttrId",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_OtherKeyAttribute, keyAttr),
		.field_name = "keyAttr",
		.item = &ASN1_ANY_it,
	},
};

const ASN1_ITEM CMS_OtherKeyAttribute_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OtherKeyAttribute_seq_tt,
	.tcount = sizeof(CMS_OtherKeyAttribute_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OtherKeyAttribute),
	.sname = "CMS_OtherKeyAttribute",
};

static const ASN1_TEMPLATE CMS_RecipientKeyIdentifier_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_RecipientKeyIdentifier, subjectKeyIdentifier),
		.field_name = "subjectKeyIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_RecipientKeyIdentifier, date),
		.field_name = "date",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_RecipientKeyIdentifier, other),
		.field_name = "other",
		.item = &CMS_OtherKeyAttribute_it,
	},
};

const ASN1_ITEM CMS_RecipientKeyIdentifier_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_RecipientKeyIdentifier_seq_tt,
	.tcount = sizeof(CMS_RecipientKeyIdentifier_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_RecipientKeyIdentifier),
	.sname = "CMS_RecipientKeyIdentifier",
};

static const ASN1_TEMPLATE CMS_KeyAgreeRecipientIdentifier_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientIdentifier, d.issuerAndSerialNumber),
		.field_name = "d.issuerAndSerialNumber",
		.item = &CMS_IssuerAndSerialNumber_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientIdentifier, d.rKeyId),
		.field_name = "d.rKeyId",
		.item = &CMS_RecipientKeyIdentifier_it,
	},
};

static const ASN1_ITEM CMS_KeyAgreeRecipientIdentifier_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_KeyAgreeRecipientIdentifier, type),
	.templates = CMS_KeyAgreeRecipientIdentifier_ch_tt,
	.tcount = sizeof(CMS_KeyAgreeRecipientIdentifier_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_KeyAgreeRecipientIdentifier),
	.sname = "CMS_KeyAgreeRecipientIdentifier",
};

static int
cms_rek_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	CMS_RecipientEncryptedKey *rek = (CMS_RecipientEncryptedKey *)*pval;
	if (operation == ASN1_OP_FREE_POST) {
		EVP_PKEY_free(rek->pkey);
	}
	return 1;
}

static const ASN1_AUX CMS_RecipientEncryptedKey_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = cms_rek_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE CMS_RecipientEncryptedKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_RecipientEncryptedKey, rid),
		.field_name = "rid",
		.item = &CMS_KeyAgreeRecipientIdentifier_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_RecipientEncryptedKey, encryptedKey),
		.field_name = "encryptedKey",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_RecipientEncryptedKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_RecipientEncryptedKey_seq_tt,
	.tcount = sizeof(CMS_RecipientEncryptedKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &CMS_RecipientEncryptedKey_aux,
	.size = sizeof(CMS_RecipientEncryptedKey),
	.sname = "CMS_RecipientEncryptedKey",
};

static const ASN1_TEMPLATE CMS_OriginatorPublicKey_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OriginatorPublicKey, algorithm),
		.field_name = "algorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OriginatorPublicKey, publicKey),
		.field_name = "publicKey",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM CMS_OriginatorPublicKey_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OriginatorPublicKey_seq_tt,
	.tcount = sizeof(CMS_OriginatorPublicKey_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OriginatorPublicKey),
	.sname = "CMS_OriginatorPublicKey",
};

static const ASN1_TEMPLATE CMS_OriginatorIdentifierOrKey_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OriginatorIdentifierOrKey, d.issuerAndSerialNumber),
		.field_name = "d.issuerAndSerialNumber",
		.item = &CMS_IssuerAndSerialNumber_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_OriginatorIdentifierOrKey, d.subjectKeyIdentifier),
		.field_name = "d.subjectKeyIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(CMS_OriginatorIdentifierOrKey, d.originatorKey),
		.field_name = "d.originatorKey",
		.item = &CMS_OriginatorPublicKey_it,
	},
};

static const ASN1_ITEM CMS_OriginatorIdentifierOrKey_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_OriginatorIdentifierOrKey, type),
	.templates = CMS_OriginatorIdentifierOrKey_ch_tt,
	.tcount = sizeof(CMS_OriginatorIdentifierOrKey_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OriginatorIdentifierOrKey),
	.sname = "CMS_OriginatorIdentifierOrKey",
};

static int
cms_kari_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	CMS_KeyAgreeRecipientInfo *kari = (CMS_KeyAgreeRecipientInfo *)*pval;
	if (operation == ASN1_OP_NEW_POST) {
		kari->ctx = EVP_CIPHER_CTX_new();
		if (kari->ctx == NULL)
			return 0;
		EVP_CIPHER_CTX_set_flags(kari->ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
		kari->pctx = NULL;
	} else if (operation == ASN1_OP_FREE_POST) {
		EVP_PKEY_CTX_free(kari->pctx);
		EVP_CIPHER_CTX_free(kari->ctx);
	}
	return 1;
}

static const ASN1_AUX CMS_KeyAgreeRecipientInfo_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = cms_kari_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE CMS_KeyAgreeRecipientInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientInfo, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientInfo, originator),
		.field_name = "originator",
		.item = &CMS_OriginatorIdentifierOrKey_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_KeyAgreeRecipientInfo, ukm),
		.field_name = "ukm",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientInfo, keyEncryptionAlgorithm),
		.field_name = "keyEncryptionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(CMS_KeyAgreeRecipientInfo, recipientEncryptedKeys),
		.field_name = "recipientEncryptedKeys",
		.item = &CMS_RecipientEncryptedKey_it,
	},
};

const ASN1_ITEM CMS_KeyAgreeRecipientInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_KeyAgreeRecipientInfo_seq_tt,
	.tcount = sizeof(CMS_KeyAgreeRecipientInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &CMS_KeyAgreeRecipientInfo_aux,
	.size = sizeof(CMS_KeyAgreeRecipientInfo),
	.sname = "CMS_KeyAgreeRecipientInfo",
};

static const ASN1_TEMPLATE CMS_KEKIdentifier_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KEKIdentifier, keyIdentifier),
		.field_name = "keyIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_KEKIdentifier, date),
		.field_name = "date",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_KEKIdentifier, other),
		.field_name = "other",
		.item = &CMS_OtherKeyAttribute_it,
	},
};

static const ASN1_ITEM CMS_KEKIdentifier_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_KEKIdentifier_seq_tt,
	.tcount = sizeof(CMS_KEKIdentifier_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_KEKIdentifier),
	.sname = "CMS_KEKIdentifier",
};

static const ASN1_TEMPLATE CMS_KEKRecipientInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KEKRecipientInfo, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KEKRecipientInfo, kekid),
		.field_name = "kekid",
		.item = &CMS_KEKIdentifier_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KEKRecipientInfo, keyEncryptionAlgorithm),
		.field_name = "keyEncryptionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_KEKRecipientInfo, encryptedKey),
		.field_name = "encryptedKey",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_KEKRecipientInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_KEKRecipientInfo_seq_tt,
	.tcount = sizeof(CMS_KEKRecipientInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_KEKRecipientInfo),
	.sname = "CMS_KEKRecipientInfo",
};

static const ASN1_TEMPLATE CMS_PasswordRecipientInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_PasswordRecipientInfo, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_PasswordRecipientInfo, keyDerivationAlgorithm),
		.field_name = "keyDerivationAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_PasswordRecipientInfo, keyEncryptionAlgorithm),
		.field_name = "keyEncryptionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_PasswordRecipientInfo, encryptedKey),
		.field_name = "encryptedKey",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_PasswordRecipientInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_PasswordRecipientInfo_seq_tt,
	.tcount = sizeof(CMS_PasswordRecipientInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_PasswordRecipientInfo),
	.sname = "CMS_PasswordRecipientInfo",
};

static const ASN1_TEMPLATE CMS_OtherRecipientInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_OtherRecipientInfo, oriType),
		.field_name = "oriType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_OtherRecipientInfo, oriValue),
		.field_name = "oriValue",
		.item = &ASN1_ANY_it,
	},
};

static const ASN1_ITEM CMS_OtherRecipientInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_OtherRecipientInfo_seq_tt,
	.tcount = sizeof(CMS_OtherRecipientInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_OtherRecipientInfo),
	.sname = "CMS_OtherRecipientInfo",
};

/* Free up RecipientInfo additional data */
static int
cms_ri_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_FREE_PRE) {
		CMS_RecipientInfo *ri = (CMS_RecipientInfo *)*pval;
		if (ri->type == CMS_RECIPINFO_TRANS) {
			CMS_KeyTransRecipientInfo *ktri = ri->d.ktri;
			EVP_PKEY_free(ktri->pkey);
			X509_free(ktri->recip);
			EVP_PKEY_CTX_free(ktri->pctx);
		} else if (ri->type == CMS_RECIPINFO_KEK) {
			CMS_KEKRecipientInfo *kekri = ri->d.kekri;
			freezero(kekri->key, kekri->keylen);
		} else if (ri->type == CMS_RECIPINFO_PASS) {
			CMS_PasswordRecipientInfo *pwri = ri->d.pwri;
			freezero(pwri->pass, pwri->passlen);
		}
	}
	return 1;
}

static const ASN1_AUX CMS_RecipientInfo_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = cms_ri_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE CMS_RecipientInfo_ch_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_RecipientInfo, d.ktri),
		.field_name = "d.ktri",
		.item = &CMS_KeyTransRecipientInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(CMS_RecipientInfo, d.kari),
		.field_name = "d.kari",
		.item = &CMS_KeyAgreeRecipientInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 2,
		.offset = offsetof(CMS_RecipientInfo, d.kekri),
		.field_name = "d.kekri",
		.item = &CMS_KEKRecipientInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 3,
		.offset = offsetof(CMS_RecipientInfo, d.pwri),
		.field_name = "d.pwri",
		.item = &CMS_PasswordRecipientInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 4,
		.offset = offsetof(CMS_RecipientInfo, d.ori),
		.field_name = "d.ori",
		.item = &CMS_OtherRecipientInfo_it,
	},
};

const ASN1_ITEM CMS_RecipientInfo_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_RecipientInfo, type),
	.templates = CMS_RecipientInfo_ch_tt,
	.tcount = sizeof(CMS_RecipientInfo_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &CMS_RecipientInfo_aux,
	.size = sizeof(CMS_RecipientInfo),
	.sname = "CMS_RecipientInfo",
};

static const ASN1_TEMPLATE CMS_EnvelopedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EnvelopedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_EnvelopedData, originatorInfo),
		.field_name = "originatorInfo",
		.item = &CMS_OriginatorInfo_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(CMS_EnvelopedData, recipientInfos),
		.field_name = "recipientInfos",
		.item = &CMS_RecipientInfo_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EnvelopedData, encryptedContentInfo),
		.field_name = "encryptedContentInfo",
		.item = &CMS_EncryptedContentInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_EnvelopedData, unprotectedAttrs),
		.field_name = "unprotectedAttrs",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM CMS_EnvelopedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_EnvelopedData_seq_tt,
	.tcount = sizeof(CMS_EnvelopedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_EnvelopedData),
	.sname = "CMS_EnvelopedData",
};

static const ASN1_TEMPLATE CMS_DigestedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_DigestedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_DigestedData, digestAlgorithm),
		.field_name = "digestAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_DigestedData, encapContentInfo),
		.field_name = "encapContentInfo",
		.item = &CMS_EncapsulatedContentInfo_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_DigestedData, digest),
		.field_name = "digest",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_DigestedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_DigestedData_seq_tt,
	.tcount = sizeof(CMS_DigestedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_DigestedData),
	.sname = "CMS_DigestedData",
};

static const ASN1_TEMPLATE CMS_EncryptedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EncryptedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_EncryptedData, encryptedContentInfo),
		.field_name = "encryptedContentInfo",
		.item = &CMS_EncryptedContentInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(CMS_EncryptedData, unprotectedAttrs),
		.field_name = "unprotectedAttrs",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM CMS_EncryptedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_EncryptedData_seq_tt,
	.tcount = sizeof(CMS_EncryptedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_EncryptedData),
	.sname = "CMS_EncryptedData",
};

static const ASN1_TEMPLATE CMS_AuthenticatedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, originatorInfo),
		.field_name = "originatorInfo",
		.item = &CMS_OriginatorInfo_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, recipientInfos),
		.field_name = "recipientInfos",
		.item = &CMS_RecipientInfo_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, macAlgorithm),
		.field_name = "macAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 1,
		.offset = offsetof(CMS_AuthenticatedData, digestAlgorithm),
		.field_name = "digestAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, encapContentInfo),
		.field_name = "encapContentInfo",
		.item = &CMS_EncapsulatedContentInfo_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(CMS_AuthenticatedData, authAttrs),
		.field_name = "authAttrs",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_AuthenticatedData, mac),
		.field_name = "mac",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 3,
		.offset = offsetof(CMS_AuthenticatedData, unauthAttrs),
		.field_name = "unauthAttrs",
		.item = &X509_ALGOR_it,
	},
};

static const ASN1_ITEM CMS_AuthenticatedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_AuthenticatedData_seq_tt,
	.tcount = sizeof(CMS_AuthenticatedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_AuthenticatedData),
	.sname = "CMS_AuthenticatedData",
};

static const ASN1_TEMPLATE CMS_CompressedData_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_CompressedData, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_CompressedData, compressionAlgorithm),
		.field_name = "compressionAlgorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_CompressedData, encapContentInfo),
		.field_name = "encapContentInfo",
		.item = &CMS_EncapsulatedContentInfo_it,
	},
};

const ASN1_ITEM CMS_CompressedData_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_CompressedData_seq_tt,
	.tcount = sizeof(CMS_CompressedData_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_CompressedData),
	.sname = "CMS_CompressedData",
};

/* This is the ANY DEFINED BY table for the top level ContentInfo structure */

static const ASN1_TEMPLATE cms_default_tt = {
	.flags = ASN1_TFLG_EXPLICIT,
	.tag = 0,
	.offset = offsetof(CMS_ContentInfo, d.other),
	.field_name = "d.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE CMS_ContentInfo_adbtbl[] = {
	{
		.value = NID_pkcs7_data,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.data),
			.field_name = "d.data",
			.item = &ASN1_OCTET_STRING_NDEF_it,
		},
	},
	{
		.value = NID_pkcs7_signed,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.signedData),
			.field_name = "d.signedData",
			.item = &CMS_SignedData_it,
		},
	},
	{
		.value = NID_pkcs7_enveloped,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.envelopedData),
			.field_name = "d.envelopedData",
			.item = &CMS_EnvelopedData_it,
		},
	},
	{
		.value = NID_pkcs7_digest,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.digestedData),
			.field_name = "d.digestedData",
			.item = &CMS_DigestedData_it,
		},
	},
	{
		.value = NID_pkcs7_encrypted,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.encryptedData),
			.field_name = "d.encryptedData",
			.item = &CMS_EncryptedData_it,
		},
	},
	{
		.value = NID_id_smime_ct_authData,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.authenticatedData),
			.field_name = "d.authenticatedData",
			.item = &CMS_AuthenticatedData_it,
		},
	},
	{
		.value = NID_id_smime_ct_compressedData,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(CMS_ContentInfo, d.compressedData),
			.field_name = "d.compressedData",
			.item = &CMS_CompressedData_it,
		},
	},
};

static const ASN1_ADB CMS_ContentInfo_adb = {
	.flags = 0,
	.offset = offsetof(CMS_ContentInfo, contentType),
	.tbl = CMS_ContentInfo_adbtbl,
	.tblcount = sizeof(CMS_ContentInfo_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &cms_default_tt,
	.null_tt = NULL,
};

/* CMS streaming support */
static int
cms_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	ASN1_STREAM_ARG *sarg = exarg;
	CMS_ContentInfo *cms = NULL;

	if (pval)
		cms = (CMS_ContentInfo *)*pval;
	else
		return 1;

	switch (operation) {
	case ASN1_OP_STREAM_PRE:
		if (CMS_stream(&sarg->boundary, cms) <= 0)
			return 0;
		/* FALLTHROUGH */

	case ASN1_OP_DETACHED_PRE:
		sarg->ndef_bio = CMS_dataInit(cms, sarg->out);
		if (!sarg->ndef_bio)
			return 0;
		break;

	case ASN1_OP_STREAM_POST:
	case ASN1_OP_DETACHED_POST:
		if (CMS_dataFinal(cms, sarg->ndef_bio) <= 0)
			return 0;
		break;
	}

	return 1;
}

static const ASN1_AUX CMS_ContentInfo_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = cms_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE CMS_ContentInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_ContentInfo, contentType),
		.field_name = "contentType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "CMS_ContentInfo",
		.item = (const ASN1_ITEM *)&CMS_ContentInfo_adb,
	},
};

const ASN1_ITEM CMS_ContentInfo_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_ContentInfo_seq_tt,
	.tcount = sizeof(CMS_ContentInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &CMS_ContentInfo_aux,
	.size = sizeof(CMS_ContentInfo),
	.sname = "CMS_ContentInfo",
};
LCRYPTO_ALIAS(CMS_ContentInfo_it);

/* Specials for signed attributes */

/*
 * When signing attributes we want to reorder them to match the sorted
 * encoding.
 */

static const ASN1_TEMPLATE CMS_Attributes_Sign_item_tt = {
	.flags = ASN1_TFLG_SET_ORDER,
	.tag = 0,
	.offset = 0,
	.field_name = "CMS_ATTRIBUTES",
	.item = &X509_ATTRIBUTE_it,
};

const ASN1_ITEM CMS_Attributes_Sign_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &CMS_Attributes_Sign_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "CMS_Attributes_Sign",
};

/*
 * When verifying attributes we need to use the received order. So we use
 * SEQUENCE OF and tag it to SET OF
 */

static const ASN1_TEMPLATE CMS_Attributes_Verify_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_IMPTAG | ASN1_TFLG_UNIVERSAL,
	.tag = V_ASN1_SET,
	.offset = 0,
	.field_name = "CMS_ATTRIBUTES",
	.item = &X509_ATTRIBUTE_it,
};

const ASN1_ITEM CMS_Attributes_Verify_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &CMS_Attributes_Verify_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "CMS_Attributes_Verify",
};



static const ASN1_TEMPLATE CMS_ReceiptsFrom_ch_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = 0,
		.offset = offsetof(CMS_ReceiptsFrom, d.allOrFirstTier),
		.field_name = "d.allOrFirstTier",
		.item = &LONG_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF,
		.tag = 1,
		.offset = offsetof(CMS_ReceiptsFrom, d.receiptList),
		.field_name = "d.receiptList",
		.item = &GENERAL_NAMES_it,
	},
};

static const ASN1_ITEM CMS_ReceiptsFrom_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(CMS_ReceiptsFrom, type),
	.templates = CMS_ReceiptsFrom_ch_tt,
	.tcount = sizeof(CMS_ReceiptsFrom_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_ReceiptsFrom),
	.sname = "CMS_ReceiptsFrom",
};

static const ASN1_TEMPLATE CMS_ReceiptRequest_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_ReceiptRequest, signedContentIdentifier),
		.field_name = "signedContentIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_ReceiptRequest, receiptsFrom),
		.field_name = "receiptsFrom",
		.item = &CMS_ReceiptsFrom_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(CMS_ReceiptRequest, receiptsTo),
		.field_name = "receiptsTo",
		.item = &GENERAL_NAMES_it,
	},
};

const ASN1_ITEM CMS_ReceiptRequest_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_ReceiptRequest_seq_tt,
	.tcount = sizeof(CMS_ReceiptRequest_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_ReceiptRequest),
	.sname = "CMS_ReceiptRequest",
};
LCRYPTO_ALIAS(CMS_ReceiptRequest_it);

static const ASN1_TEMPLATE CMS_Receipt_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_Receipt, version),
		.field_name = "version",
		.item = &LONG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_Receipt, contentType),
		.field_name = "contentType",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_Receipt, signedContentIdentifier),
		.field_name = "signedContentIdentifier",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_Receipt, originatorSignatureValue),
		.field_name = "originatorSignatureValue",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM CMS_Receipt_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_Receipt_seq_tt,
	.tcount = sizeof(CMS_Receipt_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_Receipt),
	.sname = "CMS_Receipt",
};

/*
 * Utilities to encode the CMS_SharedInfo structure used during key
 * derivation.
 */

typedef struct {
	X509_ALGOR *keyInfo;
	ASN1_OCTET_STRING *entityUInfo;
	ASN1_OCTET_STRING *suppPubInfo;
} CMS_SharedInfo;

static const ASN1_TEMPLATE CMS_SharedInfo_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(CMS_SharedInfo, keyInfo),
		.field_name = "keyInfo",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(CMS_SharedInfo, entityUInfo),
		.field_name = "entityUInfo",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(CMS_SharedInfo, suppPubInfo),
		.field_name = "suppPubInfo",
		.item = &ASN1_OCTET_STRING_it,
	},
};

static const ASN1_ITEM CMS_SharedInfo_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = CMS_SharedInfo_seq_tt,
	.tcount = sizeof(CMS_SharedInfo_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(CMS_SharedInfo),
	.sname = "CMS_SharedInfo",
};

int
CMS_SharedInfo_encode(unsigned char **pder, X509_ALGOR *kekalg,
    ASN1_OCTET_STRING *ukm, int keylen)
{
	union {
		CMS_SharedInfo *pecsi;
		ASN1_VALUE *a;
	} intsi = {
		NULL
	};

	ASN1_OCTET_STRING oklen;
	unsigned char kl[4];
	CMS_SharedInfo ecsi;

	keylen <<= 3;
	kl[0] = (keylen >> 24) & 0xff;
	kl[1] = (keylen >> 16) & 0xff;
	kl[2] = (keylen >> 8) & 0xff;
	kl[3] = keylen & 0xff;
	oklen.length = 4;
	oklen.data = kl;
	oklen.type = V_ASN1_OCTET_STRING;
	oklen.flags = 0;
	ecsi.keyInfo = kekalg;
	ecsi.entityUInfo = ukm;
	ecsi.suppPubInfo = &oklen;
	intsi.pecsi = &ecsi;

	return ASN1_item_i2d(intsi.a, pder, &CMS_SharedInfo_it);
}
LCRYPTO_ALIAS(CMS_SharedInfo_encode);
