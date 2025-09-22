/* $OpenBSD: x509_bcons.c,v 1.7 2025/05/10 05:54:39 tb Exp $ */
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
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static STACK_OF(CONF_VALUE) *i2v_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
    BASIC_CONSTRAINTS *bcons, STACK_OF(CONF_VALUE) *extlist);
static BASIC_CONSTRAINTS *v2i_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *values);

static const X509V3_EXT_METHOD x509v3_ext_basic_constraints = {
	.ext_nid = NID_basic_constraints,
	.ext_flags = 0,
	.it = &BASIC_CONSTRAINTS_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = (X509V3_EXT_I2V)i2v_BASIC_CONSTRAINTS,
	.v2i = (X509V3_EXT_V2I)v2i_BASIC_CONSTRAINTS,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_basic_constraints(void)
{
	return &x509v3_ext_basic_constraints;
}

static const ASN1_TEMPLATE BASIC_CONSTRAINTS_seq_tt[] = {
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(BASIC_CONSTRAINTS, ca),
		.field_name = "ca",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(BASIC_CONSTRAINTS, pathlen),
		.field_name = "pathlen",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM BASIC_CONSTRAINTS_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = BASIC_CONSTRAINTS_seq_tt,
	.tcount = sizeof(BASIC_CONSTRAINTS_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(BASIC_CONSTRAINTS),
	.sname = "BASIC_CONSTRAINTS",
};
LCRYPTO_ALIAS(BASIC_CONSTRAINTS_it);


BASIC_CONSTRAINTS *
d2i_BASIC_CONSTRAINTS(BASIC_CONSTRAINTS **a, const unsigned char **in, long len)
{
	return (BASIC_CONSTRAINTS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &BASIC_CONSTRAINTS_it);
}
LCRYPTO_ALIAS(d2i_BASIC_CONSTRAINTS);

int
i2d_BASIC_CONSTRAINTS(BASIC_CONSTRAINTS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &BASIC_CONSTRAINTS_it);
}
LCRYPTO_ALIAS(i2d_BASIC_CONSTRAINTS);

BASIC_CONSTRAINTS *
BASIC_CONSTRAINTS_new(void)
{
	return (BASIC_CONSTRAINTS *)ASN1_item_new(&BASIC_CONSTRAINTS_it);
}
LCRYPTO_ALIAS(BASIC_CONSTRAINTS_new);

void
BASIC_CONSTRAINTS_free(BASIC_CONSTRAINTS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &BASIC_CONSTRAINTS_it);
}
LCRYPTO_ALIAS(BASIC_CONSTRAINTS_free);


static STACK_OF(CONF_VALUE) *
i2v_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method, BASIC_CONSTRAINTS *bcons,
    STACK_OF(CONF_VALUE) *extlist)
{
	STACK_OF(CONF_VALUE) *free_extlist = NULL;

	if (extlist == NULL) {
		if ((free_extlist = extlist = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	if (!X509V3_add_value_bool("CA", bcons->ca, &extlist))
		goto err;
	if (!X509V3_add_value_int("pathlen", bcons->pathlen, &extlist))
		goto err;

	return extlist;

 err:
	sk_CONF_VALUE_pop_free(free_extlist, X509V3_conf_free);

	return NULL;
}

static BASIC_CONSTRAINTS *
v2i_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *values)
{
	BASIC_CONSTRAINTS *bcons = NULL;
	CONF_VALUE *val;
	int i;

	if (!(bcons = BASIC_CONSTRAINTS_new())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	for (i = 0; i < sk_CONF_VALUE_num(values); i++) {
		val = sk_CONF_VALUE_value(values, i);
		if (!strcmp(val->name, "CA")) {
			if (!X509V3_get_value_bool(val, &bcons->ca))
				goto err;
		} else if (!strcmp(val->name, "pathlen")) {
			if (!X509V3_get_value_int(val, &bcons->pathlen))
				goto err;
		} else {
			X509V3error(X509V3_R_INVALID_NAME);
			X509V3_conf_err(val);
			goto err;
		}
	}
	return bcons;

err:
	BASIC_CONSTRAINTS_free(bcons);
	return NULL;
}
