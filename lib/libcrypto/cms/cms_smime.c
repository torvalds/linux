/* $OpenBSD: cms_smime.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
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

#include <sys/types.h>

#include <stddef.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include "cms_local.h"
#include "err_local.h"

static BIO *
cms_get_text_bio(BIO *out, unsigned int flags)
{
	BIO *rbio;

	if (out == NULL)
		rbio = BIO_new(BIO_s_null());
	else if (flags & CMS_TEXT) {
		rbio = BIO_new(BIO_s_mem());
		BIO_set_mem_eof_return(rbio, 0);
	} else
		rbio = out;

	return rbio;
}

static int
cms_copy_content(BIO *out, BIO *in, unsigned int flags)
{
	unsigned char buf[4096];
	int r = 0, i;
	BIO *tmpout;

	tmpout = cms_get_text_bio(out, flags);

	if (tmpout == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* Read all content through chain to process digest, decrypt etc */
	for (;;) {
		i = BIO_read(in, buf, sizeof(buf));
		if (i <= 0) {
			if (BIO_method_type(in) == BIO_TYPE_CIPHER) {
				if (!BIO_get_cipher_status(in))
					goto err;
			}
			if (i < 0)
				goto err;
			break;
		}

		if (tmpout && (BIO_write(tmpout, buf, i) != i))
			goto err;
	}

	if (flags & CMS_TEXT) {
		if (!SMIME_text(tmpout, out)) {
			CMSerror(CMS_R_SMIME_TEXT_ERROR);
			goto err;
		}
	}

	r = 1;

 err:
	if (tmpout != out)
		BIO_free(tmpout);

	return r;
}

static int
check_content(CMS_ContentInfo *cms)
{
	ASN1_OCTET_STRING **pos = CMS_get0_content(cms);

	if (!pos || !*pos) {
		CMSerror(CMS_R_NO_CONTENT);
		return 0;
	}

	return 1;
}

static void
do_free_upto(BIO *f, BIO *upto)
{
	if (upto) {
		BIO *tbio;
		do {
			tbio = BIO_pop(f);
			BIO_free(f);
			f = tbio;
		}
		while (f && f != upto);
	} else
		BIO_free_all(f);
}

int
CMS_data(CMS_ContentInfo *cms, BIO *out, unsigned int flags)
{
	BIO *cont;
	int r;

	if (OBJ_obj2nid(CMS_get0_type(cms)) != NID_pkcs7_data) {
		CMSerror(CMS_R_TYPE_NOT_DATA);
		return 0;
	}
	cont = CMS_dataInit(cms, NULL);
	if (!cont)
		return 0;
	r = cms_copy_content(out, cont, flags);
	BIO_free_all(cont);

	return r;
}
LCRYPTO_ALIAS(CMS_data);

CMS_ContentInfo *
CMS_data_create(BIO *in, unsigned int flags)
{
	CMS_ContentInfo *cms;

	cms = cms_Data_create();
	if (!cms)
		return NULL;

	if ((flags & CMS_STREAM) || CMS_final(cms, in, NULL, flags))
		return cms;

	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_data_create);

int
CMS_digest_verify(CMS_ContentInfo *cms, BIO *dcont, BIO *out, unsigned int flags)
{
	BIO *cont;
	int r;

	if (OBJ_obj2nid(CMS_get0_type(cms)) != NID_pkcs7_digest) {
		CMSerror(CMS_R_TYPE_NOT_DIGESTED_DATA);
		return 0;
	}

	if (!dcont && !check_content(cms))
		return 0;

	cont = CMS_dataInit(cms, dcont);
	if (!cont)
		return 0;
	r = cms_copy_content(out, cont, flags);
	if (r)
		r = cms_DigestedData_do_final(cms, cont, 1);
	do_free_upto(cont, dcont);

	return r;
}
LCRYPTO_ALIAS(CMS_digest_verify);

CMS_ContentInfo *
CMS_digest_create(BIO *in, const EVP_MD *md, unsigned int flags)
{
	CMS_ContentInfo *cms;

	if (!md)
		md = EVP_sha1();
	cms = cms_DigestedData_create(md);
	if (!cms)
		return NULL;

	if (!(flags & CMS_DETACHED))
		CMS_set_detached(cms, 0);

	if ((flags & CMS_STREAM) || CMS_final(cms, in, NULL, flags))
		return cms;

	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_digest_create);

