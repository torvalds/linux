/* $OpenBSD: ec_err.c,v 1.20 2024/06/24 06:43:22 tb Exp $ */
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
#include <openssl/ec.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_EC,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_EC,0,reason)

static const ERR_STRING_DATA EC_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA EC_str_reasons[] = {
	{ERR_REASON(EC_R_ASN1_ERROR), "asn1 error"},
	{ERR_REASON(EC_R_ASN1_UNKNOWN_FIELD), "asn1 unknown field"},
	{ERR_REASON(EC_R_BAD_SIGNATURE), "bad signature"},
	{ERR_REASON(EC_R_BIGNUM_OUT_OF_RANGE), "bignum out of range"},
	{ERR_REASON(EC_R_BUFFER_TOO_SMALL), "buffer too small"},
	{ERR_REASON(EC_R_COORDINATES_OUT_OF_RANGE), "coordinates out of range"},
	{ERR_REASON(EC_R_D2I_ECPKPARAMETERS_FAILURE), "d2i ecpkparameters failure"},
	{ERR_REASON(EC_R_DECODE_ERROR), "decode error"},
	{ERR_REASON(EC_R_DISCRIMINANT_IS_ZERO), "discriminant is zero"},
	{ERR_REASON(EC_R_EC_GROUP_NEW_BY_NAME_FAILURE), "ec group new by name failure"},
	{ERR_REASON(EC_R_FIELD_TOO_LARGE), "field too large"},
	{ERR_REASON(EC_R_GF2M_NOT_SUPPORTED), "gf2m not supported"},
	{ERR_REASON(EC_R_GROUP2PKPARAMETERS_FAILURE), "group2pkparameters failure"},
	{ERR_REASON(EC_R_I2D_ECPKPARAMETERS_FAILURE), "i2d ecpkparameters failure"},
	{ERR_REASON(EC_R_INCOMPATIBLE_OBJECTS), "incompatible objects"},
	{ERR_REASON(EC_R_INVALID_ARGUMENT), "invalid argument"},
	{ERR_REASON(EC_R_INVALID_COMPRESSED_POINT), "invalid compressed point"},
	{ERR_REASON(EC_R_INVALID_COMPRESSION_BIT), "invalid compression bit"},
	{ERR_REASON(EC_R_INVALID_CURVE), "invalid curve"},
	{ERR_REASON(EC_R_INVALID_DIGEST), "invalid digest"},
	{ERR_REASON(EC_R_INVALID_DIGEST_TYPE), "invalid digest type"},
	{ERR_REASON(EC_R_INVALID_ENCODING), "invalid encoding"},
	{ERR_REASON(EC_R_INVALID_FIELD), "invalid field"},
	{ERR_REASON(EC_R_INVALID_FORM), "invalid form"},
	{ERR_REASON(EC_R_INVALID_GROUP_ORDER), "invalid group order"},
	{ERR_REASON(EC_R_INVALID_KEY), "invalid key"},
	{ERR_REASON(EC_R_INVALID_OUTPUT_LENGTH), "invalid output length"},
	{ERR_REASON(EC_R_INVALID_PEER_KEY), "invalid peer key"},
	{ERR_REASON(EC_R_INVALID_PENTANOMIAL_BASIS), "invalid pentanomial basis"},
	{ERR_REASON(EC_R_INVALID_PRIVATE_KEY), "invalid private key"},
	{ERR_REASON(EC_R_INVALID_TRINOMIAL_BASIS), "invalid trinomial basis"},
	{ERR_REASON(EC_R_KDF_FAILED), "kdf failed"},
	{ERR_REASON(EC_R_KDF_PARAMETER_ERROR), "kdf parameter error"},
	{ERR_REASON(EC_R_KEY_TRUNCATION), "key would be truncated"},
	{ERR_REASON(EC_R_KEYS_NOT_SET), "keys not set"},
	{ERR_REASON(EC_R_MISSING_PARAMETERS), "missing parameters"},
	{ERR_REASON(EC_R_MISSING_PRIVATE_KEY), "missing private key"},
	{ERR_REASON(EC_R_NEED_NEW_SETUP_VALUES), "need new setup values"},
	{ERR_REASON(EC_R_NOT_A_NIST_PRIME), "not a NIST prime"},
	{ERR_REASON(EC_R_NOT_A_SUPPORTED_NIST_PRIME), "not a supported NIST prime"},
	{ERR_REASON(EC_R_NOT_IMPLEMENTED), "not implemented"},
	{ERR_REASON(EC_R_NOT_INITIALIZED), "not initialized"},
	{ERR_REASON(EC_R_NO_FIELD_MOD), "no field mod"},
	{ERR_REASON(EC_R_NO_PARAMETERS_SET), "no parameters set"},
	{ERR_REASON(EC_R_PASSED_NULL_PARAMETER), "passed null parameter"},
	{ERR_REASON(EC_R_PEER_KEY_ERROR), "peer key error"},
	{ERR_REASON(EC_R_PKPARAMETERS2GROUP_FAILURE), "pkparameters2group failure"},
	{ERR_REASON(EC_R_POINT_ARITHMETIC_FAILURE), "point arithmetic failure"},
	{ERR_REASON(EC_R_POINT_AT_INFINITY), "point at infinity"},
	{ERR_REASON(EC_R_POINT_IS_NOT_ON_CURVE), "point is not on curve"},
	{ERR_REASON(EC_R_SHARED_INFO_ERROR), "shared info error"},
	{ERR_REASON(EC_R_SLOT_FULL), "slot full"},
	{ERR_REASON(EC_R_UNDEFINED_GENERATOR), "undefined generator"},
	{ERR_REASON(EC_R_UNDEFINED_ORDER), "undefined order"},
	{ERR_REASON(EC_R_UNKNOWN_COFACTOR), "unknown cofactor"},
	{ERR_REASON(EC_R_UNKNOWN_GROUP), "unknown group"},
	{ERR_REASON(EC_R_UNKNOWN_ORDER), "unknown order"},
	{ERR_REASON(EC_R_UNSUPPORTED_FIELD), "unsupported field"},
	{ERR_REASON(EC_R_WRONG_CURVE_PARAMETERS), "wrong curve parameters"},
	{ERR_REASON(EC_R_WRONG_ORDER), "wrong order"},
	{0, NULL}
};

#endif

void
ERR_load_EC_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(EC_str_functs[0].error) == NULL) {
		ERR_load_const_strings(EC_str_functs);
		ERR_load_const_strings(EC_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_EC_strings);
