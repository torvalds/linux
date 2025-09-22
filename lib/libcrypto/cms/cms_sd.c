/* $OpenBSD: cms_sd.c,v 1.36 2025/07/31 02:24:21 tb Exp $ */
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

#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/cms.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "cms_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

/* CMS SignedData Utilities */

static CMS_SignedData *
cms_get0_signed(CMS_ContentInfo *cms)
{
	if (OBJ_obj2nid(cms->contentType) != NID_pkcs7_signed) {
		CMSerror(CMS_R_CONTENT_TYPE_NOT_SIGNED_DATA);
		return NULL;
	}
	return cms->d.signedData;
}

static CMS_SignedData *
cms_signed_data_init(CMS_ContentInfo *cms)
{
	if (cms->d.other == NULL) {
		cms->d.signedData = (CMS_SignedData *)ASN1_item_new(&CMS_SignedData_it);
		if (!cms->d.signedData) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
		cms->d.signedData->version = 1;
		cms->d.signedData->encapContentInfo->eContentType =
		    OBJ_nid2obj(NID_pkcs7_data);
		cms->d.signedData->encapContentInfo->partial = 1;
		ASN1_OBJECT_free(cms->contentType);
		cms->contentType = OBJ_nid2obj(NID_pkcs7_signed);
		return cms->d.signedData;
	}
	return cms_get0_signed(cms);
}

/* Just initialise SignedData e.g. for certs only structure */

int
CMS_SignedData_init(CMS_ContentInfo *cms)
{
	if (cms_signed_data_init(cms))
		return 1;
	else
		return 0;
}
LCRYPTO_ALIAS(CMS_SignedData_init);

/* Check structures and fixup version numbers (if necessary) */

static void
cms_sd_set_version(CMS_SignedData *sd)
{
	int i;
	CMS_CertificateChoices *cch;
	CMS_RevocationInfoChoice *rch;
	CMS_SignerInfo *si;

	for (i = 0; i < sk_CMS_CertificateChoices_num(sd->certificates); i++) {
		cch = sk_CMS_CertificateChoices_value(sd->certificates, i);
		if (cch->type == CMS_CERTCHOICE_OTHER) {
			if (sd->version < 5)
			    sd->version = 5;
		} else if (cch->type == CMS_CERTCHOICE_V2ACERT) {
			if (sd->version < 4)
			    sd->version = 4;
		} else if (cch->type == CMS_CERTCHOICE_V1ACERT) {
			if (sd->version < 3)
			    sd->version = 3;
		}
	}

	for (i = 0; i < sk_CMS_RevocationInfoChoice_num(sd->crls); i++) {
		rch = sk_CMS_RevocationInfoChoice_value(sd->crls, i);
		if (rch->type == CMS_REVCHOICE_OTHER) {
			if (sd->version < 5)
			    sd->version = 5;
		}
	}

	if ((OBJ_obj2nid(sd->encapContentInfo->eContentType) != NID_pkcs7_data)
		&& (sd->version < 3))
		sd->version = 3;

	for (i = 0; i < sk_CMS_SignerInfo_num(sd->signerInfos); i++) {
		si = sk_CMS_SignerInfo_value(sd->signerInfos, i);
		if (si->sid->type == CMS_SIGNERINFO_KEYIDENTIFIER) {
			if (si->version < 3)
			    si->version = 3;
			if (sd->version < 3)
			    sd->version = 3;
		} else if (si->version < 1)
			si->version = 1;
	}

	if (sd->version < 1)
		sd->version = 1;
}

/* Copy an existing messageDigest value */

