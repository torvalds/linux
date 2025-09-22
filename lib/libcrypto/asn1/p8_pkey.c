/* $OpenBSD: p8_pkey.c,v 1.25 2024/07/08 14:48:49 beck Exp $ */
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
#include <string.h>

#include <openssl/asn1t.h>
#include <openssl/x509.h>

#include "x509_local.h"

/* Minor tweak to operation: zero private key data */
static int
pkey_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	/* Since the structure must still be valid use ASN1_OP_FREE_PRE */
	if (operation == ASN1_OP_FREE_PRE) {
		PKCS8_PRIV_KEY_INFO *key = (PKCS8_PRIV_KEY_INFO *)*pval;
		if (key->pkey != NULL)
			explicit_bzero(key->pkey->data, key->pkey->length);
	}
	return 1;
}

static const ASN1_AUX PKCS8_PRIV_KEY_INFO_aux = {
	.asn1_cb = pkey_cb,
};
static const ASN1_TEMPLATE PKCS8_PRIV_KEY_INFO_seq_tt[] = {
	{
		.offset = offsetof(PKCS8_PRIV_KEY_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(PKCS8_PRIV_KEY_INFO, pkeyalg),
		.field_name = "pkeyalg",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(PKCS8_PRIV_KEY_INFO, pkey),
		.field_name = "pkey",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(PKCS8_PRIV_KEY_INFO, attributes),
		.field_name = "attributes",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM PKCS8_PRIV_KEY_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS8_PRIV_KEY_INFO_seq_tt,
	.tcount = sizeof(PKCS8_PRIV_KEY_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &PKCS8_PRIV_KEY_INFO_aux,
	.size = sizeof(PKCS8_PRIV_KEY_INFO),
	.sname = "PKCS8_PRIV_KEY_INFO",
};
LCRYPTO_ALIAS(PKCS8_PRIV_KEY_INFO_it);


PKCS8_PRIV_KEY_INFO *
d2i_PKCS8_PRIV_KEY_INFO(PKCS8_PRIV_KEY_INFO **a, const unsigned char **in, long len)
{
	return (PKCS8_PRIV_KEY_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS8_PRIV_KEY_INFO_it);
}
LCRYPTO_ALIAS(d2i_PKCS8_PRIV_KEY_INFO);

int
i2d_PKCS8_PRIV_KEY_INFO(PKCS8_PRIV_KEY_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS8_PRIV_KEY_INFO_it);
}
LCRYPTO_ALIAS(i2d_PKCS8_PRIV_KEY_INFO);

PKCS8_PRIV_KEY_INFO *
PKCS8_PRIV_KEY_INFO_new(void)
{
	return (PKCS8_PRIV_KEY_INFO *)ASN1_item_new(&PKCS8_PRIV_KEY_INFO_it);
}
LCRYPTO_ALIAS(PKCS8_PRIV_KEY_INFO_new);

void
PKCS8_PRIV_KEY_INFO_free(PKCS8_PRIV_KEY_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS8_PRIV_KEY_INFO_it);
}
LCRYPTO_ALIAS(PKCS8_PRIV_KEY_INFO_free);

int
PKCS8_pkey_set0(PKCS8_PRIV_KEY_INFO *priv, ASN1_OBJECT *aobj, int version,
    int ptype, void *pval, unsigned char *penc, int penclen)
{
	if (version >= 0) {
		if (!ASN1_INTEGER_set(priv->version, version))
			return 0;
	}
	if (!X509_ALGOR_set0(priv->pkeyalg, aobj, ptype, pval))
		return 0;
	if (penc != NULL)
		ASN1_STRING_set0(priv->pkey, penc, penclen);
	return 1;
}
LCRYPTO_ALIAS(PKCS8_pkey_set0);

int
PKCS8_pkey_get0(const ASN1_OBJECT **ppkalg, const unsigned char **pk,
    int *ppklen, const X509_ALGOR **pa, const PKCS8_PRIV_KEY_INFO *p8)
{
	if (ppkalg != NULL)
		*ppkalg = p8->pkeyalg->algorithm;
	if (pk != NULL) {
		*pk = ASN1_STRING_data(p8->pkey);
		*ppklen = ASN1_STRING_length(p8->pkey);
	}
	if (pa != NULL)
		*pa = p8->pkeyalg;
	return 1;
}
LCRYPTO_ALIAS(PKCS8_pkey_get0);

const STACK_OF(X509_ATTRIBUTE) *
PKCS8_pkey_get0_attrs(const PKCS8_PRIV_KEY_INFO *p8)
{
	return p8->attributes;
}
LCRYPTO_ALIAS(PKCS8_pkey_get0_attrs);

int
PKCS8_pkey_add1_attr_by_NID(PKCS8_PRIV_KEY_INFO *p8, int nid, int type,
    const unsigned char *bytes, int len)
{
	if (X509at_add1_attr_by_NID(&p8->attributes, nid, type, bytes,
	    len) != NULL)
		return 1;
	return 0;
}
LCRYPTO_ALIAS(PKCS8_pkey_add1_attr_by_NID);
