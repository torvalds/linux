/* $OpenBSD: cms_err.c,v 1.15 2024/06/24 06:43:22 tb Exp $ */
/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/cms.h>
#include <openssl/err.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_CMS,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_CMS,0,reason)

static const ERR_STRING_DATA CMS_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA CMS_str_reasons[] = {
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_ADD_SIGNER_ERROR), "add signer error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CERTIFICATE_ALREADY_PRESENT),
	"certificate already present"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CERTIFICATE_HAS_NO_KEYID),
	"certificate has no keyid"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CERTIFICATE_VERIFY_ERROR),
	"certificate verify error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CIPHER_INITIALISATION_ERROR),
	"cipher initialisation error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CIPHER_PARAMETER_INITIALISATION_ERROR),
	"cipher parameter initialisation error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CMS_DATAFINAL_ERROR),
	"cms datafinal error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CMS_LIB), "cms lib"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENTIDENTIFIER_MISMATCH),
	"contentidentifier mismatch"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_NOT_FOUND), "content not found"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_TYPE_MISMATCH),
	"content type mismatch"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_TYPE_NOT_COMPRESSED_DATA),
	"content type not compressed data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_TYPE_NOT_ENVELOPED_DATA),
	"content type not enveloped data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_TYPE_NOT_SIGNED_DATA),
	"content type not signed data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CONTENT_VERIFY_ERROR),
	"content verify error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CTRL_ERROR), "ctrl error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_CTRL_FAILURE), "ctrl failure"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_DECRYPT_ERROR), "decrypt error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_ERROR_GETTING_PUBLIC_KEY),
	"error getting public key"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_ERROR_READING_MESSAGEDIGEST_ATTRIBUTE),
	"error reading messagedigest attribute"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_ERROR_SETTING_KEY), "error setting key"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_ERROR_SETTING_RECIPIENTINFO),
	"error setting recipientinfo"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_INVALID_ENCRYPTED_KEY_LENGTH),
	"invalid encrypted key length"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_INVALID_KEY_ENCRYPTION_PARAMETER),
	"invalid key encryption parameter"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_INVALID_KEY_LENGTH), "invalid key length"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MD_BIO_INIT_ERROR), "md bio init error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MESSAGEDIGEST_ATTRIBUTE_WRONG_LENGTH),
	"messagedigest attribute wrong length"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MESSAGEDIGEST_WRONG_LENGTH),
	"messagedigest wrong length"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MSGSIGDIGEST_ERROR), "msgsigdigest error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MSGSIGDIGEST_VERIFICATION_FAILURE),
	"msgsigdigest verification failure"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_MSGSIGDIGEST_WRONG_LENGTH),
	"msgsigdigest wrong length"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NEED_ONE_SIGNER), "need one signer"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_A_SIGNED_RECEIPT),
	"not a signed receipt"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_ENCRYPTED_DATA), "not encrypted data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_KEK), "not kek"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_KEY_AGREEMENT), "not key agreement"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_KEY_TRANSPORT), "not key transport"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_PWRI), "not pwri"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NOT_SUPPORTED_FOR_THIS_KEY_TYPE),
	"not supported for this key type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_CIPHER), "no cipher"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_CONTENT), "no content"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_CONTENT_TYPE), "no content type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_DEFAULT_DIGEST), "no default digest"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_DIGEST_SET), "no digest set"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_KEY), "no key"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_KEY_OR_CERT), "no key or cert"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_MATCHING_DIGEST), "no matching digest"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_MATCHING_RECIPIENT),
	"no matching recipient"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_MATCHING_SIGNATURE),
	"no matching signature"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_MSGSIGDIGEST), "no msgsigdigest"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_PASSWORD), "no password"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_PRIVATE_KEY), "no private key"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_PUBLIC_KEY), "no public key"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_RECEIPT_REQUEST), "no receipt request"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_NO_SIGNERS), "no signers"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE),
	"private key does not match certificate"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_RECEIPT_DECODE_ERROR),
	"receipt decode error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_RECIPIENT_ERROR), "recipient error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_SIGNER_CERTIFICATE_NOT_FOUND),
	"signer certificate not found"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_SIGNFINAL_ERROR), "signfinal error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_SMIME_TEXT_ERROR), "smime text error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_STORE_INIT_ERROR), "store init error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_TYPE_NOT_COMPRESSED_DATA),
	"type not compressed data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_TYPE_NOT_DATA), "type not data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_TYPE_NOT_DIGESTED_DATA),
	"type not digested data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_TYPE_NOT_ENCRYPTED_DATA),
	"type not encrypted data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_TYPE_NOT_ENVELOPED_DATA),
	"type not enveloped data"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNABLE_TO_FINALIZE_CONTEXT),
	"unable to finalize context"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNKNOWN_CIPHER), "unknown cipher"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNKNOWN_DIGEST_ALGORITHM),
	"unknown digest algorithm"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNKNOWN_ID), "unknown id"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_COMPRESSION_ALGORITHM),
	"unsupported compression algorithm"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_CONTENT_TYPE),
	"unsupported content type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_KEK_ALGORITHM),
	"unsupported kek algorithm"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_KEY_ENCRYPTION_ALGORITHM),
	"unsupported key encryption algorithm"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_RECIPIENTINFO_TYPE),
	"unsupported recipientinfo type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_RECIPIENT_TYPE),
	"unsupported recipient type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNSUPPORTED_TYPE), "unsupported type"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNWRAP_ERROR), "unwrap error"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_UNWRAP_FAILURE), "unwrap failure"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_VERIFICATION_FAILURE),
	"verification failure"},
	{ERR_PACK(ERR_LIB_CMS, 0, CMS_R_WRAP_ERROR), "wrap error"},
	{0, NULL}
};

#endif

int
ERR_load_CMS_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(CMS_str_functs[0].error) == NULL) {
		ERR_load_const_strings(CMS_str_functs);
		ERR_load_const_strings(CMS_str_reasons);
	}
#endif
	return 1;
}
LCRYPTO_ALIAS(ERR_load_CMS_strings);
