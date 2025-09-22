/* $OpenBSD: p12_sbag.c,v 1.10 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL project
 * 1999-2018.
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
#include "x509_local.h"

const ASN1_TYPE *
PKCS12_SAFEBAG_get0_attr(const PKCS12_SAFEBAG *bag, int attr_nid)
{
	return PKCS12_get_attr_gen(bag->attrib, attr_nid);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_attr);

ASN1_TYPE *
PKCS8_get_attr(PKCS8_PRIV_KEY_INFO *p8, int attr_nid)
{
	return PKCS12_get_attr_gen(p8->attributes, attr_nid);
}
LCRYPTO_ALIAS(PKCS8_get_attr);

const PKCS8_PRIV_KEY_INFO *
PKCS12_SAFEBAG_get0_p8inf(const PKCS12_SAFEBAG *bag)
{
	if (PKCS12_SAFEBAG_get_nid(bag) != NID_keyBag)
		return NULL;

	return bag->value.keybag;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_p8inf);

const X509_SIG *
PKCS12_SAFEBAG_get0_pkcs8(const PKCS12_SAFEBAG *bag)
{
	if (PKCS12_SAFEBAG_get_nid(bag) != NID_pkcs8ShroudedKeyBag)
		return NULL;

	return bag->value.shkeybag;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_pkcs8);

const STACK_OF(PKCS12_SAFEBAG) *
PKCS12_SAFEBAG_get0_safes(const PKCS12_SAFEBAG *bag)
{
	if (PKCS12_SAFEBAG_get_nid(bag) != NID_safeContentsBag)
		return NULL;

	return bag->value.safes;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_safes);

const ASN1_OBJECT *
PKCS12_SAFEBAG_get0_type(const PKCS12_SAFEBAG *bag)
{
	return bag->type;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_type);

int
PKCS12_SAFEBAG_get_nid(const PKCS12_SAFEBAG *bag)
{
	return OBJ_obj2nid(bag->type);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get_nid);

int
PKCS12_SAFEBAG_get_bag_nid(const PKCS12_SAFEBAG *bag)
{
	int bag_type;

	bag_type = PKCS12_SAFEBAG_get_nid(bag);

	if (bag_type == NID_certBag || bag_type == NID_crlBag ||
	    bag_type == NID_secretBag)
		return OBJ_obj2nid(bag->value.bag->type);

	return -1;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get_bag_nid);

X509 *
PKCS12_SAFEBAG_get1_cert(const PKCS12_SAFEBAG *bag)
{
	if (OBJ_obj2nid(bag->type) != NID_certBag)
		return NULL;
	if (OBJ_obj2nid(bag->value.bag->type) != NID_x509Certificate)
		return NULL;
	return ASN1_item_unpack(bag->value.bag->value.octet, &X509_it);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get1_cert);

X509_CRL *
PKCS12_SAFEBAG_get1_crl(const PKCS12_SAFEBAG *bag)
{
	if (OBJ_obj2nid(bag->type) != NID_crlBag)
		return NULL;
	if (OBJ_obj2nid(bag->value.bag->type) != NID_x509Crl)
		return NULL;
	return ASN1_item_unpack(bag->value.bag->value.octet, &X509_CRL_it);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get1_crl);

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_create_cert(X509 *x509)
{
	return PKCS12_item_pack_safebag(x509, &X509_it,
	    NID_x509Certificate, NID_certBag);
}

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_create_crl(X509_CRL *crl)
{
	return PKCS12_item_pack_safebag(crl, &X509_CRL_it,
	    NID_x509Crl, NID_crlBag);
}

/* Turn PKCS8 object into a keybag */

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_create0_p8inf(PKCS8_PRIV_KEY_INFO *p8)
{
	PKCS12_SAFEBAG *bag;

	if ((bag = PKCS12_SAFEBAG_new()) == NULL) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	bag->type = OBJ_nid2obj(NID_keyBag);
	bag->value.keybag = p8;

	return bag;
}

/* Turn PKCS8 object into a shrouded keybag */

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_create0_pkcs8(X509_SIG *p8)
{
	PKCS12_SAFEBAG *bag;

	/* Set up the safe bag */
	if ((bag = PKCS12_SAFEBAG_new()) == NULL) {
		PKCS12error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	bag->type = OBJ_nid2obj(NID_pkcs8ShroudedKeyBag);
	bag->value.shkeybag = p8;

	return bag;
}

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_create_pkcs8_encrypt(int pbe_nid, const char *pass, int passlen,
    unsigned char *salt, int saltlen, int iter, PKCS8_PRIV_KEY_INFO *p8info)
{
	const EVP_CIPHER *pbe_ciph;
	X509_SIG *p8;
	PKCS12_SAFEBAG *bag;

	if ((pbe_ciph = EVP_get_cipherbynid(pbe_nid)) != NULL)
		pbe_nid = -1;

	if ((p8 = PKCS8_encrypt(pbe_nid, pbe_ciph, pass, passlen, salt, saltlen,
	    iter, p8info)) == NULL)
		return NULL;

	if ((bag = PKCS12_SAFEBAG_create0_pkcs8(p8)) == NULL) {
		X509_SIG_free(p8);
		return NULL;
	}

	return bag;
}
