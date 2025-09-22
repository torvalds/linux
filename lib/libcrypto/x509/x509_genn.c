/* $OpenBSD: x509_genn.c,v 1.8 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2008 The OpenSSL Project.  All rights reserved.
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

static const ASN1_TEMPLATE OTHERNAME_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(OTHERNAME, type_id),
		.field_name = "type_id",
		.item = &ASN1_OBJECT_it,
	},
	/* Maybe have a true ANY DEFINED BY later */
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = 0,
		.offset = offsetof(OTHERNAME, value),
		.field_name = "value",
		.item = &ASN1_ANY_it,
	},
};

const ASN1_ITEM OTHERNAME_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = OTHERNAME_seq_tt,
	.tcount = sizeof(OTHERNAME_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(OTHERNAME),
	.sname = "OTHERNAME",
};
LCRYPTO_ALIAS(OTHERNAME_it);


OTHERNAME *
d2i_OTHERNAME(OTHERNAME **a, const unsigned char **in, long len)
{
	return (OTHERNAME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &OTHERNAME_it);
}
LCRYPTO_ALIAS(d2i_OTHERNAME);

int
i2d_OTHERNAME(OTHERNAME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &OTHERNAME_it);
}
LCRYPTO_ALIAS(i2d_OTHERNAME);

OTHERNAME *
OTHERNAME_new(void)
{
	return (OTHERNAME *)ASN1_item_new(&OTHERNAME_it);
}
LCRYPTO_ALIAS(OTHERNAME_new);

void
OTHERNAME_free(OTHERNAME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &OTHERNAME_it);
}
LCRYPTO_ALIAS(OTHERNAME_free);

/* Uses explicit tagging since DIRECTORYSTRING is a CHOICE type */
static const ASN1_TEMPLATE EDIPARTYNAME_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(EDIPARTYNAME, nameAssigner),
		.field_name = "nameAssigner",
		.item = &DIRECTORYSTRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = 1,
		.offset = offsetof(EDIPARTYNAME, partyName),
		.field_name = "partyName",
		.item = &DIRECTORYSTRING_it,
	},
};

const ASN1_ITEM EDIPARTYNAME_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = EDIPARTYNAME_seq_tt,
	.tcount = sizeof(EDIPARTYNAME_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(EDIPARTYNAME),
	.sname = "EDIPARTYNAME",
};
LCRYPTO_ALIAS(EDIPARTYNAME_it);


EDIPARTYNAME *
d2i_EDIPARTYNAME(EDIPARTYNAME **a, const unsigned char **in, long len)
{
	return (EDIPARTYNAME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &EDIPARTYNAME_it);
}
LCRYPTO_ALIAS(d2i_EDIPARTYNAME);

int
i2d_EDIPARTYNAME(EDIPARTYNAME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &EDIPARTYNAME_it);
}
LCRYPTO_ALIAS(i2d_EDIPARTYNAME);

EDIPARTYNAME *
EDIPARTYNAME_new(void)
{
	return (EDIPARTYNAME *)ASN1_item_new(&EDIPARTYNAME_it);
}
LCRYPTO_ALIAS(EDIPARTYNAME_new);

void
EDIPARTYNAME_free(EDIPARTYNAME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &EDIPARTYNAME_it);
}
LCRYPTO_ALIAS(EDIPARTYNAME_free);

static const ASN1_TEMPLATE GENERAL_NAME_ch_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_OTHERNAME,
		.offset = offsetof(GENERAL_NAME, d.otherName),
		.field_name = "d.otherName",
		.item = &OTHERNAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_EMAIL,
		.offset = offsetof(GENERAL_NAME, d.rfc822Name),
		.field_name = "d.rfc822Name",
		.item = &ASN1_IA5STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_DNS,
		.offset = offsetof(GENERAL_NAME, d.dNSName),
		.field_name = "d.dNSName",
		.item = &ASN1_IA5STRING_it,
	},
	/* Don't decode this */
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_X400,
		.offset = offsetof(GENERAL_NAME, d.x400Address),
		.field_name = "d.x400Address",
		.item = &ASN1_SEQUENCE_it,
	},
	/* X509_NAME is a CHOICE type so use EXPLICIT */
	{
		.flags = ASN1_TFLG_EXPLICIT,
		.tag = GEN_DIRNAME,
		.offset = offsetof(GENERAL_NAME, d.directoryName),
		.field_name = "d.directoryName",
		.item = &X509_NAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_EDIPARTY,
		.offset = offsetof(GENERAL_NAME, d.ediPartyName),
		.field_name = "d.ediPartyName",
		.item = &EDIPARTYNAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_URI,
		.offset = offsetof(GENERAL_NAME, d.uniformResourceIdentifier),
		.field_name = "d.uniformResourceIdentifier",
		.item = &ASN1_IA5STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_IPADD,
		.offset = offsetof(GENERAL_NAME, d.iPAddress),
		.field_name = "d.iPAddress",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT,
		.tag = GEN_RID,
		.offset = offsetof(GENERAL_NAME, d.registeredID),
		.field_name = "d.registeredID",
		.item = &ASN1_OBJECT_it,
	},
};

