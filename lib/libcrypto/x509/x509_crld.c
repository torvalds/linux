/* $OpenBSD: x509_crld.c,v 1.10 2025/05/10 05:54:39 tb Exp $ */
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
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static void *v2i_crld(const X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static int i2r_crldp(const X509V3_EXT_METHOD *method, void *pcrldp, BIO *out,
    int indent);

static const X509V3_EXT_METHOD x509v3_ext_crl_distribution_points = {
	.ext_nid = NID_crl_distribution_points,
	.ext_flags = 0,
	.it = &CRL_DIST_POINTS_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = v2i_crld,
	.i2r = i2r_crldp,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_crl_distribution_points(void)
{
	return &x509v3_ext_crl_distribution_points;
}

static const X509V3_EXT_METHOD x509v3_ext_freshest_crl = {
	.ext_nid = NID_freshest_crl,
	.ext_flags = 0,
	.it = &CRL_DIST_POINTS_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = v2i_crld,
	.i2r = i2r_crldp,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_freshest_crl(void)
{
	return &x509v3_ext_freshest_crl;
}

static STACK_OF(GENERAL_NAME) *
gnames_from_sectname(X509V3_CTX *ctx, char *sect)
{
	STACK_OF(CONF_VALUE) *gnsect;
	STACK_OF(GENERAL_NAME) *gens;

	if (*sect == '@')
		gnsect = X509V3_get0_section(ctx, sect + 1);
	else
		gnsect = X509V3_parse_list(sect);
	if (!gnsect) {
		X509V3error(X509V3_R_SECTION_NOT_FOUND);
		return NULL;
	}
	gens = v2i_GENERAL_NAMES(NULL, ctx, gnsect);
	if (*sect != '@')
		sk_CONF_VALUE_pop_free(gnsect, X509V3_conf_free);
	return gens;
}

static int
set_dist_point_name(DIST_POINT_NAME **pdp, X509V3_CTX *ctx, CONF_VALUE *cnf)
{
	STACK_OF(GENERAL_NAME) *fnm = NULL;
	STACK_OF(X509_NAME_ENTRY) *rnm = NULL;

	if (!strncmp(cnf->name, "fullname", 9)) {
		fnm = gnames_from_sectname(ctx, cnf->value);
		if (!fnm)
			goto err;
	} else if (!strcmp(cnf->name, "relativename")) {
		int ret;
		STACK_OF(CONF_VALUE) *dnsect;
		X509_NAME *nm;
		nm = X509_NAME_new();
		if (!nm)
			return -1;
		dnsect = X509V3_get0_section(ctx, cnf->value);
		if (!dnsect) {
			X509V3error(X509V3_R_SECTION_NOT_FOUND);
			X509_NAME_free(nm);
			return -1;
		}
		ret = X509V3_NAME_from_section(nm, dnsect, MBSTRING_ASC);
		rnm = nm->entries;
		nm->entries = NULL;
		X509_NAME_free(nm);
		if (!ret || sk_X509_NAME_ENTRY_num(rnm) <= 0)
			goto err;
		/* Since its a name fragment can't have more than one
		 * RDNSequence
		 */
		if (sk_X509_NAME_ENTRY_value(rnm,
		    sk_X509_NAME_ENTRY_num(rnm) - 1)->set) {
			X509V3error(X509V3_R_INVALID_MULTIPLE_RDNS);
			goto err;
		}
	} else
		return 0;

	if (*pdp) {
		X509V3error(X509V3_R_DISTPOINT_ALREADY_SET);
		goto err;
	}

	*pdp = DIST_POINT_NAME_new();
	if (!*pdp)
		goto err;
	if (fnm) {
		(*pdp)->type = 0;
		(*pdp)->name.fullname = fnm;
	} else {
		(*pdp)->type = 1;
		(*pdp)->name.relativename = rnm;
	}

	return 1;

err:
	sk_GENERAL_NAME_pop_free(fnm, GENERAL_NAME_free);
	sk_X509_NAME_ENTRY_pop_free(rnm, X509_NAME_ENTRY_free);
	return -1;
}

static const BIT_STRING_BITNAME reason_flags[] = {
	{0, "Unused", "unused"},
	{1, "Key Compromise", "keyCompromise"},
	{2, "CA Compromise", "CACompromise"},
	{3, "Affiliation Changed", "affiliationChanged"},
	{4, "Superseded", "superseded"},
	{5, "Cessation Of Operation", "cessationOfOperation"},
	{6, "Certificate Hold", "certificateHold"},
	{7, "Privilege Withdrawn", "privilegeWithdrawn"},
	{8, "AA Compromise", "AACompromise"},
	{-1, NULL, NULL}
};

static int
set_reasons(ASN1_BIT_STRING **preas, char *value)
{
	STACK_OF(CONF_VALUE) *rsk = NULL;
	const BIT_STRING_BITNAME *pbn;
	const char *bnam;
	int i, ret = 0;

	if (*preas != NULL)
		return 0;
	rsk = X509V3_parse_list(value);
	if (rsk == NULL)
		return 0;
	for (i = 0; i < sk_CONF_VALUE_num(rsk); i++) {
		bnam = sk_CONF_VALUE_value(rsk, i)->name;
		if (!*preas) {
			*preas = ASN1_BIT_STRING_new();
			if (!*preas)
				goto err;
		}
		for (pbn = reason_flags; pbn->lname; pbn++) {
			if (!strcmp(pbn->sname, bnam)) {
				if (!ASN1_BIT_STRING_set_bit(*preas,
				    pbn->bitnum, 1))
					goto err;
				break;
			}
		}
		if (!pbn->lname)
			goto err;
	}
	ret = 1;

err:
	sk_CONF_VALUE_pop_free(rsk, X509V3_conf_free);
	return ret;
}

static int
print_reasons(BIO *out, const char *rname, ASN1_BIT_STRING *rflags, int indent)
{
	int first = 1;
	const BIT_STRING_BITNAME *pbn;

	BIO_printf(out, "%*s%s:\n%*s", indent, "", rname, indent + 2, "");
	for (pbn = reason_flags; pbn->lname; pbn++) {
		if (ASN1_BIT_STRING_get_bit(rflags, pbn->bitnum)) {
			if (first)
				first = 0;
			else
				BIO_puts(out, ", ");
			BIO_puts(out, pbn->lname);
		}
	}
	if (first)
		BIO_puts(out, "<EMPTY>\n");
	else
		BIO_puts(out, "\n");
	return 1;
}

static DIST_POINT *
crldp_from_section(X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval)
{
	int i;
	CONF_VALUE *cnf;
	DIST_POINT *point = NULL;

	point = DIST_POINT_new();
	if (!point)
		goto err;
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		int ret;
		cnf = sk_CONF_VALUE_value(nval, i);
		ret = set_dist_point_name(&point->distpoint, ctx, cnf);
		if (ret > 0)
			continue;
		if (ret < 0)
			goto err;
		if (!strcmp(cnf->name, "reasons")) {
			if (!set_reasons(&point->reasons, cnf->value))
				goto err;
		}
		else if (!strcmp(cnf->name, "CRLissuer")) {
			point->CRLissuer =
			    gnames_from_sectname(ctx, cnf->value);
			if (!point->CRLissuer)
				goto err;
		}
	}

	return point;

err:
	DIST_POINT_free(point);
	return NULL;
}

static void *
v2i_crld(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	STACK_OF(DIST_POINT) *crld = NULL;
	GENERAL_NAMES *gens = NULL;
	GENERAL_NAME *gen = NULL;
	CONF_VALUE *cnf;
	int i;

	if (!(crld = sk_DIST_POINT_new_null()))
		goto merr;
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		DIST_POINT *point;
		cnf = sk_CONF_VALUE_value(nval, i);
		if (!cnf->value) {
			STACK_OF(CONF_VALUE) *dpsect;
			dpsect = X509V3_get0_section(ctx, cnf->name);
			if (!dpsect)
				goto err;
			point = crldp_from_section(ctx, dpsect);
			if (!point)
				goto err;
			if (!sk_DIST_POINT_push(crld, point)) {
				DIST_POINT_free(point);
				goto merr;
			}
		} else {
			if (!(gen = v2i_GENERAL_NAME(method, ctx, cnf)))
				goto err;
			if (!(gens = GENERAL_NAMES_new()))
				goto merr;
			if (!sk_GENERAL_NAME_push(gens, gen))
				goto merr;
			gen = NULL;
			if (!(point = DIST_POINT_new()))
				goto merr;
			if (!sk_DIST_POINT_push(crld, point)) {
				DIST_POINT_free(point);
				goto merr;
			}
			if (!(point->distpoint = DIST_POINT_NAME_new()))
				goto merr;
			point->distpoint->name.fullname = gens;
			point->distpoint->type = 0;
			gens = NULL;
		}
	}
	return crld;

merr:
	X509V3error(ERR_R_MALLOC_FAILURE);
err:
	GENERAL_NAME_free(gen);
	GENERAL_NAMES_free(gens);
	sk_DIST_POINT_pop_free(crld, DIST_POINT_free);
	return NULL;
}

static int
dpn_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	DIST_POINT_NAME *dpn = (DIST_POINT_NAME *)*pval;

	switch (operation) {
	case ASN1_OP_NEW_POST:
		dpn->dpname = NULL;
		break;

	case ASN1_OP_FREE_POST:
		if (dpn->dpname)
			X509_NAME_free(dpn->dpname);
		break;
	}
	return 1;
}


