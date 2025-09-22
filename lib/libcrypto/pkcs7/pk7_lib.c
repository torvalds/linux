/* $OpenBSD: pk7_lib.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include <openssl/objects.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

long
PKCS7_ctrl(PKCS7 *p7, int cmd, long larg, char *parg)
{
	int nid;
	long ret = 0;

	nid = OBJ_obj2nid(p7->type);

	switch (cmd) {
	case PKCS7_OP_SET_DETACHED_SIGNATURE:
		if (nid == NID_pkcs7_signed) {
			if (p7->d.sign == NULL) {
				PKCS7error(PKCS7_R_NO_CONTENT);
				break;
			}
			ret = p7->detached = (int)larg;
			if (ret && PKCS7_type_is_data(p7->d.sign->contents)) {
				ASN1_OCTET_STRING *os;
				os = p7->d.sign->contents->d.data;
				ASN1_OCTET_STRING_free(os);
				p7->d.sign->contents->d.data = NULL;
			}
		} else {
			PKCS7error(PKCS7_R_OPERATION_NOT_SUPPORTED_ON_THIS_TYPE);
			ret = 0;
		}
		break;
	case PKCS7_OP_GET_DETACHED_SIGNATURE:
		if (nid == NID_pkcs7_signed) {
			if (p7->d.sign == NULL ||
			    p7->d.sign->contents->d.ptr == NULL)
				ret = 1;
			else
				ret = 0;

			p7->detached = ret;
		} else {
			PKCS7error(PKCS7_R_OPERATION_NOT_SUPPORTED_ON_THIS_TYPE);
			ret = 0;
		}

		break;
	default:
		PKCS7error(PKCS7_R_UNKNOWN_OPERATION);
		ret = 0;
	}
	return (ret);
}
LCRYPTO_ALIAS(PKCS7_ctrl);

int
PKCS7_content_new(PKCS7 *p7, int type)
{
	PKCS7 *ret = NULL;

	if ((ret = PKCS7_new()) == NULL)
		goto err;
	if (!PKCS7_set_type(ret, type))
		goto err;
	if (!PKCS7_set_content(p7, ret))
		goto err;

	return (1);
err:
	if (ret != NULL)
		PKCS7_free(ret);
	return (0);
}
LCRYPTO_ALIAS(PKCS7_content_new);

int
PKCS7_set_content(PKCS7 *p7, PKCS7 *p7_data)
{
	int i;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signed:
		if (p7->d.sign->contents != NULL)
			PKCS7_free(p7->d.sign->contents);
		p7->d.sign->contents = p7_data;
		break;
	case NID_pkcs7_digest:
		if (p7->d.digest->contents != NULL)
			PKCS7_free(p7->d.digest->contents);
		p7->d.digest->contents = p7_data;
		break;
	case NID_pkcs7_data:
	case NID_pkcs7_enveloped:
	case NID_pkcs7_signedAndEnveloped:
	case NID_pkcs7_encrypted:
	default:
		PKCS7error(PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
	}
	return (1);
err:
	return (0);
}
LCRYPTO_ALIAS(PKCS7_set_content);

int
PKCS7_set_type(PKCS7 *p7, int type)
{
	ASN1_OBJECT *obj;

	/*PKCS7_content_free(p7);*/
	obj=OBJ_nid2obj(type); /* will not fail */

	switch (type) {
	case NID_pkcs7_signed:
		p7->type = obj;
		if ((p7->d.sign = PKCS7_SIGNED_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(p7->d.sign->version, 1)) {
			PKCS7_SIGNED_free(p7->d.sign);
			p7->d.sign = NULL;
			goto err;
		}
		break;
	case NID_pkcs7_data:
		p7->type = obj;
		if ((p7->d.data = ASN1_OCTET_STRING_new()) == NULL)
			goto err;
		break;
	case NID_pkcs7_signedAndEnveloped:
		p7->type = obj;
		if ((p7->d.signed_and_enveloped =
		    PKCS7_SIGN_ENVELOPE_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(p7->d.signed_and_enveloped->version, 1))
			goto err;
		p7->d.signed_and_enveloped->enc_data->content_type =
		    OBJ_nid2obj(NID_pkcs7_data);
		break;
	case NID_pkcs7_enveloped:
		p7->type = obj;
		if ((p7->d.enveloped = PKCS7_ENVELOPE_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(p7->d.enveloped->version, 0))
			goto err;
		p7->d.enveloped->enc_data->content_type =
		    OBJ_nid2obj(NID_pkcs7_data);
		break;
	case NID_pkcs7_encrypted:
		p7->type = obj;
		if ((p7->d.encrypted = PKCS7_ENCRYPT_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(p7->d.encrypted->version, 0))
			goto err;
		p7->d.encrypted->enc_data->content_type =
		    OBJ_nid2obj(NID_pkcs7_data);
		break;

	case NID_pkcs7_digest:
		p7->type = obj;
		if ((p7->d.digest = PKCS7_DIGEST_new()) == NULL)
			goto err;
		if (!ASN1_INTEGER_set(p7->d.digest->version, 0))
			goto err;
		break;
	default:
		PKCS7error(PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
	}
	return (1);
err:
	return (0);
}
LCRYPTO_ALIAS(PKCS7_set_type);

int
PKCS7_set0_type_other(PKCS7 *p7, int type, ASN1_TYPE *other)
{
	p7->type = OBJ_nid2obj(type);
	p7->d.other = other;
	return 1;
}
LCRYPTO_ALIAS(PKCS7_set0_type_other);

int
PKCS7_add_signer(PKCS7 *p7, PKCS7_SIGNER_INFO *psi)
{
	int i, j, nid;
	X509_ALGOR *alg;
	STACK_OF(PKCS7_SIGNER_INFO) *signer_sk;
	STACK_OF(X509_ALGOR) *md_sk;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signed:
		signer_sk = p7->d.sign->signer_info;
		md_sk = p7->d.sign->md_algs;
		break;
	case NID_pkcs7_signedAndEnveloped:
		signer_sk = p7->d.signed_and_enveloped->signer_info;
		md_sk = p7->d.signed_and_enveloped->md_algs;
		break;
	default:
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return (0);
	}

	nid = OBJ_obj2nid(psi->digest_alg->algorithm);

	/* If the digest is not currently listed, add it */
	j = 0;
	for (i = 0; i < sk_X509_ALGOR_num(md_sk); i++) {
		alg = sk_X509_ALGOR_value(md_sk, i);
		if (OBJ_obj2nid(alg->algorithm) == nid) {
			j = 1;
			break;
		}
	}
	if (!j) /* we need to add another algorithm */
	{
		if (!(alg = X509_ALGOR_new()) ||
		    !(alg->parameter = ASN1_TYPE_new())) {
			X509_ALGOR_free(alg);
			PKCS7error(ERR_R_MALLOC_FAILURE);
			return (0);
		}
		alg->algorithm = OBJ_nid2obj(nid);
		alg->parameter->type = V_ASN1_NULL;
		if (!sk_X509_ALGOR_push(md_sk, alg)) {
			X509_ALGOR_free(alg);
			return 0;
		}
	}

	if (!sk_PKCS7_SIGNER_INFO_push(signer_sk, psi))
		return 0;
	return (1);
}
LCRYPTO_ALIAS(PKCS7_add_signer);

int
PKCS7_add_certificate(PKCS7 *p7, X509 *x509)
{
	int i;
	STACK_OF(X509) **sk;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signed:
		sk = &(p7->d.sign->cert);
		break;
	case NID_pkcs7_signedAndEnveloped:
		sk = &(p7->d.signed_and_enveloped->cert);
		break;
	default:
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return (0);
	}

	if (*sk == NULL)
		*sk = sk_X509_new_null();
	if (*sk == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	CRYPTO_add(&x509->references, 1, CRYPTO_LOCK_X509);
	if (!sk_X509_push(*sk, x509)) {
		X509_free(x509);
		return 0;
	}
	return (1);
}
LCRYPTO_ALIAS(PKCS7_add_certificate);

int
PKCS7_add_crl(PKCS7 *p7, X509_CRL *crl)
{
	int i;
	STACK_OF(X509_CRL) **sk;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signed:
		sk = &(p7->d.sign->crl);
		break;
	case NID_pkcs7_signedAndEnveloped:
		sk = &(p7->d.signed_and_enveloped->crl);
		break;
	default:
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return (0);
	}

	if (*sk == NULL)
		*sk = sk_X509_CRL_new_null();
	if (*sk == NULL) {
		PKCS7error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	CRYPTO_add(&crl->references, 1, CRYPTO_LOCK_X509_CRL);
	if (!sk_X509_CRL_push(*sk, crl)) {
		X509_CRL_free(crl);
		return 0;
	}
	return (1);
}
LCRYPTO_ALIAS(PKCS7_add_crl);

int
PKCS7_SIGNER_INFO_set(PKCS7_SIGNER_INFO *p7i, X509 *x509, EVP_PKEY *pkey,
    const EVP_MD *dgst)
{
	int nid;
	int ret;

	/* We now need to add another PKCS7_SIGNER_INFO entry */
	if (!ASN1_INTEGER_set(p7i->version, 1))
		goto err;
	if (!X509_NAME_set(&p7i->issuer_and_serial->issuer,
	    X509_get_issuer_name(x509)))
		goto err;

	/* because ASN1_INTEGER_set is used to set a 'long' we will do
	 * things the ugly way. */
	ASN1_INTEGER_free(p7i->issuer_and_serial->serial);
	if (!(p7i->issuer_and_serial->serial =
	    ASN1_INTEGER_dup(X509_get_serialNumber(x509))))
		goto err;

	/* lets keep the pkey around for a while */
	CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);
	p7i->pkey = pkey;

	/*
	 * Do not use X509_ALGOR_set_evp_md() to match historical behavior.
	 * A mistranslation of the ASN.1 from 1988 to 1997 syntax lost the
	 * OPTIONAL field, cf. the NOTE above RFC 5754, 2.1.
	 * Using X509_ALGOR_set_evp_md() would change encoding of the SHAs.
	 */
	nid = EVP_MD_type(dgst);
	if (!X509_ALGOR_set0_by_nid(p7i->digest_alg, nid, V_ASN1_NULL, NULL))
		return 0;

	if (pkey->ameth && pkey->ameth->pkey_ctrl) {
		ret = pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_PKCS7_SIGN,
		    0, p7i);
		if (ret > 0)
			return 1;
		if (ret != -2) {
			PKCS7error(PKCS7_R_SIGNING_CTRL_FAILURE);
			return 0;
		}
	}
	PKCS7error(PKCS7_R_SIGNING_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
err:
	return 0;
}
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_set);

PKCS7_SIGNER_INFO *
PKCS7_add_signature(PKCS7 *p7, X509 *x509, EVP_PKEY *pkey, const EVP_MD *dgst)
{
	PKCS7_SIGNER_INFO *si = NULL;

	if (dgst == NULL) {
		int def_nid;
		if (EVP_PKEY_get_default_digest_nid(pkey, &def_nid) <= 0)
			goto err;
		dgst = EVP_get_digestbynid(def_nid);
		if (dgst == NULL) {
			PKCS7error(PKCS7_R_NO_DEFAULT_DIGEST);
			goto err;
		}
	}

	if ((si = PKCS7_SIGNER_INFO_new()) == NULL)
		goto err;
	if (!PKCS7_SIGNER_INFO_set(si, x509, pkey, dgst))
		goto err;
	if (!PKCS7_add_signer(p7, si))
		goto err;
	return (si);
err:
	if (si)
		PKCS7_SIGNER_INFO_free(si);
	return (NULL);
}
LCRYPTO_ALIAS(PKCS7_add_signature);

int
PKCS7_set_digest(PKCS7 *p7, const EVP_MD *md)
{
	if (PKCS7_type_is_digest(p7)) {
		if (!(p7->d.digest->md->parameter = ASN1_TYPE_new())) {
			PKCS7error(ERR_R_MALLOC_FAILURE);
			return 0;
		}
		p7->d.digest->md->parameter->type = V_ASN1_NULL;
		p7->d.digest->md->algorithm = OBJ_nid2obj(EVP_MD_nid(md));
		return 1;
	}

	PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
	return 1;
}
LCRYPTO_ALIAS(PKCS7_set_digest);

STACK_OF(PKCS7_SIGNER_INFO) *
PKCS7_get_signer_info(PKCS7 *p7)
{
	if (p7 == NULL || p7->d.ptr == NULL)
		return (NULL);
	if (PKCS7_type_is_signed(p7)) {
		return (p7->d.sign->signer_info);
	} else if (PKCS7_type_is_signedAndEnveloped(p7)) {
		return (p7->d.signed_and_enveloped->signer_info);
	} else
		return (NULL);
}
LCRYPTO_ALIAS(PKCS7_get_signer_info);

void
PKCS7_SIGNER_INFO_get0_algs(PKCS7_SIGNER_INFO *si, EVP_PKEY **pk,
    X509_ALGOR **pdig, X509_ALGOR **psig)
{
	if (pk)
		*pk = si->pkey;
	if (pdig)
		*pdig = si->digest_alg;
	if (psig)
		*psig = si->digest_enc_alg;
}
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_get0_algs);

