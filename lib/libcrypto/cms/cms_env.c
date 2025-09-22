/* $OpenBSD: cms_env.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/aes.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "cms_local.h"
#include "err_local.h"
#include "evp_local.h"

/* CMS EnvelopedData Utilities */

CMS_EnvelopedData *
cms_get0_enveloped(CMS_ContentInfo *cms)
{
	if (OBJ_obj2nid(cms->contentType) != NID_pkcs7_enveloped) {
		CMSerror(CMS_R_CONTENT_TYPE_NOT_ENVELOPED_DATA);
		return NULL;
	}
	return cms->d.envelopedData;
}

static CMS_EnvelopedData *
cms_enveloped_data_init(CMS_ContentInfo *cms)
{
	if (cms->d.other == NULL) {
		cms->d.envelopedData = (CMS_EnvelopedData *)ASN1_item_new(&CMS_EnvelopedData_it);
		if (!cms->d.envelopedData) {
			CMSerror(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
		cms->d.envelopedData->version = 0;
		cms->d.envelopedData->encryptedContentInfo->contentType =
			OBJ_nid2obj(NID_pkcs7_data);
		ASN1_OBJECT_free(cms->contentType);
		cms->contentType = OBJ_nid2obj(NID_pkcs7_enveloped);
		return cms->d.envelopedData;
	}
	return cms_get0_enveloped(cms);
}

int
cms_env_asn1_ctrl(CMS_RecipientInfo *ri, int cmd)
{
	EVP_PKEY *pkey;
	int i;

	if (ri->type == CMS_RECIPINFO_TRANS)
		pkey = ri->d.ktri->pkey;
	else if (ri->type == CMS_RECIPINFO_AGREE) {
		EVP_PKEY_CTX *pctx = ri->d.kari->pctx;
		if (!pctx)
			return 0;
		pkey = EVP_PKEY_CTX_get0_pkey(pctx);
		if (!pkey)
			return 0;
	} else
		return 0;
	if (!pkey->ameth || !pkey->ameth->pkey_ctrl)
		return 1;
	i = pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_CMS_ENVELOPE, cmd, ri);
	if (i == -2) {
		CMSerror(CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		return 0;
	}
	if (i <= 0) {
		CMSerror(CMS_R_CTRL_FAILURE);
		return 0;
	}

	return 1;
}

STACK_OF(CMS_RecipientInfo) *
CMS_get0_RecipientInfos(CMS_ContentInfo *cms)
{
	CMS_EnvelopedData *env;

	env = cms_get0_enveloped(cms);
	if (!env)
		return NULL;

	return env->recipientInfos;
}
LCRYPTO_ALIAS(CMS_get0_RecipientInfos);

int
CMS_RecipientInfo_type(CMS_RecipientInfo *ri)
{
	return ri->type;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_type);

EVP_PKEY_CTX *
CMS_RecipientInfo_get0_pkey_ctx(CMS_RecipientInfo *ri)
{
	if (ri->type == CMS_RECIPINFO_TRANS)
		return ri->d.ktri->pctx;
	else if (ri->type == CMS_RECIPINFO_AGREE)
		return ri->d.kari->pctx;

	return NULL;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_get0_pkey_ctx);

CMS_ContentInfo *
CMS_EnvelopedData_create(const EVP_CIPHER *cipher)
{
	CMS_ContentInfo *cms;
	CMS_EnvelopedData *env;

	cms = CMS_ContentInfo_new();
	if (cms == NULL)
		goto merr;
	env = cms_enveloped_data_init(cms);
	if (env == NULL)
		goto merr;
	if (!cms_EncryptedContent_init(env->encryptedContentInfo, cipher,
	    NULL, 0))
		goto merr;

	return cms;

 merr:
	CMS_ContentInfo_free(cms);
	CMSerror(ERR_R_MALLOC_FAILURE);
	return NULL;
}
LCRYPTO_ALIAS(CMS_EnvelopedData_create);

/* Key Transport Recipient Info (KTRI) routines */

/* Initialise a ktri based on passed certificate and key */

static int
cms_RecipientInfo_ktri_init(CMS_RecipientInfo *ri, X509 *recip, EVP_PKEY *pk,
    unsigned int flags)
{
	CMS_KeyTransRecipientInfo *ktri;
	int idtype;

	ri->d.ktri = (CMS_KeyTransRecipientInfo *)ASN1_item_new(&CMS_KeyTransRecipientInfo_it);
	if (!ri->d.ktri)
		return 0;
	ri->type = CMS_RECIPINFO_TRANS;

	ktri = ri->d.ktri;

	if (flags & CMS_USE_KEYID) {
		ktri->version = 2;
		idtype = CMS_RECIPINFO_KEYIDENTIFIER;
	} else {
		ktri->version = 0;
		idtype = CMS_RECIPINFO_ISSUER_SERIAL;
	}

	/*
	 * Not a typo: RecipientIdentifier and SignerIdentifier are the same
	 * structure.
	 */

	if (!cms_set1_SignerIdentifier(ktri->rid, recip, idtype))
		return 0;

	X509_up_ref(recip);
	EVP_PKEY_up_ref(pk);

	ktri->pkey = pk;
	ktri->recip = recip;

	if (flags & CMS_KEY_PARAM) {
		ktri->pctx = EVP_PKEY_CTX_new(ktri->pkey, NULL);
		if (ktri->pctx == NULL)
			return 0;
		if (EVP_PKEY_encrypt_init(ktri->pctx) <= 0)
			return 0;
	} else if (!cms_env_asn1_ctrl(ri, 0))
		return 0;

	return 1;
}

/*
 * Add a recipient certificate using appropriate type of RecipientInfo
 */

CMS_RecipientInfo *
CMS_add1_recipient_cert(CMS_ContentInfo *cms, X509 *recip, unsigned int flags)
{
	CMS_RecipientInfo *ri = NULL;
	CMS_EnvelopedData *env;
	EVP_PKEY *pk = NULL;

	env = cms_get0_enveloped(cms);
	if (!env)
		goto err;

	/* Initialize recipient info */
	ri = (CMS_RecipientInfo *)ASN1_item_new(&CMS_RecipientInfo_it);
	if (!ri)
		goto merr;

	pk = X509_get0_pubkey(recip);
	if (!pk) {
		CMSerror(CMS_R_ERROR_GETTING_PUBLIC_KEY);
		goto err;
	}

	switch (cms_pkey_get_ri_type(pk)) {

	case CMS_RECIPINFO_TRANS:
		if (!cms_RecipientInfo_ktri_init(ri, recip, pk, flags))
			goto err;
		break;

	case CMS_RECIPINFO_AGREE:
		if (!cms_RecipientInfo_kari_init(ri, recip, pk, flags))
			goto err;
		break;

	default:
		CMSerror(CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE);
		goto err;

	}

	if (!sk_CMS_RecipientInfo_push(env->recipientInfos, ri))
		goto merr;

	return ri;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);
 err:
	ASN1_item_free((ASN1_VALUE *)ri, &CMS_RecipientInfo_it);
	return NULL;
}
LCRYPTO_ALIAS(CMS_add1_recipient_cert);

int
CMS_RecipientInfo_ktri_get0_algs(CMS_RecipientInfo *ri, EVP_PKEY **pk,
    X509 **recip, X509_ALGOR **palg)
{
	CMS_KeyTransRecipientInfo *ktri;

	if (ri->type != CMS_RECIPINFO_TRANS) {
		CMSerror(CMS_R_NOT_KEY_TRANSPORT);
		return 0;
	}

	ktri = ri->d.ktri;

	if (pk)
		*pk = ktri->pkey;
	if (recip)
		*recip = ktri->recip;
	if (palg)
		*palg = ktri->keyEncryptionAlgorithm;

	return 1;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_ktri_get0_algs);

int
CMS_RecipientInfo_ktri_get0_signer_id(CMS_RecipientInfo *ri,
    ASN1_OCTET_STRING **keyid, X509_NAME **issuer, ASN1_INTEGER **sno)
{
	CMS_KeyTransRecipientInfo *ktri;

	if (ri->type != CMS_RECIPINFO_TRANS) {
		CMSerror(CMS_R_NOT_KEY_TRANSPORT);
		return 0;
	}
	ktri = ri->d.ktri;

	return cms_SignerIdentifier_get0_signer_id(ktri->rid, keyid, issuer, sno);
}
LCRYPTO_ALIAS(CMS_RecipientInfo_ktri_get0_signer_id);

int
CMS_RecipientInfo_ktri_cert_cmp(CMS_RecipientInfo *ri, X509 *cert)
{
	if (ri->type != CMS_RECIPINFO_TRANS) {
		CMSerror(CMS_R_NOT_KEY_TRANSPORT);
		return -2;
	}

	return cms_SignerIdentifier_cert_cmp(ri->d.ktri->rid, cert);
}
LCRYPTO_ALIAS(CMS_RecipientInfo_ktri_cert_cmp);

int
CMS_RecipientInfo_set0_pkey(CMS_RecipientInfo *ri, EVP_PKEY *pkey)
{
	if (ri->type != CMS_RECIPINFO_TRANS) {
		CMSerror(CMS_R_NOT_KEY_TRANSPORT);
		return 0;
	}
	EVP_PKEY_free(ri->d.ktri->pkey);
	ri->d.ktri->pkey = pkey;

	return 1;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_set0_pkey);

/* Encrypt content key in key transport recipient info */

static int
cms_RecipientInfo_ktri_encrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	CMS_KeyTransRecipientInfo *ktri;
	CMS_EncryptedContentInfo *ec;
	EVP_PKEY_CTX *pctx;
	unsigned char *ek = NULL;
	size_t eklen;

	int ret = 0;

	if (ri->type != CMS_RECIPINFO_TRANS) {
		CMSerror(CMS_R_NOT_KEY_TRANSPORT);
		return 0;
	}
	ktri = ri->d.ktri;
	ec = cms->d.envelopedData->encryptedContentInfo;

	pctx = ktri->pctx;

	if (pctx) {
		if (!cms_env_asn1_ctrl(ri, 0))
			goto err;
	} else {
		pctx = EVP_PKEY_CTX_new(ktri->pkey, NULL);
		if (pctx == NULL)
			return 0;

		if (EVP_PKEY_encrypt_init(pctx) <= 0)
			goto err;
	}

	if (EVP_PKEY_CTX_ctrl(pctx, -1, EVP_PKEY_OP_ENCRYPT,
			              EVP_PKEY_CTRL_CMS_ENCRYPT, 0, ri) <= 0) {
		CMSerror(CMS_R_CTRL_ERROR);
		goto err;
	}

	if (EVP_PKEY_encrypt(pctx, NULL, &eklen, ec->key, ec->keylen) <= 0)
		goto err;

	ek = malloc(eklen);

	if (ek == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (EVP_PKEY_encrypt(pctx, ek, &eklen, ec->key, ec->keylen) <= 0)
		goto err;

	ASN1_STRING_set0(ktri->encryptedKey, ek, eklen);
	ek = NULL;

	ret = 1;

 err:
	EVP_PKEY_CTX_free(pctx);
	ktri->pctx = NULL;
	free(ek);

	return ret;
}

/* Decrypt content key from KTRI */

static int
cms_RecipientInfo_ktri_decrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	CMS_KeyTransRecipientInfo *ktri = ri->d.ktri;
	EVP_PKEY *pkey = ktri->pkey;
	unsigned char *ek = NULL;
	size_t eklen;
	size_t fixlen = 0;
	int ret = 0;
	CMS_EncryptedContentInfo *ec;

	ec = cms->d.envelopedData->encryptedContentInfo;

	if (ktri->pkey == NULL) {
		CMSerror(CMS_R_NO_PRIVATE_KEY);
		return 0;
	}

	if (cms->d.envelopedData->encryptedContentInfo->havenocert &&
	    !cms->d.envelopedData->encryptedContentInfo->debug) {
		X509_ALGOR *calg = ec->contentEncryptionAlgorithm;
		const EVP_CIPHER *ciph;

		if ((ciph = EVP_get_cipherbyobj(calg->algorithm)) == NULL) {
			CMSerror(CMS_R_UNKNOWN_CIPHER);
			return 0;
		}

		fixlen = EVP_CIPHER_key_length(ciph);
	}

	ktri->pctx = EVP_PKEY_CTX_new(pkey, NULL);
	if (ktri->pctx == NULL)
		return 0;

	if (EVP_PKEY_decrypt_init(ktri->pctx) <= 0)
		goto err;

	if (!cms_env_asn1_ctrl(ri, 1))
		goto err;

	if (EVP_PKEY_CTX_ctrl(ktri->pctx, -1, EVP_PKEY_OP_DECRYPT,
	    EVP_PKEY_CTRL_CMS_DECRYPT, 0, ri) <= 0) {
		CMSerror(CMS_R_CTRL_ERROR);
		goto err;
	}

	if (EVP_PKEY_decrypt(ktri->pctx, NULL, &eklen, ktri->encryptedKey->data,
	    ktri->encryptedKey->length) <= 0 || eklen == 0 ||
	    (fixlen != 0 && eklen != fixlen)) {
		CMSerror(CMS_R_CMS_LIB);
		goto err;
	}

	ek = malloc(eklen);

	if (ek == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (EVP_PKEY_decrypt(ktri->pctx, ek, &eklen, ktri->encryptedKey->data,
	    ktri->encryptedKey->length) <= 0) {
		CMSerror(CMS_R_CMS_LIB);
		goto err;
	}

	ret = 1;

	freezero(ec->key, ec->keylen);
	ec->key = ek;
	ec->keylen = eklen;

 err:
	EVP_PKEY_CTX_free(ktri->pctx);
	ktri->pctx = NULL;
	if (!ret)
		free(ek);

	return ret;
}

/* Key Encrypted Key (KEK) RecipientInfo routines */

int
CMS_RecipientInfo_kekri_id_cmp(CMS_RecipientInfo *ri, const unsigned char *id,
    size_t idlen)
{
	ASN1_OCTET_STRING tmp_os;
	CMS_KEKRecipientInfo *kekri;

	if (ri->type != CMS_RECIPINFO_KEK) {
		CMSerror(CMS_R_NOT_KEK);
		return -2;
	}
	kekri = ri->d.kekri;
	tmp_os.type = V_ASN1_OCTET_STRING;
	tmp_os.flags = 0;
	tmp_os.data = (unsigned char *)id;
	tmp_os.length = (int)idlen;

	return ASN1_OCTET_STRING_cmp(&tmp_os, kekri->kekid->keyIdentifier);
}
LCRYPTO_ALIAS(CMS_RecipientInfo_kekri_id_cmp);

/* For now hard code AES key wrap info */

static size_t
aes_wrap_keylen(int nid)
{
	switch (nid) {
	case NID_id_aes128_wrap:
		return 16;

	case NID_id_aes192_wrap:
		return 24;

	case NID_id_aes256_wrap:
		return 32;

	default:
		return 0;
	}
}

CMS_RecipientInfo *
CMS_add0_recipient_key(CMS_ContentInfo *cms, int nid, unsigned char *key,
    size_t keylen, unsigned char *id, size_t idlen, ASN1_GENERALIZEDTIME *date,
    ASN1_OBJECT *otherTypeId, ASN1_TYPE *otherType)
{
	CMS_RecipientInfo *ri = NULL;
	CMS_EnvelopedData *env;
	CMS_KEKRecipientInfo *kekri;

	env = cms_get0_enveloped(cms);
	if (!env)
		goto err;

	if (nid == NID_undef) {
		switch (keylen) {
		case 16:
			nid = NID_id_aes128_wrap;
			break;

		case 24:
			nid = NID_id_aes192_wrap;
			break;

		case 32:
			nid = NID_id_aes256_wrap;
			break;

		default:
			CMSerror(CMS_R_INVALID_KEY_LENGTH);
			goto err;
		}

	} else {

		size_t exp_keylen = aes_wrap_keylen(nid);

		if (!exp_keylen) {
			CMSerror(CMS_R_UNSUPPORTED_KEK_ALGORITHM);
			goto err;
		}

		if (keylen != exp_keylen) {
			CMSerror(CMS_R_INVALID_KEY_LENGTH);
			goto err;
		}

	}

	/* Initialize recipient info */
	ri = (CMS_RecipientInfo *)ASN1_item_new(&CMS_RecipientInfo_it);
	if (!ri)
		goto merr;

	ri->d.kekri = (CMS_KEKRecipientInfo *)ASN1_item_new(&CMS_KEKRecipientInfo_it);
	if (!ri->d.kekri)
		goto merr;
	ri->type = CMS_RECIPINFO_KEK;

	kekri = ri->d.kekri;

	if (otherTypeId) {
		kekri->kekid->other = (CMS_OtherKeyAttribute *)ASN1_item_new(&CMS_OtherKeyAttribute_it);
		if (kekri->kekid->other == NULL)
			goto merr;
	}

	if (!sk_CMS_RecipientInfo_push(env->recipientInfos, ri))
		goto merr;

	/* After this point no calls can fail */

	kekri->version = 4;

	kekri->key = key;
	kekri->keylen = keylen;

	ASN1_STRING_set0(kekri->kekid->keyIdentifier, id, idlen);

	kekri->kekid->date = date;

	if (kekri->kekid->other) {
		kekri->kekid->other->keyAttrId = otherTypeId;
		kekri->kekid->other->keyAttr = otherType;
	}

	X509_ALGOR_set0(kekri->keyEncryptionAlgorithm,
			        OBJ_nid2obj(nid), V_ASN1_UNDEF, NULL);

	return ri;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);
 err:
	ASN1_item_free((ASN1_VALUE *)ri, &CMS_RecipientInfo_it);
	return NULL;
}
LCRYPTO_ALIAS(CMS_add0_recipient_key);

