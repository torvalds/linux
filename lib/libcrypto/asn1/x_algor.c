/* $OpenBSD: x_algor.c,v 1.41 2024/07/08 14:48:49 beck Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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

#include <stddef.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>

#include "x509_local.h"

static const ASN1_TEMPLATE X509_ALGOR_seq_tt[] = {
	{
		.offset = offsetof(X509_ALGOR, algorithm),
		.field_name = "algorithm",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_ALGOR, parameter),
		.field_name = "parameter",
		.item = &ASN1_ANY_it,
	},
};

const ASN1_ITEM X509_ALGOR_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_ALGOR_seq_tt,
	.tcount = sizeof(X509_ALGOR_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(X509_ALGOR),
	.sname = "X509_ALGOR",
};
LCRYPTO_ALIAS(X509_ALGOR_it);

static const ASN1_TEMPLATE X509_ALGORS_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "algorithms",
	.item = &X509_ALGOR_it,
};

const ASN1_ITEM X509_ALGORS_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &X509_ALGORS_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "X509_ALGORS",
};
LCRYPTO_ALIAS(X509_ALGORS_it);

X509_ALGOR *
d2i_X509_ALGOR(X509_ALGOR **a, const unsigned char **in, long len)
{
	return (X509_ALGOR *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_ALGOR_it);
}
LCRYPTO_ALIAS(d2i_X509_ALGOR);

int
i2d_X509_ALGOR(X509_ALGOR *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_ALGOR_it);
}
LCRYPTO_ALIAS(i2d_X509_ALGOR);

X509_ALGOR *
X509_ALGOR_new(void)
{
	return (X509_ALGOR *)ASN1_item_new(&X509_ALGOR_it);
}
LCRYPTO_ALIAS(X509_ALGOR_new);

void
X509_ALGOR_free(X509_ALGOR *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_ALGOR_it);
}
LCRYPTO_ALIAS(X509_ALGOR_free);

X509_ALGORS *
d2i_X509_ALGORS(X509_ALGORS **a, const unsigned char **in, long len)
{
	return (X509_ALGORS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_ALGORS_it);
}
LCRYPTO_ALIAS(d2i_X509_ALGORS);

int
i2d_X509_ALGORS(X509_ALGORS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_ALGORS_it);
}
LCRYPTO_ALIAS(i2d_X509_ALGORS);

X509_ALGOR *
X509_ALGOR_dup(X509_ALGOR *x)
{
	return ASN1_item_dup(&X509_ALGOR_it, x);
}
LCRYPTO_ALIAS(X509_ALGOR_dup);

static int
X509_ALGOR_set0_obj(X509_ALGOR *alg, ASN1_OBJECT *aobj)
{
	ASN1_OBJECT_free(alg->algorithm);
	alg->algorithm = aobj;

	return 1;
}

static int
X509_ALGOR_set_obj_by_nid(X509_ALGOR *alg, int nid)
{
	ASN1_OBJECT *aobj;

	if ((aobj = OBJ_nid2obj(nid)) == NULL)
		return 0;
	if (!X509_ALGOR_set0_obj(alg, aobj))
		return 0;

	return 1;
}

static int
X509_ALGOR_set0_parameter(X509_ALGOR *alg, int parameter_type,
    void *parameter_value)
{
	if (parameter_type == V_ASN1_UNDEF) {
		ASN1_TYPE_free(alg->parameter);
		alg->parameter = NULL;

		return 1;
	}

	if (alg->parameter == NULL)
		alg->parameter = ASN1_TYPE_new();
	if (alg->parameter == NULL)
		return 0;

	if (parameter_type != 0)
		ASN1_TYPE_set(alg->parameter, parameter_type, parameter_value);

	return 1;
}

int
X509_ALGOR_set0_by_nid(X509_ALGOR *alg, int nid, int parameter_type,
    void *parameter_value)
{
	if (alg == NULL)
		return 0;

	if (!X509_ALGOR_set_obj_by_nid(alg, nid))
		return 0;

	if (!X509_ALGOR_set0_parameter(alg, parameter_type, parameter_value))
		return 0;

	return 1;
}

int
X509_ALGOR_set0(X509_ALGOR *alg, ASN1_OBJECT *aobj, int parameter_type,
    void *parameter_value)
{
	if (alg == NULL)
		return 0;

	/* Set parameter first to preserve public API behavior on failure. */
	if (!X509_ALGOR_set0_parameter(alg, parameter_type, parameter_value))
		return 0;

	if (!X509_ALGOR_set0_obj(alg, aobj))
		return 0;

	return 1;
}
LCRYPTO_ALIAS(X509_ALGOR_set0);

void
X509_ALGOR_get0(const ASN1_OBJECT **out_aobj, int *out_type,
    const void **out_value, const X509_ALGOR *alg)
{
	int type = V_ASN1_UNDEF;
	const void *value = NULL;

	if (out_aobj != NULL)
		*out_aobj = alg->algorithm;

	/* Ensure out_value is not left uninitialized if out_type is NULL. */
	if (out_value != NULL)
		*out_value = NULL;

	if (out_type == NULL)
		return;

	if (alg->parameter != NULL) {
		type = alg->parameter->type;
		value = alg->parameter->value.ptr;
	}

	*out_type = type;
	if (out_value != NULL)
		*out_value = value;
}
LCRYPTO_ALIAS(X509_ALGOR_get0);

int
X509_ALGOR_set_evp_md(X509_ALGOR *alg, const EVP_MD *md)
{
	int parameter_type = V_ASN1_NULL;
	int nid = EVP_MD_type(md);

	if ((EVP_MD_flags(md) & EVP_MD_FLAG_DIGALGID_ABSENT) != 0)
		parameter_type = V_ASN1_UNDEF;

	if (!X509_ALGOR_set0_by_nid(alg, nid, parameter_type, NULL))
		return 0;

	return 1;
}

int
X509_ALGOR_cmp(const X509_ALGOR *a, const X509_ALGOR *b)
{
	int cmp;

	if ((cmp = OBJ_cmp(a->algorithm, b->algorithm)) != 0)
		return cmp;

	if (a->parameter == NULL && b->parameter == NULL)
		return 0;

	return ASN1_TYPE_cmp(a->parameter, b->parameter);
}
LCRYPTO_ALIAS(X509_ALGOR_cmp);