static int
cms_copy_messageDigest(CMS_ContentInfo *cms, CMS_SignerInfo *si)
{
	STACK_OF(CMS_SignerInfo) *sinfos;
	CMS_SignerInfo *sitmp;
	int i;

	sinfos = CMS_get0_SignerInfos(cms);
	for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
		ASN1_OCTET_STRING *messageDigest;
		sitmp = sk_CMS_SignerInfo_value(sinfos, i);
		if (sitmp == si)
			continue;
		if (CMS_signed_get_attr_count(sitmp) < 0)
			continue;
		if (OBJ_cmp(si->digestAlgorithm->algorithm,
		    sitmp->digestAlgorithm->algorithm))
			continue;
		messageDigest = CMS_signed_get0_data_by_OBJ(sitmp,
		    OBJ_nid2obj(NID_pkcs9_messageDigest), -3, V_ASN1_OCTET_STRING);
		if (!messageDigest) {
			CMSerror(CMS_R_ERROR_READING_MESSAGEDIGEST_ATTRIBUTE);
			return 0;
		}

		if (CMS_signed_add1_attr_by_NID(si, NID_pkcs9_messageDigest,
		    V_ASN1_OCTET_STRING, messageDigest, -1))
			return 1;
		else
			return 0;
	}

	CMSerror(CMS_R_NO_MATCHING_DIGEST);

	return 0;
}

int
cms_set1_SignerIdentifier(CMS_SignerIdentifier *sid, X509 *cert, int type)
{
	switch (type) {
	case CMS_SIGNERINFO_ISSUER_SERIAL:
		if (!cms_set1_ias(&sid->d.issuerAndSerialNumber, cert))
			return 0;
		break;

	case CMS_SIGNERINFO_KEYIDENTIFIER:
		if (!cms_set1_keyid(&sid->d.subjectKeyIdentifier, cert))
			return 0;
		break;

	default:
		CMSerror(CMS_R_UNKNOWN_ID);
		return 0;
	}

	sid->type = type;

	return 1;
}

int
cms_SignerIdentifier_get0_signer_id(CMS_SignerIdentifier *sid,
    ASN1_OCTET_STRING **keyid, X509_NAME **issuer, ASN1_INTEGER **sno)
{
	if (sid->type == CMS_SIGNERINFO_ISSUER_SERIAL) {
		if (issuer)
			*issuer = sid->d.issuerAndSerialNumber->issuer;
		if (sno)
			*sno = sid->d.issuerAndSerialNumber->serialNumber;
	} else if (sid->type == CMS_SIGNERINFO_KEYIDENTIFIER) {
		if (keyid)
			*keyid = sid->d.subjectKeyIdentifier;
	} else
		return 0;

	return 1;
}

int
cms_SignerIdentifier_cert_cmp(CMS_SignerIdentifier *sid, X509 *cert)
{
	if (sid->type == CMS_SIGNERINFO_ISSUER_SERIAL)
		return cms_ias_cert_cmp(sid->d.issuerAndSerialNumber, cert);
	else if (sid->type == CMS_SIGNERINFO_KEYIDENTIFIER)
		return cms_keyid_cert_cmp(sid->d.subjectKeyIdentifier, cert);
	else
		return -1;
}

static int
cms_sd_asn1_ctrl(CMS_SignerInfo *si, int cmd)
{
	EVP_PKEY *pkey = si->pkey;
	int ret;

	if (pkey->ameth == NULL || pkey->ameth->pkey_ctrl == NULL)
		return 1;
	ret = pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_CMS_SIGN, cmd, si);
	if (ret == -2) {
		CMSerror(CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		return 0;
	}
	if (ret <= 0) {
		CMSerror(CMS_R_CTRL_FAILURE);
		return 0;
	}

	return 1;
}

static const EVP_MD *
cms_SignerInfo_default_digest_md(const CMS_SignerInfo *si)
{
	int rv, nid;

	if (si->pkey == NULL) {
		CMSerror(CMS_R_NO_PUBLIC_KEY);
		return NULL;
	}

	/* On failure or unsupported operation, give up. */
	if ((rv = EVP_PKEY_get_default_digest_nid(si->pkey, &nid)) <= 0)
		return NULL;
	if (rv > 2)
		return NULL;

	/*
	 * XXX - we need to identify EdDSA in a better way. Figure out where
	 * and how. This mimics EdDSA checks in openssl/ca.c and openssl/req.c.
	 */

	/* The digest md is required to be EVP_sha512() (EdDSA). */
	if (rv == 2 && nid == NID_undef)
		return EVP_sha512();

	/* Use mandatory or default digest. */
	return EVP_get_digestbynid(nid);
}

