/* $OpenBSD: a_pubkey.c,v 1.8 2025/05/10 05:54:38 tb Exp $ */
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

EVP_PKEY *
d2i_PublicKey(int type, EVP_PKEY **a, const unsigned char **pp, long length)
{
	EVP_PKEY *ret;

	if ((a == NULL) || (*a == NULL)) {
		if ((ret = EVP_PKEY_new()) == NULL) {
			ASN1error(ERR_R_EVP_LIB);
			return (NULL);
		}
	} else
		ret = *a;

	if (!EVP_PKEY_set_type(ret, type)) {
		ASN1error(ERR_R_EVP_LIB);
		goto err;
	}

	switch (EVP_PKEY_id(ret)) {
#ifndef OPENSSL_NO_RSA
	case EVP_PKEY_RSA:
		if ((ret->pkey.rsa = d2i_RSAPublicKey(NULL, pp, length)) ==
		    NULL) {
			ASN1error(ERR_R_ASN1_LIB);
			goto err;
		}
		break;
#endif
#ifndef OPENSSL_NO_DSA
	case EVP_PKEY_DSA:
		if (!d2i_DSAPublicKey(&(ret->pkey.dsa), pp, length)) {
			ASN1error(ERR_R_ASN1_LIB);
			goto err;
		}
		break;
#endif
#ifndef OPENSSL_NO_EC
	case EVP_PKEY_EC:
		if (!o2i_ECPublicKey(&(ret->pkey.ec), pp, length)) {
			ASN1error(ERR_R_ASN1_LIB);
			goto err;
		}
		break;
#endif
	default:
		ASN1error(ASN1_R_UNKNOWN_PUBLIC_KEY_TYPE);
		goto err;
		/* break; */
	}
	if (a != NULL)
		(*a) = ret;
	return (ret);

 err:
	if (a == NULL || *a != ret)
		EVP_PKEY_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(d2i_PublicKey);

int
i2d_PublicKey(EVP_PKEY *a, unsigned char **pp)
{
	switch (a->type) {
#ifndef OPENSSL_NO_RSA
	case EVP_PKEY_RSA:
		return (i2d_RSAPublicKey(a->pkey.rsa, pp));
#endif
#ifndef OPENSSL_NO_DSA
	case EVP_PKEY_DSA:
		return (i2d_DSAPublicKey(a->pkey.dsa, pp));
#endif
#ifndef OPENSSL_NO_EC
	case EVP_PKEY_EC:
		return (i2o_ECPublicKey(a->pkey.ec, pp));
#endif
	default:
		ASN1error(ASN1_R_UNSUPPORTED_PUBLIC_KEY_TYPE);
		return (-1);
	}
}
LCRYPTO_ALIAS(i2d_PublicKey);