int
CMS_EncryptedData_decrypt(CMS_ContentInfo *cms, const unsigned char *key,
    size_t keylen, BIO *dcont, BIO *out, unsigned int flags)
{
	BIO *cont;
	int r;

	if (OBJ_obj2nid(CMS_get0_type(cms)) != NID_pkcs7_encrypted) {
		CMSerror(CMS_R_TYPE_NOT_ENCRYPTED_DATA);
		return 0;
	}

	if (!dcont && !check_content(cms))
		return 0;

	if (CMS_EncryptedData_set1_key(cms, NULL, key, keylen) <= 0)
		return 0;
	cont = CMS_dataInit(cms, dcont);
	if (!cont)
		return 0;
	r = cms_copy_content(out, cont, flags);
	do_free_upto(cont, dcont);

	return r;
}
LCRYPTO_ALIAS(CMS_EncryptedData_decrypt);

CMS_ContentInfo *
CMS_EncryptedData_encrypt(BIO *in, const EVP_CIPHER *cipher,
    const unsigned char *key, size_t keylen, unsigned int flags)
{
	CMS_ContentInfo *cms;

	if (!cipher) {
		CMSerror(CMS_R_NO_CIPHER);
		return NULL;
	}
	cms = CMS_ContentInfo_new();
	if (cms == NULL)
		return NULL;
	if (!CMS_EncryptedData_set1_key(cms, cipher, key, keylen))
		return NULL;

	if (!(flags & CMS_DETACHED))
		CMS_set_detached(cms, 0);

	if ((flags & (CMS_STREAM | CMS_PARTIAL)) ||
	    CMS_final(cms, in, NULL, flags))
		return cms;

	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_EncryptedData_encrypt);