const ASN1_ITEM GENERAL_NAME_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(GENERAL_NAME, type),
	.templates = GENERAL_NAME_ch_tt,
	.tcount = sizeof(GENERAL_NAME_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(GENERAL_NAME),
	.sname = "GENERAL_NAME",
};
LCRYPTO_ALIAS(GENERAL_NAME_it);


GENERAL_NAME *
d2i_GENERAL_NAME(GENERAL_NAME **a, const unsigned char **in, long len)
{
	return (GENERAL_NAME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &GENERAL_NAME_it);
}
LCRYPTO_ALIAS(d2i_GENERAL_NAME);

int
i2d_GENERAL_NAME(GENERAL_NAME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &GENERAL_NAME_it);
}
LCRYPTO_ALIAS(i2d_GENERAL_NAME);

GENERAL_NAME *
GENERAL_NAME_new(void)
{
	return (GENERAL_NAME *)ASN1_item_new(&GENERAL_NAME_it);
}
LCRYPTO_ALIAS(GENERAL_NAME_new);

void
GENERAL_NAME_free(GENERAL_NAME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &GENERAL_NAME_it);
}
LCRYPTO_ALIAS(GENERAL_NAME_free);

static const ASN1_TEMPLATE GENERAL_NAMES_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "GeneralNames",
	.item = &GENERAL_NAME_it,
};

const ASN1_ITEM GENERAL_NAMES_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &GENERAL_NAMES_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "GENERAL_NAMES",
};
LCRYPTO_ALIAS(GENERAL_NAMES_it);


GENERAL_NAMES *
d2i_GENERAL_NAMES(GENERAL_NAMES **a, const unsigned char **in, long len)
{
	return (GENERAL_NAMES *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &GENERAL_NAMES_it);
}
LCRYPTO_ALIAS(d2i_GENERAL_NAMES);

int
i2d_GENERAL_NAMES(GENERAL_NAMES *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &GENERAL_NAMES_it);
}
LCRYPTO_ALIAS(i2d_GENERAL_NAMES);

GENERAL_NAMES *
GENERAL_NAMES_new(void)
{
	return (GENERAL_NAMES *)ASN1_item_new(&GENERAL_NAMES_it);
}
LCRYPTO_ALIAS(GENERAL_NAMES_new);

void
GENERAL_NAMES_free(GENERAL_NAMES *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &GENERAL_NAMES_it);
}
LCRYPTO_ALIAS(GENERAL_NAMES_free);

GENERAL_NAME *
GENERAL_NAME_dup(GENERAL_NAME *a)
{
	return ASN1_item_dup(&GENERAL_NAME_it, a);
}
LCRYPTO_ALIAS(GENERAL_NAME_dup);

static int
EDIPARTYNAME_cmp(const EDIPARTYNAME *a, const EDIPARTYNAME *b)
{
	int res;

	/*
	 * Shouldn't be possible in a valid GENERAL_NAME, but we handle it
	 * anyway. OTHERNAME_cmp treats NULL != NULL, so we do the same here.
	 */
	if (a == NULL || b == NULL)
		return -1;
	if (a->nameAssigner == NULL && b->nameAssigner != NULL)
		return -1;
	if (a->nameAssigner != NULL && b->nameAssigner == NULL)
		return 1;
	/* If we get here, both have nameAssigner set or both unset. */
	if (a->nameAssigner != NULL) {
		res = ASN1_STRING_cmp(a->nameAssigner, b->nameAssigner);
		if (res != 0)
			return res;
	}
	/*
	 * partyName is required, so these should never be NULL. We treat it in
	 * the same way as the a == NULL || b == NULL case above.
	 */
	if (a->partyName == NULL || b->partyName == NULL)
		return -1;

	return ASN1_STRING_cmp(a->partyName, b->partyName);
}

