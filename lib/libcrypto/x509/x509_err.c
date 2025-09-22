/* $OpenBSD: x509_err.c,v 1.23 2024/06/24 06:43:23 tb Exp $ */
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
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"

#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_X509,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_X509,0,reason)

static const ERR_STRING_DATA X509_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA X509_str_reasons[] = {
	{ERR_REASON(X509_R_BAD_X509_FILETYPE)    , "bad x509 filetype"},
	{ERR_REASON(X509_R_BASE64_DECODE_ERROR)  , "base64 decode error"},
	{ERR_REASON(X509_R_CANT_CHECK_DH_KEY)    , "cant check dh key"},
	{ERR_REASON(X509_R_CERT_ALREADY_IN_HASH_TABLE), "cert already in hash table"},
	{ERR_REASON(X509_R_ERR_ASN1_LIB)         , "err asn1 lib"},
	{ERR_REASON(X509_R_INVALID_DIRECTORY)    , "invalid directory"},
	{ERR_REASON(X509_R_INVALID_FIELD_NAME)   , "invalid field name"},
	{ERR_REASON(X509_R_INVALID_TRUST)        , "invalid trust"},
	{ERR_REASON(X509_R_INVALID_VERSION)      , "invalid x509 version"},
	{ERR_REASON(X509_R_KEY_TYPE_MISMATCH)    , "key type mismatch"},
	{ERR_REASON(X509_R_KEY_VALUES_MISMATCH)  , "key values mismatch"},
	{ERR_REASON(X509_R_LOADING_CERT_DIR)     , "loading cert dir"},
	{ERR_REASON(X509_R_LOADING_DEFAULTS)     , "loading defaults"},
	{ERR_REASON(X509_R_METHOD_NOT_SUPPORTED) , "method not supported"},
	{ERR_REASON(X509_R_NO_CERTIFICATE_OR_CRL_FOUND), "no certificate or crl found"},
	{ERR_REASON(X509_R_NO_CERT_SET_FOR_US_TO_VERIFY), "no cert set for us to verify"},
	{ERR_REASON(X509_R_PUBLIC_KEY_DECODE_ERROR), "public key decode error"},
	{ERR_REASON(X509_R_PUBLIC_KEY_ENCODE_ERROR), "public key encode error"},
	{ERR_REASON(X509_R_SHOULD_RETRY)         , "should retry"},
	{ERR_REASON(X509_R_UNABLE_TO_FIND_PARAMETERS_IN_CHAIN), "unable to find parameters in chain"},
	{ERR_REASON(X509_R_UNABLE_TO_GET_CERTS_PUBLIC_KEY), "unable to get certs public key"},
	{ERR_REASON(X509_R_UNKNOWN_KEY_TYPE)     , "unknown key type"},
	{ERR_REASON(X509_R_UNKNOWN_NID)          , "unknown nid"},
	{ERR_REASON(X509_R_UNKNOWN_PURPOSE_ID)   , "unknown purpose id"},
	{ERR_REASON(X509_R_UNKNOWN_TRUST_ID)     , "unknown trust id"},
	{ERR_REASON(X509_R_UNSUPPORTED_ALGORITHM), "unsupported algorithm"},
	{ERR_REASON(X509_R_WRONG_LOOKUP_TYPE)    , "wrong lookup type"},
	{ERR_REASON(X509_R_WRONG_TYPE)           , "wrong type"},
	{0, NULL}
};

#undef ERR_FUNC
#undef ERR_REASON
#define ERR_FUNC(func) ERR_PACK(ERR_LIB_X509V3,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_X509V3,0,reason)

static const ERR_STRING_DATA X509V3_str_functs[] = {
	{ERR_FUNC(0xfff), "CRYPTO_internal"},
	{0, NULL}
};

