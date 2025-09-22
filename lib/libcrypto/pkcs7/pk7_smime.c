/* $OpenBSD: pk7_smime.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 1999-2004 The OpenSSL Project.  All rights reserved.
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

/* Simple PKCS#7 processing functions */

#include <stdio.h>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static int pkcs7_copy_existing_digest(PKCS7 *p7, PKCS7_SIGNER_INFO *si);

PKCS7 *
PKCS7_sign(X509 *signcert, EVP_PKEY *pkey, STACK_OF(X509) *certs, BIO *data,
    int flags)
{
	PKCS7 *p7;
	int i;

	if (!(p7 = PKCS7_new())) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (!PKCS7_set_type(p7, NID_pkcs7_signed))
		goto err;

	if (!PKCS7_content_new(p7, NID_pkcs7_data))
		goto err;

	if (pkey && !PKCS7_sign_add_signer(p7, signcert, pkey, NULL, flags)) {
		PKCS7error(PKCS7_R_PKCS7_ADD_SIGNER_ERROR);
		goto err;
	}

	if (!(flags & PKCS7_NOCERTS)) {
		for (i = 0; i < sk_X509_num(certs); i++) {
			if (!PKCS7_add_certificate(p7, sk_X509_value(certs, i)))
				goto err;
		}
	}

	if (flags & PKCS7_DETACHED)
		PKCS7_set_detached(p7, 1);

	if (flags & (PKCS7_STREAM|PKCS7_PARTIAL))
		return p7;

	if (PKCS7_final(p7, data, flags))
		return p7;

err:
	PKCS7_free(p7);
	return NULL;
}
LCRYPTO_ALIAS(PKCS7_sign);

int
PKCS7_final(PKCS7 *p7, BIO *data, int flags)
{
	BIO *p7bio;
	int ret = 0;

	if (!(p7bio = PKCS7_dataInit(p7, NULL))) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	SMIME_crlf_copy(data, p7bio, flags);

	(void)BIO_flush(p7bio);

	if (!PKCS7_dataFinal(p7, p7bio)) {
		PKCS7error(PKCS7_R_PKCS7_DATASIGN);
		goto err;
	}

	ret = 1;

err:
	BIO_free_all(p7bio);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_final);

/* Check to see if a cipher exists and if so add S/MIME capabilities */

static int
add_cipher_smcap(STACK_OF(X509_ALGOR) *sk, int nid, int arg)
{
	if (EVP_get_cipherbynid(nid))
		return PKCS7_simple_smimecap(sk, nid, arg);
	return 1;
}