int
CMS_RecipientInfo_kekri_get0_id(CMS_RecipientInfo *ri, X509_ALGOR **palg,
    ASN1_OCTET_STRING **pid, ASN1_GENERALIZEDTIME **pdate,
    ASN1_OBJECT **potherid, ASN1_TYPE **pothertype)
{
	CMS_KEKIdentifier *rkid;

	if (ri->type != CMS_RECIPINFO_KEK) {
		CMSerror(CMS_R_NOT_KEK);
		return 0;
	}
	rkid = ri->d.kekri->kekid;
	if (palg)
		*palg = ri->d.kekri->keyEncryptionAlgorithm;
	if (pid)
		*pid = rkid->keyIdentifier;
	if (pdate)
		*pdate = rkid->date;
	if (potherid) {
		if (rkid->other)
			*potherid = rkid->other->keyAttrId;
		else
			*potherid = NULL;
	}
	if (pothertype) {
		if (rkid->other)
			*pothertype = rkid->other->keyAttr;
		else
			*pothertype = NULL;
	}

	return 1;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_kekri_get0_id);

int
CMS_RecipientInfo_set0_key(CMS_RecipientInfo *ri, unsigned char *key,
    size_t keylen)
{
	CMS_KEKRecipientInfo *kekri;

	if (ri->type != CMS_RECIPINFO_KEK) {
		CMSerror(CMS_R_NOT_KEK);
		return 0;
	}

	kekri = ri->d.kekri;
	kekri->key = key;
	kekri->keylen = keylen;
	return 1;
}
LCRYPTO_ALIAS(CMS_RecipientInfo_set0_key);