static const ASN1_AUX DIST_POINT_NAME_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = dpn_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE DIST_POINT_NAME_ch_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(DIST_POINT_NAME, name.fullname),
		.field_name = "name.fullname",
		.item = &GENERAL_NAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF,
		.tag = 1,
		.offset = offsetof(DIST_POINT_NAME, name.relativename),
		.field_name = "name.relativename",
		.item = &X509_NAME_ENTRY_it,
	},
};

const ASN1_ITEM DIST_POINT_NAME_it = {
	.itype = ASN1_ITYPE_CHOICE,
	.utype = offsetof(DIST_POINT_NAME, type),
	.templates = DIST_POINT_NAME_ch_tt,
	.tcount = sizeof(DIST_POINT_NAME_ch_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &DIST_POINT_NAME_aux,
	.size = sizeof(DIST_POINT_NAME),
	.sname = "DIST_POINT_NAME",
};
LCRYPTO_ALIAS(DIST_POINT_NAME_it);



DIST_POINT_NAME *
d2i_DIST_POINT_NAME(DIST_POINT_NAME **a, const unsigned char **in, long len)
{
	return (DIST_POINT_NAME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DIST_POINT_NAME_it);
}
LCRYPTO_ALIAS(d2i_DIST_POINT_NAME);

int
i2d_DIST_POINT_NAME(DIST_POINT_NAME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DIST_POINT_NAME_it);
}
LCRYPTO_ALIAS(i2d_DIST_POINT_NAME);