static const EVP_MD *
cms_SignerInfo_signature_md(const CMS_SignerInfo *si)
{
	int rv, nid;

	if (si->pkey == NULL) {
		CMSerror(CMS_R_NO_PUBLIC_KEY);
		return NULL;
	}

	/* Fall back to digestAlgorithm unless pkey has a mandatory digest. */
	if ((rv = EVP_PKEY_get_default_digest_nid(si->pkey, &nid)) <= 1)
		return EVP_get_digestbyobj(si->digestAlgorithm->algorithm);
	if (rv > 2)
		return NULL;

	/*
	 * XXX - we need to identify EdDSA in a better way. Figure out where
	 * and how. This mimics EdDSA checks in openssl/ca.c and openssl/req.c.
	 */

	/* The signature md is required to be EVP_md_null() (EdDSA). */
	if (nid == NID_undef)
		return EVP_md_null();

	/* Use mandatory digest. */
	return EVP_get_digestbynid(nid);
}

CMS_SignerInfo *
CMS_add1_signer(CMS_ContentInfo *cms, X509 *signer, EVP_PKEY *pk,
    const EVP_MD *md, unsigned int flags)
{
	CMS_SignedData *sd;
	CMS_SignerInfo *si = NULL;
	X509_ALGOR *alg = NULL;
	int i, type;

	if (!X509_check_private_key(signer, pk)) {
		CMSerror(CMS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
		return NULL;
	}
	sd = cms_signed_data_init(cms);
	if (!sd)
		goto err;
	si = (CMS_SignerInfo *)ASN1_item_new(&CMS_SignerInfo_it);
	if (!si)
		goto merr;
	/* Call for side-effect of computing hash and caching extensions */
	X509_check_purpose(signer, -1, -1);

	X509_up_ref(signer);
	EVP_PKEY_up_ref(pk);

	si->pkey = pk;
	si->signer = signer;
	si->mctx = EVP_MD_CTX_new();
	si->pctx = NULL;

	if (si->mctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (flags & CMS_USE_KEYID) {
		si->version = 3;
		if (sd->version < 3)
			sd->version = 3;
		type = CMS_SIGNERINFO_KEYIDENTIFIER;
	} else {
		type = CMS_SIGNERINFO_ISSUER_SERIAL;
		si->version = 1;
	}

	if (!cms_set1_SignerIdentifier(si->sid, signer, type))
		goto err;

	if (md == NULL)
		md = cms_SignerInfo_default_digest_md(si);
	if (md == NULL) {
		CMSerror(CMS_R_NO_DEFAULT_DIGEST);
		goto err;
	}

	if (!X509_ALGOR_set_evp_md(si->digestAlgorithm, md))
		goto err;

	/* See if digest is present in digestAlgorithms */
	for (i = 0; i < sk_X509_ALGOR_num(sd->digestAlgorithms); i++) {
		const X509_ALGOR *x509_alg;
		const ASN1_OBJECT *aoid;

		x509_alg = sk_X509_ALGOR_value(sd->digestAlgorithms, i);
		X509_ALGOR_get0(&aoid, NULL, NULL, x509_alg);
		if (OBJ_obj2nid(aoid) == EVP_MD_type(md))
			break;
	}

	if (i == sk_X509_ALGOR_num(sd->digestAlgorithms)) {
		if ((alg = X509_ALGOR_new()) == NULL)
			goto merr;
		if (!X509_ALGOR_set_evp_md(alg, md))
			goto merr;
		if (!sk_X509_ALGOR_push(sd->digestAlgorithms, alg)) {
			goto merr;
		}
		alg = NULL;
	}

	if (!(flags & CMS_KEY_PARAM) && !cms_sd_asn1_ctrl(si, 0))
		goto err;
	if (!(flags & CMS_NOATTR)) {
		/*
		 * Initialize signed attributes structure so other attributes
		 * such as signing time etc are added later even if we add none here.
		 */
		if (!si->signedAttrs) {
			si->signedAttrs = sk_X509_ATTRIBUTE_new_null();
			if (!si->signedAttrs)
				goto merr;
		}

		if (!(flags & CMS_NOSMIMECAP)) {
			STACK_OF(X509_ALGOR) *smcap = NULL;

			i = CMS_add_standard_smimecap(&smcap);
			if (i)
			    i = CMS_add_smimecap(si, smcap);
			sk_X509_ALGOR_pop_free(smcap, X509_ALGOR_free);
			if (!i)
				goto merr;
		}
		if (flags & CMS_REUSE_DIGEST) {
			if (!cms_copy_messageDigest(cms, si))
				goto err;
			if (!(flags & (CMS_PARTIAL | CMS_KEY_PARAM)) &&
			    !CMS_SignerInfo_sign(si))
				goto err;
		}
	}

	if (!(flags & CMS_NOCERTS)) {
		/* NB ignore -1 return for duplicate cert */
		if (!CMS_add1_cert(cms, signer))
			goto merr;
	}

	if (flags & CMS_KEY_PARAM) {
		if (flags & CMS_NOATTR) {
			si->pctx = EVP_PKEY_CTX_new(si->pkey, NULL);
			if (si->pctx == NULL)
				goto err;
			if (EVP_PKEY_sign_init(si->pctx) <= 0)
				goto err;
			if (EVP_PKEY_CTX_set_signature_md(si->pctx, md) <= 0)
				goto err;
		} else if (EVP_DigestSignInit(si->mctx, &si->pctx, md,
		    NULL, pk) <= 0)
			goto err;
	}

	if (!sd->signerInfos)
		sd->signerInfos = sk_CMS_SignerInfo_new_null();
	if (!sd->signerInfos || !sk_CMS_SignerInfo_push(sd->signerInfos, si))
		goto merr;

	return si;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);
 err:
	ASN1_item_free((ASN1_VALUE *)si, &CMS_SignerInfo_it);
	X509_ALGOR_free(alg);

	return NULL;
}
LCRYPTO_ALIAS(CMS_add1_signer);

EVP_PKEY_CTX *
CMS_SignerInfo_get0_pkey_ctx(CMS_SignerInfo *si)
{
	return si->pctx;
}
LCRYPTO_ALIAS(CMS_SignerInfo_get0_pkey_ctx);

EVP_MD_CTX *
CMS_SignerInfo_get0_md_ctx(CMS_SignerInfo *si)
{
	return si->mctx;
}
LCRYPTO_ALIAS(CMS_SignerInfo_get0_md_ctx);

STACK_OF(CMS_SignerInfo) *
CMS_get0_SignerInfos(CMS_ContentInfo *cms)
{
	CMS_SignedData *sd;

	sd = cms_get0_signed(cms);
	if (!sd)
		return NULL;

	return sd->signerInfos;
}
LCRYPTO_ALIAS(CMS_get0_SignerInfos);

STACK_OF(X509) *
CMS_get0_signers(CMS_ContentInfo *cms)
{
	STACK_OF(X509) *signers = NULL;
	STACK_OF(CMS_SignerInfo) *sinfos;
	CMS_SignerInfo *si;
	int i;

	sinfos = CMS_get0_SignerInfos(cms);
	for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
		si = sk_CMS_SignerInfo_value(sinfos, i);
		if (si->signer) {
			if (!signers) {
				signers = sk_X509_new_null();
				if (!signers)
					return NULL;
			}
			if (!sk_X509_push(signers, si->signer)) {
				sk_X509_free(signers);
				return NULL;
			}
		}
	}

	return signers;
}
LCRYPTO_ALIAS(CMS_get0_signers);

