/* $OpenBSD: x_x509a.c,v 1.22 2024/04/09 13:55:02 beck Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "x509_local.h"

/* X509_CERT_AUX routines. These are used to encode additional
 * user modifiable data about a certificate. This data is
 * appended to the X509 encoding when the *_X509_AUX routines
 * are used. This means that the "traditional" X509 routines
 * will simply ignore the extra data.
 */

static X509_CERT_AUX *aux_get(X509 *x);

static const ASN1_TEMPLATE X509_CERT_AUX_seq_tt[] = {
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CERT_AUX, trust),
		.field_name = "trust",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF |
		    ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(X509_CERT_AUX, reject),
		.field_name = "reject",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CERT_AUX, alias),
		.field_name = "alias",
		.item = &ASN1_UTF8STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CERT_AUX, keyid),
		.field_name = "keyid",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF |
		    ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(X509_CERT_AUX, other),
		.field_name = "other",
		.item = &X509_ALGOR_it,
	},
};

const ASN1_ITEM X509_CERT_AUX_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_CERT_AUX_seq_tt,
	.tcount = sizeof(X509_CERT_AUX_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(X509_CERT_AUX),
	.sname = "X509_CERT_AUX",
};


X509_CERT_AUX *
d2i_X509_CERT_AUX(X509_CERT_AUX **a, const unsigned char **in, long len)
{
	return (X509_CERT_AUX *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_CERT_AUX_it);
}

int
i2d_X509_CERT_AUX(X509_CERT_AUX *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_CERT_AUX_it);
}

X509_CERT_AUX *
X509_CERT_AUX_new(void)
{
	return (X509_CERT_AUX *)ASN1_item_new(&X509_CERT_AUX_it);
}

void
X509_CERT_AUX_free(X509_CERT_AUX *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_CERT_AUX_it);
}

static X509_CERT_AUX *
aux_get(X509 *x)
{
	if (!x)
		return NULL;
	if (!x->aux && !(x->aux = X509_CERT_AUX_new()))
		return NULL;
	return x->aux;
}

int
X509_alias_set1(X509 *x, const unsigned char *name, int len)
{
	X509_CERT_AUX *aux;
	if (!name) {
		if (!x || !x->aux || !x->aux->alias)
			return 1;
		ASN1_UTF8STRING_free(x->aux->alias);
		x->aux->alias = NULL;
		return 1;
	}
	if (!(aux = aux_get(x)))
		return 0;
	if (!aux->alias && !(aux->alias = ASN1_UTF8STRING_new()))
		return 0;
	return ASN1_STRING_set(aux->alias, name, len);
}
LCRYPTO_ALIAS(X509_alias_set1);

int
X509_keyid_set1(X509 *x, const unsigned char *id, int len)
{
	X509_CERT_AUX *aux;
	if (!id) {
		if (!x || !x->aux || !x->aux->keyid)
			return 1;
		ASN1_OCTET_STRING_free(x->aux->keyid);
		x->aux->keyid = NULL;
		return 1;
	}
	if (!(aux = aux_get(x)))
		return 0;
	if (!aux->keyid && !(aux->keyid = ASN1_OCTET_STRING_new()))
		return 0;
	return ASN1_STRING_set(aux->keyid, id, len);
}
LCRYPTO_ALIAS(X509_keyid_set1);

unsigned char *
X509_alias_get0(X509 *x, int *len)
{
	if (!x->aux || !x->aux->alias)
		return NULL;
	if (len)
		*len = x->aux->alias->length;
	return x->aux->alias->data;
}
LCRYPTO_ALIAS(X509_alias_get0);

unsigned char *
X509_keyid_get0(X509 *x, int *len)
{
	if (!x->aux || !x->aux->keyid)
		return NULL;
	if (len)
		*len = x->aux->keyid->length;
	return x->aux->keyid->data;
}
LCRYPTO_ALIAS(X509_keyid_get0);

int
X509_add1_trust_object(X509 *x, const ASN1_OBJECT *obj)
{
	X509_CERT_AUX *aux;
	ASN1_OBJECT *objtmp;
	int rc;

	if (!(objtmp = OBJ_dup(obj)))
		return 0;
	if (!(aux = aux_get(x)))
		goto err;
	if (!aux->trust && !(aux->trust = sk_ASN1_OBJECT_new_null()))
		goto err;
	rc = sk_ASN1_OBJECT_push(aux->trust, objtmp);
	if (rc != 0)
		return rc;

 err:
	ASN1_OBJECT_free(objtmp);
	return 0;
}
LCRYPTO_ALIAS(X509_add1_trust_object);

int
X509_add1_reject_object(X509 *x, const ASN1_OBJECT *obj)
{
	X509_CERT_AUX *aux;
	ASN1_OBJECT *objtmp;
	int rc;

	if (!(objtmp = OBJ_dup(obj)))
		return 0;
	if (!(aux = aux_get(x)))
		goto err;
	if (!aux->reject && !(aux->reject = sk_ASN1_OBJECT_new_null()))
		goto err;
	rc = sk_ASN1_OBJECT_push(aux->reject, objtmp);
	if (rc != 0)
		return rc;

 err:
	ASN1_OBJECT_free(objtmp);
	return 0;
}
LCRYPTO_ALIAS(X509_add1_reject_object);

void
X509_trust_clear(X509 *x)
{
	if (x->aux && x->aux->trust) {
		sk_ASN1_OBJECT_pop_free(x->aux->trust, ASN1_OBJECT_free);
		x->aux->trust = NULL;
	}
}
LCRYPTO_ALIAS(X509_trust_clear);

void
X509_reject_clear(X509 *x)
{
	if (x->aux && x->aux->reject) {
		sk_ASN1_OBJECT_pop_free(x->aux->reject, ASN1_OBJECT_free);
		x->aux->reject = NULL;
	}
}
LCRYPTO_ALIAS(X509_reject_clear);