PKCS7_SIGNER_INFO *
PKCS7_sign_add_signer(PKCS7 *p7, X509 *signcert, EVP_PKEY *pkey,
    const EVP_MD *md, int flags)
{
	PKCS7_SIGNER_INFO *si = NULL;
	STACK_OF(X509_ALGOR) *smcap = NULL;

	if (!X509_check_private_key(signcert, pkey)) {
		PKCS7error(PKCS7_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
		return NULL;
	}

	if (!(si = PKCS7_add_signature(p7, signcert, pkey, md))) {
		PKCS7error(PKCS7_R_PKCS7_ADD_SIGNATURE_ERROR);
		return NULL;
	}

	if (!(flags & PKCS7_NOCERTS)) {
		if (!PKCS7_add_certificate(p7, signcert))
			goto err;
	}

	if (!(flags & PKCS7_NOATTR)) {
		if (!PKCS7_add_attrib_content_type(si, NULL))
			goto err;
		/* Add SMIMECapabilities */
		if (!(flags & PKCS7_NOSMIMECAP)) {
			if (!(smcap = sk_X509_ALGOR_new_null())) {
				PKCS7error(ERR_R_MALLOC_FAILURE);
				goto err;
			}
			if (!add_cipher_smcap(smcap, NID_aes_256_cbc, -1) ||
			    !add_cipher_smcap(smcap, NID_aes_192_cbc, -1) ||
			    !add_cipher_smcap(smcap, NID_aes_128_cbc, -1) ||
			    !add_cipher_smcap(smcap, NID_des_ede3_cbc, -1) ||
			    !add_cipher_smcap(smcap, NID_rc2_cbc, 128) ||
			    !add_cipher_smcap(smcap, NID_rc2_cbc, 64) ||
			    !add_cipher_smcap(smcap, NID_des_cbc, -1) ||
			    !add_cipher_smcap(smcap, NID_rc2_cbc, 40) ||
			    !PKCS7_add_attrib_smimecap(si, smcap))
				goto err;
			sk_X509_ALGOR_pop_free(smcap, X509_ALGOR_free);
			smcap = NULL;
		}
		if (flags & PKCS7_REUSE_DIGEST) {
			if (!pkcs7_copy_existing_digest(p7, si))
				goto err;
			if (!(flags & PKCS7_PARTIAL) &&
			    !PKCS7_SIGNER_INFO_sign(si))
				goto err;
		}
	}
	return si;

err:
	if (smcap)
		sk_X509_ALGOR_pop_free(smcap, X509_ALGOR_free);
	return NULL;
}
LCRYPTO_ALIAS(PKCS7_sign_add_signer);

/* Search for a digest matching SignerInfo digest type and if found
 * copy across.
 */

static int
pkcs7_copy_existing_digest(PKCS7 *p7, PKCS7_SIGNER_INFO *si)
{
	int i;
	STACK_OF(PKCS7_SIGNER_INFO) *sinfos;
	PKCS7_SIGNER_INFO *sitmp;
	ASN1_OCTET_STRING *osdig = NULL;

	sinfos = PKCS7_get_signer_info(p7);
	for (i = 0; i < sk_PKCS7_SIGNER_INFO_num(sinfos); i++) {
		sitmp = sk_PKCS7_SIGNER_INFO_value(sinfos, i);
		if (si == sitmp)
			break;
		if (sk_X509_ATTRIBUTE_num(sitmp->auth_attr) <= 0)
			continue;
		if (!OBJ_cmp(si->digest_alg->algorithm,
		    sitmp->digest_alg->algorithm)) {
			osdig = PKCS7_digest_from_attributes(sitmp->auth_attr);
			break;
		}

	}

	if (osdig)
		return PKCS7_add1_attrib_digest(si, osdig->data, osdig->length);

	PKCS7error(PKCS7_R_NO_MATCHING_DIGEST_TYPE_FOUND);
	return 0;
}

int
PKCS7_verify(PKCS7 *p7, STACK_OF(X509) *certs, X509_STORE *store, BIO *indata,
    BIO *out, int flags)
{
	STACK_OF(X509) *signers;
	X509 *signer;
	STACK_OF(PKCS7_SIGNER_INFO) *sinfos;
	PKCS7_SIGNER_INFO *si;
	X509_STORE_CTX cert_ctx;
	char buf[4096];
	int i, j = 0, k, ret = 0;
	BIO *p7bio;
	BIO *tmpin, *tmpout;

	if (!p7) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return 0;
	}

	if (!PKCS7_type_is_signed(p7)) {
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return 0;
	}

	/* Check for no data and no content: no data to verify signature */
	if (PKCS7_get_detached(p7) && !indata) {
		PKCS7error(PKCS7_R_NO_CONTENT);
		return 0;
	}

	/*
	 * Very old Netscape illegally included empty content with
	 * a detached signature.  Very old users should upgrade.
	 */
	/* Check for data and content: two sets of data */
	if (!PKCS7_get_detached(p7) && indata) {
		PKCS7error(PKCS7_R_CONTENT_AND_DATA_PRESENT);
		return 0;
	}

	sinfos = PKCS7_get_signer_info(p7);

	if (!sinfos || !sk_PKCS7_SIGNER_INFO_num(sinfos)) {
		PKCS7error(PKCS7_R_NO_SIGNATURES_ON_DATA);
		return 0;
	}


	signers = PKCS7_get0_signers(p7, certs, flags);

	if (!signers)
		return 0;

	/* Now verify the certificates */

	if (!(flags & PKCS7_NOVERIFY))
		for (k = 0; k < sk_X509_num(signers); k++) {
			signer = sk_X509_value (signers, k);
			if  (!(flags & PKCS7_NOCHAIN)) {
				if (!X509_STORE_CTX_init(&cert_ctx, store,
				    signer, p7->d.sign->cert)) {
					PKCS7error(ERR_R_X509_LIB);
					sk_X509_free(signers);
					return 0;
				}
				if (X509_STORE_CTX_set_default(&cert_ctx,
				    "smime_sign") == 0) {
					sk_X509_free(signers);
					return 0;
				}
			} else if (!X509_STORE_CTX_init(&cert_ctx, store,
			    signer, NULL)) {
				PKCS7error(ERR_R_X509_LIB);
				sk_X509_free(signers);
				return 0;
			}
			if (!(flags & PKCS7_NOCRL))
				X509_STORE_CTX_set0_crls(&cert_ctx, p7->d.sign->crl);
			i = X509_verify_cert(&cert_ctx);
			if (i <= 0)
				j = X509_STORE_CTX_get_error(&cert_ctx);
			X509_STORE_CTX_cleanup(&cert_ctx);
			if (i <= 0) {
				PKCS7error(PKCS7_R_CERTIFICATE_VERIFY_ERROR);
				ERR_asprintf_error_data("Verify error:%s",
				    X509_verify_cert_error_string(j));
				sk_X509_free(signers);
				return 0;
			}
			/* Check for revocation status here */
		}

	/*
	 * Performance optimization: if the content is a memory BIO then
	 * store its contents in a temporary read only memory BIO. This
	 * avoids potentially large numbers of slow copies of data which will
	 * occur when reading from a read write memory BIO when signatures
	 * are calculated.
	 */
	if (indata && (BIO_method_type(indata) == BIO_TYPE_MEM)) {
		char *ptr;
		long len;

		len = BIO_get_mem_data(indata, &ptr);
		tmpin = BIO_new_mem_buf(ptr, len);
		if (tmpin == NULL) {
			PKCS7error(ERR_R_MALLOC_FAILURE);
			return 0;
		}
	} else
		tmpin = indata;


	if (!(p7bio = PKCS7_dataInit(p7, tmpin)))
		goto err;

	if (flags & PKCS7_TEXT) {
		if (!(tmpout = BIO_new(BIO_s_mem()))) {
			PKCS7error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		BIO_set_mem_eof_return(tmpout, 0);
	} else
		tmpout = out;

	/* We now have to 'read' from p7bio to calculate digests etc. */
	for (;;) {
		i = BIO_read(p7bio, buf, sizeof(buf));
		if (i <= 0)
			break;
		if (tmpout)
			BIO_write(tmpout, buf, i);
	}

	if (flags & PKCS7_TEXT) {
		if (!SMIME_text(tmpout, out)) {
			PKCS7error(PKCS7_R_SMIME_TEXT_ERROR);
			BIO_free(tmpout);
			goto err;
		}
		BIO_free(tmpout);
	}

	/* Now Verify All Signatures */
	if (!(flags & PKCS7_NOSIGS))
		for (i = 0; i < sk_PKCS7_SIGNER_INFO_num(sinfos); i++) {
			si = sk_PKCS7_SIGNER_INFO_value(sinfos, i);
			signer = sk_X509_value (signers, i);
			j = PKCS7_signatureVerify(p7bio, p7, si, signer);
			if (j <= 0) {
				PKCS7error(PKCS7_R_SIGNATURE_FAILURE);
				goto err;
			}
		}

	ret = 1;

err:
	if (tmpin == indata) {
		if (indata)
			BIO_pop(p7bio);
	}
	BIO_free_all(p7bio);
	sk_X509_free(signers);

	return ret;
}
LCRYPTO_ALIAS(PKCS7_verify);

STACK_OF(X509) *
PKCS7_get0_signers(PKCS7 *p7, STACK_OF(X509) *certs, int flags)
{
	STACK_OF(X509) *signers;
	STACK_OF(PKCS7_SIGNER_INFO) *sinfos;
	PKCS7_SIGNER_INFO *si;
	PKCS7_ISSUER_AND_SERIAL *ias;
	X509 *signer;
	int i;

	if (!p7) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return NULL;
	}

	if (!PKCS7_type_is_signed(p7)) {
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return NULL;
	}

	/* Collect all the signers together */
	sinfos = PKCS7_get_signer_info(p7);
	if (sk_PKCS7_SIGNER_INFO_num(sinfos) <= 0) {
		PKCS7error(PKCS7_R_NO_SIGNERS);
		return 0;
	}

	if (!(signers = sk_X509_new_null())) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	for (i = 0; i < sk_PKCS7_SIGNER_INFO_num(sinfos); i++) {
		si = sk_PKCS7_SIGNER_INFO_value(sinfos, i);
		ias = si->issuer_and_serial;
		signer = NULL;
		/* If any certificates passed they take priority */
		if (certs)
			signer = X509_find_by_issuer_and_serial (certs,
			    ias->issuer, ias->serial);
		if (!signer && !(flags & PKCS7_NOINTERN) && p7->d.sign->cert)
			signer =
			    X509_find_by_issuer_and_serial(p7->d.sign->cert,
			      ias->issuer, ias->serial);
		if (!signer) {
			PKCS7error(PKCS7_R_SIGNER_CERTIFICATE_NOT_FOUND);
			sk_X509_free(signers);
			return 0;
		}

		if (!sk_X509_push(signers, signer)) {
			sk_X509_free(signers);
			return NULL;
		}
	}
	return signers;
}
LCRYPTO_ALIAS(PKCS7_get0_signers);