DIST_POINT_NAME *
DIST_POINT_NAME_new(void)
{
	return (DIST_POINT_NAME *)ASN1_item_new(&DIST_POINT_NAME_it);
}
LCRYPTO_ALIAS(DIST_POINT_NAME_new);

void
DIST_POINT_NAME_free(DIST_POINT_NAME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &DIST_POINT_NAME_it);
}
LCRYPTO_ALIAS(DIST_POINT_NAME_free);

static const ASN1_TEMPLATE DIST_POINT_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(DIST_POINT, distpoint),
		.field_name = "distpoint",
		.item = &DIST_POINT_NAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(DIST_POINT, reasons),
		.field_name = "reasons",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(DIST_POINT, CRLissuer),
		.field_name = "CRLissuer",
		.item = &GENERAL_NAME_it,
	},
};

const ASN1_ITEM DIST_POINT_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = DIST_POINT_seq_tt,
	.tcount = sizeof(DIST_POINT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(DIST_POINT),
	.sname = "DIST_POINT",
};
LCRYPTO_ALIAS(DIST_POINT_it);


DIST_POINT *
d2i_DIST_POINT(DIST_POINT **a, const unsigned char **in, long len)
{
	return (DIST_POINT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DIST_POINT_it);
}
LCRYPTO_ALIAS(d2i_DIST_POINT);

int
i2d_DIST_POINT(DIST_POINT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DIST_POINT_it);
}
LCRYPTO_ALIAS(i2d_DIST_POINT);

DIST_POINT *
DIST_POINT_new(void)
{
	return (DIST_POINT *)ASN1_item_new(&DIST_POINT_it);
}
LCRYPTO_ALIAS(DIST_POINT_new);

void
DIST_POINT_free(DIST_POINT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &DIST_POINT_it);
}
LCRYPTO_ALIAS(DIST_POINT_free);

static const ASN1_TEMPLATE CRL_DIST_POINTS_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "CRLDistributionPoints",
	.item = &DIST_POINT_it,
};

const ASN1_ITEM CRL_DIST_POINTS_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &CRL_DIST_POINTS_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "CRL_DIST_POINTS",
};
LCRYPTO_ALIAS(CRL_DIST_POINTS_it);


CRL_DIST_POINTS *
d2i_CRL_DIST_POINTS(CRL_DIST_POINTS **a, const unsigned char **in, long len)
{
	return (CRL_DIST_POINTS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &CRL_DIST_POINTS_it);
}
LCRYPTO_ALIAS(d2i_CRL_DIST_POINTS);