void
PKCS7_RECIP_INFO_get0_alg(PKCS7_RECIP_INFO *ri, X509_ALGOR **penc)
{
	if (penc)
		*penc = ri->key_enc_algor;
}
LCRYPTO_ALIAS(PKCS7_RECIP_INFO_get0_alg);

PKCS7_RECIP_INFO *
PKCS7_add_recipient(PKCS7 *p7, X509 *x509)
{
	PKCS7_RECIP_INFO *ri;

	if ((ri = PKCS7_RECIP_INFO_new()) == NULL)
		goto err;
	if (!PKCS7_RECIP_INFO_set(ri, x509))
		goto err;
	if (!PKCS7_add_recipient_info(p7, ri))
		goto err;
	return ri;
err:
	if (ri)
		PKCS7_RECIP_INFO_free(ri);
	return NULL;
}
LCRYPTO_ALIAS(PKCS7_add_recipient);

int
PKCS7_add_recipient_info(PKCS7 *p7, PKCS7_RECIP_INFO *ri)
{
	int i;
	STACK_OF(PKCS7_RECIP_INFO) *sk;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signedAndEnveloped:
		sk = p7->d.signed_and_enveloped->recipientinfo;
		break;
	case NID_pkcs7_enveloped:
		sk = p7->d.enveloped->recipientinfo;
		break;
	default:
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return (0);
	}

	if (!sk_PKCS7_RECIP_INFO_push(sk, ri))
		return 0;
	return (1);
}
LCRYPTO_ALIAS(PKCS7_add_recipient_info);

