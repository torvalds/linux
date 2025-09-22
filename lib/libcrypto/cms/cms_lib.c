/* $OpenBSD: cms_lib.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "cms_local.h"
#include "err_local.h"
#include "x509_local.h"

CMS_ContentInfo *
d2i_CMS_ContentInfo(CMS_ContentInfo **a, const unsigned char **in, long len)
{
	return (CMS_ContentInfo *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(d2i_CMS_ContentInfo);

int
i2d_CMS_ContentInfo(CMS_ContentInfo *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(i2d_CMS_ContentInfo);

CMS_ContentInfo *
CMS_ContentInfo_new(void)
{
	return (CMS_ContentInfo *)ASN1_item_new(&CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(CMS_ContentInfo_new);

void
CMS_ContentInfo_free(CMS_ContentInfo *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(CMS_ContentInfo_free);

int
CMS_ContentInfo_print_ctx(BIO *out, CMS_ContentInfo *x, int indent, const ASN1_PCTX *pctx)
{
	return ASN1_item_print(out, (ASN1_VALUE *)x, indent,
	    &CMS_ContentInfo_it, pctx);
}
LCRYPTO_ALIAS(CMS_ContentInfo_print_ctx);

const ASN1_OBJECT *
CMS_get0_type(const CMS_ContentInfo *cms)
{
	return cms->contentType;
}
LCRYPTO_ALIAS(CMS_get0_type);

CMS_ContentInfo *
cms_Data_create(void)
{
	CMS_ContentInfo *cms;

	cms = CMS_ContentInfo_new();
	if (cms != NULL) {
		cms->contentType = OBJ_nid2obj(NID_pkcs7_data);
		/* Never detached */
		CMS_set_detached(cms, 0);
	}
	return cms;
}

static BIO *
cms_content_bio(CMS_ContentInfo *cms)
{
	ASN1_OCTET_STRING **pos;

	if ((pos = CMS_get0_content(cms)) == NULL)
		return NULL;

	/* If content is detached, data goes nowhere: create null BIO. */
	if (*pos == NULL)
		return BIO_new(BIO_s_null());

	/* If content is not detached and was created, return memory BIO. */
	if ((*pos)->flags == ASN1_STRING_FLAG_CONT)
		return BIO_new(BIO_s_mem());

	/* Else content was read in: return read-only BIO for it. */
	return BIO_new_mem_buf((*pos)->data, (*pos)->length);
}

BIO *
CMS_dataInit(CMS_ContentInfo *cms, BIO *in_content_bio)
{
	BIO *cms_bio = NULL, *content_bio = NULL;

	if ((content_bio = in_content_bio) == NULL)
		content_bio = cms_content_bio(cms);
	if (content_bio == NULL) {
		CMSerror(CMS_R_NO_CONTENT);
		goto err;
	}

	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_data:
		return content_bio;
	case NID_pkcs7_signed:
		if ((cms_bio = cms_SignedData_init_bio(cms)) == NULL)
			goto err;
		break;
	case NID_pkcs7_digest:
		if ((cms_bio = cms_DigestedData_init_bio(cms)) == NULL)
			goto err;
		break;
	case NID_pkcs7_encrypted:
		if ((cms_bio = cms_EncryptedData_init_bio(cms)) == NULL)
			goto err;
		break;
	case NID_pkcs7_enveloped:
		if ((cms_bio = cms_EnvelopedData_init_bio(cms)) == NULL)
			goto err;
		break;
	default:
		CMSerror(CMS_R_UNSUPPORTED_TYPE);
		goto err;
	}

	return BIO_push(cms_bio, content_bio);

 err:
	if (content_bio != in_content_bio)
		BIO_free(content_bio);

	return NULL;
}
LCRYPTO_ALIAS(CMS_dataInit);