static int
cms_signerinfo_verify_cert(CMS_SignerInfo *si, X509_STORE *store,
    STACK_OF(X509) *certs, STACK_OF(X509_CRL) *crls)
{
	X509_STORE_CTX *ctx = X509_STORE_CTX_new();
	X509 *signer;
	int i, j, r = 0;

	if (ctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	CMS_SignerInfo_get0_algs(si, NULL, &signer, NULL, NULL);
	if (!X509_STORE_CTX_init(ctx, store, signer, certs)) {
		CMSerror(CMS_R_STORE_INIT_ERROR);
		goto err;
	}
	X509_STORE_CTX_set_default(ctx, "smime_sign");
	if (crls)
		X509_STORE_CTX_set0_crls(ctx, crls);

	i = X509_verify_cert(ctx);
	if (i <= 0) {
		j = X509_STORE_CTX_get_error(ctx);
		CMSerror(CMS_R_CERTIFICATE_VERIFY_ERROR);
		ERR_asprintf_error_data("Verify error: %s",
		    X509_verify_cert_error_string(j));
		goto err;
	}
	r = 1;

 err:
	X509_STORE_CTX_free(ctx);

	return r;
}

int
CMS_verify(CMS_ContentInfo *cms, STACK_OF(X509) *certs, X509_STORE *store,
    BIO *dcont, BIO *out, unsigned int flags)
{
	CMS_SignerInfo *si;
	STACK_OF(CMS_SignerInfo) *sinfos;
	STACK_OF(X509) *cms_certs = NULL;
	STACK_OF(X509_CRL) *crls = NULL;
	X509 *signer;
	int i, scount = 0, ret = 0;
	BIO *cmsbio = NULL, *tmpin = NULL, *tmpout = NULL;

	if (!dcont && !check_content(cms))
		return 0;
	if (dcont && !(flags & CMS_BINARY)) {
		const ASN1_OBJECT *coid = CMS_get0_eContentType(cms);
		if (OBJ_obj2nid(coid) == NID_id_ct_asciiTextWithCRLF)
			flags |= CMS_ASCIICRLF;
	}

	/* Attempt to find all signer certificates */

	sinfos = CMS_get0_SignerInfos(cms);
	if (sk_CMS_SignerInfo_num(sinfos) <= 0) {
		CMSerror(CMS_R_NO_SIGNERS);
		goto err;
	}

	for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
		si = sk_CMS_SignerInfo_value(sinfos, i);
		CMS_SignerInfo_get0_algs(si, NULL, &signer, NULL, NULL);
		if (signer)
			scount++;
	}

	if (scount != sk_CMS_SignerInfo_num(sinfos))
		scount += CMS_set1_signers_certs(cms, certs, flags);

	if (scount != sk_CMS_SignerInfo_num(sinfos)) {
		CMSerror(CMS_R_SIGNER_CERTIFICATE_NOT_FOUND);
		goto err;
	}

	/* Attempt to verify all signers certs */

	if (!(flags & CMS_NO_SIGNER_CERT_VERIFY)) {
		cms_certs = CMS_get1_certs(cms);
		if (!(flags & CMS_NOCRL))
			crls = CMS_get1_crls(cms);
		for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
			si = sk_CMS_SignerInfo_value(sinfos, i);
			if (!cms_signerinfo_verify_cert(si, store, cms_certs, crls))
				goto err;
		}
	}

	/* Attempt to verify all SignerInfo signed attribute signatures */

	if (!(flags & CMS_NO_ATTR_VERIFY)) {
		for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
			si = sk_CMS_SignerInfo_value(sinfos, i);
			if (CMS_signed_get_attr_count(si) < 0)
				continue;
			if (CMS_SignerInfo_verify(si) <= 0)
				goto err;
		}
	}

	/*
	 * Performance optimization: if the content is a memory BIO then store
	 * its contents in a temporary read only memory BIO. This avoids
	 * potentially large numbers of slow copies of data which will occur when
	 * reading from a read write memory BIO when signatures are calculated.
	 */

	if (dcont && (BIO_method_type(dcont) == BIO_TYPE_MEM)) {
		char *ptr;
		long len;

		len = BIO_get_mem_data(dcont, &ptr);
		tmpin = BIO_new_mem_buf(ptr, len);
		if (tmpin == NULL) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err2;
		}
	} else
		tmpin = dcont;

	/*
	 * If not binary mode and detached generate digests by *writing* through
	 * the BIO. That makes it possible to canonicalise the input.
	 */
	if (!(flags & SMIME_BINARY) && dcont) {
		/*
		 * Create output BIO so we can either handle text or to ensure
		 * included content doesn't override detached content.
		 */
		tmpout = cms_get_text_bio(out, flags);
		if (!tmpout) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		cmsbio = CMS_dataInit(cms, tmpout);
		if (!cmsbio)
			goto err;
		/*
		 * Don't use SMIME_TEXT for verify: it adds headers and we want to
		 * remove them.
		 */
		SMIME_crlf_copy(dcont, cmsbio, flags & ~SMIME_TEXT);

		if (flags & CMS_TEXT) {
			if (!SMIME_text(tmpout, out)) {
				CMSerror(CMS_R_SMIME_TEXT_ERROR);
				goto err;
			}
		}
	} else {
		cmsbio = CMS_dataInit(cms, tmpin);
		if (!cmsbio)
			goto err;

		if (!cms_copy_content(out, cmsbio, flags))
			goto err;

	}
	if (!(flags & CMS_NO_CONTENT_VERIFY)) {
		for (i = 0; i < sk_CMS_SignerInfo_num(sinfos); i++) {
			si = sk_CMS_SignerInfo_value(sinfos, i);
			if (CMS_SignerInfo_verify_content(si, cmsbio) <= 0) {
				CMSerror(CMS_R_CONTENT_VERIFY_ERROR);
				goto err;
			}
		}
	}

	ret = 1;

 err:
	if (!(flags & SMIME_BINARY) && dcont) {
		do_free_upto(cmsbio, tmpout);
		if (tmpin != dcont)
			BIO_free(tmpin);
	} else {
		if (dcont && (tmpin == dcont))
			do_free_upto(cmsbio, dcont);
		else
			BIO_free_all(cmsbio);
	}

	if (out != tmpout)
		BIO_free_all(tmpout);

 err2:
	sk_X509_pop_free(cms_certs, X509_free);
	sk_X509_CRL_pop_free(crls, X509_CRL_free);

	return ret;
}
LCRYPTO_ALIAS(CMS_verify);

int
CMS_verify_receipt(CMS_ContentInfo *rcms, CMS_ContentInfo *ocms,
    STACK_OF(X509) *certs, X509_STORE *store, unsigned int flags)
{
	int r;

	flags &= ~(CMS_DETACHED | CMS_TEXT);
	r = CMS_verify(rcms, certs, store, NULL, NULL, flags);
	if (r <= 0)
		return r;

	return cms_Receipt_verify(rcms, ocms);
}
LCRYPTO_ALIAS(CMS_verify_receipt);