void
CMS_SignerInfo_set1_signer_cert(CMS_SignerInfo *si, X509 *signer)
{
	if (signer) {
		X509_up_ref(signer);
		EVP_PKEY_free(si->pkey);
		si->pkey = X509_get_pubkey(signer);
	}
	X509_free(si->signer);
	si->signer = signer;
}
LCRYPTO_ALIAS(CMS_SignerInfo_set1_signer_cert);

int
CMS_SignerInfo_get0_signer_id(CMS_SignerInfo *si, ASN1_OCTET_STRING **keyid,
    X509_NAME **issuer, ASN1_INTEGER **sno)
{
	return cms_SignerIdentifier_get0_signer_id(si->sid, keyid, issuer, sno);
}
LCRYPTO_ALIAS(CMS_SignerInfo_get0_signer_id);

int
CMS_SignerInfo_cert_cmp(CMS_SignerInfo *si, X509 *cert)
{
	return cms_SignerIdentifier_cert_cmp(si->sid, cert);
}
LCRYPTO_ALIAS(CMS_SignerInfo_cert_cmp);

int
CMS_set1_signers_certs(CMS_ContentInfo *cms, STACK_OF(X509) *scerts,
    unsigned int flags)
{
	CMS_SignedData *sd;
	CMS_SignerInfo *si;
	CMS_CertificateChoices *cch;
	STACK_OF(CMS_CertificateChoices) *certs;
	X509 *x;
	int i, j;
	int ret = 0;

	sd = cms_get0_signed(cms);
	if (!sd)
		return -1;
	certs = sd->certificates;
	for (i = 0; i < sk_CMS_SignerInfo_num(sd->signerInfos); i++) {
		si = sk_CMS_SignerInfo_value(sd->signerInfos, i);
		if (si->signer)
			continue;

		for (j = 0; j < sk_X509_num(scerts); j++) {
			x = sk_X509_value(scerts, j);
			if (CMS_SignerInfo_cert_cmp(si, x) == 0) {
				CMS_SignerInfo_set1_signer_cert(si, x);
				ret++;
				break;
			}
		}

		if (si->signer || (flags & CMS_NOINTERN))
			continue;

		for (j = 0; j < sk_CMS_CertificateChoices_num(certs); j++) {
			cch = sk_CMS_CertificateChoices_value(certs, j);
			if (cch->type != 0)
				continue;
			x = cch->d.certificate;
			if (CMS_SignerInfo_cert_cmp(si, x) == 0) {
				CMS_SignerInfo_set1_signer_cert(si, x);
				ret++;
				break;
			}
		}
	}
	return ret;
}
LCRYPTO_ALIAS(CMS_set1_signers_certs);

