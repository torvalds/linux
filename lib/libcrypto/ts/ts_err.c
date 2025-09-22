/* $OpenBSD: ts_err.c,v 1.8 2024/06/24 06:43:22 tb Exp $ */
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
#include <openssl/ts.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_TS,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_TS,0,reason)

static const ERR_STRING_DATA TS_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA TS_str_reasons[] = {
	{ERR_REASON(TS_R_BAD_PKCS7_TYPE)         , "bad pkcs7 type"},
	{ERR_REASON(TS_R_BAD_TYPE)               , "bad type"},
	{ERR_REASON(TS_R_CERTIFICATE_VERIFY_ERROR), "certificate verify error"},
	{ERR_REASON(TS_R_COULD_NOT_SET_ENGINE)   , "could not set engine"},
	{ERR_REASON(TS_R_COULD_NOT_SET_TIME)     , "could not set time"},
	{ERR_REASON(TS_R_D2I_TS_RESP_INT_FAILED) , "d2i ts resp int failed"},
	{ERR_REASON(TS_R_DETACHED_CONTENT)       , "detached content"},
	{ERR_REASON(TS_R_ESS_ADD_SIGNING_CERT_ERROR), "ess add signing cert error"},
	{ERR_REASON(TS_R_ESS_SIGNING_CERTIFICATE_ERROR), "ess signing certificate error"},
	{ERR_REASON(TS_R_INVALID_NULL_POINTER)   , "invalid null pointer"},
	{ERR_REASON(TS_R_INVALID_SIGNER_CERTIFICATE_PURPOSE), "invalid signer certificate purpose"},
	{ERR_REASON(TS_R_MESSAGE_IMPRINT_MISMATCH), "message imprint mismatch"},
	{ERR_REASON(TS_R_NONCE_MISMATCH)         , "nonce mismatch"},
	{ERR_REASON(TS_R_NONCE_NOT_RETURNED)     , "nonce not returned"},
	{ERR_REASON(TS_R_NO_CONTENT)             , "no content"},
	{ERR_REASON(TS_R_NO_TIME_STAMP_TOKEN)    , "no time stamp token"},
	{ERR_REASON(TS_R_PKCS7_ADD_SIGNATURE_ERROR), "pkcs7 add signature error"},
	{ERR_REASON(TS_R_PKCS7_ADD_SIGNED_ATTR_ERROR), "pkcs7 add signed attr error"},
	{ERR_REASON(TS_R_PKCS7_TO_TS_TST_INFO_FAILED), "pkcs7 to ts tst info failed"},
	{ERR_REASON(TS_R_POLICY_MISMATCH)        , "policy mismatch"},
	{ERR_REASON(TS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE), "private key does not match certificate"},
	{ERR_REASON(TS_R_RESPONSE_SETUP_ERROR)   , "response setup error"},
	{ERR_REASON(TS_R_SIGNATURE_FAILURE)      , "signature failure"},
	{ERR_REASON(TS_R_THERE_MUST_BE_ONE_SIGNER), "there must be one signer"},
	{ERR_REASON(TS_R_TIME_SYSCALL_ERROR)     , "time syscall error"},
	{ERR_REASON(TS_R_TOKEN_NOT_PRESENT)      , "token not present"},
	{ERR_REASON(TS_R_TOKEN_PRESENT)          , "token present"},
	{ERR_REASON(TS_R_TSA_NAME_MISMATCH)      , "tsa name mismatch"},
	{ERR_REASON(TS_R_TSA_UNTRUSTED)          , "tsa untrusted"},
	{ERR_REASON(TS_R_TST_INFO_SETUP_ERROR)   , "tst info setup error"},
	{ERR_REASON(TS_R_TS_DATASIGN)            , "ts datasign"},
	{ERR_REASON(TS_R_UNACCEPTABLE_POLICY)    , "unacceptable policy"},
	{ERR_REASON(TS_R_UNSUPPORTED_MD_ALGORITHM), "unsupported md algorithm"},
	{ERR_REASON(TS_R_UNSUPPORTED_VERSION)    , "unsupported version"},
	{ERR_REASON(TS_R_WRONG_CONTENT_TYPE)     , "wrong content type"},
	{0, NULL}
};

#endif

void
ERR_load_TS_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(TS_str_functs[0].error) == NULL) {
		ERR_load_const_strings(TS_str_functs);
		ERR_load_const_strings(TS_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_TS_strings);