int
i2d_CRL_DIST_POINTS(CRL_DIST_POINTS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &CRL_DIST_POINTS_it);
}
LCRYPTO_ALIAS(i2d_CRL_DIST_POINTS);

CRL_DIST_POINTS *
CRL_DIST_POINTS_new(void)
{
	return (CRL_DIST_POINTS *)ASN1_item_new(&CRL_DIST_POINTS_it);
}
LCRYPTO_ALIAS(CRL_DIST_POINTS_new);

void
CRL_DIST_POINTS_free(CRL_DIST_POINTS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &CRL_DIST_POINTS_it);
}
LCRYPTO_ALIAS(CRL_DIST_POINTS_free);

static const ASN1_TEMPLATE ISSUING_DIST_POINT_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ISSUING_DIST_POINT, distpoint),
		.field_name = "distpoint",
		.item = &DIST_POINT_NAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(ISSUING_DIST_POINT, onlyuser),
		.field_name = "onlyuser",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(ISSUING_DIST_POINT, onlyCA),
		.field_name = "onlyCA",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 3,
		.offset = offsetof(ISSUING_DIST_POINT, onlysomereasons),
		.field_name = "onlysomereasons",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 4,
		.offset = offsetof(ISSUING_DIST_POINT, indirectCRL),
		.field_name = "indirectCRL",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 5,
		.offset = offsetof(ISSUING_DIST_POINT, onlyattr),
		.field_name = "onlyattr",
		.item = &ASN1_FBOOLEAN_it,
	},
};

const ASN1_ITEM ISSUING_DIST_POINT_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ISSUING_DIST_POINT_seq_tt,
	.tcount = sizeof(ISSUING_DIST_POINT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ISSUING_DIST_POINT),
	.sname = "ISSUING_DIST_POINT",
};
LCRYPTO_ALIAS(ISSUING_DIST_POINT_it);


ISSUING_DIST_POINT *
d2i_ISSUING_DIST_POINT(ISSUING_DIST_POINT **a, const unsigned char **in, long len)
{
	return (ISSUING_DIST_POINT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ISSUING_DIST_POINT_it);
}
LCRYPTO_ALIAS(d2i_ISSUING_DIST_POINT);

int
i2d_ISSUING_DIST_POINT(ISSUING_DIST_POINT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ISSUING_DIST_POINT_it);
}
LCRYPTO_ALIAS(i2d_ISSUING_DIST_POINT);

ISSUING_DIST_POINT *
ISSUING_DIST_POINT_new(void)
{
	return (ISSUING_DIST_POINT *)ASN1_item_new(&ISSUING_DIST_POINT_it);
}
LCRYPTO_ALIAS(ISSUING_DIST_POINT_new);

void
ISSUING_DIST_POINT_free(ISSUING_DIST_POINT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ISSUING_DIST_POINT_it);
}
LCRYPTO_ALIAS(ISSUING_DIST_POINT_free);

static int i2r_idp(const X509V3_EXT_METHOD *method, void *pidp, BIO *out,
    int indent);
static void *v2i_idp(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval);

static const X509V3_EXT_METHOD x509v3_ext_issuing_distribution_point = {
	.ext_nid = NID_issuing_distribution_point,
	.ext_flags = X509V3_EXT_MULTILINE,
	.it = &ISSUING_DIST_POINT_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = v2i_idp,
	.i2r = i2r_idp,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_issuing_distribution_point(void)
{
	return &x509v3_ext_issuing_distribution_point;
}

static void *
v2i_idp(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	ISSUING_DIST_POINT *idp = NULL;
	CONF_VALUE *cnf;
	char *name, *val;
	int i, ret;

	idp = ISSUING_DIST_POINT_new();
	if (!idp)
		goto merr;
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		cnf = sk_CONF_VALUE_value(nval, i);
		name = cnf->name;
		val = cnf->value;
		ret = set_dist_point_name(&idp->distpoint, ctx, cnf);
		if (ret > 0)
			continue;
		if (ret < 0)
			goto err;
		if (!strcmp(name, "onlyuser")) {
			if (!X509V3_get_value_bool(cnf, &idp->onlyuser))
				goto err;
		}
		else if (!strcmp(name, "onlyCA")) {
			if (!X509V3_get_value_bool(cnf, &idp->onlyCA))
				goto err;
		}
		else if (!strcmp(name, "onlyAA")) {
			if (!X509V3_get_value_bool(cnf, &idp->onlyattr))
				goto err;
		}
		else if (!strcmp(name, "indirectCRL")) {
			if (!X509V3_get_value_bool(cnf, &idp->indirectCRL))
				goto err;
		}
		else if (!strcmp(name, "onlysomereasons")) {
			if (!set_reasons(&idp->onlysomereasons, val))
				goto err;
		} else {
			X509V3error(X509V3_R_INVALID_NAME);
			X509V3_conf_err(cnf);
			goto err;
		}
	}
	return idp;

merr:
	X509V3error(ERR_R_MALLOC_FAILURE);
err:
	ISSUING_DIST_POINT_free(idp);
	return NULL;
}