int
CMS_dataFinal(CMS_ContentInfo *cms, BIO *cmsbio)
{
	ASN1_OCTET_STRING **pos = CMS_get0_content(cms);

	if (!pos)
		return 0;
	/* If embedded content find memory BIO and set content */
	if (*pos && ((*pos)->flags & ASN1_STRING_FLAG_CONT)) {
		BIO *mbio;
		unsigned char *cont;
		long contlen;
		mbio = BIO_find_type(cmsbio, BIO_TYPE_MEM);
		if (!mbio) {
			CMSerror(CMS_R_CONTENT_NOT_FOUND);
			return 0;
		}
		contlen = BIO_get_mem_data(mbio, &cont);
		/* Set bio as read only so its content can't be clobbered */
		BIO_set_flags(mbio, BIO_FLAGS_MEM_RDONLY);
		BIO_set_mem_eof_return(mbio, 0);
		ASN1_STRING_set0(*pos, cont, contlen);
		(*pos)->flags &= ~ASN1_STRING_FLAG_CONT;
	}

	switch (OBJ_obj2nid(cms->contentType)) {

	case NID_pkcs7_data:
	case NID_pkcs7_enveloped:
	case NID_pkcs7_encrypted:
	case NID_id_smime_ct_compressedData:
		/* Nothing to do */
		return 1;

	case NID_pkcs7_signed:
		return cms_SignedData_final(cms, cmsbio);

	case NID_pkcs7_digest:
		return cms_DigestedData_do_final(cms, cmsbio, 0);

	default:
		CMSerror(CMS_R_UNSUPPORTED_TYPE);
		return 0;
	}
}
LCRYPTO_ALIAS(CMS_dataFinal);

int
CMS_get_version(const CMS_ContentInfo *cms, long *version)
{
	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_signed:
		*version = cms->d.signedData->version;
		return 1;

	case NID_pkcs7_enveloped:
		*version = cms->d.envelopedData->version;
		return 1;

	case NID_pkcs7_digest:
		*version = cms->d.digestedData->version;
		return 1;

	case NID_pkcs7_encrypted:
		*version = cms->d.encryptedData->version;
		return 1;

	case NID_id_smime_ct_authData:
		*version = cms->d.authenticatedData->version;
		return 1;

	case NID_id_smime_ct_compressedData:
		*version = cms->d.compressedData->version;
		return 1;

	default:
		CMSerror(CMS_R_UNSUPPORTED_TYPE);
		return 0;
	}
}
LCRYPTO_ALIAS(CMS_get_version);

int
CMS_SignerInfo_get_version(const CMS_SignerInfo *si, long *version)
{
	*version = si->version;
	return 1;
}
LCRYPTO_ALIAS(CMS_SignerInfo_get_version);

/*
 * Return an OCTET STRING pointer to content. This allows it to be accessed
 * or set later.
 */

ASN1_OCTET_STRING **
CMS_get0_content(CMS_ContentInfo *cms)
{
	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_data:
		return &cms->d.data;

	case NID_pkcs7_signed:
		return &cms->d.signedData->encapContentInfo->eContent;

	case NID_pkcs7_enveloped:
		return &cms->d.envelopedData->encryptedContentInfo->encryptedContent;

	case NID_pkcs7_digest:
		return &cms->d.digestedData->encapContentInfo->eContent;

	case NID_pkcs7_encrypted:
		return &cms->d.encryptedData->encryptedContentInfo->encryptedContent;

	case NID_id_smime_ct_authData:
		return &cms->d.authenticatedData->encapContentInfo->eContent;

	case NID_id_smime_ct_compressedData:
		return &cms->d.compressedData->encapContentInfo->eContent;

	default:
		if (cms->d.other->type == V_ASN1_OCTET_STRING)
			return &cms->d.other->value.octet_string;
		CMSerror(CMS_R_UNSUPPORTED_CONTENT_TYPE);
		return NULL;
	}
}
LCRYPTO_ALIAS(CMS_get0_content);

/*
 * Return an ASN1_OBJECT pointer to content type. This allows it to be
 * accessed or set later.
 */

static ASN1_OBJECT **
cms_get0_econtent_type(CMS_ContentInfo *cms)
{
	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_signed:
		return &cms->d.signedData->encapContentInfo->eContentType;

	case NID_pkcs7_enveloped:
		return &cms->d.envelopedData->encryptedContentInfo->contentType;

	case NID_pkcs7_digest:
		return &cms->d.digestedData->encapContentInfo->eContentType;

	case NID_pkcs7_encrypted:
		return &cms->d.encryptedData->encryptedContentInfo->contentType;

	case NID_id_smime_ct_authData:
		return &cms->d.authenticatedData->encapContentInfo->eContentType;

	case NID_id_smime_ct_compressedData:
		return &cms->d.compressedData->encapContentInfo->eContentType;

	default:
		CMSerror(CMS_R_UNSUPPORTED_CONTENT_TYPE);
		return NULL;
	}
}

const ASN1_OBJECT *
CMS_get0_eContentType(CMS_ContentInfo *cms)
{
	ASN1_OBJECT **petype;

	petype = cms_get0_econtent_type(cms);
	if (petype)
		return *petype;

	return NULL;
}
LCRYPTO_ALIAS(CMS_get0_eContentType);

