/* $OpenBSD: p12_npas.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
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
#include <stdlib.h>
#include <string.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>

#include "err_local.h"
#include "pkcs12_local.h"
#include "x509_local.h"

/* PKCS#12 password change routine */

static int
alg_get(X509_ALGOR *alg, int *nid, int *iter, int *salt_len)
{
	const ASN1_OBJECT *aobj;
	int param_type;
	const void *param;
	PBEPARAM *pbe = NULL;
	int ret = 0;

	*nid = *iter = *salt_len = 0;

	X509_ALGOR_get0(&aobj, &param_type, &param, alg);
	if (param_type != V_ASN1_SEQUENCE)
		goto err;
	if ((pbe = ASN1_item_unpack(param, &PBEPARAM_it)) == NULL)
		goto err;

	/* XXX - can we validate these somehow? */
	*nid = OBJ_obj2nid(alg->algorithm);
	*iter = ASN1_INTEGER_get(pbe->iter);
	*salt_len = pbe->salt->length;

	ret = 1;

 err:
	PBEPARAM_free(pbe);

	return ret;
}

/* Change password of safebag: only needs handle shrouded keybags */
static int
newpass_bag(PKCS12_SAFEBAG *bag, const char *oldpass, const char *newpass)
{
	PKCS8_PRIV_KEY_INFO *p8 = NULL;
	X509_SIG *keybag;
	int nid, salt_len, iter;
	int ret = 0;

	if (OBJ_obj2nid(bag->type) != NID_pkcs8ShroudedKeyBag)
		goto done;

	if ((p8 = PKCS8_decrypt(bag->value.shkeybag, oldpass, -1)) == NULL)
		goto err;
	if (!alg_get(bag->value.shkeybag->algor, &nid, &iter, &salt_len))
		goto err;

	if ((keybag = PKCS8_encrypt(nid, NULL, newpass, -1, NULL, salt_len,
	    iter, p8)) == NULL)
		goto err;

	X509_SIG_free(bag->value.shkeybag);
	bag->value.shkeybag = keybag;

 done:
	ret = 1;

 err:
	PKCS8_PRIV_KEY_INFO_free(p8);

	return ret;
}

static int
newpass_bags(STACK_OF(PKCS12_SAFEBAG) *bags, const char *oldpass,
    const char *newpass)
{
	int i;

	for (i = 0; i < sk_PKCS12_SAFEBAG_num(bags); i++) {
		PKCS12_SAFEBAG *bag = sk_PKCS12_SAFEBAG_value(bags, i);

		if (!newpass_bag(bag, oldpass, newpass))
			return 0;
	}

	return 1;
}

static int
pkcs7_repack_data(PKCS7 *pkcs7, STACK_OF(PKCS7) *safes, const char *oldpass,
    const char *newpass)
{
	STACK_OF(PKCS12_SAFEBAG) *bags;
	PKCS7 *data = NULL;
	int ret = 0;

	if ((bags = PKCS12_unpack_p7data(pkcs7)) == NULL)
		goto err;
	if (!newpass_bags(bags, oldpass, newpass))
		goto err;
	if ((data = PKCS12_pack_p7data(bags)) == NULL)
		goto err;
	if (sk_PKCS7_push(safes, data) == 0)
		goto err;
	data = NULL;

	ret = 1;

 err:
	sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	PKCS7_free(data);

	return ret;
}

static int
pkcs7_repack_encdata(PKCS7 *pkcs7, STACK_OF(PKCS7) *safes, const char *oldpass,
    const char *newpass)
{
	STACK_OF(PKCS12_SAFEBAG) *bags;
	int nid, iter, salt_len;
	PKCS7 *data = NULL;
	int ret = 0;

	if ((bags = PKCS12_unpack_p7encdata(pkcs7, oldpass, -1)) == NULL)
		goto err;
	if (!alg_get(pkcs7->d.encrypted->enc_data->algorithm, &nid,
	    &iter, &salt_len))
		goto err;
	if (!newpass_bags(bags, oldpass, newpass))
		goto err;
	if ((data = PKCS12_pack_p7encdata(nid, newpass, -1, NULL, salt_len,
	    iter, bags)) == NULL)
		goto err;
	if (!sk_PKCS7_push(safes, data))
		goto err;
	data = NULL;

	ret = 1;

 err:
	sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
	PKCS7_free(data);

	return ret;
}

static int
pkcs12_repack_authsafes(PKCS12 *pkcs12, STACK_OF(PKCS7) *safes,
    const char *newpass)
{
	ASN1_OCTET_STRING *old_data;
	ASN1_OCTET_STRING *new_mac = NULL;
	unsigned char mac[EVP_MAX_MD_SIZE];
	unsigned int mac_len;
	int ret = 0;

	if ((old_data = pkcs12->authsafes->d.data) == NULL)
		goto err;
	if ((pkcs12->authsafes->d.data = ASN1_OCTET_STRING_new()) == NULL)
		goto err;
	if (!PKCS12_pack_authsafes(pkcs12, safes))
		goto err;
	if (!PKCS12_gen_mac(pkcs12, newpass, -1, mac, &mac_len))
		goto err;
	if ((new_mac = ASN1_OCTET_STRING_new()) == NULL)
		goto err;
	if (!ASN1_OCTET_STRING_set(new_mac, mac, mac_len))
		goto err;

	ASN1_OCTET_STRING_free(pkcs12->mac->dinfo->digest);
	pkcs12->mac->dinfo->digest = new_mac;
	new_mac = NULL;

	ASN1_OCTET_STRING_free(old_data);
	old_data = NULL;

	ret = 1;

 err:
	if (old_data != NULL) {
		ASN1_OCTET_STRING_free(pkcs12->authsafes->d.data);
		pkcs12->authsafes->d.data = old_data;
	}
	explicit_bzero(mac, sizeof(mac));
	ASN1_OCTET_STRING_free(new_mac);

	return ret;
}

int
PKCS12_newpass(PKCS12 *pkcs12, const char *oldpass, const char *newpass)
{
	STACK_OF(PKCS7) *authsafes = NULL, *safes = NULL;
	int i;
	int ret = 0;

	if (pkcs12 == NULL) {
		PKCS12error(PKCS12_R_INVALID_NULL_PKCS12_POINTER);
		goto err;
	}

	if (!PKCS12_verify_mac(pkcs12, oldpass, -1)) {
		PKCS12error(PKCS12_R_MAC_VERIFY_FAILURE);
		goto err;
	}

	if ((authsafes = PKCS12_unpack_authsafes(pkcs12)) == NULL)
		goto err;
	if ((safes = sk_PKCS7_new_null()) == NULL)
		goto err;

	for (i = 0; i < sk_PKCS7_num(authsafes); i++) {
		PKCS7 *pkcs7 = sk_PKCS7_value(authsafes, i);

		switch (OBJ_obj2nid(pkcs7->type)) {
		case NID_pkcs7_data:
			if (pkcs7_repack_data(pkcs7, safes, oldpass, newpass))
				goto err;
			break;
		case NID_pkcs7_encrypted:
			if (pkcs7_repack_encdata(pkcs7, safes, oldpass, newpass))
				goto err;
			break;
		}
	}

	if (!pkcs12_repack_authsafes(pkcs12, safes, newpass))
		goto err;

	ret = 1;

 err:
	sk_PKCS7_pop_free(authsafes, PKCS7_free);
	sk_PKCS7_pop_free(safes, PKCS7_free);

	return ret;
}
LCRYPTO_ALIAS(PKCS12_newpass);
