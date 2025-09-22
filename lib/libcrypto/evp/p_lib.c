/* $OpenBSD: p_lib.c,v 1.63 2025/07/02 06:36:52 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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
 *    licensing@OpenSSL.org.
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/cmac.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif
#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif
#ifndef OPENSSL_NO_EC
#include <openssl/ec.h>
#endif
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif

#include "err_local.h"
#include "evp_local.h"

extern const EVP_PKEY_ASN1_METHOD cmac_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dh_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa1_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa2_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa3_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD dsa4_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD eckey_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD ed25519_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD hmac_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD rsa_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD rsa2_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD rsa_pss_asn1_meth;
extern const EVP_PKEY_ASN1_METHOD x25519_asn1_meth;

static const EVP_PKEY_ASN1_METHOD *asn1_methods[] = {
	&cmac_asn1_meth,
	&dh_asn1_meth,
	&dsa_asn1_meth,
	&dsa1_asn1_meth,
	&dsa2_asn1_meth,
	&dsa3_asn1_meth,
	&dsa4_asn1_meth,
	&eckey_asn1_meth,
	&ed25519_asn1_meth,
	&hmac_asn1_meth,
	&rsa_asn1_meth,
	&rsa2_asn1_meth,
	&rsa_pss_asn1_meth,
	&x25519_asn1_meth,
};

#define N_ASN1_METHODS (sizeof(asn1_methods) / sizeof(asn1_methods[0]))

int
EVP_PKEY_asn1_get_count(void)
{
	return N_ASN1_METHODS;
}
LCRYPTO_ALIAS(EVP_PKEY_asn1_get_count);

const EVP_PKEY_ASN1_METHOD *
EVP_PKEY_asn1_get0(int idx)
{
	if (idx < 0 || idx >= N_ASN1_METHODS)
		return NULL;

	return asn1_methods[idx];
}
LCRYPTO_ALIAS(EVP_PKEY_asn1_get0);

const EVP_PKEY_ASN1_METHOD *
EVP_PKEY_asn1_find(ENGINE **engine, int pkey_id)
{
	size_t i;

	if (engine != NULL)
		*engine = NULL;

	for (i = 0; i < N_ASN1_METHODS; i++) {
		if (asn1_methods[i]->pkey_id == pkey_id)
			return asn1_methods[i]->base_method;
	}

	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_asn1_find);

const EVP_PKEY_ASN1_METHOD *
EVP_PKEY_asn1_find_str(ENGINE **engine, const char *str, int len)
{
	const EVP_PKEY_ASN1_METHOD *ameth;
	size_t i, str_len;

	if (engine != NULL)
		*engine = NULL;

	if (len < -1)
		return NULL;
	if (len == -1)
		str_len = strlen(str);
	else
		str_len = len;

	for (i = 0; i < N_ASN1_METHODS; i++) {
		ameth = asn1_methods[i];
		if ((ameth->pkey_flags & ASN1_PKEY_ALIAS) != 0)
			continue;
		if (strlen(ameth->pem_str) != str_len)
			continue;
		if (strncasecmp(ameth->pem_str, str, str_len) == 0)
			return ameth;
	}

	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_asn1_find_str);

int
EVP_PKEY_asn1_get0_info(int *pkey_id, int *pkey_base_id, int *pkey_flags,
    const char **info, const char **pem_str,
    const EVP_PKEY_ASN1_METHOD *ameth)
{
	if (ameth == NULL)
		return 0;

	if (pkey_id != NULL)
		*pkey_id = ameth->pkey_id;
	if (pkey_base_id != NULL)
		*pkey_base_id = ameth->base_method->pkey_id;
	if (pkey_flags != NULL)
		*pkey_flags = ameth->pkey_flags;
	if (info != NULL)
		*info = ameth->info;
	if (pem_str != NULL)
		*pem_str = ameth->pem_str;

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_asn1_get0_info);

const EVP_PKEY_ASN1_METHOD*
EVP_PKEY_get0_asn1(const EVP_PKEY *pkey)
{
	return pkey->ameth;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_asn1);

int
EVP_PKEY_bits(const EVP_PKEY *pkey)
{
	if (pkey && pkey->ameth && pkey->ameth->pkey_bits)
		return pkey->ameth->pkey_bits(pkey);
	return 0;
}
LCRYPTO_ALIAS(EVP_PKEY_bits);

int
EVP_PKEY_security_bits(const EVP_PKEY *pkey)
{
	if (pkey == NULL)
		return 0;
	if (pkey->ameth == NULL || pkey->ameth->pkey_security_bits == NULL)
		return -2;

	return pkey->ameth->pkey_security_bits(pkey);
}
LCRYPTO_ALIAS(EVP_PKEY_security_bits);

int
EVP_PKEY_size(const EVP_PKEY *pkey)
{
	if (pkey && pkey->ameth && pkey->ameth->pkey_size)
		return pkey->ameth->pkey_size(pkey);
	return 0;
}
LCRYPTO_ALIAS(EVP_PKEY_size);

int
EVP_PKEY_save_parameters(EVP_PKEY *pkey, int mode)
{
#ifndef OPENSSL_NO_DSA
	if (pkey->type == EVP_PKEY_DSA) {
		int ret = pkey->save_parameters;

		if (mode >= 0)
			pkey->save_parameters = mode;
		return (ret);
	}
#endif
#ifndef OPENSSL_NO_EC
	if (pkey->type == EVP_PKEY_EC) {
		int ret = pkey->save_parameters;

		if (mode >= 0)
			pkey->save_parameters = mode;
		return (ret);
	}
#endif
	return (0);
}
LCRYPTO_ALIAS(EVP_PKEY_save_parameters);

int
EVP_PKEY_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from)
{
	if (to->type != from->type) {
		EVPerror(EVP_R_DIFFERENT_KEY_TYPES);
		goto err;
	}

	if (EVP_PKEY_missing_parameters(from)) {
		EVPerror(EVP_R_MISSING_PARAMETERS);
		goto err;
	}
	if (from->ameth && from->ameth->param_copy)
		return from->ameth->param_copy(to, from);

err:
	return 0;
}
LCRYPTO_ALIAS(EVP_PKEY_copy_parameters);

int
EVP_PKEY_missing_parameters(const EVP_PKEY *pkey)
{
	if (pkey->ameth && pkey->ameth->param_missing)
		return pkey->ameth->param_missing(pkey);
	return 0;
}
LCRYPTO_ALIAS(EVP_PKEY_missing_parameters);

int
EVP_PKEY_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (a->type != b->type)
		return -1;
	if (a->ameth && a->ameth->param_cmp)
		return a->ameth->param_cmp(a, b);
	return -2;
}
LCRYPTO_ALIAS(EVP_PKEY_cmp_parameters);

int
EVP_PKEY_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
	if (a->type != b->type)
		return -1;

	if (a->ameth) {
		int ret;
		/* Compare parameters if the algorithm has them */
		if (a->ameth->param_cmp) {
			ret = a->ameth->param_cmp(a, b);
			if (ret <= 0)
				return ret;
		}

		if (a->ameth->pub_cmp)
			return a->ameth->pub_cmp(a, b);
	}

	return -2;
}
LCRYPTO_ALIAS(EVP_PKEY_cmp);

