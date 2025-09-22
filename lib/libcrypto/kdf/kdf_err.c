/*	$OpenBSD: kdf_err.c,v 1.11 2024/07/09 16:20:17 tb Exp $ */
/* ====================================================================
 * Copyright (c) 1999-2018 The OpenSSL Project.  All rights reserved.
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

#include <openssl/err.h>
#include <openssl/kdf.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

static const ERR_STRING_DATA KDF_str_functs[] = {
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_HKDF_CTRL_STR, 0), "pkey_hkdf_ctrl_str"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_HKDF_DERIVE, 0), "pkey_hkdf_derive"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_HKDF_INIT, 0), "pkey_hkdf_init"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_TLS1_PRF_CTRL_STR, 0), "pkey_tls1_prf_ctrl_str"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_TLS1_PRF_DERIVE, 0), "pkey_tls1_prf_derive"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_PKEY_TLS1_PRF_INIT, 0), "pkey_tls1_prf_init"},
	{ERR_PACK(ERR_LIB_KDF, KDF_F_TLS1_PRF_ALG, 0), "pkey_tls1_prf_alg"},
	{0, NULL},
};

static const ERR_STRING_DATA KDF_str_reasons[] = {
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_INVALID_DIGEST), "invalid digest"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_MISSING_KEY), "missing key"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_MISSING_MESSAGE_DIGEST),
	 "missing message digest"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_MISSING_SECRET), "missing secret"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_MISSING_SEED), "missing seed"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_UNKNOWN_PARAMETER_TYPE),
	 "unknown parameter type"},
	{ERR_PACK(ERR_LIB_KDF, 0, KDF_R_VALUE_MISSING), "value missing"},
	{0, NULL},
};

#endif

int
ERR_load_KDF_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(KDF_str_functs[0].error) == NULL) {
		ERR_load_const_strings(KDF_str_functs);
		ERR_load_const_strings(KDF_str_reasons);
	}
#endif
	return 1;
}