/* Encrypt content key in KEK recipient info */

static int
cms_RecipientInfo_kekri_encrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	CMS_EncryptedContentInfo *ec;
	CMS_KEKRecipientInfo *kekri;
	AES_KEY actx;
	unsigned char *wkey = NULL;
	int wkeylen;
	int r = 0;

	ec = cms->d.envelopedData->encryptedContentInfo;
	kekri = ri->d.kekri;

	if (!kekri->key) {
		CMSerror(CMS_R_NO_KEY);
		return 0;
	}

	if (AES_set_encrypt_key(kekri->key, kekri->keylen << 3, &actx)) {
		CMSerror(CMS_R_ERROR_SETTING_KEY);
		goto err;
	}

	wkey = malloc(ec->keylen + 8);
	if (wkey == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	wkeylen = AES_wrap_key(&actx, NULL, wkey, ec->key, ec->keylen);
	if (wkeylen <= 0) {
		CMSerror(CMS_R_WRAP_ERROR);
		goto err;
	}

	ASN1_STRING_set0(kekri->encryptedKey, wkey, wkeylen);

	r = 1;

 err:
	if (!r)
		free(wkey);
	explicit_bzero(&actx, sizeof(actx));

	return r;
}

/* Decrypt content key in KEK recipient info */

static int
cms_RecipientInfo_kekri_decrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	CMS_EncryptedContentInfo *ec;
	CMS_KEKRecipientInfo *kekri;
	AES_KEY actx;
	unsigned char *ukey = NULL;
	int ukeylen;
	int r = 0, wrap_nid;

	ec = cms->d.envelopedData->encryptedContentInfo;
	kekri = ri->d.kekri;

	if (!kekri->key) {
		CMSerror(CMS_R_NO_KEY);
		return 0;
	}

	wrap_nid = OBJ_obj2nid(kekri->keyEncryptionAlgorithm->algorithm);
	if (aes_wrap_keylen(wrap_nid) != kekri->keylen) {
		CMSerror(CMS_R_INVALID_KEY_LENGTH);
		return 0;
	}

	/* If encrypted key length is invalid don't bother */

	if (kekri->encryptedKey->length < 16) {
		CMSerror(CMS_R_INVALID_ENCRYPTED_KEY_LENGTH);
		goto err;
	}

	if (AES_set_decrypt_key(kekri->key, kekri->keylen << 3, &actx)) {
		CMSerror(CMS_R_ERROR_SETTING_KEY);
		goto err;
	}

	ukey = malloc(kekri->encryptedKey->length - 8);
	if (ukey == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	ukeylen = AES_unwrap_key(&actx, NULL, ukey, kekri->encryptedKey->data,
	    kekri->encryptedKey->length);

	if (ukeylen <= 0) {
		CMSerror(CMS_R_UNWRAP_ERROR);
		goto err;
	}

	freezero(ec->key, ec->keylen);
	ec->key = ukey;
	ec->keylen = ukeylen;

	r = 1;

 err:

	if (!r)
		free(ukey);
	explicit_bzero(&actx, sizeof(actx));

	return r;
}