CMS_ContentInfo *
CMS_sign(X509 *signcert, EVP_PKEY *pkey, STACK_OF(X509) *certs, BIO *data,
    unsigned int flags)
{
	CMS_ContentInfo *cms;
	int i;

	cms = CMS_ContentInfo_new();
	if (cms == NULL || !CMS_SignedData_init(cms))
		goto merr;
	if (flags & CMS_ASCIICRLF &&
	    !CMS_set1_eContentType(cms, OBJ_nid2obj(NID_id_ct_asciiTextWithCRLF)))
		goto err;

	if (pkey && !CMS_add1_signer(cms, signcert, pkey, NULL, flags)) {
		CMSerror(CMS_R_ADD_SIGNER_ERROR);
		goto err;
	}

	for (i = 0; i < sk_X509_num(certs); i++) {
		X509 *x = sk_X509_value(certs, i);
		if (!CMS_add1_cert(cms, x))
			goto merr;
	}

	if (!(flags & CMS_DETACHED))
		CMS_set_detached(cms, 0);

	if ((flags & (CMS_STREAM | CMS_PARTIAL)) ||
	    CMS_final(cms, data, NULL, flags))
		return cms;
	else
		goto err;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);

 err:
	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_sign);

CMS_ContentInfo *
CMS_sign_receipt(CMS_SignerInfo *si, X509 *signcert, EVP_PKEY *pkey,
    STACK_OF(X509) *certs, unsigned int flags)
{
	CMS_SignerInfo *rct_si;
	CMS_ContentInfo *cms = NULL;
	ASN1_OCTET_STRING **pos, *os;
	BIO *rct_cont = NULL;
	int r = 0;

	flags &= ~(CMS_STREAM | CMS_TEXT);
	/* Not really detached but avoids content being allocated */
	flags |= CMS_PARTIAL | CMS_BINARY | CMS_DETACHED;
	if (!pkey || !signcert) {
		CMSerror(CMS_R_NO_KEY_OR_CERT);
		return NULL;
	}

	/* Initialize signed data */

	cms = CMS_sign(NULL, NULL, certs, NULL, flags);
	if (!cms)
		goto err;

	/* Set inner content type to signed receipt */
	if (!CMS_set1_eContentType(cms, OBJ_nid2obj(NID_id_smime_ct_receipt)))
		goto err;

	rct_si = CMS_add1_signer(cms, signcert, pkey, NULL, flags);
	if (!rct_si) {
		CMSerror(CMS_R_ADD_SIGNER_ERROR);
		goto err;
	}

	os = cms_encode_Receipt(si);
	if (!os)
		goto err;

	/* Set content to digest */
	rct_cont = BIO_new_mem_buf(os->data, os->length);
	if (!rct_cont)
		goto err;

	/* Add msgSigDigest attribute */

	if (!cms_msgSigDigest_add1(rct_si, si))
		goto err;

	/* Finalize structure */
	if (!CMS_final(cms, rct_cont, NULL, flags))
		goto err;

	/* Set embedded content */
	pos = CMS_get0_content(cms);
	*pos = os;

	r = 1;

 err:
	BIO_free(rct_cont);
	if (r)
		return cms;
	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_sign_receipt);

CMS_ContentInfo *
CMS_encrypt(STACK_OF(X509) *certs, BIO *data, const EVP_CIPHER *cipher,
    unsigned int flags)
{
	CMS_ContentInfo *cms;
	int i;
	X509 *recip;

	cms = CMS_EnvelopedData_create(cipher);
	if (!cms)
		goto merr;
	for (i = 0; i < sk_X509_num(certs); i++) {
		recip = sk_X509_value(certs, i);
		if (!CMS_add1_recipient_cert(cms, recip, flags)) {
			CMSerror(CMS_R_RECIPIENT_ERROR);
			goto err;
		}
	}

	if (!(flags & CMS_DETACHED))
		CMS_set_detached(cms, 0);

	if ((flags & (CMS_STREAM | CMS_PARTIAL)) ||
	    CMS_final(cms, data, NULL, flags))
		return cms;
	else
		goto err;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);
 err:
	CMS_ContentInfo_free(cms);

	return NULL;
}
LCRYPTO_ALIAS(CMS_encrypt);