int
CMS_set1_eContentType(CMS_ContentInfo *cms, const ASN1_OBJECT *oid)
{
	ASN1_OBJECT **petype, *etype;

	petype = cms_get0_econtent_type(cms);
	if (!petype)
		return 0;
	if (!oid)
		return 1;
	etype = OBJ_dup(oid);
	if (!etype)
		return 0;
	ASN1_OBJECT_free(*petype);
	*petype = etype;

	return 1;
}
LCRYPTO_ALIAS(CMS_set1_eContentType);

int
CMS_is_detached(CMS_ContentInfo *cms)
{
	ASN1_OCTET_STRING **pos;

	pos = CMS_get0_content(cms);
	if (!pos)
		return -1;
	if (*pos)
		return 0;

	return 1;
}
LCRYPTO_ALIAS(CMS_is_detached);

int
CMS_set_detached(CMS_ContentInfo *cms, int detached)
{
	ASN1_OCTET_STRING **pos;

	pos = CMS_get0_content(cms);
	if (!pos)
		return 0;
	if (detached) {
		ASN1_OCTET_STRING_free(*pos);
		*pos = NULL;
		return 1;
	}
	if (*pos == NULL)
		*pos = ASN1_OCTET_STRING_new();
	if (*pos != NULL) {
		/*
		 * NB: special flag to show content is created and not read in.
		 */
		(*pos)->flags |= ASN1_STRING_FLAG_CONT;
		return 1;
	}
	CMSerror(ERR_R_MALLOC_FAILURE);

	return 0;
}
LCRYPTO_ALIAS(CMS_set_detached);

/* Create a digest BIO from an X509_ALGOR structure */

BIO *
cms_DigestAlgorithm_init_bio(X509_ALGOR *digestAlgorithm)
{
	BIO *mdbio = NULL;
	const ASN1_OBJECT *digestoid;
	const EVP_MD *digest;

	X509_ALGOR_get0(&digestoid, NULL, NULL, digestAlgorithm);
	digest = EVP_get_digestbyobj(digestoid);
	if (!digest) {
		CMSerror(CMS_R_UNKNOWN_DIGEST_ALGORITHM);
		goto err;
	}
	mdbio = BIO_new(BIO_f_md());
	if (mdbio == NULL || !BIO_set_md(mdbio, digest)) {
		CMSerror(CMS_R_MD_BIO_INIT_ERROR);
		goto err;
	}
	return mdbio;

 err:
	BIO_free(mdbio);

	return NULL;
}

/* Locate a message digest content from a BIO chain based on SignerInfo */

int
cms_DigestAlgorithm_find_ctx(EVP_MD_CTX *mctx, BIO *chain, X509_ALGOR *mdalg)
{
	int nid;
	const ASN1_OBJECT *mdoid;

	X509_ALGOR_get0(&mdoid, NULL, NULL, mdalg);
	nid = OBJ_obj2nid(mdoid);
	/* Look for digest type to match signature */
	for (;;) {
		EVP_MD_CTX *mtmp;
		chain = BIO_find_type(chain, BIO_TYPE_MD);
		if (chain == NULL) {
			CMSerror(CMS_R_NO_MATCHING_DIGEST);
			return 0;
		}
		BIO_get_md_ctx(chain, &mtmp);
		if (EVP_MD_CTX_type(mtmp) == nid
			/*
			 * Workaround for broken implementations that use signature
			 * algorithm OID instead of digest.
			 */
			|| EVP_MD_pkey_type(EVP_MD_CTX_md(mtmp)) == nid)
			return EVP_MD_CTX_copy_ex(mctx, mtmp);
		chain = BIO_next(chain);
	}
}

static STACK_OF(CMS_CertificateChoices) **
cms_get0_certificate_choices(CMS_ContentInfo *cms)
{
	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_signed:
		return &cms->d.signedData->certificates;

	case NID_pkcs7_enveloped:
		if (cms->d.envelopedData->originatorInfo == NULL)
			return NULL;
		return &cms->d.envelopedData->originatorInfo->certificates;

	default:
		CMSerror(CMS_R_UNSUPPORTED_CONTENT_TYPE);
		return NULL;
	}
}