static const ERR_STRING_DATA X509V3_str_reasons[] = {
	{ERR_REASON(X509V3_R_BAD_IP_ADDRESS)     , "bad ip address"},
	{ERR_REASON(X509V3_R_BAD_OBJECT)         , "bad object"},
	{ERR_REASON(X509V3_R_BN_DEC2BN_ERROR)    , "bn dec2bn error"},
	{ERR_REASON(X509V3_R_BN_TO_ASN1_INTEGER_ERROR), "bn to asn1 integer error"},
	{ERR_REASON(X509V3_R_DIRNAME_ERROR)      , "dirname error"},
	{ERR_REASON(X509V3_R_DISTPOINT_ALREADY_SET), "distpoint already set"},
	{ERR_REASON(X509V3_R_DUPLICATE_ZONE_ID)  , "duplicate zone id"},
	{ERR_REASON(X509V3_R_ERROR_CONVERTING_ZONE), "error converting zone"},
	{ERR_REASON(X509V3_R_ERROR_CREATING_EXTENSION), "error creating extension"},
	{ERR_REASON(X509V3_R_ERROR_IN_EXTENSION) , "error in extension"},
	{ERR_REASON(X509V3_R_EXPECTED_A_SECTION_NAME), "expected a section name"},
	{ERR_REASON(X509V3_R_EXTENSION_EXISTS)   , "extension exists"},
	{ERR_REASON(X509V3_R_EXTENSION_NAME_ERROR), "extension name error"},
	{ERR_REASON(X509V3_R_EXTENSION_NOT_FOUND), "extension not found"},
	{ERR_REASON(X509V3_R_EXTENSION_SETTING_NOT_SUPPORTED), "extension setting not supported"},
	{ERR_REASON(X509V3_R_EXTENSION_VALUE_ERROR), "extension value error"},
	{ERR_REASON(X509V3_R_ILLEGAL_EMPTY_EXTENSION), "illegal empty extension"},
	{ERR_REASON(X509V3_R_ILLEGAL_HEX_DIGIT)  , "illegal hex digit"},
	{ERR_REASON(X509V3_R_INCORRECT_POLICY_SYNTAX_TAG), "incorrect policy syntax tag"},
	{ERR_REASON(X509V3_R_INVALID_MULTIPLE_RDNS), "invalid multiple rdns"},
	{ERR_REASON(X509V3_R_INVALID_ASNUMBER)   , "invalid asnumber"},
	{ERR_REASON(X509V3_R_INVALID_ASRANGE)    , "invalid asrange"},
	{ERR_REASON(X509V3_R_INVALID_BOOLEAN_STRING), "invalid boolean string"},
	{ERR_REASON(X509V3_R_INVALID_EXTENSION_STRING), "invalid extension string"},
	{ERR_REASON(X509V3_R_INVALID_INHERITANCE), "invalid inheritance"},
	{ERR_REASON(X509V3_R_INVALID_IPADDRESS)  , "invalid ipaddress"},
	{ERR_REASON(X509V3_R_INVALID_NAME)       , "invalid name"},
	{ERR_REASON(X509V3_R_INVALID_NULL_ARGUMENT), "invalid null argument"},
	{ERR_REASON(X509V3_R_INVALID_NULL_NAME)  , "invalid null name"},
	{ERR_REASON(X509V3_R_INVALID_NULL_VALUE) , "invalid null value"},
	{ERR_REASON(X509V3_R_INVALID_NUMBER)     , "invalid number"},
	{ERR_REASON(X509V3_R_INVALID_NUMBERS)    , "invalid numbers"},
	{ERR_REASON(X509V3_R_INVALID_OBJECT_IDENTIFIER), "invalid object identifier"},
	{ERR_REASON(X509V3_R_INVALID_OPTION)     , "invalid option"},
	{ERR_REASON(X509V3_R_INVALID_POLICY_IDENTIFIER), "invalid policy identifier"},
	{ERR_REASON(X509V3_R_INVALID_PROXY_POLICY_SETTING), "invalid proxy policy setting"},
	{ERR_REASON(X509V3_R_INVALID_PURPOSE)    , "invalid purpose"},
	{ERR_REASON(X509V3_R_INVALID_SAFI)       , "invalid safi"},
	{ERR_REASON(X509V3_R_INVALID_SECTION)    , "invalid section"},
	{ERR_REASON(X509V3_R_INVALID_SYNTAX)     , "invalid syntax"},
	{ERR_REASON(X509V3_R_ISSUER_DECODE_ERROR), "issuer decode error"},
	{ERR_REASON(X509V3_R_MISSING_VALUE)      , "missing value"},
	{ERR_REASON(X509V3_R_NEED_ORGANIZATION_AND_NUMBERS), "need organization and numbers"},
	{ERR_REASON(X509V3_R_NO_CONFIG_DATABASE) , "no config database"},
	{ERR_REASON(X509V3_R_NO_ISSUER_CERTIFICATE), "no issuer certificate"},
	{ERR_REASON(X509V3_R_NO_ISSUER_DETAILS)  , "no issuer details"},
	{ERR_REASON(X509V3_R_NO_POLICY_IDENTIFIER), "no policy identifier"},
	{ERR_REASON(X509V3_R_NO_PROXY_CERT_POLICY_LANGUAGE_DEFINED), "no proxy cert policy language defined"},
	{ERR_REASON(X509V3_R_NO_PUBLIC_KEY)      , "no public key"},
	{ERR_REASON(X509V3_R_NO_SUBJECT_DETAILS) , "no subject details"},
	{ERR_REASON(X509V3_R_ODD_NUMBER_OF_DIGITS), "odd number of digits"},
	{ERR_REASON(X509V3_R_OPERATION_NOT_DEFINED), "operation not defined"},
	{ERR_REASON(X509V3_R_OTHERNAME_ERROR)    , "othername error"},
	{ERR_REASON(X509V3_R_POLICY_LANGUAGE_ALREADY_DEFINED), "policy language already defined"},
	{ERR_REASON(X509V3_R_POLICY_PATH_LENGTH) , "policy path length"},
	{ERR_REASON(X509V3_R_POLICY_PATH_LENGTH_ALREADY_DEFINED), "policy path length already defined"},
	{ERR_REASON(X509V3_R_POLICY_SYNTAX_NOT_CURRENTLY_SUPPORTED), "policy syntax not currently supported"},
	{ERR_REASON(X509V3_R_POLICY_WHEN_PROXY_LANGUAGE_REQUIRES_NO_POLICY), "policy when proxy language requires no policy"},
	{ERR_REASON(X509V3_R_SECTION_NOT_FOUND)  , "section not found"},
	{ERR_REASON(X509V3_R_UNABLE_TO_GET_ISSUER_DETAILS), "unable to get issuer details"},
	{ERR_REASON(X509V3_R_UNABLE_TO_GET_ISSUER_KEYID), "unable to get issuer keyid"},
	{ERR_REASON(X509V3_R_UNKNOWN_BIT_STRING_ARGUMENT), "unknown bit string argument"},
	{ERR_REASON(X509V3_R_UNKNOWN_EXTENSION)  , "unknown extension"},
	{ERR_REASON(X509V3_R_UNKNOWN_EXTENSION_NAME), "unknown extension name"},
	{ERR_REASON(X509V3_R_UNKNOWN_OPTION)     , "unknown option"},
	{ERR_REASON(X509V3_R_UNSUPPORTED_OPTION) , "unsupported option"},
	{ERR_REASON(X509V3_R_UNSUPPORTED_TYPE)   , "unsupported type"},
	{ERR_REASON(X509V3_R_USER_TOO_LONG)      , "user too long"},
	{0, NULL}
};

#endif

void
ERR_load_X509_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(X509_str_functs[0].error) == NULL) {
		ERR_load_const_strings(X509_str_functs);
		ERR_load_const_strings(X509_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_X509_strings);


void
ERR_load_X509V3_strings(void)
{
#ifndef OPENSSL_NO_ERR
	if (ERR_func_error_string(X509V3_str_functs[0].error) == NULL) {
		ERR_load_const_strings(X509V3_str_functs);
		ERR_load_const_strings(X509V3_str_reasons);
	}
#endif
}
LCRYPTO_ALIAS(ERR_load_X509V3_strings);
