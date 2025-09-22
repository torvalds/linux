/* $OpenBSD: evp_pkey.c,v 1.34 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
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

#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"

/* Extract a private key from a PKCS8 structure */

EVP_PKEY *
EVP_PKCS82PKEY(const PKCS8_PRIV_KEY_INFO *p8)
{
	EVP_PKEY *pkey = NULL;
	const ASN1_OBJECT *algoid;
	char obj_tmp[80];

	if (!PKCS8_pkey_get0(&algoid, NULL, NULL, NULL, p8))
		return NULL;

	if (!(pkey = EVP_PKEY_new())) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (!EVP_PKEY_set_type(pkey, OBJ_obj2nid(algoid))) {
		EVPerror(EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM);
		if (i2t_ASN1_OBJECT(obj_tmp, sizeof(obj_tmp), algoid) == 0)
			(void)strlcpy(obj_tmp, "unknown", sizeof(obj_tmp));
		ERR_asprintf_error_data("TYPE=%s", obj_tmp);
		goto error;
	}

	if (pkey->ameth->priv_decode) {
		if (!pkey->ameth->priv_decode(pkey, p8)) {
			EVPerror(EVP_R_PRIVATE_KEY_DECODE_ERROR);
			goto error;
		}
	} else {
		EVPerror(EVP_R_METHOD_NOT_SUPPORTED);
		goto error;
	}

	return pkey;

error:
	EVP_PKEY_free(pkey);
	return NULL;
}
LCRYPTO_ALIAS(EVP_PKCS82PKEY);

/* Turn a private key into a PKCS8 structure */

PKCS8_PRIV_KEY_INFO *
EVP_PKEY2PKCS8(EVP_PKEY *pkey)
{
	PKCS8_PRIV_KEY_INFO *p8;

	if (!(p8 = PKCS8_PRIV_KEY_INFO_new())) {
		EVPerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (pkey->ameth) {
		if (pkey->ameth->priv_encode) {
			if (!pkey->ameth->priv_encode(p8, pkey)) {
				EVPerror(EVP_R_PRIVATE_KEY_ENCODE_ERROR);
				goto error;
			}
		} else {
			EVPerror(EVP_R_METHOD_NOT_SUPPORTED);
			goto error;
		}
	} else {
		EVPerror(EVP_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM);
		goto error;
	}
	return p8;

error:
	PKCS8_PRIV_KEY_INFO_free(p8);
	return NULL;
}
LCRYPTO_ALIAS(EVP_PKEY2PKCS8);