CMS_CertificateChoices *
CMS_add0_CertificateChoices(CMS_ContentInfo *cms)
{
	STACK_OF(CMS_CertificateChoices) **pcerts;
	CMS_CertificateChoices *cch;

	pcerts = cms_get0_certificate_choices(cms);
	if (!pcerts)
		return NULL;
	if (!*pcerts)
		*pcerts = sk_CMS_CertificateChoices_new_null();
	if (!*pcerts)
		return NULL;
	cch = (CMS_CertificateChoices *)ASN1_item_new(&CMS_CertificateChoices_it);
	if (!cch)
		return NULL;
	if (!sk_CMS_CertificateChoices_push(*pcerts, cch)) {
		ASN1_item_free((ASN1_VALUE *)cch, &CMS_CertificateChoices_it);
		return NULL;
	}

	return cch;
}
LCRYPTO_ALIAS(CMS_add0_CertificateChoices);

int
CMS_add0_cert(CMS_ContentInfo *cms, X509 *cert)
{
	CMS_CertificateChoices *cch;
	STACK_OF(CMS_CertificateChoices) **pcerts;
	int i;

	pcerts = cms_get0_certificate_choices(cms);
	if (!pcerts)
		return 0;
	for (i = 0; i < sk_CMS_CertificateChoices_num(*pcerts); i++) {
		cch = sk_CMS_CertificateChoices_value(*pcerts, i);
		if (cch->type == CMS_CERTCHOICE_CERT) {
			if (!X509_cmp(cch->d.certificate, cert)) {
			    CMSerror(CMS_R_CERTIFICATE_ALREADY_PRESENT);
			    return 0;
			}
		}
	}
	cch = CMS_add0_CertificateChoices(cms);
	if (!cch)
		return 0;
	cch->type = CMS_CERTCHOICE_CERT;
	cch->d.certificate = cert;

	return 1;
}
LCRYPTO_ALIAS(CMS_add0_cert);

int
CMS_add1_cert(CMS_ContentInfo *cms, X509 *cert)
{
	int r;

	r = CMS_add0_cert(cms, cert);
	if (r > 0)
		X509_up_ref(cert);

	return r;
}
LCRYPTO_ALIAS(CMS_add1_cert);

static STACK_OF(CMS_RevocationInfoChoice) **
cms_get0_revocation_choices(CMS_ContentInfo *cms)
{
	switch (OBJ_obj2nid(cms->contentType)) {
	case NID_pkcs7_signed:
		return &cms->d.signedData->crls;

	case NID_pkcs7_enveloped:
		if (cms->d.envelopedData->originatorInfo == NULL)
			return NULL;
		return &cms->d.envelopedData->originatorInfo->crls;

	default:
		CMSerror(CMS_R_UNSUPPORTED_CONTENT_TYPE);
		return NULL;
	}
}

CMS_RevocationInfoChoice *
CMS_add0_RevocationInfoChoice(CMS_ContentInfo *cms)
{
	STACK_OF(CMS_RevocationInfoChoice) **pcrls;
	CMS_RevocationInfoChoice *rch;

	pcrls = cms_get0_revocation_choices(cms);
	if (!pcrls)
		return NULL;
	if (!*pcrls)
		*pcrls = sk_CMS_RevocationInfoChoice_new_null();
	if (!*pcrls)
		return NULL;
	rch = (CMS_RevocationInfoChoice *)ASN1_item_new(&CMS_RevocationInfoChoice_it);
	if (!rch)
		return NULL;
	if (!sk_CMS_RevocationInfoChoice_push(*pcrls, rch)) {
		ASN1_item_free((ASN1_VALUE *)rch, &CMS_RevocationInfoChoice_it);
		return NULL;
	}

	return rch;
}
LCRYPTO_ALIAS(CMS_add0_RevocationInfoChoice);

int
CMS_add0_crl(CMS_ContentInfo *cms, X509_CRL *crl)
{
	CMS_RevocationInfoChoice *rch;

	rch = CMS_add0_RevocationInfoChoice(cms);
	if (!rch)
		return 0;
	rch->type = CMS_REVCHOICE_CRL;
	rch->d.crl = crl;

	return 1;
}
LCRYPTO_ALIAS(CMS_add0_crl);

int
CMS_add1_crl(CMS_ContentInfo *cms, X509_CRL *crl)
{
	int r;

	r = CMS_add0_crl(cms, crl);
	if (r > 0)
		X509_CRL_up_ref(crl);

	return r;
}
LCRYPTO_ALIAS(CMS_add1_crl);

