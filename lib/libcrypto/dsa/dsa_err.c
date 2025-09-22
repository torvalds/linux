/* $OpenBSD: dsa_err.c,v 1.22 2024/06/24 06:43:22 tb Exp $ */
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
#include <openssl/dsa.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_DSA,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_DSA,0,reason)

static const ERR_STRING_DATA DSA_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA DSA_str_reasons[] = {
	{ERR_REASON(DSA_R_BAD_Q_VALUE)           ,"bad q value"},
	{ERR_REASON(DSA_R_BN_DECODE_ERROR)       ,"bn decode error"},
	{ERR_REASON(DSA_R_BN_ERROR)              ,"bn error"},
	{ERR_REASON(DSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE),"data too large for key size"},
	{ERR_REASON(DSA_R_DECODE_ERROR)          ,"decode error"},
	{ERR_REASON(DSA_R_INVALID_DIGEST_TYPE)   ,"invalid digest type"},
	{ERR_REASON(DSA_R_INVALID_PARAMETERS)    ,"invalid parameters"},
	{ERR_REASON(DSA_R_MISSING_PARAMETERS)    ,"missing parameters"},
	{ERR_REASON(DSA_R_MODULUS_TOO_LARGE)     ,"modulus too large"},
	{ERR_REASON(DSA_R_NEED_NEW_SETUP_VALUES) ,"need new setup values"},
	{ERR_REASON(DSA_R_NON_FIPS_DSA_METHOD)   ,"non fips dsa method"},
	{ERR_REASON(DSA_R_NO_PARAMETERS_SET)     ,"no parameters set"},
	{ERR_REASON(DSA_R_PARAMETER_ENCODING_ERROR),"parameter encoding error"},
	{0,NULL}
};

#endif

void
ERR_load_DSA_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(DSA_str_functs[0].error) == NULL) {
		ERR_load_const_strings(DSA_str_functs);
		ERR_load_const_strings(DSA_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_DSA_strings);
