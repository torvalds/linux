/* $OpenBSD: pem_err.c,v 1.15 2024/06/24 06:43:22 tb Exp $ */
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
#include <openssl/pem.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_PEM,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_PEM,0,reason)

static const ERR_STRING_DATA PEM_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA PEM_str_reasons[] = {
	{ERR_REASON(PEM_R_BAD_BASE64_DECODE)     , "bad base64 decode"},
	{ERR_REASON(PEM_R_BAD_DECRYPT)           , "bad decrypt"},
	{ERR_REASON(PEM_R_BAD_END_LINE)          , "bad end line"},
	{ERR_REASON(PEM_R_BAD_IV_CHARS)          , "bad iv chars"},
	{ERR_REASON(PEM_R_BAD_MAGIC_NUMBER)      , "bad magic number"},
	{ERR_REASON(PEM_R_BAD_PASSWORD_READ)     , "bad password read"},
	{ERR_REASON(PEM_R_BAD_VERSION_NUMBER)    , "bad version number"},
	{ERR_REASON(PEM_R_BIO_WRITE_FAILURE)     , "bio write failure"},
	{ERR_REASON(PEM_R_CIPHER_IS_NULL)        , "cipher is null"},
	{ERR_REASON(PEM_R_ERROR_CONVERTING_PRIVATE_KEY), "error converting private key"},
	{ERR_REASON(PEM_R_EXPECTING_PRIVATE_KEY_BLOB), "expecting private key blob"},
	{ERR_REASON(PEM_R_EXPECTING_PUBLIC_KEY_BLOB), "expecting public key blob"},
	{ERR_REASON(PEM_R_INCONSISTENT_HEADER)   , "inconsistent header"},
	{ERR_REASON(PEM_R_KEYBLOB_HEADER_PARSE_ERROR), "keyblob header parse error"},
	{ERR_REASON(PEM_R_KEYBLOB_TOO_SHORT)     , "keyblob too short"},
	{ERR_REASON(PEM_R_NOT_DEK_INFO)          , "not dek info"},
	{ERR_REASON(PEM_R_NOT_ENCRYPTED)         , "not encrypted"},
	{ERR_REASON(PEM_R_NOT_PROC_TYPE)         , "not proc type"},
	{ERR_REASON(PEM_R_NO_START_LINE)         , "no start line"},
	{ERR_REASON(PEM_R_PROBLEMS_GETTING_PASSWORD), "problems getting password"},
	{ERR_REASON(PEM_R_PUBLIC_KEY_NO_RSA)     , "public key no rsa"},
	{ERR_REASON(PEM_R_PVK_DATA_TOO_SHORT)    , "pvk data too short"},
	{ERR_REASON(PEM_R_PVK_TOO_SHORT)         , "pvk too short"},
	{ERR_REASON(PEM_R_READ_KEY)              , "read key"},
	{ERR_REASON(PEM_R_SHORT_HEADER)          , "short header"},
	{ERR_REASON(PEM_R_UNSUPPORTED_CIPHER)    , "unsupported cipher"},
	{ERR_REASON(PEM_R_UNSUPPORTED_ENCRYPTION), "unsupported encryption"},
	{ERR_REASON(PEM_R_UNSUPPORTED_KEY_COMPONENTS), "unsupported key components"},
	{0, NULL}
};

#endif

void
ERR_load_PEM_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(PEM_str_functs[0].error) == NULL) {
		ERR_load_const_strings(PEM_str_functs);
		ERR_load_const_strings(PEM_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_PEM_strings);
