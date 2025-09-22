/* $OpenBSD: p12_crt.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 1999-2002 The OpenSSL Project.  All rights reserved.
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
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"
#include "pkcs12_local.h"
#include "x509_local.h"

static int pkcs12_add_bag(STACK_OF(PKCS12_SAFEBAG) **pbags,
    PKCS12_SAFEBAG *bag);

PKCS12 *
PKCS12_create(const char *pass, const char *name, EVP_PKEY *pkey, X509 *cert,
    STACK_OF(X509) *ca, int nid_key, int nid_cert, int iter, int mac_iter,
    int keytype)
{
	PKCS12 *p12 = NULL;
	STACK_OF(PKCS7) *safes = NULL;
	STACK_OF(PKCS12_SAFEBAG) *bags = NULL;
	PKCS12_SAFEBAG *bag = NULL;
	int i;
	unsigned char keyid[EVP_MAX_MD_SIZE];
	unsigned int keyidlen = 0;

	/* Set defaults */
	if (!nid_cert) {
		nid_cert = NID_pbe_WithSHA1And40BitRC2_CBC;
	}
	if (!nid_key)
		nid_key = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
	if (!iter)
		iter = PKCS12_DEFAULT_ITER;
	if (!mac_iter)
		mac_iter = 1;

	if (!pkey && !cert && !ca) {
		PKCS12error(PKCS12_R_INVALID_NULL_ARGUMENT);
		return NULL;
	}

	if (pkey && cert) {
		if (!X509_check_private_key(cert, pkey))
			return NULL;
		if (!X509_digest(cert, EVP_sha1(), keyid, &keyidlen))
			return NULL;
	}

	if (cert) {
		bag = PKCS12_add_cert(&bags, cert);
		if (name && !PKCS12_add_friendlyname(bag, name, -1))
			goto err;
		if (keyidlen && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
			goto err;
	}

	/* Add all other certificates */
	for (i = 0; i < sk_X509_num(ca); i++) {
		if (!PKCS12_add_cert(&bags, sk_X509_value(ca, i)))
			goto err;
	}

	if (bags && !PKCS12_add_safe(&safes, bags, nid_cert, iter, pass))
		goto err;

	sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	bags = NULL;

	if (pkey) {
		bag = PKCS12_add_key(&bags, pkey, keytype, iter, nid_key, pass);

		if (!bag)
			goto err;

		if (name && !PKCS12_add_friendlyname(bag, name, -1))
			goto err;
		if (keyidlen && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
			goto err;
	}

	if (bags && !PKCS12_add_safe(&safes, bags, -1, 0, NULL))
		goto err;

	sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	bags = NULL;

	p12 = PKCS12_add_safes(safes, 0);

	if (!p12)
		goto err;

	sk_PKCS7_pop_free(safes, PKCS7_free);

	safes = NULL;

	if ((mac_iter != -1) &&
	    !PKCS12_set_mac(p12, pass, -1, NULL, 0, mac_iter, NULL))
		goto err;

	return p12;

err:
	if (p12)
		PKCS12_free(p12);
	if (safes)
		sk_PKCS7_pop_free(safes, PKCS7_free);
	if (bags)
		sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	return NULL;
}
LCRYPTO_ALIAS(PKCS12_create);

PKCS12_SAFEBAG *
PKCS12_add_cert(STACK_OF(PKCS12_SAFEBAG) **pbags, X509 *cert)
{
	PKCS12_SAFEBAG *bag = NULL;
	char *name;
	int namelen = -1;
	unsigned char *keyid;
	int keyidlen = -1;

	/* Add user certificate */
	if (!(bag = PKCS12_x5092certbag(cert)))
		goto err;

	/* Use friendlyName and localKeyID in certificate.
	 * (if present)
	 */
	name = (char *)X509_alias_get0(cert, &namelen);
	if (name && !PKCS12_add_friendlyname(bag, name, namelen))
		goto err;

	keyid = X509_keyid_get0(cert, &keyidlen);

	if (keyid && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
		goto err;

	if (!pkcs12_add_bag(pbags, bag))
		goto err;

	return bag;

err:
	if (bag)
		PKCS12_SAFEBAG_free(bag);

	return NULL;
}

PKCS12_SAFEBAG *
PKCS12_add_key(STACK_OF(PKCS12_SAFEBAG) **pbags, EVP_PKEY *key, int key_usage,
    int iter, int nid_key, const char *pass)
{
	PKCS12_SAFEBAG *bag = NULL;
	PKCS8_PRIV_KEY_INFO *p8 = NULL;

	/* Make a PKCS#8 structure */
	if (!(p8 = EVP_PKEY2PKCS8(key)))
		goto err;
	if (key_usage && !PKCS8_add_keyusage(p8, key_usage))
		goto err;
	if (nid_key != -1) {
		bag = PKCS12_SAFEBAG_create_pkcs8_encrypt(nid_key, pass, -1,
		    NULL, 0, iter, p8);
		PKCS8_PRIV_KEY_INFO_free(p8);
		p8 = NULL;
	} else {
		bag = PKCS12_SAFEBAG_create0_p8inf(p8);
		if (bag != NULL)
			p8 = NULL;
	}

	if (!bag)
		goto err;

	if (!pkcs12_add_bag(pbags, bag))
		goto err;

	return bag;

err:
	if (bag)
		PKCS12_SAFEBAG_free(bag);
	if (p8)
		PKCS8_PRIV_KEY_INFO_free(p8);

	return NULL;
}

int
PKCS12_add_safe(STACK_OF(PKCS7) **psafes, STACK_OF(PKCS12_SAFEBAG) *bags,
    int nid_safe, int iter, const char *pass)
{
	PKCS7 *p7 = NULL;
	int free_safes = 0;

	if (!*psafes) {
		*psafes = sk_PKCS7_new_null();
		if (!*psafes)
			return 0;
		free_safes = 1;
	} else
		free_safes = 0;

	if (nid_safe == 0)
		nid_safe = NID_pbe_WithSHA1And40BitRC2_CBC;

	if (nid_safe == -1)
		p7 = PKCS12_pack_p7data(bags);
	else
		p7 = PKCS12_pack_p7encdata(nid_safe, pass, -1, NULL, 0,
		    iter, bags);
	if (!p7)
		goto err;

	if (!sk_PKCS7_push(*psafes, p7))
		goto err;

	return 1;

err:
	if (free_safes) {
		sk_PKCS7_free(*psafes);
		*psafes = NULL;
	}

	if (p7)
		PKCS7_free(p7);

	return 0;
}

static int
pkcs12_add_bag(STACK_OF(PKCS12_SAFEBAG) **pbags, PKCS12_SAFEBAG *bag)
{
	int free_bags;

	if (!pbags)
		return 1;
	if (!*pbags) {
		*pbags = sk_PKCS12_SAFEBAG_new_null();
		if (!*pbags)
			return 0;
		free_bags = 1;
	} else
		free_bags = 0;

	if (!sk_PKCS12_SAFEBAG_push(*pbags, bag)) {
		if (free_bags) {
			sk_PKCS12_SAFEBAG_free(*pbags);
			*pbags = NULL;
		}
		return 0;
	}

	return 1;
}

PKCS12 *
PKCS12_add_safes(STACK_OF(PKCS7) *safes, int nid_p7)
{
	PKCS12 *p12;

	if (nid_p7 <= 0)
		nid_p7 = NID_pkcs7_data;
	p12 = PKCS12_init(nid_p7);

	if (!p12)
		return NULL;

	if (!PKCS12_pack_authsafes(p12, safes)) {
		PKCS12_free(p12);
		return NULL;
	}

	return p12;
}
