/* $OpenBSD: pkcs7err.c,v 1.16 2024/06/24 06:43:22 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999-2007 The OpenSSL Project.  All rights reserved.
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
 *    openssl-core@OpenSSL.org.
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/err.h>
#include <openssl/pkcs7.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_PKCS7,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_PKCS7,0,reason)

static const ERR_STRING_DATA PKCS7_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA PKCS7_str_reasons[] = {
	{ERR_REASON(PKCS7_R_CERTIFICATE_VERIFY_ERROR), "certificate verify error"},
	{ERR_REASON(PKCS7_R_CIPHER_HAS_NO_OBJECT_IDENTIFIER), "cipher has no object identifier"},
	{ERR_REASON(PKCS7_R_CIPHER_NOT_INITIALIZED), "cipher not initialized"},
	{ERR_REASON(PKCS7_R_CONTENT_AND_DATA_PRESENT), "content and data present"},
	{ERR_REASON(PKCS7_R_CTRL_ERROR)          , "ctrl error"},
	{ERR_REASON(PKCS7_R_DECODE_ERROR)        , "decode error"},
	{ERR_REASON(PKCS7_R_DECRYPTED_KEY_IS_WRONG_LENGTH), "decrypted key is wrong length"},
	{ERR_REASON(PKCS7_R_DECRYPT_ERROR)       , "decrypt error"},
	{ERR_REASON(PKCS7_R_DIGEST_FAILURE)      , "digest failure"},
	{ERR_REASON(PKCS7_R_ENCRYPTION_CTRL_FAILURE), "encryption ctrl failure"},
	{ERR_REASON(PKCS7_R_ENCRYPTION_NOT_SUPPORTED_FOR_THIS_KEY_TYPE), "encryption not supported for this key type"},
	{ERR_REASON(PKCS7_R_ERROR_ADDING_RECIPIENT), "error adding recipient"},
	{ERR_REASON(PKCS7_R_ERROR_SETTING_CIPHER), "error setting cipher"},
	{ERR_REASON(PKCS7_R_INVALID_MIME_TYPE)   , "invalid mime type"},
	{ERR_REASON(PKCS7_R_INVALID_NULL_POINTER), "invalid null pointer"},
	{ERR_REASON(PKCS7_R_MIME_NO_CONTENT_TYPE), "mime no content type"},
	{ERR_REASON(PKCS7_R_MIME_PARSE_ERROR)    , "mime parse error"},
	{ERR_REASON(PKCS7_R_MIME_SIG_PARSE_ERROR), "mime sig parse error"},
	{ERR_REASON(PKCS7_R_MISSING_CERIPEND_INFO), "missing ceripend info"},
	{ERR_REASON(PKCS7_R_NO_CONTENT)          , "no content"},
	{ERR_REASON(PKCS7_R_NO_CONTENT_TYPE)     , "no content type"},
	{ERR_REASON(PKCS7_R_NO_DEFAULT_DIGEST)   , "no default digest"},
	{ERR_REASON(PKCS7_R_NO_MATCHING_DIGEST_TYPE_FOUND), "no matching digest type found"},
	{ERR_REASON(PKCS7_R_NO_MULTIPART_BODY_FAILURE), "no multipart body failure"},
	{ERR_REASON(PKCS7_R_NO_MULTIPART_BOUNDARY), "no multipart boundary"},
	{ERR_REASON(PKCS7_R_NO_RECIPIENT_MATCHES_CERTIFICATE), "no recipient matches certificate"},
	{ERR_REASON(PKCS7_R_NO_RECIPIENT_MATCHES_KEY), "no recipient matches key"},
	{ERR_REASON(PKCS7_R_NO_SIGNATURES_ON_DATA), "no signatures on data"},
	{ERR_REASON(PKCS7_R_NO_SIGNERS)          , "no signers"},
	{ERR_REASON(PKCS7_R_NO_SIG_CONTENT_TYPE) , "no sig content type"},
	{ERR_REASON(PKCS7_R_OPERATION_NOT_SUPPORTED_ON_THIS_TYPE), "operation not supported on this type"},
	{ERR_REASON(PKCS7_R_PKCS7_ADD_SIGNATURE_ERROR), "pkcs7 add signature error"},
	{ERR_REASON(PKCS7_R_PKCS7_ADD_SIGNER_ERROR), "pkcs7 add signer error"},
	{ERR_REASON(PKCS7_R_PKCS7_DATAFINAL)     , "pkcs7 datafinal"},
	{ERR_REASON(PKCS7_R_PKCS7_DATAFINAL_ERROR), "pkcs7 datafinal error"},
	{ERR_REASON(PKCS7_R_PKCS7_DATASIGN)      , "pkcs7 datasign"},
	{ERR_REASON(PKCS7_R_PKCS7_PARSE_ERROR)   , "pkcs7 parse error"},
	{ERR_REASON(PKCS7_R_PKCS7_SIG_PARSE_ERROR), "pkcs7 sig parse error"},
	{ERR_REASON(PKCS7_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE), "private key does not match certificate"},
	{ERR_REASON(PKCS7_R_SIGNATURE_FAILURE)   , "signature failure"},
	{ERR_REASON(PKCS7_R_SIGNER_CERTIFICATE_NOT_FOUND), "signer certificate not found"},
	{ERR_REASON(PKCS7_R_SIGNING_CTRL_FAILURE), "signing ctrl failure"},
	{ERR_REASON(PKCS7_R_SIGNING_NOT_SUPPORTED_FOR_THIS_KEY_TYPE), "signing not supported for this key type"},
	{ERR_REASON(PKCS7_R_SIG_INVALID_MIME_TYPE), "sig invalid mime type"},
	{ERR_REASON(PKCS7_R_SMIME_TEXT_ERROR)    , "smime text error"},
	{ERR_REASON(PKCS7_R_UNABLE_TO_FIND_CERTIFICATE), "unable to find certificate"},
	{ERR_REASON(PKCS7_R_UNABLE_TO_FIND_MEM_BIO), "unable to find mem bio"},
	{ERR_REASON(PKCS7_R_UNABLE_TO_FIND_MESSAGE_DIGEST), "unable to find message digest"},
	{ERR_REASON(PKCS7_R_UNKNOWN_DIGEST_TYPE) , "unknown digest type"},
	{ERR_REASON(PKCS7_R_UNKNOWN_OPERATION)   , "unknown operation"},
	{ERR_REASON(PKCS7_R_UNSUPPORTED_CIPHER_TYPE), "unsupported cipher type"},
	{ERR_REASON(PKCS7_R_UNSUPPORTED_CONTENT_TYPE), "unsupported content type"},
	{ERR_REASON(PKCS7_R_WRONG_CONTENT_TYPE)  , "wrong content type"},
	{ERR_REASON(PKCS7_R_WRONG_PKCS7_TYPE)    , "wrong pkcs7 type"},
	{0, NULL}
};

#endif

void
ERR_load_PKCS7_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(PKCS7_str_functs[0].error) == NULL) {
		ERR_load_const_strings(PKCS7_str_functs);
		ERR_load_const_strings(PKCS7_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_PKCS7_strings);