int
CMS_RecipientInfo_decrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	switch (ri->type) {
	case CMS_RECIPINFO_TRANS:
		return cms_RecipientInfo_ktri_decrypt(cms, ri);

	case CMS_RECIPINFO_KEK:
		return cms_RecipientInfo_kekri_decrypt(cms, ri);

	case CMS_RECIPINFO_PASS:
		return cms_RecipientInfo_pwri_crypt(cms, ri, 0);

	default:
		CMSerror(CMS_R_UNSUPPORTED_RECIPIENTINFO_TYPE);
		return 0;
	}
}
LCRYPTO_ALIAS(CMS_RecipientInfo_decrypt);

int
CMS_RecipientInfo_encrypt(CMS_ContentInfo *cms, CMS_RecipientInfo *ri)
{
	switch (ri->type) {
	case CMS_RECIPINFO_TRANS:
		return cms_RecipientInfo_ktri_encrypt(cms, ri);

	case CMS_RECIPINFO_AGREE:
		return cms_RecipientInfo_kari_encrypt(cms, ri);

	case CMS_RECIPINFO_KEK:
		return cms_RecipientInfo_kekri_encrypt(cms, ri);

	case CMS_RECIPINFO_PASS:
		return cms_RecipientInfo_pwri_crypt(cms, ri, 1);

	default:
		CMSerror(CMS_R_UNSUPPORTED_RECIPIENT_TYPE);
		return 0;
	}
}
LCRYPTO_ALIAS(CMS_RecipientInfo_encrypt);