/* Build a complete PKCS#7 enveloped data */

PKCS7 *
PKCS7_encrypt(STACK_OF(X509) *certs, BIO *in, const EVP_CIPHER *cipher,
    int flags)
{
	PKCS7 *p7;
	BIO *p7bio = NULL;
	int i;
	X509 *x509;

	if (!(p7 = PKCS7_new())) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (!PKCS7_set_type(p7, NID_pkcs7_enveloped))
		goto err;
	if (!PKCS7_set_cipher(p7, cipher)) {
		PKCS7error(PKCS7_R_ERROR_SETTING_CIPHER);
		goto err;
	}

	for (i = 0; i < sk_X509_num(certs); i++) {
		x509 = sk_X509_value(certs, i);
		if (!PKCS7_add_recipient(p7, x509)) {
			PKCS7error(PKCS7_R_ERROR_ADDING_RECIPIENT);
			goto err;
		}
	}

	if (flags & PKCS7_STREAM)
		return p7;

	if (PKCS7_final(p7, in, flags))
		return p7;

err:
	BIO_free_all(p7bio);
	PKCS7_free(p7);
	return NULL;
}
LCRYPTO_ALIAS(PKCS7_encrypt);

int
PKCS7_decrypt(PKCS7 *p7, EVP_PKEY *pkey, X509 *cert, BIO *data, int flags)
{
	BIO *tmpmem;
	int ret, i;
	char buf[4096];

	if (!p7) {
		PKCS7error(PKCS7_R_INVALID_NULL_POINTER);
		return 0;
	}

	if (!PKCS7_type_is_enveloped(p7)) {
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return 0;
	}

	if (cert && !X509_check_private_key(cert, pkey)) {
		PKCS7error(PKCS7_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
		return 0;
	}

	if (!(tmpmem = PKCS7_dataDecode(p7, pkey, NULL, cert))) {
		PKCS7error(PKCS7_R_DECRYPT_ERROR);
		return 0;
	}

	if (flags & PKCS7_TEXT) {
		BIO *tmpbuf;

		/* Encrypt BIOs can't do BIO_gets() so add a buffer BIO */
		if (!(tmpbuf = BIO_new(BIO_f_buffer()))) {
			PKCS7error(ERR_R_MALLOC_FAILURE);
			BIO_free_all(tmpmem);
			return 0;
		}
		BIO_push(tmpbuf, tmpmem);
		ret = SMIME_text(tmpbuf, data);
		if (ret > 0 && BIO_method_type(tmpmem) == BIO_TYPE_CIPHER) {
			if (!BIO_get_cipher_status(tmpmem))
				ret = 0;
		}
		BIO_free_all(tmpbuf);
		return ret;
	} else {
		for (;;) {
			i = BIO_read(tmpmem, buf, sizeof(buf));
			if (i <= 0) {
				ret = 1;
				if (BIO_method_type(tmpmem) ==
				    BIO_TYPE_CIPHER) {
					if (!BIO_get_cipher_status(tmpmem))
						ret = 0;
				}
				break;
			}
			if (BIO_write(data, buf, i) != i) {
				ret = 0;
				break;
			}
		}
		BIO_free_all(tmpmem);
		return ret;
	}
}
LCRYPTO_ALIAS(PKCS7_decrypt);