STACK_OF(X509) *
CMS_get1_certs(CMS_ContentInfo *cms)
{
	STACK_OF(X509) *certs = NULL;
	CMS_CertificateChoices *cch;
	STACK_OF(CMS_CertificateChoices) **pcerts;
	int i;

	pcerts = cms_get0_certificate_choices(cms);
	if (!pcerts)
		return NULL;
	for (i = 0; i < sk_CMS_CertificateChoices_num(*pcerts); i++) {
		cch = sk_CMS_CertificateChoices_value(*pcerts, i);
		if (cch->type == 0) {
			if (!certs) {
			    certs = sk_X509_new_null();
			    if (!certs)
			        return NULL;
			}
			if (!sk_X509_push(certs, cch->d.certificate)) {
			    sk_X509_pop_free(certs, X509_free);
			    return NULL;
			}
			X509_up_ref(cch->d.certificate);
		}
	}
	return certs;
}
LCRYPTO_ALIAS(CMS_get1_certs);

STACK_OF(X509_CRL) *
CMS_get1_crls(CMS_ContentInfo *cms)
{
	STACK_OF(X509_CRL) *crls = NULL;
	STACK_OF(CMS_RevocationInfoChoice) **pcrls;
	CMS_RevocationInfoChoice *rch;
	int i;

	pcrls = cms_get0_revocation_choices(cms);
	if (!pcrls)
		return NULL;
	for (i = 0; i < sk_CMS_RevocationInfoChoice_num(*pcrls); i++) {
		rch = sk_CMS_RevocationInfoChoice_value(*pcrls, i);
		if (rch->type == 0) {
			if (!crls) {
			    crls = sk_X509_CRL_new_null();
			    if (!crls)
			        return NULL;
			}
			if (!sk_X509_CRL_push(crls, rch->d.crl)) {
			    sk_X509_CRL_pop_free(crls, X509_CRL_free);
			    return NULL;
			}
			X509_CRL_up_ref(rch->d.crl);
		}
	}
	return crls;
}
LCRYPTO_ALIAS(CMS_get1_crls);

static const ASN1_OCTET_STRING *
cms_X509_get0_subject_key_id(X509 *x)
{
	/* Call for side-effect of computing hash and caching extensions */
	X509_check_purpose(x, -1, -1);
	return x->skid;
}

int
cms_ias_cert_cmp(CMS_IssuerAndSerialNumber *ias, X509 *cert)
{
	int ret;

	ret = X509_NAME_cmp(ias->issuer, X509_get_issuer_name(cert));
	if (ret)
		return ret;

	return ASN1_INTEGER_cmp(ias->serialNumber, X509_get_serialNumber(cert));
}

int
cms_keyid_cert_cmp(ASN1_OCTET_STRING *keyid, X509 *cert)
{
	const ASN1_OCTET_STRING *cert_keyid = cms_X509_get0_subject_key_id(cert);

	if (cert_keyid == NULL)
		return -1;

	return ASN1_OCTET_STRING_cmp(keyid, cert_keyid);
}

int
cms_set1_ias(CMS_IssuerAndSerialNumber **pias, X509 *cert)
{
	CMS_IssuerAndSerialNumber *ias;

	ias = (CMS_IssuerAndSerialNumber *)ASN1_item_new(&CMS_IssuerAndSerialNumber_it);
	if (!ias)
		goto err;
	if (!X509_NAME_set(&ias->issuer, X509_get_issuer_name(cert)))
		goto err;
	if (!ASN1_STRING_copy(ias->serialNumber, X509_get_serialNumber(cert)))
		goto err;
	ASN1_item_free((ASN1_VALUE *)*pias, &CMS_IssuerAndSerialNumber_it);
	*pias = ias;

	return 1;

 err:
	ASN1_item_free((ASN1_VALUE *)ias, &CMS_IssuerAndSerialNumber_it);
	CMSerror(ERR_R_MALLOC_FAILURE);

	return 0;
}

int
cms_set1_keyid(ASN1_OCTET_STRING **pkeyid, X509 *cert)
{
	ASN1_OCTET_STRING *keyid = NULL;
	const ASN1_OCTET_STRING *cert_keyid;

	cert_keyid = cms_X509_get0_subject_key_id(cert);
	if (cert_keyid == NULL) {
		CMSerror(CMS_R_CERTIFICATE_HAS_NO_KEYID);
		return 0;
	}
	keyid = ASN1_STRING_dup(cert_keyid);
	if (!keyid) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ASN1_OCTET_STRING_free(*pkeyid);
	*pkeyid = keyid;

	return 1;
}