int
PKCS7_RECIP_INFO_set(PKCS7_RECIP_INFO *p7i, X509 *x509)
{
	int ret;
	EVP_PKEY *pkey = NULL;
	if (!ASN1_INTEGER_set(p7i->version, 0))
		return 0;
	if (!X509_NAME_set(&p7i->issuer_and_serial->issuer,
	    X509_get_issuer_name(x509)))
		return 0;

	ASN1_INTEGER_free(p7i->issuer_and_serial->serial);
	if (!(p7i->issuer_and_serial->serial =
	    ASN1_INTEGER_dup(X509_get_serialNumber(x509))))
		return 0;

	pkey = X509_get_pubkey(x509);

	if (!pkey || !pkey->ameth || !pkey->ameth->pkey_ctrl) {
		PKCS7error(PKCS7_R_ENCRYPTION_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		goto err;
	}

	ret = pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_PKCS7_ENCRYPT,
	    0, p7i);
	if (ret == -2) {
		PKCS7error(PKCS7_R_ENCRYPTION_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		goto err;
	}
	if (ret <= 0) {
		PKCS7error(PKCS7_R_ENCRYPTION_CTRL_FAILURE);
		goto err;
	}

	EVP_PKEY_free(pkey);

	CRYPTO_add(&x509->references, 1, CRYPTO_LOCK_X509);
	p7i->cert = x509;

	return 1;

err:
	EVP_PKEY_free(pkey);
	return 0;
}
LCRYPTO_ALIAS(PKCS7_RECIP_INFO_set);

X509 *
PKCS7_cert_from_signer_info(PKCS7 *p7, PKCS7_SIGNER_INFO *si)
{
	if (PKCS7_type_is_signed(p7))
		return(X509_find_by_issuer_and_serial(p7->d.sign->cert,
		    si->issuer_and_serial->issuer,
		    si->issuer_and_serial->serial));
	else
		return (NULL);
}
LCRYPTO_ALIAS(PKCS7_cert_from_signer_info);

int
PKCS7_set_cipher(PKCS7 *p7, const EVP_CIPHER *cipher)
{
	int i;
	PKCS7_ENC_CONTENT *ec;

	i = OBJ_obj2nid(p7->type);
	switch (i) {
	case NID_pkcs7_signedAndEnveloped:
		ec = p7->d.signed_and_enveloped->enc_data;
		break;
	case NID_pkcs7_enveloped:
		ec = p7->d.enveloped->enc_data;
		break;
	default:
		PKCS7error(PKCS7_R_WRONG_CONTENT_TYPE);
		return (0);
	}

	/* Check cipher OID exists and has data in it*/
	i = EVP_CIPHER_type(cipher);
	if (i == NID_undef) {
		PKCS7error(PKCS7_R_CIPHER_HAS_NO_OBJECT_IDENTIFIER);
		return (0);
	}

	ec->cipher = cipher;
	return 1;
}
LCRYPTO_ALIAS(PKCS7_set_cipher);

int
PKCS7_stream(unsigned char ***boundary, PKCS7 *p7)
{
	ASN1_OCTET_STRING *os = NULL;

	switch (OBJ_obj2nid(p7->type)) {
	case NID_pkcs7_data:
		os = p7->d.data;
		break;

	case NID_pkcs7_signedAndEnveloped:
		os = p7->d.signed_and_enveloped->enc_data->enc_data;
		if (os == NULL) {
			os = ASN1_OCTET_STRING_new();
			p7->d.signed_and_enveloped->enc_data->enc_data = os;
		}
		break;

	case NID_pkcs7_enveloped:
		os = p7->d.enveloped->enc_data->enc_data;
		if (os == NULL) {
			os = ASN1_OCTET_STRING_new();
			p7->d.enveloped->enc_data->enc_data = os;
		}
		break;

	case NID_pkcs7_signed:
		os = p7->d.sign->contents->d.data;
		break;

	default:
		os = NULL;
		break;
	}

	if (os == NULL)
		return 0;

	os->flags |= ASN1_STRING_FLAG_NDEF;
	*boundary = &os->data;

	return 1;
}
LCRYPTO_ALIAS(PKCS7_stream);