/* Check structures and fixup version numbers (if necessary) */

static void
cms_env_set_originfo_version(CMS_EnvelopedData *env)
{
	CMS_OriginatorInfo *org = env->originatorInfo;
	int i;

	if (org == NULL)
		return;
	for (i = 0; i < sk_CMS_CertificateChoices_num(org->certificates); i++) {
		CMS_CertificateChoices *cch;

		cch = sk_CMS_CertificateChoices_value(org->certificates, i);
		if (cch->type == CMS_CERTCHOICE_OTHER) {
			env->version = 4;
			return;
		} else if (cch->type == CMS_CERTCHOICE_V2ACERT) {
			if (env->version < 3)
			    env->version = 3;
		}
	}

	for (i = 0; i < sk_CMS_RevocationInfoChoice_num(org->crls); i++) {
		CMS_RevocationInfoChoice *rch;

		rch = sk_CMS_RevocationInfoChoice_value(org->crls, i);
		if (rch->type == CMS_REVCHOICE_OTHER) {
			env->version = 4;
			return;
		}
	}
}

static void
cms_env_set_version(CMS_EnvelopedData *env)
{
	int i;
	CMS_RecipientInfo *ri;

	/*
	 * Can't set version higher than 4 so if 4 or more already nothing to do.
	 */
	if (env->version >= 4)
		return;

	cms_env_set_originfo_version(env);

	if (env->version >= 3)
		return;

	for (i = 0; i < sk_CMS_RecipientInfo_num(env->recipientInfos); i++) {
		ri = sk_CMS_RecipientInfo_value(env->recipientInfos, i);
		if (ri->type == CMS_RECIPINFO_PASS || ri->type == CMS_RECIPINFO_OTHER) {
			env->version = 3;
			return;
		} else if (ri->type != CMS_RECIPINFO_TRANS
			       || ri->d.ktri->version != 0) {
			env->version = 2;
		}
	}
	if (env->originatorInfo || env->unprotectedAttrs)
		env->version = 2;
	if (env->version == 2)
		return;
	env->version = 0;
}

