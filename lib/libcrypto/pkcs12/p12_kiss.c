/* $OpenBSD: p12_kiss.c,v 1.29 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <stdio.h>

#include <openssl/pkcs12.h>

#include "err_local.h"
#include "pkcs12_local.h"

/* Simplified PKCS#12 routines */

static int parse_pk12( PKCS12 *p12, const char *pass, int passlen,
    EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

static int parse_bags( STACK_OF(PKCS12_SAFEBAG) *bags, const char *pass,
    int passlen, EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

static int parse_bag( PKCS12_SAFEBAG *bag, const char *pass, int passlen,
    EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

/* Parse and decrypt a PKCS#12 structure returning user key, user cert
 * and other (CA) certs. Note either ca should be NULL, *ca should be NULL,
 * or it should point to a valid STACK structure. pkey and cert can be
 * passed uninitialised.
 */

int
PKCS12_parse(PKCS12 *p12, const char *pass, EVP_PKEY **pkey, X509 **cert,
    STACK_OF(X509) **ca)
{
	STACK_OF(X509) *ocerts = NULL;
	X509 *x = NULL;

	if (pkey != NULL)
		*pkey = NULL;
	if (cert != NULL)
		*cert = NULL;

	if (p12 == NULL) {
		PKCS12error(PKCS12_R_INVALID_NULL_PKCS12_POINTER);
		goto err;
	}

	/* Check the mac */

	/* If password is zero length or NULL then try verifying both cases
	 * to determine which password is correct. The reason for this is that
	 * under PKCS#12 password based encryption no password and a zero length
	 * password are two different things...
	 */

	if (pass == NULL || *pass == '\0') {
		if (PKCS12_verify_mac(p12, NULL, 0))
			pass = NULL;
		else if (PKCS12_verify_mac(p12, "", 0))
			pass = "";
		else {
			PKCS12error(PKCS12_R_MAC_VERIFY_FAILURE);
			goto err;
		}
	} else if (!PKCS12_verify_mac(p12, pass, -1)) {
		PKCS12error(PKCS12_R_MAC_VERIFY_FAILURE);
		goto err;
	}

	/* Allocate stack for other certificates */
	if ((ocerts = sk_X509_new_null()) == NULL) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!parse_pk12(p12, pass, -1, pkey, ocerts)) {
		PKCS12error(PKCS12_R_PARSE_ERROR);
		goto err;
	}

	while ((x = sk_X509_pop(ocerts)) != NULL) {
		if (pkey != NULL && *pkey != NULL &&
		    cert != NULL && *cert == NULL) {
			ERR_set_mark();
			if (X509_check_private_key(x, *pkey)) {
				*cert = x;
				x = NULL;
			}
			ERR_pop_to_mark();
		}

		if (ca != NULL && x != NULL) {
			if (*ca == NULL)
				*ca = sk_X509_new_null();
			if (*ca == NULL)
				goto err;
			if (!sk_X509_push(*ca, x))
				goto err;
			x = NULL;
		}
		X509_free(x);
		x = NULL;
	}

	sk_X509_pop_free(ocerts, X509_free);

	return 1;

err:
	if (pkey != NULL)
		EVP_PKEY_free(*pkey);
	if (cert != NULL)
		X509_free(*cert);
	X509_free(x);
	sk_X509_pop_free(ocerts, X509_free);

	return 0;
}
LCRYPTO_ALIAS(PKCS12_parse);

/* Parse the outer PKCS#12 structure */

static int
parse_pk12(PKCS12 *p12, const char *pass, int passlen, EVP_PKEY **pkey,
    STACK_OF(X509) *ocerts)
{
	STACK_OF(PKCS7) *asafes;
	STACK_OF(PKCS12_SAFEBAG) *bags;
	int i, bagnid;
	PKCS7 *p7;

	if (!(asafes = PKCS12_unpack_authsafes(p12)))
		return 0;
	for (i = 0; i < sk_PKCS7_num(asafes); i++) {
		p7 = sk_PKCS7_value(asafes, i);
		bagnid = OBJ_obj2nid(p7->type);
		if (bagnid == NID_pkcs7_data) {
			bags = PKCS12_unpack_p7data(p7);
		} else if (bagnid == NID_pkcs7_encrypted) {
			bags = PKCS12_unpack_p7encdata(p7, pass, passlen);
		} else
			continue;
		if (!bags) {
			sk_PKCS7_pop_free(asafes, PKCS7_free);
			return 0;
		}
		if (!parse_bags(bags, pass, passlen, pkey, ocerts)) {
			sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
			sk_PKCS7_pop_free(asafes, PKCS7_free);
			return 0;
		}
		sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	}
	sk_PKCS7_pop_free(asafes, PKCS7_free);
	return 1;
}

static int
parse_bags(STACK_OF(PKCS12_SAFEBAG) *bags, const char *pass, int passlen,
    EVP_PKEY **pkey, STACK_OF(X509) *ocerts)
{
	int i;

	for (i = 0; i < sk_PKCS12_SAFEBAG_num(bags); i++) {
		if (!parse_bag(sk_PKCS12_SAFEBAG_value(bags, i), pass, passlen,
		    pkey, ocerts))
			return 0;
	}
	return 1;
}

static int
parse_bag(PKCS12_SAFEBAG *bag, const char *pass, int passlen, EVP_PKEY **pkey,
    STACK_OF(X509) *ocerts)
{
	PKCS8_PRIV_KEY_INFO *p8;
	X509 *x509;
	const ASN1_TYPE *attrib;
	ASN1_BMPSTRING *fname = NULL;
	ASN1_OCTET_STRING *lkid = NULL;

	if ((attrib = PKCS12_SAFEBAG_get0_attr(bag, NID_friendlyName)))
		fname = attrib->value.bmpstring;

	if ((attrib = PKCS12_SAFEBAG_get0_attr(bag, NID_localKeyID)))
		lkid = attrib->value.octet_string;

	switch (OBJ_obj2nid(bag->type)) {
	case NID_keyBag:
		if (!pkey || *pkey)
			return 1;
		if (!(*pkey = EVP_PKCS82PKEY(bag->value.keybag)))
			return 0;
		break;

	case NID_pkcs8ShroudedKeyBag:
		if (!pkey || *pkey)
			return 1;
		if (!(p8 = PKCS12_decrypt_skey(bag, pass, passlen)))
			return 0;
		*pkey = EVP_PKCS82PKEY(p8);
		PKCS8_PRIV_KEY_INFO_free(p8);
		if (!(*pkey))
			return 0;
		break;

	case NID_certBag:
		if (OBJ_obj2nid(bag->value.bag->type) != NID_x509Certificate )
			return 1;
		if (!(x509 = PKCS12_certbag2x509(bag)))
			return 0;
		if (lkid && !X509_keyid_set1(x509, lkid->data, lkid->length)) {
			X509_free(x509);
			return 0;
		}
		if (fname) {
			int len, r;
			unsigned char *data = NULL;
			len = ASN1_STRING_to_UTF8(&data, fname);
			if (len >= 0) {
				r = X509_alias_set1(x509, data, len);
				free(data);
				if (!r) {
					X509_free(x509);
					return 0;
				}
			}
		}

		if (!sk_X509_push(ocerts, x509)) {
			X509_free(x509);
			return 0;
		}

		break;

	case NID_safeContentsBag:
		return parse_bags(bag->value.safes, pass, passlen,
		    pkey, ocerts);
		break;

	default:
		return 1;
		break;
	}
	return 1;
}
