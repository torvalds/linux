/* $OpenBSD: p5_pbe.c,v 1.30 2025/05/24 02:57:14 tb Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "x509_local.h"

/* RFC 8018, section 6.1 specifies an eight-octet salt for PBES1. */
#define PKCS5_PBE1_SALT_LEN	8

/* PKCS#5 password based encryption structure */

static const ASN1_TEMPLATE PBEPARAM_seq_tt[] = {
	{
		.offset = offsetof(PBEPARAM, salt),
		.field_name = "salt",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.offset = offsetof(PBEPARAM, iter),
		.field_name = "iter",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM PBEPARAM_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PBEPARAM_seq_tt,
	.tcount = sizeof(PBEPARAM_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(PBEPARAM),
	.sname = "PBEPARAM",
};
LCRYPTO_ALIAS(PBEPARAM_it);


PBEPARAM *
d2i_PBEPARAM(PBEPARAM **a, const unsigned char **in, long len)
{
	return (PBEPARAM *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PBEPARAM_it);
}

int
i2d_PBEPARAM(PBEPARAM *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PBEPARAM_it);
}

PBEPARAM *
PBEPARAM_new(void)
{
	return (PBEPARAM *)ASN1_item_new(&PBEPARAM_it);
}

void
PBEPARAM_free(PBEPARAM *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PBEPARAM_it);
}


/* Set an algorithm identifier for a PKCS#5 PBE algorithm */

int
PKCS5_pbe_set0_algor(X509_ALGOR *algor, int alg, int iter,
    const unsigned char *salt, int saltlen)
{
	PBEPARAM *pbe = NULL;
	ASN1_STRING *pbe_str = NULL;
	unsigned char *sstr;

	if ((pbe = PBEPARAM_new()) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (iter <= 0)
		iter = PKCS5_DEFAULT_ITER;
	if (!ASN1_INTEGER_set(pbe->iter, iter)) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (!saltlen)
		saltlen = PKCS5_PBE1_SALT_LEN;
	if (!ASN1_STRING_set(pbe->salt, NULL, saltlen)) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	sstr = ASN1_STRING_data(pbe->salt);
	if (salt)
		memcpy(sstr, salt, saltlen);
	else
		arc4random_buf(sstr, saltlen);

	if (!ASN1_item_pack(pbe, &PBEPARAM_it, &pbe_str)) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	PBEPARAM_free(pbe);
	pbe = NULL;

	if (X509_ALGOR_set0(algor, OBJ_nid2obj(alg), V_ASN1_SEQUENCE, pbe_str))
		return 1;

 err:
	if (pbe != NULL)
		PBEPARAM_free(pbe);
	ASN1_STRING_free(pbe_str);
	return 0;
}

/* Return an algorithm identifier for a PKCS#5 PBE algorithm */

X509_ALGOR *
PKCS5_pbe_set(int alg, int iter, const unsigned char *salt, int saltlen)
{
	X509_ALGOR *ret;
	ret = X509_ALGOR_new();
	if (!ret) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	if (PKCS5_pbe_set0_algor(ret, alg, iter, salt, saltlen))
		return ret;

	X509_ALGOR_free(ret);
	return NULL;
}
