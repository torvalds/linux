/* $OpenBSD: a_pkey.c,v 1.9 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"

EVP_PKEY *
d2i_PrivateKey(int type, EVP_PKEY **a, const unsigned char **pp, long length)
{
	const unsigned char *p = *pp;
	EVP_PKEY *ret;

	if ((a == NULL) || (*a == NULL)) {
		if ((ret = EVP_PKEY_new()) == NULL) {
			ASN1error(ERR_R_EVP_LIB);
			return (NULL);
		}
	} else {
		ret = *a;
	}

	if (!EVP_PKEY_set_type(ret, type)) {
		ASN1error(ASN1_R_UNKNOWN_PUBLIC_KEY_TYPE);
		goto err;
	}

	if (!ret->ameth->old_priv_decode ||
	    !ret->ameth->old_priv_decode(ret, pp, length)) {
		if (ret->ameth->priv_decode) {
			PKCS8_PRIV_KEY_INFO *p8 = NULL;
			*pp = p; /* XXX */
			p8 = d2i_PKCS8_PRIV_KEY_INFO(NULL, pp, length);
			if (!p8)
				goto err;
			EVP_PKEY_free(ret);
			ret = EVP_PKCS82PKEY(p8);
			PKCS8_PRIV_KEY_INFO_free(p8);
		} else {
			ASN1error(ERR_R_ASN1_LIB);
			goto err;
		}
	}
	if (a != NULL)
		(*a) = ret;
	return (ret);

 err:
	if (a == NULL || *a != ret)
		EVP_PKEY_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(d2i_PrivateKey);

int
i2d_PrivateKey(EVP_PKEY *a, unsigned char **pp)
{
	if (a->ameth && a->ameth->old_priv_encode) {
		return a->ameth->old_priv_encode(a, pp);
	}
	if (a->ameth && a->ameth->priv_encode) {
		PKCS8_PRIV_KEY_INFO *p8 = EVP_PKEY2PKCS8(a);
		int ret = i2d_PKCS8_PRIV_KEY_INFO(p8, pp);
		PKCS8_PRIV_KEY_INFO_free(p8);
		return ret;
	}
	ASN1error(ASN1_R_UNSUPPORTED_PUBLIC_KEY_TYPE);
	return (-1);
}
LCRYPTO_ALIAS(i2d_PrivateKey);

/* This works like d2i_PrivateKey() except it automatically works out the type */

EVP_PKEY *
d2i_AutoPrivateKey(EVP_PKEY **a, const unsigned char **pp, long length)
{
	STACK_OF(ASN1_TYPE) *inkey;
	const unsigned char *p;
	int keytype;

	p = *pp;
	/* Dirty trick: read in the ASN1 data into a STACK_OF(ASN1_TYPE):
	 * by analyzing it we can determine the passed structure: this
	 * assumes the input is surrounded by an ASN1 SEQUENCE.
	 */
	inkey = d2i_ASN1_SEQUENCE_ANY(NULL, &p, length);
	/* Since we only need to discern "traditional format" RSA and DSA
	 * keys we can just count the elements.
         */
	if (sk_ASN1_TYPE_num(inkey) == 6)
		keytype = EVP_PKEY_DSA;
	else if (sk_ASN1_TYPE_num(inkey) == 4)
		keytype = EVP_PKEY_EC;
	else if (sk_ASN1_TYPE_num(inkey) == 3)  {
		/* This seems to be PKCS8, not traditional format */
		PKCS8_PRIV_KEY_INFO *p8 = d2i_PKCS8_PRIV_KEY_INFO(
		    NULL, pp, length);
		EVP_PKEY *ret;

		sk_ASN1_TYPE_pop_free(inkey, ASN1_TYPE_free);
		if (!p8) {
			ASN1error(ASN1_R_UNSUPPORTED_PUBLIC_KEY_TYPE);
			return NULL;
		}
		ret = EVP_PKCS82PKEY(p8);
		PKCS8_PRIV_KEY_INFO_free(p8);
		if (a) {
			*a = ret;
		}
		return ret;
	} else
		keytype = EVP_PKEY_RSA;
	sk_ASN1_TYPE_pop_free(inkey, ASN1_TYPE_free);
	return d2i_PrivateKey(keytype, a, pp, length);
}
LCRYPTO_ALIAS(d2i_AutoPrivateKey);
