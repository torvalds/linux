/* $OpenBSD: evp_err.c,v 1.34 2024/06/24 06:43:22 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999-2011 The OpenSSL Project.  All rights reserved.
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
#include <openssl/evp.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_EVP,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_EVP,0,reason)

static const ERR_STRING_DATA EVP_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA EVP_str_reasons[] = {
	{ERR_REASON(EVP_R_AES_IV_SETUP_FAILED)   , "aes iv setup failed"},
	{ERR_REASON(EVP_R_AES_KEY_SETUP_FAILED)  , "aes key setup failed"},
	{ERR_REASON(EVP_R_ASN1_LIB)              , "asn1 lib"},
	{ERR_REASON(EVP_R_BAD_BLOCK_LENGTH)      , "bad block length"},
	{ERR_REASON(EVP_R_BAD_DECRYPT)           , "bad decrypt"},
	{ERR_REASON(EVP_R_BAD_KEY_LENGTH)        , "bad key length"},
	{ERR_REASON(EVP_R_BN_DECODE_ERROR)       , "bn decode error"},
	{ERR_REASON(EVP_R_BN_PUBKEY_ERROR)       , "bn pubkey error"},
	{ERR_REASON(EVP_R_BUFFER_TOO_SMALL)      , "buffer too small"},
	{ERR_REASON(EVP_R_CAMELLIA_KEY_SETUP_FAILED), "camellia key setup failed"},
	{ERR_REASON(EVP_R_CIPHER_PARAMETER_ERROR), "cipher parameter error"},
	{ERR_REASON(EVP_R_COMMAND_NOT_SUPPORTED) , "command not supported"},
	{ERR_REASON(EVP_R_CTRL_NOT_IMPLEMENTED)  , "ctrl not implemented"},
	{ERR_REASON(EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED), "ctrl operation not implemented"},
	{ERR_REASON(EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH), "data not multiple of block length"},
	{ERR_REASON(EVP_R_DECODE_ERROR)          , "decode error"},
	{ERR_REASON(EVP_R_DIFFERENT_KEY_TYPES)   , "different key types"},
	{ERR_REASON(EVP_R_DIFFERENT_PARAMETERS)  , "different parameters"},
	{ERR_REASON(EVP_R_DISABLED_FOR_FIPS)     , "disabled for fips"},
	{ERR_REASON(EVP_R_ENCODE_ERROR)          , "encode error"},
	{ERR_REASON(EVP_R_ERROR_LOADING_SECTION) , "error loading section"},
	{ERR_REASON(EVP_R_ERROR_SETTING_FIPS_MODE), "error setting fips mode"},
	{ERR_REASON(EVP_R_EVP_PBE_CIPHERINIT_ERROR), "evp pbe cipherinit error"},
	{ERR_REASON(EVP_R_EXPECTING_AN_HMAC_KEY), "expecting an hmac key"},
	{ERR_REASON(EVP_R_EXPECTING_AN_RSA_KEY)  , "expecting an rsa key"},
	{ERR_REASON(EVP_R_EXPECTING_A_DH_KEY)    , "expecting a dh key"},
	{ERR_REASON(EVP_R_EXPECTING_A_DSA_KEY)   , "expecting a dsa key"},
	{ERR_REASON(EVP_R_EXPECTING_A_ECDSA_KEY) , "expecting a ecdsa key"},
	{ERR_REASON(EVP_R_EXPECTING_A_EC_KEY)    , "expecting a ec key"},
	{ERR_REASON(EVP_R_FIPS_MODE_NOT_SUPPORTED), "fips mode not supported"},
	{ERR_REASON(EVP_R_GET_RAW_KEY_FAILED)    , "get raw key failed"},
	{ERR_REASON(EVP_R_INITIALIZATION_ERROR)  , "initialization error"},
	{ERR_REASON(EVP_R_INPUT_NOT_INITIALIZED) , "input not initialized"},
	{ERR_REASON(EVP_R_INVALID_DIGEST)        , "invalid digest"},
	{ERR_REASON(EVP_R_INVALID_FIPS_MODE)     , "invalid fips mode"},
	{ERR_REASON(EVP_R_INVALID_IV_LENGTH)     , "invalid iv length"},
	{ERR_REASON(EVP_R_INVALID_KEY_LENGTH)    , "invalid key length"},
	{ERR_REASON(EVP_R_INVALID_OPERATION)     , "invalid operation"},
	{ERR_REASON(EVP_R_IV_TOO_LARGE)          , "iv too large"},
	{ERR_REASON(EVP_R_KEYGEN_FAILURE)        , "keygen failure"},
	{ERR_REASON(EVP_R_KEY_SETUP_FAILED)      , "key setup failed"},
	{ERR_REASON(EVP_R_MESSAGE_DIGEST_IS_NULL), "message digest is null"},
	{ERR_REASON(EVP_R_METHOD_NOT_SUPPORTED)  , "method not supported"},
	{ERR_REASON(EVP_R_MISSING_PARAMETERS)    , "missing parameters"},
	{ERR_REASON(EVP_R_NO_CIPHER_SET)         , "no cipher set"},
	{ERR_REASON(EVP_R_NO_DEFAULT_DIGEST)     , "no default digest"},
	{ERR_REASON(EVP_R_NO_DIGEST_SET)         , "no digest set"},
	{ERR_REASON(EVP_R_NO_DSA_PARAMETERS)     , "no dsa parameters"},
	{ERR_REASON(EVP_R_NO_KEY_SET)            , "no key set"},
	{ERR_REASON(EVP_R_NO_OPERATION_SET)      , "no operation set"},
	{ERR_REASON(EVP_R_NO_SIGN_FUNCTION_CONFIGURED), "no sign function configured"},
	{ERR_REASON(EVP_R_NO_VERIFY_FUNCTION_CONFIGURED), "no verify function configured"},
	{ERR_REASON(EVP_R_ONLY_ONESHOT_SUPPORTED), "only oneshot supported"},
	{ERR_REASON(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE), "operation not supported for this keytype"},
	{ERR_REASON(EVP_R_OPERATON_NOT_INITIALIZED), "operaton not initialized"},
	{ERR_REASON(EVP_R_OUTPUT_ALIASES_INPUT)  , "output aliases input"},
	{ERR_REASON(EVP_R_PKCS8_UNKNOWN_BROKEN_TYPE), "pkcs8 unknown broken type"},
	{ERR_REASON(EVP_R_PRIVATE_KEY_DECODE_ERROR), "private key decode error"},
	{ERR_REASON(EVP_R_PRIVATE_KEY_ENCODE_ERROR), "private key encode error"},
	{ERR_REASON(EVP_R_PUBLIC_KEY_NOT_RSA)    , "public key not rsa"},
	{ERR_REASON(EVP_R_TAG_TOO_LARGE)         , "tag too large"},
	{ERR_REASON(EVP_R_TOO_LARGE)             , "too large"},
	{ERR_REASON(EVP_R_UNKNOWN_CIPHER)        , "unknown cipher"},
	{ERR_REASON(EVP_R_UNKNOWN_DIGEST)        , "unknown digest"},
	{ERR_REASON(EVP_R_UNKNOWN_OPTION)        , "unknown option"},
	{ERR_REASON(EVP_R_UNKNOWN_PBE_ALGORITHM) , "unknown pbe algorithm"},
	{ERR_REASON(EVP_R_UNSUPORTED_NUMBER_OF_ROUNDS), "unsuported number of rounds"},
	{ERR_REASON(EVP_R_UNSUPPORTED_ALGORITHM) , "unsupported algorithm"},
	{ERR_REASON(EVP_R_UNSUPPORTED_CIPHER)    , "unsupported cipher"},
	{ERR_REASON(EVP_R_UNSUPPORTED_KEYLENGTH) , "unsupported keylength"},
	{ERR_REASON(EVP_R_UNSUPPORTED_KEY_DERIVATION_FUNCTION), "unsupported key derivation function"},
	{ERR_REASON(EVP_R_UNSUPPORTED_KEY_SIZE)  , "unsupported key size"},
	{ERR_REASON(EVP_R_UNSUPPORTED_PRF)       , "unsupported prf"},
	{ERR_REASON(EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM), "unsupported private key algorithm"},
	{ERR_REASON(EVP_R_UNSUPPORTED_SALT_TYPE) , "unsupported salt type"},
	{ERR_REASON(EVP_R_WRAP_MODE_NOT_ALLOWED), "wrap mode not allowed"},
	{ERR_REASON(EVP_R_WRONG_FINAL_BLOCK_LENGTH), "wrong final block length"},
	{ERR_REASON(EVP_R_WRONG_PUBLIC_KEY_TYPE) , "wrong public key type"},
	{0, NULL}
};

#endif

void
ERR_load_EVP_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(EVP_str_functs[0].error) == NULL) {
		ERR_load_const_strings(EVP_str_functs);
		ERR_load_const_strings(EVP_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_EVP_strings);