EVP_PKEY *
EVP_PKEY_new(void)
{
	EVP_PKEY *pkey;

	if ((pkey = calloc(1, sizeof(*pkey))) == NULL) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	pkey->type = EVP_PKEY_NONE;
	pkey->references = 1;
	pkey->save_parameters = 1;

	return pkey;
}
LCRYPTO_ALIAS(EVP_PKEY_new);

int
EVP_PKEY_up_ref(EVP_PKEY *pkey)
{
	return CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY) > 1;
}
LCRYPTO_ALIAS(EVP_PKEY_up_ref);

static void
evp_pkey_free_pkey_ptr(EVP_PKEY *pkey)
{
	if (pkey == NULL || pkey->ameth == NULL || pkey->ameth->pkey_free == NULL)
		return;

	pkey->ameth->pkey_free(pkey);
	pkey->pkey.ptr = NULL;
}

void
EVP_PKEY_free(EVP_PKEY *pkey)
{
	if (pkey == NULL)
		return;

	if (CRYPTO_add(&pkey->references, -1, CRYPTO_LOCK_EVP_PKEY) > 0)
		return;

	evp_pkey_free_pkey_ptr(pkey);
	freezero(pkey, sizeof(*pkey));
}
LCRYPTO_ALIAS(EVP_PKEY_free);

