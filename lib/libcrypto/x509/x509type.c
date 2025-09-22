/* $OpenBSD: x509type.c,v 1.24 2023/11/13 16:16:14 tb Exp $ */
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

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>

#include "evp_local.h"
#include "x509_local.h"

int
X509_certificate_type(const X509 *x, const EVP_PKEY *pkey)
{
	const EVP_PKEY *pk = pkey;
	int nid;
	int ret = 0;

	if (x == NULL)
		goto done;

	if (pk == NULL)
		pk = X509_get0_pubkey(x);
	if (pk == NULL)
		goto done;

	switch (pk->type) {
	case EVP_PKEY_RSA:
		ret = EVP_PK_RSA|EVP_PKT_SIGN|EVP_PKT_ENC;
		break;
	case EVP_PKEY_RSA_PSS:
		ret = EVP_PK_RSA|EVP_PKT_SIGN;
		break;
	case EVP_PKEY_DSA:
		ret = EVP_PK_DSA|EVP_PKT_SIGN;
		break;
	case EVP_PKEY_EC:
		ret = EVP_PK_EC|EVP_PKT_SIGN|EVP_PKT_EXCH;
		break;
	case EVP_PKEY_ED25519:
		ret = EVP_PKT_SIGN;
		break;
	case EVP_PKEY_DH:
		ret = EVP_PK_DH|EVP_PKT_EXCH;
		break;
	case NID_id_GostR3410_94:
	case NID_id_GostR3410_2001:
		ret = EVP_PKT_EXCH|EVP_PKT_SIGN;
		break;
	default:
		break;
	}

	if ((nid = X509_get_signature_nid(x)) == NID_undef)
		goto done;

	if (!OBJ_find_sigid_algs(nid, NULL, &nid))
		goto done;

	switch (nid) {
	case NID_rsaEncryption:
	case NID_rsa:
		ret |= EVP_PKS_RSA;
		break;
	case NID_dsa:
	case NID_dsa_2:
		ret |= EVP_PKS_DSA;
		break;
	case NID_X9_62_id_ecPublicKey:
		ret |= EVP_PKS_EC;
		break;
	default:
		break;
	}

 done:

	return ret;
}
LCRYPTO_ALIAS(X509_certificate_type);
