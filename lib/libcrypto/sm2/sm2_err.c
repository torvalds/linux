/*	$OpenBSD: sm2_err.c,v 1.2 2022/07/12 14:42:50 kn Exp $ */
/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_NO_SM2

#include <openssl/err.h>
#include <openssl/sm2.h>

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_SM2,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_SM2,0,reason)

static ERR_STRING_DATA SM2_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static ERR_STRING_DATA SM2_str_reasons[] = {
	{ERR_REASON(SM2_R_ASN1_ERROR), "asn1 error"},
	{ERR_REASON(SM2_R_ASN5_ERROR), "asn5 error"},
	{ERR_REASON(SM2_R_BAD_SIGNATURE), "bad signature"},
	{ERR_REASON(SM2_R_BIGNUM_OUT_OF_RANGE), "bignum out of range"},
	{ERR_REASON(SM2_R_BUFFER_TOO_SMALL), "buffer too small"},
	{ERR_REASON(SM2_R_COORDINATES_OUT_OF_RANGE), "coordinates out of range"},
	{ERR_REASON(SM2_R_CURVE_DOES_NOT_SUPPORT_ECDH), "curve does not support ecdh"},
	{ERR_REASON(SM2_R_CURVE_DOES_NOT_SUPPORT_SIGNING), "curve does not support signing"},
	{ERR_REASON(SM2_R_D2I_ECPKPARAMETERS_FAILURE), "d2i ecpkparameters failure"},
	{ERR_REASON(SM2_R_DECODE_ERROR), "decode error"},
	{ERR_REASON(SM2_R_DIGEST_FAILURE), "digest calculation failure"},
	{ERR_REASON(SM2_R_DISCRIMINANT_IS_ZERO), "discriminant is zero"},
	{ERR_REASON(SM2_R_EC_GROUP_NEW_BY_NAME_FAILURE), "ec group new by name failure"},
	{ERR_REASON(SM2_R_FIELD_TOO_LARGE), "field too large"},
	{ERR_REASON(SM2_R_GF2M_NOT_SUPPORTED), "gf2m not supported"},
	{ERR_REASON(SM2_R_GROUP2PKPARAMETERS_FAILURE), "group2pkparameters failure"},
	{ERR_REASON(SM2_R_I2D_ECPKPARAMETERS_FAILURE), "i2d ecpkparameters failure"},
	{ERR_REASON(SM2_R_INCOMPATIBLE_OBJECTS), "incompatible objects"},
	{ERR_REASON(SM2_R_INVALID_ARGUMENT), "invalid argument"},
	{ERR_REASON(SM2_R_INVALID_COMPRESSED_POINT), "invalid compressed point"},
	{ERR_REASON(SM2_R_INVALID_COMPRESSION_BIT), "invalid compression bit"},
	{ERR_REASON(SM2_R_INVALID_CURVE), "invalid curve"},
	{ERR_REASON(SM2_R_INVALID_DIGEST), "invalid digest"},
	{ERR_REASON(SM2_R_INVALID_DIGEST_TYPE), "invalid digest type"},
	{ERR_REASON(SM2_R_INVALID_ENCODING), "invalid encoding"},
	{ERR_REASON(SM2_R_INVALID_FIELD), "invalid field"},
	{ERR_REASON(SM2_R_INVALID_FORM), "invalid form"},
	{ERR_REASON(SM2_R_INVALID_GROUP_ORDER), "invalid group order"},
	{ERR_REASON(SM2_R_INVALID_KEY), "invalid key"},
	{ERR_REASON(SM2_R_INVALID_OUTPUT_LENGTH), "invalid output length"},
	{ERR_REASON(SM2_R_INVALID_PEER_KEY), "invalid peer key"},
	{ERR_REASON(SM2_R_INVALID_PENTANOMIAL_BASIS), "invalid pentanomial basis"},
	{ERR_REASON(SM2_R_INVALID_PRIVATE_KEY), "invalid private key"},
	{ERR_REASON(SM2_R_INVALID_TRINOMIAL_BASIS), "invalid trinomial basis"},
	{ERR_REASON(SM2_R_KDF_FAILURE), "kdf calculation failure"},
	{ERR_REASON(SM2_R_KDF_PARAMETER_ERROR), "kdf parameter error"},
	{ERR_REASON(SM2_R_KEYS_NOT_SET), "keys not set"},
	{ERR_REASON(SM2_R_MISSING_PARAMETERS), "missing parameters"},
	{ERR_REASON(SM2_R_MISSING_PRIVATE_KEY), "missing private key"},
	{ERR_REASON(SM2_R_NEED_NEW_SETUP_VALUES), "need new setup values"},
	{ERR_REASON(SM2_R_NOT_A_NIST_PRIME), "not a NIST prime"},
	{ERR_REASON(SM2_R_NOT_IMPLEMENTED), "not implemented"},
	{ERR_REASON(SM2_R_NOT_INITIALIZED), "not initialized"},
	{ERR_REASON(SM2_R_NO_PARAMETERS_SET), "no parameters set"},
	{ERR_REASON(SM2_R_NO_PRIVATE_VALUE), "no private value"},
	{ERR_REASON(SM2_R_OPERATION_NOT_SUPPORTED), "operation not supported"},
	{ERR_REASON(SM2_R_PASSED_NULL_PARAMETER), "passed null parameter"},
	{ERR_REASON(SM2_R_PEER_KEY_ERROR), "peer key error"},
	{ERR_REASON(SM2_R_PKPARAMETERS2GROUP_FAILURE), "pkparameters2group failure"},
	{ERR_REASON(SM2_R_POINT_ARITHMETIC_FAILURE), "point arithmetic failure"},
	{ERR_REASON(SM2_R_POINT_AT_INFINITY), "point at infinity"},
	{ERR_REASON(SM2_R_POINT_IS_NOT_ON_CURVE), "point is not on curve"},
	{ERR_REASON(SM2_R_RANDOM_NUMBER_GENERATION_FAILED), "random number generation failed"},
	{ERR_REASON(SM2_R_SHARED_INFO_ERROR), "shared info error"},
	{ERR_REASON(SM2_R_SLOT_FULL), "slot full"},
	{ERR_REASON(SM2_R_UNDEFINED_GENERATOR), "undefined generator"},
	{ERR_REASON(SM2_R_UNDEFINED_ORDER), "undefined order"},
	{ERR_REASON(SM2_R_UNKNOWN_GROUP), "unknown group"},
	{ERR_REASON(SM2_R_UNKNOWN_ORDER), "unknown order"},
	{ERR_REASON(SM2_R_UNSUPPORTED_FIELD), "unsupported field"},
	{ERR_REASON(SM2_R_WRONG_CURVE_PARAMETERS), "wrong curve parameters"},
	{ERR_REASON(SM2_R_WRONG_ORDER), "wrong order"},
	{0, NULL}
};

#endif

void
ERR_load_SM2_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(SM2_str_functs[0].error) == NULL) {
		ERR_load_strings(0, SM2_str_functs);
		ERR_load_strings(0, SM2_str_reasons);
	}
#endif
}

#endif /* OPENSSL_NO_SM2 */