int
EVP_PKEY_set_type(EVP_PKEY *pkey, int type)
{
	const EVP_PKEY_ASN1_METHOD *ameth;

	evp_pkey_free_pkey_ptr(pkey);

	if ((ameth = EVP_PKEY_asn1_find(NULL, type)) == NULL) {
		EVPerror(EVP_R_UNSUPPORTED_ALGORITHM);
		return 0;
	}
	if (pkey != NULL) {
		pkey->ameth = ameth;
		pkey->type = pkey->ameth->pkey_id;
	}

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_set_type);

int
EVP_PKEY_set_type_str(EVP_PKEY *pkey, const char *str, int len)
{
	const EVP_PKEY_ASN1_METHOD *ameth;

	evp_pkey_free_pkey_ptr(pkey);

	if ((ameth = EVP_PKEY_asn1_find_str(NULL, str, len)) == NULL) {
		EVPerror(EVP_R_UNSUPPORTED_ALGORITHM);
		return 0;
	}
	if (pkey != NULL) {
		pkey->ameth = ameth;
		pkey->type = pkey->ameth->pkey_id;
	}

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_set_type_str);

int
EVP_PKEY_assign(EVP_PKEY *pkey, int type, void *key)
{
	if (!EVP_PKEY_set_type(pkey, type))
		return 0;

	return (pkey->pkey.ptr = key) != NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_assign);

