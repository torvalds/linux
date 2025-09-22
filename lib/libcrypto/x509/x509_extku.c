/* $OpenBSD: x509_extku.c,v 1.7 2025/05/10 05:54:39 tb Exp $ */
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
#include <openssl/conf.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static void *v2i_EXTENDED_KEY_USAGE(const X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static STACK_OF(CONF_VALUE) *i2v_EXTENDED_KEY_USAGE(
    const X509V3_EXT_METHOD *method, void *eku, STACK_OF(CONF_VALUE) *extlist);

static const X509V3_EXT_METHOD x509v3_ext_ext_key_usage = {
	.ext_nid = NID_ext_key_usage,
	.ext_flags = 0,
	.it = &EXTENDED_KEY_USAGE_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = i2v_EXTENDED_KEY_USAGE,
	.v2i = v2i_EXTENDED_KEY_USAGE,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_ext_key_usage(void)
{
	return &x509v3_ext_ext_key_usage;
}

/* NB OCSP acceptable responses also is a SEQUENCE OF OBJECT */
static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_acceptableResponses = {
	.ext_nid = NID_id_pkix_OCSP_acceptableResponses,
	.ext_flags = 0,
	.it = &EXTENDED_KEY_USAGE_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = i2v_EXTENDED_KEY_USAGE,
	.v2i = v2i_EXTENDED_KEY_USAGE,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_acceptableResponses(void)
{
	return &x509v3_ext_id_pkix_OCSP_acceptableResponses;
}

static const ASN1_TEMPLATE EXTENDED_KEY_USAGE_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "EXTENDED_KEY_USAGE",
	.item = &ASN1_OBJECT_it,
};

const ASN1_ITEM EXTENDED_KEY_USAGE_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &EXTENDED_KEY_USAGE_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "EXTENDED_KEY_USAGE",
};
LCRYPTO_ALIAS(EXTENDED_KEY_USAGE_it);


EXTENDED_KEY_USAGE *
d2i_EXTENDED_KEY_USAGE(EXTENDED_KEY_USAGE **a, const unsigned char **in, long len)
{
	return (EXTENDED_KEY_USAGE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &EXTENDED_KEY_USAGE_it);
}
LCRYPTO_ALIAS(d2i_EXTENDED_KEY_USAGE);

int
i2d_EXTENDED_KEY_USAGE(EXTENDED_KEY_USAGE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &EXTENDED_KEY_USAGE_it);
}
LCRYPTO_ALIAS(i2d_EXTENDED_KEY_USAGE);

EXTENDED_KEY_USAGE *
EXTENDED_KEY_USAGE_new(void)
{
	return (EXTENDED_KEY_USAGE *)ASN1_item_new(&EXTENDED_KEY_USAGE_it);
}
LCRYPTO_ALIAS(EXTENDED_KEY_USAGE_new);

void
EXTENDED_KEY_USAGE_free(EXTENDED_KEY_USAGE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &EXTENDED_KEY_USAGE_it);
}
LCRYPTO_ALIAS(EXTENDED_KEY_USAGE_free);

static STACK_OF(CONF_VALUE) *
i2v_EXTENDED_KEY_USAGE(const X509V3_EXT_METHOD *method, void *a,
    STACK_OF(CONF_VALUE) *extlist)
{
	ASN1_OBJECT *obj;
	EXTENDED_KEY_USAGE *eku = a;
	STACK_OF(CONF_VALUE) *free_extlist = NULL;
	char obj_tmp[80];
	int i;

	if (extlist == NULL) {
		if ((free_extlist = extlist = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	for (i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
		if ((obj = sk_ASN1_OBJECT_value(eku, i)) == NULL)
			goto err;
		if (!i2t_ASN1_OBJECT(obj_tmp, sizeof obj_tmp, obj))
			goto err;
		if (!X509V3_add_value(NULL, obj_tmp, &extlist))
			goto err;
	}

	return extlist;

 err:
	sk_CONF_VALUE_pop_free(free_extlist, X509V3_conf_free);

	return NULL;
}

static void *
v2i_EXTENDED_KEY_USAGE(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	EXTENDED_KEY_USAGE *extku;
	char *extval;
	ASN1_OBJECT *objtmp;
	CONF_VALUE *val;
	int i;

	if (!(extku = sk_ASN1_OBJECT_new_null())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		val = sk_CONF_VALUE_value(nval, i);
		if (val->value)
			extval = val->value;
		else
			extval = val->name;
		if (!(objtmp = OBJ_txt2obj(extval, 0))) {
			sk_ASN1_OBJECT_pop_free(extku, ASN1_OBJECT_free);
			X509V3error(X509V3_R_INVALID_OBJECT_IDENTIFIER);
			X509V3_conf_err(val);
			return NULL;
		}
		if (sk_ASN1_OBJECT_push(extku, objtmp) == 0) {
			ASN1_OBJECT_free(objtmp);
			sk_ASN1_OBJECT_pop_free(extku, ASN1_OBJECT_free);
			X509V3error(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
	}
	return extku;
}