BIO *
cms_EnvelopedData_init_bio(CMS_ContentInfo *cms)
{
	CMS_EncryptedContentInfo *ec;
	STACK_OF(CMS_RecipientInfo) *rinfos;
	CMS_RecipientInfo *ri;
	int i, ok = 0;
	BIO *ret;

	/* Get BIO first to set up key */

	ec = cms->d.envelopedData->encryptedContentInfo;
	ret = cms_EncryptedContent_init_bio(ec);

	/* If error or no cipher end of processing */

	if (!ret || !ec->cipher)
		return ret;

	/* Now encrypt content key according to each RecipientInfo type */

	rinfos = cms->d.envelopedData->recipientInfos;

	for (i = 0; i < sk_CMS_RecipientInfo_num(rinfos); i++) {
		ri = sk_CMS_RecipientInfo_value(rinfos, i);
		if (CMS_RecipientInfo_encrypt(cms, ri) <= 0) {
			CMSerror(CMS_R_ERROR_SETTING_RECIPIENTINFO);
			goto err;
		}
	}
	cms_env_set_version(cms->d.envelopedData);

	ok = 1;

 err:
	ec->cipher = NULL;
	freezero(ec->key, ec->keylen);
	ec->key = NULL;
	ec->keylen = 0;
	if (ok)
		return ret;
	BIO_free(ret);
	return NULL;
}

/*
 * Get RecipientInfo type (if any) supported by a key (public or private). To
 * retain compatibility with previous behaviour if the ctrl value isn't
 * supported we assume key transport.
 */
int
cms_pkey_get_ri_type(EVP_PKEY *pk)
{
	if (pk->ameth && pk->ameth->pkey_ctrl) {
		int i, r;
		i = pk->ameth->pkey_ctrl(pk, ASN1_PKEY_CTRL_CMS_RI_TYPE, 0, &r);
		if (i > 0)
			return r;
	}
	return CMS_RECIPINFO_TRANS;
}
