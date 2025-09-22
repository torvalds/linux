/* $OpenBSD: x509_pmaps.c,v 1.7 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2003 The OpenSSL Project.  All rights reserved.
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

static void *v2i_POLICY_MAPPINGS(const X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static STACK_OF(CONF_VALUE) *i2v_POLICY_MAPPINGS(
    const X509V3_EXT_METHOD *method, void *pmps, STACK_OF(CONF_VALUE) *extlist);

static const X509V3_EXT_METHOD x509v3_ext_policy_mappings = {
	.ext_nid = NID_policy_mappings,
	.ext_flags = 0,
	.it = &POLICY_MAPPINGS_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = i2v_POLICY_MAPPINGS,
	.v2i = v2i_POLICY_MAPPINGS,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_policy_mappings(void)
{
	return &x509v3_ext_policy_mappings;
}

static const ASN1_TEMPLATE POLICY_MAPPING_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(POLICY_MAPPING, issuerDomainPolicy),
		.field_name = "issuerDomainPolicy",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(POLICY_MAPPING, subjectDomainPolicy),
		.field_name = "subjectDomainPolicy",
		.item = &ASN1_OBJECT_it,
	},
};

const ASN1_ITEM POLICY_MAPPING_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = POLICY_MAPPING_seq_tt,
	.tcount = sizeof(POLICY_MAPPING_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(POLICY_MAPPING),
	.sname = "POLICY_MAPPING",
};
LCRYPTO_ALIAS(POLICY_MAPPING_it);

static const ASN1_TEMPLATE POLICY_MAPPINGS_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "POLICY_MAPPINGS",
	.item = &POLICY_MAPPING_it,
};

const ASN1_ITEM POLICY_MAPPINGS_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &POLICY_MAPPINGS_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "POLICY_MAPPINGS",
};
LCRYPTO_ALIAS(POLICY_MAPPINGS_it);


POLICY_MAPPING *
POLICY_MAPPING_new(void)
{
	return (POLICY_MAPPING*)ASN1_item_new(&POLICY_MAPPING_it);
}
LCRYPTO_ALIAS(POLICY_MAPPING_new);

void
POLICY_MAPPING_free(POLICY_MAPPING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &POLICY_MAPPING_it);
}
LCRYPTO_ALIAS(POLICY_MAPPING_free);

static STACK_OF(CONF_VALUE) *
i2v_POLICY_MAPPINGS(const X509V3_EXT_METHOD *method, void *a,
    STACK_OF(CONF_VALUE) *extlist)
{
	STACK_OF(CONF_VALUE) *free_extlist = NULL;
	POLICY_MAPPINGS *pmaps = a;
	POLICY_MAPPING *pmap;
	char issuer[80], subject[80];
	int i;

	if (extlist == NULL) {
		if ((free_extlist = extlist = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	for (i = 0; i < sk_POLICY_MAPPING_num(pmaps); i++) {
		if ((pmap = sk_POLICY_MAPPING_value(pmaps, i)) == NULL)
			goto err;
		if (!i2t_ASN1_OBJECT(issuer, sizeof issuer,
		    pmap->issuerDomainPolicy))
			goto err;
		if (!i2t_ASN1_OBJECT(subject, sizeof subject,
		    pmap->subjectDomainPolicy))
			goto err;
		if (!X509V3_add_value(issuer, subject, &extlist))
			goto err;
	}

	return extlist;

 err:
	sk_CONF_VALUE_pop_free(free_extlist, X509V3_conf_free);

	return NULL;
}

static void *
v2i_POLICY_MAPPINGS(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	POLICY_MAPPINGS *pmaps = NULL;
	POLICY_MAPPING *pmap = NULL;
	ASN1_OBJECT *obj1 = NULL, *obj2 = NULL;
	CONF_VALUE *val;
	int i, rc;

	if (!(pmaps = sk_POLICY_MAPPING_new_null())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		val = sk_CONF_VALUE_value(nval, i);
		if (!val->value || !val->name) {
			rc = X509V3_R_INVALID_OBJECT_IDENTIFIER;
			goto err;
		}
		obj1 = OBJ_txt2obj(val->name, 0);
		obj2 = OBJ_txt2obj(val->value, 0);
		if (!obj1 || !obj2) {
			rc = X509V3_R_INVALID_OBJECT_IDENTIFIER;
			goto err;
		}
		pmap = POLICY_MAPPING_new();
		if (!pmap) {
	    		rc = ERR_R_MALLOC_FAILURE;
			goto err;
		}
		pmap->issuerDomainPolicy = obj1;
		pmap->subjectDomainPolicy = obj2;
		obj1 = obj2 = NULL;
		if (sk_POLICY_MAPPING_push(pmaps, pmap) == 0) {
	    		rc = ERR_R_MALLOC_FAILURE;
			goto err;
		}
		pmap = NULL;
	}
	return pmaps;

err:
	sk_POLICY_MAPPING_pop_free(pmaps, POLICY_MAPPING_free);
	X509V3error(rc);
	if (rc == X509V3_R_INVALID_OBJECT_IDENTIFIER)
		X509V3_conf_err(val);
	ASN1_OBJECT_free(obj1);
	ASN1_OBJECT_free(obj2);
	POLICY_MAPPING_free(pmap);
	return NULL;
}