static int
cms_kari_set1_pkey(CMS_ContentInfo *cms, CMS_RecipientInfo *ri, EVP_PKEY *pk,
    X509 *cert)
{
	int i;
	STACK_OF(CMS_RecipientEncryptedKey) *reks;
	CMS_RecipientEncryptedKey *rek;

	reks = CMS_RecipientInfo_kari_get0_reks(ri);
	for (i = 0; i < sk_CMS_RecipientEncryptedKey_num(reks); i++) {
		int rv;

		rek = sk_CMS_RecipientEncryptedKey_value(reks, i);
		if (cert != NULL && CMS_RecipientEncryptedKey_cert_cmp(rek, cert))
			continue;
		CMS_RecipientInfo_kari_set0_pkey(ri, pk);
		rv = CMS_RecipientInfo_kari_decrypt(cms, ri, rek);
		CMS_RecipientInfo_kari_set0_pkey(ri, NULL);
		if (rv > 0)
			return 1;
		return cert == NULL ? 0 : -1;
	}

	return 0;
}

int
CMS_decrypt_set1_pkey(CMS_ContentInfo *cms, EVP_PKEY *pk, X509 *cert)
{
	STACK_OF(CMS_RecipientInfo) *ris;
	CMS_RecipientInfo *ri;
	int i, r, ri_type;
	int debug = 0, match_ri = 0;

	ris = CMS_get0_RecipientInfos(cms);
	if (ris)
		debug = cms->d.envelopedData->encryptedContentInfo->debug;
	ri_type = cms_pkey_get_ri_type(pk);
	if (ri_type == CMS_RECIPINFO_NONE) {
		CMSerror(CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		return 0;
	}

	for (i = 0; i < sk_CMS_RecipientInfo_num(ris); i++) {
		ri = sk_CMS_RecipientInfo_value(ris, i);
		if (CMS_RecipientInfo_type(ri) != ri_type)
			continue;
		match_ri = 1;
		if (ri_type == CMS_RECIPINFO_AGREE) {
			r = cms_kari_set1_pkey(cms, ri, pk, cert);
			if (r > 0)
				return 1;
			if (r < 0)
				return 0;
		}
		/*
		 * If we have a cert try matching RecipientInfo otherwise try them
		 * all.
		 */
		else if (!cert || !CMS_RecipientInfo_ktri_cert_cmp(ri, cert)) {
			EVP_PKEY_up_ref(pk);
			CMS_RecipientInfo_set0_pkey(ri, pk);
			r = CMS_RecipientInfo_decrypt(cms, ri);
			CMS_RecipientInfo_set0_pkey(ri, NULL);
			if (cert) {
				/*
				 * If not debugging clear any error and return success to
				 * avoid leaking of information useful to MMA
				 */
				if (!debug) {
					ERR_clear_error();
					return 1;
				}
				if (r > 0)
					return 1;
				CMSerror(CMS_R_DECRYPT_ERROR);
				return 0;
			}
			/*
			 * If no cert and not debugging don't leave loop after first
			 * successful decrypt. Always attempt to decrypt all recipients
			 * to avoid leaking timing of a successful decrypt.
			 */
			else if (r > 0 && debug)
				return 1;
		}
	}
	/* If no cert, key transport and not debugging always return success */
	if (cert == NULL && ri_type == CMS_RECIPINFO_TRANS && match_ri && !debug) {
		ERR_clear_error();
		return 1;
	}

	CMSerror(CMS_R_NO_MATCHING_RECIPIENT);

	return 0;
}
LCRYPTO_ALIAS(CMS_decrypt_set1_pkey);

int
CMS_decrypt_set1_key(CMS_ContentInfo *cms, unsigned char *key, size_t keylen,
    const unsigned char *id, size_t idlen)
{
	STACK_OF(CMS_RecipientInfo) *ris;
	CMS_RecipientInfo *ri;
	int i, r;

	ris = CMS_get0_RecipientInfos(cms);
	for (i = 0; i < sk_CMS_RecipientInfo_num(ris); i++) {
		ri = sk_CMS_RecipientInfo_value(ris, i);
		if (CMS_RecipientInfo_type(ri) != CMS_RECIPINFO_KEK)
			continue;

		/*
		 * If we have an id try matching RecipientInfo otherwise try them
		 * all.
		 */
		if (!id || (CMS_RecipientInfo_kekri_id_cmp(ri, id, idlen) == 0)) {
			CMS_RecipientInfo_set0_key(ri, key, keylen);
			r = CMS_RecipientInfo_decrypt(cms, ri);
			CMS_RecipientInfo_set0_key(ri, NULL, 0);
			if (r > 0)
				return 1;
			if (id) {
				CMSerror(CMS_R_DECRYPT_ERROR);
				return 0;
			}
			ERR_clear_error();
		}
	}

	CMSerror(CMS_R_NO_MATCHING_RECIPIENT);

	return 0;
}
LCRYPTO_ALIAS(CMS_decrypt_set1_key);

int
CMS_decrypt_set1_password(CMS_ContentInfo *cms, unsigned char *pass,
    ssize_t passlen)
{
	STACK_OF(CMS_RecipientInfo) *ris;
	CMS_RecipientInfo *ri;
	int i, r;

	ris = CMS_get0_RecipientInfos(cms);
	for (i = 0; i < sk_CMS_RecipientInfo_num(ris); i++) {
		ri = sk_CMS_RecipientInfo_value(ris, i);
		if (CMS_RecipientInfo_type(ri) != CMS_RECIPINFO_PASS)
			continue;
		CMS_RecipientInfo_set0_password(ri, pass, passlen);
		r = CMS_RecipientInfo_decrypt(cms, ri);
		CMS_RecipientInfo_set0_password(ri, NULL, 0);
		if (r > 0)
			return 1;
	}

	CMSerror(CMS_R_NO_MATCHING_RECIPIENT);

	return 0;
}
LCRYPTO_ALIAS(CMS_decrypt_set1_password);

int
CMS_decrypt(CMS_ContentInfo *cms, EVP_PKEY *pk, X509 *cert, BIO *dcont,
    BIO *out, unsigned int flags)
{
	int r;
	BIO *cont;

	if (OBJ_obj2nid(CMS_get0_type(cms)) != NID_pkcs7_enveloped) {
		CMSerror(CMS_R_TYPE_NOT_ENVELOPED_DATA);
		return 0;
	}
	if (!dcont && !check_content(cms))
		return 0;
	if (flags & CMS_DEBUG_DECRYPT)
		cms->d.envelopedData->encryptedContentInfo->debug = 1;
	else
		cms->d.envelopedData->encryptedContentInfo->debug = 0;
	if (!cert)
		cms->d.envelopedData->encryptedContentInfo->havenocert = 1;
	else
		cms->d.envelopedData->encryptedContentInfo->havenocert = 0;
	if (!pk && !cert && !dcont && !out)
		return 1;
	if (pk && !CMS_decrypt_set1_pkey(cms, pk, cert))
		return 0;
	cont = CMS_dataInit(cms, dcont);
	if (!cont)
		return 0;
	r = cms_copy_content(out, cont, flags);
	do_free_upto(cont, dcont);

	return r;
}
LCRYPTO_ALIAS(CMS_decrypt);

int
CMS_final(CMS_ContentInfo *cms, BIO *data, BIO *dcont, unsigned int flags)
{
	BIO *cmsbio;
	int ret = 0;

	if ((cmsbio = CMS_dataInit(cms, dcont)) == NULL) {
		CMSerror(CMS_R_CMS_LIB);
		return 0;
	}

	SMIME_crlf_copy(data, cmsbio, flags);

	(void)BIO_flush(cmsbio);

	if (!CMS_dataFinal(cms, cmsbio)) {
		CMSerror(CMS_R_CMS_DATAFINAL_ERROR);
		goto err;
	}

	ret = 1;

 err:
	do_free_upto(cmsbio, dcont);

	return ret;
}
LCRYPTO_ALIAS(CMS_final);

int
CMS_uncompress(CMS_ContentInfo *cms, BIO *dcont, BIO *out, unsigned int flags)
{
	CMSerror(CMS_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
	return 0;
}
LCRYPTO_ALIAS(CMS_uncompress);

CMS_ContentInfo *
CMS_compress(BIO *in, int comp_nid, unsigned int flags)
{
	CMSerror(CMS_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
	return NULL;
}
LCRYPTO_ALIAS(CMS_compress);