void
CMS_SignerInfo_get0_algs(CMS_SignerInfo *si, EVP_PKEY **pk, X509 **signer,
X509_ALGOR **pdig, X509_ALGOR **psig)
{
	if (pk)
		*pk = si->pkey;
	if (signer)
		*signer = si->signer;
	if (pdig)
		*pdig = si->digestAlgorithm;
	if (psig)
		*psig = si->signatureAlgorithm;
}
LCRYPTO_ALIAS(CMS_SignerInfo_get0_algs);

ASN1_OCTET_STRING *
CMS_SignerInfo_get0_signature(CMS_SignerInfo *si)
{
	return si->signature;
}
LCRYPTO_ALIAS(CMS_SignerInfo_get0_signature);

static int
cms_SignerInfo_content_sign(CMS_ContentInfo *cms, CMS_SignerInfo *si, BIO *chain)
{
	EVP_MD_CTX *mctx = EVP_MD_CTX_new();
	int r = 0;
	EVP_PKEY_CTX *pctx = NULL;

	if (mctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	if (!si->pkey) {
		CMSerror(CMS_R_NO_PRIVATE_KEY);
		goto err;
	}

	if (!cms_DigestAlgorithm_find_ctx(mctx, chain, si->digestAlgorithm))
		goto err;
	/* Set SignerInfo algorithm details if we used custom parameter */
	if (si->pctx && !cms_sd_asn1_ctrl(si, 0))
		goto err;

	/*
	 * If any signed attributes calculate and add messageDigest attribute
	 */

	if (CMS_signed_get_attr_count(si) >= 0) {
		ASN1_OBJECT *ctype =
		    cms->d.signedData->encapContentInfo->eContentType;
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned int mdlen;

		if (!EVP_DigestFinal_ex(mctx, md, &mdlen))
			goto err;
		if (!CMS_signed_add1_attr_by_NID(si, NID_pkcs9_messageDigest,
		    V_ASN1_OCTET_STRING, md, mdlen))
			goto err;
		/* Copy content type across */
		if (CMS_signed_add1_attr_by_NID(si, NID_pkcs9_contentType,
		    V_ASN1_OBJECT, ctype, -1) <= 0)
			goto err;
		if (!CMS_SignerInfo_sign(si))
			goto err;
	} else if (si->pctx) {
		unsigned char *sig;
		size_t siglen;
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned int mdlen;

		pctx = si->pctx;
		if (!EVP_DigestFinal_ex(mctx, md, &mdlen))
			goto err;
		siglen = EVP_PKEY_size(si->pkey);
		sig = malloc(siglen);
		if (sig == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (EVP_PKEY_sign(pctx, sig, &siglen, md, mdlen) <= 0) {
			free(sig);
			goto err;
		}
		ASN1_STRING_set0(si->signature, sig, siglen);
	} else {
		unsigned char *sig;
		unsigned int siglen;

		sig = malloc(EVP_PKEY_size(si->pkey));
		if (sig == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (!EVP_SignFinal(mctx, sig, &siglen, si->pkey)) {
			CMSerror(CMS_R_SIGNFINAL_ERROR);
			free(sig);
			goto err;
		}
		ASN1_STRING_set0(si->signature, sig, siglen);
	}

	r = 1;

 err:
	EVP_MD_CTX_free(mctx);
	EVP_PKEY_CTX_free(pctx);

	return r;
}

int
cms_SignedData_final(CMS_ContentInfo *cms, BIO *chain)
{
	STACK_OF(CMS_SignerInfo) *sinfos;
	CMS_SignerInfo *si;
	int i;

	sinfos = CMS_get0_SignerInfos(cms);
	for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
		si = sk_CMS_SignerInfo_value(sinfos, i);
		if (!cms_SignerInfo_content_sign(cms, si, chain))
			return 0;
	}
	cms->d.signedData->encapContentInfo->partial = 0;

	return 1;
}

int
CMS_SignerInfo_sign(CMS_SignerInfo *si)
{
	ASN1_TIME *at = NULL;
	const EVP_MD *md;
	unsigned char *buf = NULL, *sig = NULL;
	int buf_len = 0;
	size_t sig_len = 0;
	int ret = 0;

	if ((md = cms_SignerInfo_signature_md(si)) == NULL)
		goto err;

	if (CMS_signed_get_attr_by_NID(si, NID_pkcs9_signingTime, -1) < 0) {
		if ((at = X509_gmtime_adj(NULL, 0)) == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		if (!CMS_signed_add1_attr_by_NID(si, NID_pkcs9_signingTime,
		    at->type, at, -1))
			goto err;
	}

	if (si->pctx == NULL) {
		(void)EVP_MD_CTX_reset(si->mctx);
		if (!EVP_DigestSignInit(si->mctx, &si->pctx, md, NULL, si->pkey))
			goto err;
	}

	if (EVP_PKEY_CTX_ctrl(si->pctx, -1, EVP_PKEY_OP_SIGN,
	    EVP_PKEY_CTRL_CMS_SIGN, 0, si) <= 0) {
		CMSerror(CMS_R_CTRL_ERROR);
		goto err;
	}

	if ((buf_len = ASN1_item_i2d((ASN1_VALUE *)si->signedAttrs, &buf,
	    &CMS_Attributes_Sign_it)) <= 0) {
		buf_len = 0;
		goto err;
	}
	if (!EVP_DigestSign(si->mctx, NULL, &sig_len, buf, buf_len))
		goto err;
	if ((sig = calloc(1, sig_len)) == NULL)
		goto err;
	if (!EVP_DigestSign(si->mctx, sig, &sig_len, buf, buf_len))
		goto err;

	if (EVP_PKEY_CTX_ctrl(si->pctx, -1, EVP_PKEY_OP_SIGN,
	    EVP_PKEY_CTRL_CMS_SIGN, 1, si) <= 0) {
		CMSerror(CMS_R_CTRL_ERROR);
		goto err;
	}

	ASN1_STRING_set0(si->signature, sig, sig_len);
	sig = NULL;

	ret = 1;

 err:
	ASN1_TIME_free(at);
	(void)EVP_MD_CTX_reset(si->mctx);
	freezero(buf, buf_len);
	freezero(sig, sig_len);

	return ret;
}
LCRYPTO_ALIAS(CMS_SignerInfo_sign);

int
CMS_SignerInfo_verify(CMS_SignerInfo *si)
{
	const EVP_MD *md;
	unsigned char *buf = NULL;
	int buf_len = 0;
	int ret = -1;

	if ((md = cms_SignerInfo_signature_md(si)) == NULL)
		goto err;

	if (si->mctx == NULL)
		si->mctx = EVP_MD_CTX_new();
	if (si->mctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (EVP_DigestVerifyInit(si->mctx, &si->pctx, md, NULL, si->pkey) <= 0)
		goto err;

	if (!cms_sd_asn1_ctrl(si, 1))
		goto err;

	if ((buf_len = ASN1_item_i2d((ASN1_VALUE *)si->signedAttrs, &buf,
	    &CMS_Attributes_Verify_it)) <= 0) {
		buf_len = 0;
		goto err;
	}

	ret = EVP_DigestVerify(si->mctx, si->signature->data, si->signature->length,
	    buf, buf_len);
	if (ret <= 0) {
		CMSerror(CMS_R_VERIFICATION_FAILURE);
		goto err;
	}

 err:
	(void)EVP_MD_CTX_reset(si->mctx);
	freezero(buf, buf_len);

	return ret;
}
LCRYPTO_ALIAS(CMS_SignerInfo_verify);

/* Create a chain of digest BIOs from a CMS ContentInfo */

BIO *
cms_SignedData_init_bio(CMS_ContentInfo *cms)
{
	int i;
	CMS_SignedData *sd;
	BIO *chain = NULL;

	sd = cms_get0_signed(cms);
	if (!sd)
		return NULL;
	if (cms->d.signedData->encapContentInfo->partial)
		cms_sd_set_version(sd);
	for (i = 0; i < sk_X509_ALGOR_num(sd->digestAlgorithms); i++) {
		X509_ALGOR *digestAlgorithm;
		BIO *mdbio;
		digestAlgorithm = sk_X509_ALGOR_value(sd->digestAlgorithms, i);
		mdbio = cms_DigestAlgorithm_init_bio(digestAlgorithm);
		if (!mdbio)
			goto err;
		if (chain)
			BIO_push(chain, mdbio);
		else
			chain = mdbio;
	}

	return chain;

 err:
	BIO_free_all(chain);

	return NULL;
}

int
CMS_SignerInfo_verify_content(CMS_SignerInfo *si, BIO *chain)
{
	ASN1_OCTET_STRING *os = NULL;
	EVP_MD_CTX *mctx = EVP_MD_CTX_new();
	EVP_PKEY_CTX *pkctx = NULL;
	int r = -1;
	unsigned char mval[EVP_MAX_MD_SIZE];
	unsigned int mlen;

	if (mctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	/* If we have any signed attributes look for messageDigest value */
	if (CMS_signed_get_attr_count(si) >= 0) {
		os = CMS_signed_get0_data_by_OBJ(si,
		    OBJ_nid2obj(NID_pkcs9_messageDigest), -3,
		    V_ASN1_OCTET_STRING);
		if (!os) {
			CMSerror(CMS_R_ERROR_READING_MESSAGEDIGEST_ATTRIBUTE);
			goto err;
		}
	}

	if (!cms_DigestAlgorithm_find_ctx(mctx, chain, si->digestAlgorithm))
		goto err;

	if (EVP_DigestFinal_ex(mctx, mval, &mlen) <= 0) {
		CMSerror(CMS_R_UNABLE_TO_FINALIZE_CONTEXT);
		goto err;
	}

	/* If messageDigest found compare it */

	if (os) {
		if (mlen != (unsigned int)os->length) {
			CMSerror(CMS_R_MESSAGEDIGEST_ATTRIBUTE_WRONG_LENGTH);
			goto err;
		}

		if (memcmp(mval, os->data, mlen)) {
			CMSerror(CMS_R_VERIFICATION_FAILURE);
			r = 0;
		} else
			r = 1;
	} else {
		const EVP_MD *md = EVP_MD_CTX_md(mctx);

		pkctx = EVP_PKEY_CTX_new(si->pkey, NULL);
		if (pkctx == NULL)
			goto err;
		if (EVP_PKEY_verify_init(pkctx) <= 0)
			goto err;
		if (EVP_PKEY_CTX_set_signature_md(pkctx, md) <= 0)
			goto err;
		si->pctx = pkctx;
		if (!cms_sd_asn1_ctrl(si, 1))
			goto err;
		r = EVP_PKEY_verify(pkctx, si->signature->data,
			                si->signature->length, mval, mlen);
		if (r <= 0) {
			CMSerror(CMS_R_VERIFICATION_FAILURE);
			r = 0;
		}
	}

 err:
	EVP_PKEY_CTX_free(pkctx);
	EVP_MD_CTX_free(mctx);

	return r;
}
LCRYPTO_ALIAS(CMS_SignerInfo_verify_content);

int
CMS_add_smimecap(CMS_SignerInfo *si, STACK_OF(X509_ALGOR) *algs)
{
	unsigned char *smder = NULL;
	int smderlen, r;

	smderlen = i2d_X509_ALGORS(algs, &smder);
	if (smderlen <= 0)
		return 0;
	r = CMS_signed_add1_attr_by_NID(si, NID_SMIMECapabilities,
	    V_ASN1_SEQUENCE, smder, smderlen);
	free(smder);

	return r;
}
LCRYPTO_ALIAS(CMS_add_smimecap);

/*
 * Add AlgorithmIdentifier OID of type |nid| to the SMIMECapability attribute
 * set |*out_algs| (see RFC 3851, section 2.5.2). If keysize > 0, the OID has
 * an integer parameter of value |keysize|, otherwise parameters are omitted.
 *
 * See also PKCS7_simple_smimecap().
 */
int
CMS_add_simple_smimecap(STACK_OF(X509_ALGOR) **out_algs, int nid, int keysize)
{
	STACK_OF(X509_ALGOR) *algs;
	X509_ALGOR *alg = NULL;
	ASN1_INTEGER *parameter = NULL;
	int parameter_type = V_ASN1_UNDEF;
	int ret = 0;

	if ((algs = *out_algs) == NULL)
		algs = sk_X509_ALGOR_new_null();
	if (algs == NULL)
		goto err;

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

	if (sk_X509_ALGOR_push(algs, alg) <= 0)
		goto err;
	alg = NULL;

	*out_algs = algs;
	algs = NULL;

	ret = 1;

 err:
	if (algs != *out_algs)
		sk_X509_ALGOR_pop_free(algs, X509_ALGOR_free);
	X509_ALGOR_free(alg);
	ASN1_INTEGER_free(parameter);

	return ret;
}
LCRYPTO_ALIAS(CMS_add_simple_smimecap);

/* Check to see if a cipher exists and if so add S/MIME capabilities */

static int
cms_add_cipher_smcap(STACK_OF(X509_ALGOR) **sk, int nid, int arg)
{
	if (EVP_get_cipherbynid(nid))
		return CMS_add_simple_smimecap(sk, nid, arg);
	return 1;
}

int
CMS_add_standard_smimecap(STACK_OF(X509_ALGOR) **smcap)
{
	if (!cms_add_cipher_smcap(smcap, NID_aes_256_cbc, -1) ||
	    !cms_add_cipher_smcap(smcap, NID_aes_192_cbc, -1) ||
	    !cms_add_cipher_smcap(smcap, NID_aes_128_cbc, -1) ||
	    !cms_add_cipher_smcap(smcap, NID_des_ede3_cbc, -1) ||
	    !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 128) ||
	    !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 64) ||
	    !cms_add_cipher_smcap(smcap, NID_des_cbc, -1) ||
	    !cms_add_cipher_smcap(smcap, NID_rc2_cbc, 40))
		return 0;

	return 1;
}
LCRYPTO_ALIAS(CMS_add_standard_smimecap);