/* Returns 0 if they are equal, != 0 otherwise. */
int
GENERAL_NAME_cmp(GENERAL_NAME *a, GENERAL_NAME *b)
{
	int result = -1;

	if (!a || !b || a->type != b->type)
		return -1;
	switch (a->type) {
	case GEN_X400:
		result = ASN1_STRING_cmp(a->d.x400Address, b->d.x400Address);
		break;

	case GEN_EDIPARTY:
		result = EDIPARTYNAME_cmp(a->d.ediPartyName, b->d.ediPartyName);
		break;

	case GEN_OTHERNAME:
		result = OTHERNAME_cmp(a->d.otherName, b->d.otherName);
		break;

	case GEN_EMAIL:
	case GEN_DNS:
	case GEN_URI:
		result = ASN1_STRING_cmp(a->d.ia5, b->d.ia5);
		break;

	case GEN_DIRNAME:
		result = X509_NAME_cmp(a->d.dirn, b->d.dirn);
		break;

	case GEN_IPADD:
		result = ASN1_OCTET_STRING_cmp(a->d.ip, b->d.ip);
		break;

	case GEN_RID:
		result = OBJ_cmp(a->d.rid, b->d.rid);
		break;
	}
	return result;
}
LCRYPTO_ALIAS(GENERAL_NAME_cmp);

/* Returns 0 if they are equal, != 0 otherwise. */
int
OTHERNAME_cmp(OTHERNAME *a, OTHERNAME *b)
{
	int result = -1;

	if (!a || !b)
		return -1;
	/* Check their type first. */
	if ((result = OBJ_cmp(a->type_id, b->type_id)) != 0)
		return result;
	/* Check the value. */
	result = ASN1_TYPE_cmp(a->value, b->value);
	return result;
}
LCRYPTO_ALIAS(OTHERNAME_cmp);

void
GENERAL_NAME_set0_value(GENERAL_NAME *a, int type, void *value)
{
	switch (type) {
	case GEN_X400:
		a->d.x400Address = value;
		break;

	case GEN_EDIPARTY:
		a->d.ediPartyName = value;
		break;

	case GEN_OTHERNAME:
		a->d.otherName = value;
		break;

	case GEN_EMAIL:
	case GEN_DNS:
	case GEN_URI:
		a->d.ia5 = value;
		break;

	case GEN_DIRNAME:
		a->d.dirn = value;
		break;

	case GEN_IPADD:
		a->d.ip = value;
		break;

	case GEN_RID:
		a->d.rid = value;
		break;
	}
	a->type = type;
}
LCRYPTO_ALIAS(GENERAL_NAME_set0_value);

void *
GENERAL_NAME_get0_value(GENERAL_NAME *a, int *ptype)
{
	if (ptype)
		*ptype = a->type;
	switch (a->type) {
	case GEN_X400:
		return a->d.x400Address;

	case GEN_EDIPARTY:
		return a->d.ediPartyName;

	case GEN_OTHERNAME:
		return a->d.otherName;

	case GEN_EMAIL:
	case GEN_DNS:
	case GEN_URI:
		return a->d.ia5;

	case GEN_DIRNAME:
		return a->d.dirn;

	case GEN_IPADD:
		return a->d.ip;

	case GEN_RID:
		return a->d.rid;

	default:
		return NULL;
	}
}
LCRYPTO_ALIAS(GENERAL_NAME_get0_value);

int
GENERAL_NAME_set0_othername(GENERAL_NAME *gen, ASN1_OBJECT *oid,
    ASN1_TYPE *value)
{
	OTHERNAME *oth;

	oth = OTHERNAME_new();
	if (!oth)
		return 0;
	oth->type_id = oid;
	oth->value = value;
	GENERAL_NAME_set0_value(gen, GEN_OTHERNAME, oth);
	return 1;
}
LCRYPTO_ALIAS(GENERAL_NAME_set0_othername);

int
GENERAL_NAME_get0_otherName(GENERAL_NAME *gen, ASN1_OBJECT **poid,
    ASN1_TYPE **pvalue)
{
	if (gen->type != GEN_OTHERNAME)
		return 0;
	if (poid)
		*poid = gen->d.otherName->type_id;
	if (pvalue)
		*pvalue = gen->d.otherName->value;
	return 1;
}
LCRYPTO_ALIAS(GENERAL_NAME_get0_otherName);
