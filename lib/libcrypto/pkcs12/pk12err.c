/* $OpenBSD: pk12err.c,v 1.15 2024/06/24 06:43:22 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999-2006 The OpenSSL Project.  All rights reserved.
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
#include <openssl/pkcs12.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_PKCS12,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_PKCS12,0,reason)

static const ERR_STRING_DATA PKCS12_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA PKCS12_str_reasons[] = {
	{ERR_REASON(PKCS12_R_CANT_PACK_STRUCTURE), "cant pack structure"},
	{ERR_REASON(PKCS12_R_CONTENT_TYPE_NOT_DATA), "content type not data"},
	{ERR_REASON(PKCS12_R_DECODE_ERROR)       , "decode error"},
	{ERR_REASON(PKCS12_R_ENCODE_ERROR)       , "encode error"},
	{ERR_REASON(PKCS12_R_ENCRYPT_ERROR)      , "encrypt error"},
	{ERR_REASON(PKCS12_R_ERROR_SETTING_ENCRYPTED_DATA_TYPE), "error setting encrypted data type"},
	{ERR_REASON(PKCS12_R_INVALID_NULL_ARGUMENT), "invalid null argument"},
	{ERR_REASON(PKCS12_R_INVALID_NULL_PKCS12_POINTER), "invalid null pkcs12 pointer"},
	{ERR_REASON(PKCS12_R_IV_GEN_ERROR)       , "iv gen error"},
	{ERR_REASON(PKCS12_R_KEY_GEN_ERROR)      , "key gen error"},
	{ERR_REASON(PKCS12_R_MAC_ABSENT)         , "mac absent"},
	{ERR_REASON(PKCS12_R_MAC_GENERATION_ERROR), "mac generation error"},
	{ERR_REASON(PKCS12_R_MAC_SETUP_ERROR)    , "mac setup error"},
	{ERR_REASON(PKCS12_R_MAC_STRING_SET_ERROR), "mac string set error"},
	{ERR_REASON(PKCS12_R_MAC_VERIFY_ERROR)   , "mac verify error"},
	{ERR_REASON(PKCS12_R_MAC_VERIFY_FAILURE) , "mac verify failure"},
	{ERR_REASON(PKCS12_R_PARSE_ERROR)        , "parse error"},
	{ERR_REASON(PKCS12_R_PKCS12_ALGOR_CIPHERINIT_ERROR), "pkcs12 algor cipherinit error"},
	{ERR_REASON(PKCS12_R_PKCS12_CIPHERFINAL_ERROR), "pkcs12 cipherfinal error"},
	{ERR_REASON(PKCS12_R_PKCS12_PBE_CRYPT_ERROR), "pkcs12 pbe crypt error"},
	{ERR_REASON(PKCS12_R_UNKNOWN_DIGEST_ALGORITHM), "unknown digest algorithm"},
	{ERR_REASON(PKCS12_R_UNSUPPORTED_PKCS12_MODE), "unsupported pkcs12 mode"},
	{0, NULL}
};

#endif

void
ERR_load_PKCS12_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(PKCS12_str_functs[0].error) == NULL) {
		ERR_load_const_strings(PKCS12_str_functs);
		ERR_load_const_strings(PKCS12_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_PKCS12_strings);