static int
print_gens(BIO *out, STACK_OF(GENERAL_NAME) *gens, int indent)
{
	int i;

	for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
		BIO_printf(out, "%*s", indent + 2, "");
		GENERAL_NAME_print(out, sk_GENERAL_NAME_value(gens, i));
		BIO_puts(out, "\n");
	}
	return 1;
}

static int
print_distpoint(BIO *out, DIST_POINT_NAME *dpn, int indent)
{
	if (dpn->type == 0) {
		BIO_printf(out, "%*sFull Name:\n", indent, "");
		print_gens(out, dpn->name.fullname, indent);
	} else {
		X509_NAME ntmp;
		ntmp.entries = dpn->name.relativename;
		BIO_printf(out, "%*sRelative Name:\n%*s",
		    indent, "", indent + 2, "");
		X509_NAME_print_ex(out, &ntmp, 0, XN_FLAG_ONELINE);
		BIO_puts(out, "\n");
	}
	return 1;
}

static int
i2r_idp(const X509V3_EXT_METHOD *method, void *pidp, BIO *out, int indent)
{
	ISSUING_DIST_POINT *idp = pidp;

	if (idp->distpoint)
		print_distpoint(out, idp->distpoint, indent);
	if (idp->onlyuser > 0)
		BIO_printf(out, "%*sOnly User Certificates\n", indent, "");
	if (idp->onlyCA > 0)
		BIO_printf(out, "%*sOnly CA Certificates\n", indent, "");
	if (idp->indirectCRL > 0)
		BIO_printf(out, "%*sIndirect CRL\n", indent, "");
	if (idp->onlysomereasons)
		print_reasons(out, "Only Some Reasons",
	    idp->onlysomereasons, indent);
	if (idp->onlyattr > 0)
		BIO_printf(out, "%*sOnly Attribute Certificates\n", indent, "");
	if (!idp->distpoint && (idp->onlyuser <= 0) && (idp->onlyCA <= 0) &&
	    (idp->indirectCRL <= 0) && !idp->onlysomereasons &&
	    (idp->onlyattr <= 0))
		BIO_printf(out, "%*s<EMPTY>\n", indent, "");

	return 1;
}

static int
i2r_crldp(const X509V3_EXT_METHOD *method, void *pcrldp, BIO *out, int indent)
{
	STACK_OF(DIST_POINT) *crld = pcrldp;
	DIST_POINT *point;
	int i;

	for (i = 0; i < sk_DIST_POINT_num(crld); i++) {
		BIO_puts(out, "\n");
		point = sk_DIST_POINT_value(crld, i);
		if (point->distpoint)
			print_distpoint(out, point->distpoint, indent);
		if (point->reasons)
			print_reasons(out, "Reasons", point->reasons,
		    indent);
		if (point->CRLissuer) {
			BIO_printf(out, "%*sCRL Issuer:\n", indent, "");
			print_gens(out, point->CRLissuer, indent);
		}
	}
	return 1;
}

int
DIST_POINT_set_dpname(DIST_POINT_NAME *dpn, X509_NAME *iname)
{
	int i;
	STACK_OF(X509_NAME_ENTRY) *frag;
	X509_NAME_ENTRY *ne;

	if (!dpn || (dpn->type != 1))
		return 1;
	frag = dpn->name.relativename;
	dpn->dpname = X509_NAME_dup(iname);
	if (!dpn->dpname)
		return 0;
	for (i = 0; i < sk_X509_NAME_ENTRY_num(frag); i++) {
		ne = sk_X509_NAME_ENTRY_value(frag, i);
		if (!X509_NAME_add_entry(dpn->dpname, ne, -1, i ? 0 : 1)) {
			X509_NAME_free(dpn->dpname);
			dpn->dpname = NULL;
			return 0;
		}
	}
	/* generate cached encoding of name */
	if (i2d_X509_NAME(dpn->dpname, NULL) < 0) {
		X509_NAME_free(dpn->dpname);
		dpn->dpname = NULL;
		return 0;
	}
	return 1;
}
LCRYPTO_ALIAS(DIST_POINT_set_dpname);