EVP_PKEY *
EVP_PKEY_new_raw_private_key(int type, ENGINE *engine,
    const unsigned char *private_key, size_t len)
{
	EVP_PKEY *pkey;

	if ((pkey = EVP_PKEY_new()) == NULL)
		goto err;

	if (!EVP_PKEY_set_type(pkey, type))
		goto err;

	if (pkey->ameth->set_priv_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		goto err;
	}
	if (!pkey->ameth->set_priv_key(pkey, private_key, len)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	return pkey;

 err:
	EVP_PKEY_free(pkey);

	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_new_raw_private_key);

EVP_PKEY *
EVP_PKEY_new_raw_public_key(int type, ENGINE *engine,
    const unsigned char *public_key, size_t len)
{
	EVP_PKEY *pkey;

	if ((pkey = EVP_PKEY_new()) == NULL)
		goto err;

	if (!EVP_PKEY_set_type(pkey, type))
		goto err;

	if (pkey->ameth->set_pub_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		goto err;
	}
	if (!pkey->ameth->set_pub_key(pkey, public_key, len)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	return pkey;

 err:
	EVP_PKEY_free(pkey);

	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_new_raw_public_key);

int
EVP_PKEY_get_raw_private_key(const EVP_PKEY *pkey,
    unsigned char *out_private_key, size_t *out_len)
{
	if (pkey->ameth->get_priv_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return 0;
	}
	if (!pkey->ameth->get_priv_key(pkey, out_private_key, out_len)) {
		EVPerror(EVP_R_GET_RAW_KEY_FAILED);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_get_raw_private_key);

int
EVP_PKEY_get_raw_public_key(const EVP_PKEY *pkey,
    unsigned char *out_public_key, size_t *out_len)
{
	if (pkey->ameth->get_pub_key == NULL) {
		EVPerror(EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE);
		return 0;
	}
	if (!pkey->ameth->get_pub_key(pkey, out_public_key, out_len)) {
		EVPerror(EVP_R_GET_RAW_KEY_FAILED);
		return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(EVP_PKEY_get_raw_public_key);

EVP_PKEY *
EVP_PKEY_new_CMAC_key(ENGINE *e, const unsigned char *priv, size_t len,
    const EVP_CIPHER *cipher)
{
	EVP_PKEY *pkey = NULL;
	CMAC_CTX *cmctx = NULL;

	if ((pkey = EVP_PKEY_new()) == NULL)
		goto err;
	if ((cmctx = CMAC_CTX_new()) == NULL)
		goto err;

	if (!EVP_PKEY_set_type(pkey, EVP_PKEY_CMAC))
		goto err;

	if (!CMAC_Init(cmctx, priv, len, cipher, NULL)) {
		EVPerror(EVP_R_KEY_SETUP_FAILED);
		goto err;
	}

	pkey->pkey.ptr = cmctx;

	return pkey;

 err:
	EVP_PKEY_free(pkey);
	CMAC_CTX_free(cmctx);

	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_new_CMAC_key);

void *
EVP_PKEY_get0(const EVP_PKEY *pkey)
{
	return pkey->pkey.ptr;
}
LCRYPTO_ALIAS(EVP_PKEY_get0);

const unsigned char *
EVP_PKEY_get0_hmac(const EVP_PKEY *pkey, size_t *len)
{
	ASN1_OCTET_STRING *os;

	if (pkey->type != EVP_PKEY_HMAC) {
		EVPerror(EVP_R_EXPECTING_AN_HMAC_KEY);
		return NULL;
	}

	os = EVP_PKEY_get0(pkey);
	*len = os->length;

	return os->data;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_hmac);

#ifndef OPENSSL_NO_RSA
RSA *
EVP_PKEY_get0_RSA(const EVP_PKEY *pkey)
{
	if (pkey->type == EVP_PKEY_RSA || pkey->type == EVP_PKEY_RSA_PSS)
		return pkey->pkey.rsa;

	EVPerror(EVP_R_EXPECTING_AN_RSA_KEY);
	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_RSA);

RSA *
EVP_PKEY_get1_RSA(const EVP_PKEY *pkey)
{
	RSA *rsa;

	if ((rsa = EVP_PKEY_get0_RSA(pkey)) == NULL)
		return NULL;

	RSA_up_ref(rsa);

	return rsa;
}
LCRYPTO_ALIAS(EVP_PKEY_get1_RSA);

int
EVP_PKEY_set1_RSA(EVP_PKEY *pkey, RSA *key)
{
	int ret = EVP_PKEY_assign_RSA(pkey, key);
	if (ret != 0)
		RSA_up_ref(key);
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_set1_RSA);
#endif

#ifndef OPENSSL_NO_DSA
DSA *
EVP_PKEY_get0_DSA(const EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_DSA) {
		EVPerror(EVP_R_EXPECTING_A_DSA_KEY);
		return NULL;
	}
	return pkey->pkey.dsa;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_DSA);

DSA *
EVP_PKEY_get1_DSA(const EVP_PKEY *pkey)
{
	DSA *dsa;

	if ((dsa = EVP_PKEY_get0_DSA(pkey)) == NULL)
		return NULL;

	DSA_up_ref(dsa);

	return dsa;
}
LCRYPTO_ALIAS(EVP_PKEY_get1_DSA);

int
EVP_PKEY_set1_DSA(EVP_PKEY *pkey, DSA *key)
{
	int ret = EVP_PKEY_assign_DSA(pkey, key);
	if (ret != 0)
		DSA_up_ref(key);
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_set1_DSA);
#endif

#ifndef OPENSSL_NO_EC
EC_KEY *
EVP_PKEY_get0_EC_KEY(const EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_EC) {
		EVPerror(EVP_R_EXPECTING_A_EC_KEY);
		return NULL;
	}
	return pkey->pkey.ec;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_EC_KEY);

EC_KEY *
EVP_PKEY_get1_EC_KEY(const EVP_PKEY *pkey)
{
	EC_KEY *key;

	if ((key = EVP_PKEY_get0_EC_KEY(pkey)) == NULL)
		return NULL;

	EC_KEY_up_ref(key);

	return key;
}
LCRYPTO_ALIAS(EVP_PKEY_get1_EC_KEY);

int
EVP_PKEY_set1_EC_KEY(EVP_PKEY *pkey, EC_KEY *key)
{
	int ret = EVP_PKEY_assign_EC_KEY(pkey, key);
	if (ret != 0)
		EC_KEY_up_ref(key);
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_set1_EC_KEY);
#endif


#ifndef OPENSSL_NO_DH
DH *
EVP_PKEY_get0_DH(const EVP_PKEY *pkey)
{
	if (pkey->type != EVP_PKEY_DH) {
		EVPerror(EVP_R_EXPECTING_A_DH_KEY);
		return NULL;
	}
	return pkey->pkey.dh;
}
LCRYPTO_ALIAS(EVP_PKEY_get0_DH);

DH *
EVP_PKEY_get1_DH(const EVP_PKEY *pkey)
{
	DH *dh;

	if ((dh = EVP_PKEY_get0_DH(pkey)) == NULL)
		return NULL;

	DH_up_ref(dh);

	return dh;
}
LCRYPTO_ALIAS(EVP_PKEY_get1_DH);

int
EVP_PKEY_set1_DH(EVP_PKEY *pkey, DH *key)
{
	int ret = EVP_PKEY_assign_DH(pkey, key);
	if (ret != 0)
		DH_up_ref(key);
	return ret;
}
LCRYPTO_ALIAS(EVP_PKEY_set1_DH);
#endif

int
EVP_PKEY_type(int type)
{
	const EVP_PKEY_ASN1_METHOD *ameth;

	if ((ameth = EVP_PKEY_asn1_find(NULL, type)) != NULL)
		return ameth->pkey_id;

	return NID_undef;
}
LCRYPTO_ALIAS(EVP_PKEY_type);

int
EVP_PKEY_id(const EVP_PKEY *pkey)
{
	return pkey->type;
}
LCRYPTO_ALIAS(EVP_PKEY_id);

int
EVP_PKEY_base_id(const EVP_PKEY *pkey)
{
	return EVP_PKEY_type(pkey->type);
}
LCRYPTO_ALIAS(EVP_PKEY_base_id);

static int
unsup_alg(BIO *out, const EVP_PKEY *pkey, int indent, const char *kstr)
{
	if (!BIO_indent(out, indent, 128))
		return 0;
	BIO_printf(out, "%s algorithm \"%s\" unsupported\n",
	    kstr, OBJ_nid2ln(pkey->type));
	return 1;
}

int
EVP_PKEY_print_public(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->pub_print)
		return pkey->ameth->pub_print(out, pkey, indent, pctx);

	return unsup_alg(out, pkey, indent, "Public Key");
}
LCRYPTO_ALIAS(EVP_PKEY_print_public);

int
EVP_PKEY_print_private(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->priv_print)
		return pkey->ameth->priv_print(out, pkey, indent, pctx);

	return unsup_alg(out, pkey, indent, "Private Key");
}
LCRYPTO_ALIAS(EVP_PKEY_print_private);

int
EVP_PKEY_print_params(BIO *out, const EVP_PKEY *pkey, int indent,
    ASN1_PCTX *pctx)
{
	if (pkey->ameth && pkey->ameth->param_print)
		return pkey->ameth->param_print(out, pkey, indent, pctx);
	return unsup_alg(out, pkey, indent, "Parameters");
}
LCRYPTO_ALIAS(EVP_PKEY_print_params);

int
EVP_PKEY_get_default_digest_nid(EVP_PKEY *pkey, int *pnid)
{
	if (!pkey->ameth || !pkey->ameth->pkey_ctrl)
		return -2;
	return pkey->ameth->pkey_ctrl(pkey, ASN1_PKEY_CTRL_DEFAULT_MD_NID,
	    0, pnid);
}
LCRYPTO_ALIAS(EVP_PKEY_get_default_digest_nid);
