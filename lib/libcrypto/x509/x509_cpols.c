/* $OpenBSD: x509_cpols.c,v 1.16 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2004 The OpenSSL Project.  All rights reserved.
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

/* Certificate policies extension support: this one is a bit complex... */

static int i2r_certpol(X509V3_EXT_METHOD *method, STACK_OF(POLICYINFO) *pol,
    BIO *out, int indent);
static STACK_OF(POLICYINFO) *r2i_certpol(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, char *value);
static void print_qualifiers(BIO *out, STACK_OF(POLICYQUALINFO) *quals,
    int indent);
static void print_notice(BIO *out, USERNOTICE *notice, int indent);
static POLICYINFO *policy_section(X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *polstrs, int ia5org);
static POLICYQUALINFO *notice_section(X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *unot, int ia5org);
static int nref_nos(STACK_OF(ASN1_INTEGER) *nnums, STACK_OF(CONF_VALUE) *nos);

static const X509V3_EXT_METHOD x509v3_ext_certificate_policies = {
	.ext_nid = NID_certificate_policies,
	.ext_flags = 0,
	.it = &CERTIFICATEPOLICIES_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = (X509V3_EXT_I2R)i2r_certpol,
	.r2i = (X509V3_EXT_R2I)r2i_certpol,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_certificate_policies(void)
{
	return &x509v3_ext_certificate_policies;
}

static const ASN1_TEMPLATE CERTIFICATEPOLICIES_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "CERTIFICATEPOLICIES",
	.item = &POLICYINFO_it,
};

const ASN1_ITEM CERTIFICATEPOLICIES_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &CERTIFICATEPOLICIES_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "CERTIFICATEPOLICIES",
};
LCRYPTO_ALIAS(CERTIFICATEPOLICIES_it);


CERTIFICATEPOLICIES *
d2i_CERTIFICATEPOLICIES(CERTIFICATEPOLICIES **a, const unsigned char **in, long len)
{
	return (CERTIFICATEPOLICIES *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &CERTIFICATEPOLICIES_it);
}
LCRYPTO_ALIAS(d2i_CERTIFICATEPOLICIES);

int
i2d_CERTIFICATEPOLICIES(CERTIFICATEPOLICIES *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &CERTIFICATEPOLICIES_it);
}
LCRYPTO_ALIAS(i2d_CERTIFICATEPOLICIES);

CERTIFICATEPOLICIES *
CERTIFICATEPOLICIES_new(void)
{
	return (CERTIFICATEPOLICIES *)ASN1_item_new(&CERTIFICATEPOLICIES_it);
}
LCRYPTO_ALIAS(CERTIFICATEPOLICIES_new);

void
CERTIFICATEPOLICIES_free(CERTIFICATEPOLICIES *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &CERTIFICATEPOLICIES_it);
}
LCRYPTO_ALIAS(CERTIFICATEPOLICIES_free);

static const ASN1_TEMPLATE POLICYINFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(POLICYINFO, policyid),
		.field_name = "policyid",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(POLICYINFO, qualifiers),
		.field_name = "qualifiers",
		.item = &POLICYQUALINFO_it,
	},
};

const ASN1_ITEM POLICYINFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = POLICYINFO_seq_tt,
	.tcount = sizeof(POLICYINFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(POLICYINFO),
	.sname = "POLICYINFO",
};
LCRYPTO_ALIAS(POLICYINFO_it);


POLICYINFO *
d2i_POLICYINFO(POLICYINFO **a, const unsigned char **in, long len)
{
	return (POLICYINFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &POLICYINFO_it);
}
LCRYPTO_ALIAS(d2i_POLICYINFO);

int
i2d_POLICYINFO(POLICYINFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &POLICYINFO_it);
}
LCRYPTO_ALIAS(i2d_POLICYINFO);

POLICYINFO *
POLICYINFO_new(void)
{
	return (POLICYINFO *)ASN1_item_new(&POLICYINFO_it);
}
LCRYPTO_ALIAS(POLICYINFO_new);

void
POLICYINFO_free(POLICYINFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &POLICYINFO_it);
}
LCRYPTO_ALIAS(POLICYINFO_free);

static const ASN1_TEMPLATE policydefault_tt = {
	.flags = 0,
	.tag = 0,
	.offset = offsetof(POLICYQUALINFO, d.other),
	.field_name = "d.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE POLICYQUALINFO_adbtbl[] = {
	{
		.value = NID_id_qt_cps,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(POLICYQUALINFO, d.cpsuri),
			.field_name = "d.cpsuri",
			.item = &ASN1_IA5STRING_it,
		},
	},
	{
		.value = NID_id_qt_unotice,
		.tt = {
			.flags = 0,
			.tag = 0,
			.offset = offsetof(POLICYQUALINFO, d.usernotice),
			.field_name = "d.usernotice",
			.item = &USERNOTICE_it,
		},
	},
};

static const ASN1_ADB POLICYQUALINFO_adb = {
	.flags = 0,
	.offset = offsetof(POLICYQUALINFO, pqualid),
	.tbl = POLICYQUALINFO_adbtbl,
	.tblcount = sizeof(POLICYQUALINFO_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &policydefault_tt,
	.null_tt = NULL,
};

static const ASN1_TEMPLATE POLICYQUALINFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(POLICYQUALINFO, pqualid),
		.field_name = "pqualid",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "POLICYQUALINFO",
		.item = (const ASN1_ITEM *)&POLICYQUALINFO_adb,
	},
};

const ASN1_ITEM POLICYQUALINFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = POLICYQUALINFO_seq_tt,
	.tcount = sizeof(POLICYQUALINFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(POLICYQUALINFO),
	.sname = "POLICYQUALINFO",
};
LCRYPTO_ALIAS(POLICYQUALINFO_it);


POLICYQUALINFO *
d2i_POLICYQUALINFO(POLICYQUALINFO **a, const unsigned char **in, long len)
{
	return (POLICYQUALINFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &POLICYQUALINFO_it);
}
LCRYPTO_ALIAS(d2i_POLICYQUALINFO);

int
i2d_POLICYQUALINFO(POLICYQUALINFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &POLICYQUALINFO_it);
}
LCRYPTO_ALIAS(i2d_POLICYQUALINFO);

POLICYQUALINFO *
POLICYQUALINFO_new(void)
{
	return (POLICYQUALINFO *)ASN1_item_new(&POLICYQUALINFO_it);
}
LCRYPTO_ALIAS(POLICYQUALINFO_new);

void
POLICYQUALINFO_free(POLICYQUALINFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &POLICYQUALINFO_it);
}
LCRYPTO_ALIAS(POLICYQUALINFO_free);

static const ASN1_TEMPLATE USERNOTICE_seq_tt[] = {
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(USERNOTICE, noticeref),
		.field_name = "noticeref",
		.item = &NOTICEREF_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(USERNOTICE, exptext),
		.field_name = "exptext",
		.item = &DISPLAYTEXT_it,
	},
};

const ASN1_ITEM USERNOTICE_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = USERNOTICE_seq_tt,
	.tcount = sizeof(USERNOTICE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(USERNOTICE),
	.sname = "USERNOTICE",
};
LCRYPTO_ALIAS(USERNOTICE_it);


USERNOTICE *
d2i_USERNOTICE(USERNOTICE **a, const unsigned char **in, long len)
{
	return (USERNOTICE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &USERNOTICE_it);
}
LCRYPTO_ALIAS(d2i_USERNOTICE);

int
i2d_USERNOTICE(USERNOTICE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &USERNOTICE_it);
}
LCRYPTO_ALIAS(i2d_USERNOTICE);

USERNOTICE *
USERNOTICE_new(void)
{
	return (USERNOTICE *)ASN1_item_new(&USERNOTICE_it);
}
LCRYPTO_ALIAS(USERNOTICE_new);

void
USERNOTICE_free(USERNOTICE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &USERNOTICE_it);
}
LCRYPTO_ALIAS(USERNOTICE_free);

static const ASN1_TEMPLATE NOTICEREF_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(NOTICEREF, organization),
		.field_name = "organization",
		.item = &DISPLAYTEXT_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(NOTICEREF, noticenos),
		.field_name = "noticenos",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM NOTICEREF_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = NOTICEREF_seq_tt,
	.tcount = sizeof(NOTICEREF_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(NOTICEREF),
	.sname = "NOTICEREF",
};
LCRYPTO_ALIAS(NOTICEREF_it);


NOTICEREF *
d2i_NOTICEREF(NOTICEREF **a, const unsigned char **in, long len)
{
	return (NOTICEREF *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &NOTICEREF_it);
}
LCRYPTO_ALIAS(d2i_NOTICEREF);

int
i2d_NOTICEREF(NOTICEREF *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &NOTICEREF_it);
}
LCRYPTO_ALIAS(i2d_NOTICEREF);

NOTICEREF *
NOTICEREF_new(void)
{
	return (NOTICEREF *)ASN1_item_new(&NOTICEREF_it);
}
LCRYPTO_ALIAS(NOTICEREF_new);

void
NOTICEREF_free(NOTICEREF *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &NOTICEREF_it);
}
LCRYPTO_ALIAS(NOTICEREF_free);

static STACK_OF(POLICYINFO) *
r2i_certpol(X509V3_EXT_METHOD *method, X509V3_CTX *ctx, char *value)
{
	STACK_OF(POLICYINFO) *pols = NULL;
	char *pstr;
	POLICYINFO *pol;
	ASN1_OBJECT *pobj;
	STACK_OF(CONF_VALUE) *vals;
	CONF_VALUE *cnf;
	int i, ia5org;

	pols = sk_POLICYINFO_new_null();
	if (pols == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	vals = X509V3_parse_list(value);
	if (vals == NULL) {
		X509V3error(ERR_R_X509V3_LIB);
		goto err;
	}
	ia5org = 0;
	for (i = 0; i < sk_CONF_VALUE_num(vals); i++) {
		cnf = sk_CONF_VALUE_value(vals, i);
		if (cnf->value || !cnf->name) {
			X509V3error(X509V3_R_INVALID_POLICY_IDENTIFIER);
			X509V3_conf_err(cnf);
			goto err;
		}
		pstr = cnf->name;
		if (!strcmp(pstr, "ia5org")) {
			ia5org = 1;
			continue;
		} else if (*pstr == '@') {
			STACK_OF(CONF_VALUE) *polsect;
			polsect = X509V3_get0_section(ctx, pstr + 1);
			if (!polsect) {
				X509V3error(X509V3_R_INVALID_SECTION);
				X509V3_conf_err(cnf);
				goto err;
			}
			pol = policy_section(ctx, polsect, ia5org);
			if (!pol)
				goto err;
		} else {
			if (!(pobj = OBJ_txt2obj(cnf->name, 0))) {
				X509V3error(X509V3_R_INVALID_OBJECT_IDENTIFIER);
				X509V3_conf_err(cnf);
				goto err;
			}
			pol = POLICYINFO_new();
			pol->policyid = pobj;
		}
		if (!sk_POLICYINFO_push(pols, pol)){
			POLICYINFO_free(pol);
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
	}
	sk_CONF_VALUE_pop_free(vals, X509V3_conf_free);
	return pols;

err:
	sk_CONF_VALUE_pop_free(vals, X509V3_conf_free);
	sk_POLICYINFO_pop_free(pols, POLICYINFO_free);
	return NULL;
}

static POLICYINFO *
policy_section(X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *polstrs, int ia5org)
{
	int i;
	CONF_VALUE *cnf;
	POLICYINFO *pol;
	POLICYQUALINFO *nqual = NULL;

	if ((pol = POLICYINFO_new()) == NULL)
		goto merr;
	for (i = 0; i < sk_CONF_VALUE_num(polstrs); i++) {
		cnf = sk_CONF_VALUE_value(polstrs, i);
		if (strcmp(cnf->name, "policyIdentifier") == 0) {
			ASN1_OBJECT *pobj;

			if ((pobj = OBJ_txt2obj(cnf->value, 0)) == NULL) {
				X509V3error(X509V3_R_INVALID_OBJECT_IDENTIFIER);
				X509V3_conf_err(cnf);
				goto err;
			}
			pol->policyid = pobj;
		} else if (name_cmp(cnf->name, "CPS") == 0) {
			if ((nqual = POLICYQUALINFO_new()) == NULL)
				goto merr;
			nqual->pqualid = OBJ_nid2obj(NID_id_qt_cps);
			nqual->d.cpsuri = ASN1_IA5STRING_new();
			if (nqual->d.cpsuri == NULL)
				goto merr;
			if (ASN1_STRING_set(nqual->d.cpsuri, cnf->value,
			    strlen(cnf->value)) == 0)
				goto merr;

			if (pol->qualifiers == NULL) {
				pol->qualifiers = sk_POLICYQUALINFO_new_null();
				if (pol->qualifiers == NULL)
					goto merr;
			}
			if (sk_POLICYQUALINFO_push(pol->qualifiers, nqual) == 0)
				goto merr;
			nqual = NULL;
		} else if (name_cmp(cnf->name, "userNotice") == 0) {
			STACK_OF(CONF_VALUE) *unot;
			POLICYQUALINFO *qual;

			if (*cnf->value != '@') {
				X509V3error(X509V3_R_EXPECTED_A_SECTION_NAME);
				X509V3_conf_err(cnf);
				goto err;
			}
			unot = X509V3_get0_section(ctx, cnf->value + 1);
			if (unot == NULL) {
				X509V3error(X509V3_R_INVALID_SECTION);
				X509V3_conf_err(cnf);
				goto err;
			}
			qual = notice_section(ctx, unot, ia5org);
			if (qual == NULL)
				goto err;

			if (pol->qualifiers == NULL) {
				pol->qualifiers = sk_POLICYQUALINFO_new_null();
				if (pol->qualifiers == NULL)
					goto merr;
			}
			if (sk_POLICYQUALINFO_push(pol->qualifiers, qual) == 0)
				goto merr;
		} else {
			X509V3error(X509V3_R_INVALID_OPTION);
			X509V3_conf_err(cnf);
			goto err;
		}
	}
	if (pol->policyid == NULL) {
		X509V3error(X509V3_R_NO_POLICY_IDENTIFIER);
		goto err;
	}

	return pol;

merr:
	X509V3error(ERR_R_MALLOC_FAILURE);

err:
	POLICYQUALINFO_free(nqual);
	POLICYINFO_free(pol);
	return NULL;
}

static POLICYQUALINFO *
notice_section(X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *unot, int ia5org)
{
	int i, ret;
	CONF_VALUE *cnf;
	USERNOTICE *not;
	POLICYQUALINFO *qual;

	if (!(qual = POLICYQUALINFO_new()))
		goto merr;
	qual->pqualid = OBJ_nid2obj(NID_id_qt_unotice);
	if (!(not = USERNOTICE_new()))
		goto merr;
	qual->d.usernotice = not;
	for (i = 0; i < sk_CONF_VALUE_num(unot); i++) {
		cnf = sk_CONF_VALUE_value(unot, i);
		if (!strcmp(cnf->name, "explicitText")) {
			if (not->exptext == NULL) {
				not->exptext = ASN1_UTF8STRING_new();
				if (not->exptext == NULL)
					goto merr;
			}
			if (!ASN1_STRING_set(not->exptext, cnf->value,
			    strlen(cnf->value)))
				goto merr;
		} else if (!strcmp(cnf->name, "organization")) {
			NOTICEREF *nref;
			if (!not->noticeref) {
				if (!(nref = NOTICEREF_new()))
					goto merr;
				not->noticeref = nref;
			} else
				nref = not->noticeref;
			if (ia5org)
				nref->organization->type = V_ASN1_IA5STRING;
			else
				nref->organization->type = V_ASN1_VISIBLESTRING;
			if (!ASN1_STRING_set(nref->organization, cnf->value,
			    strlen(cnf->value)))
				goto merr;
		} else if (!strcmp(cnf->name, "noticeNumbers")) {
			NOTICEREF *nref;
			STACK_OF(CONF_VALUE) *nos;
			if (!not->noticeref) {
				if (!(nref = NOTICEREF_new()))
					goto merr;
				not->noticeref = nref;
			} else
				nref = not->noticeref;
			nos = X509V3_parse_list(cnf->value);
			if (!nos || !sk_CONF_VALUE_num(nos)) {
				X509V3error(X509V3_R_INVALID_NUMBERS);
				X509V3_conf_err(cnf);
				if (nos != NULL)
					sk_CONF_VALUE_pop_free(nos,
					    X509V3_conf_free);
				goto err;
			}
			ret = nref_nos(nref->noticenos, nos);
			sk_CONF_VALUE_pop_free(nos, X509V3_conf_free);
			if (!ret)
				goto err;
		} else {
			X509V3error(X509V3_R_INVALID_OPTION);
			X509V3_conf_err(cnf);
			goto err;
		}
	}

	if (not->noticeref &&
	    (!not->noticeref->noticenos || !not->noticeref->organization)) {
		X509V3error(X509V3_R_NEED_ORGANIZATION_AND_NUMBERS);
		goto err;
	}

	return qual;

merr:
	X509V3error(ERR_R_MALLOC_FAILURE);

err:
	POLICYQUALINFO_free(qual);
	return NULL;
}

static int
nref_nos(STACK_OF(ASN1_INTEGER) *nnums, STACK_OF(CONF_VALUE) *nos)
{
	CONF_VALUE *cnf;
	ASN1_INTEGER *aint;
	int i;

	for (i = 0; i < sk_CONF_VALUE_num(nos); i++) {
		cnf = sk_CONF_VALUE_value(nos, i);
		if (!(aint = s2i_ASN1_INTEGER(NULL, cnf->name))) {
			X509V3error(X509V3_R_INVALID_NUMBER);
			goto err;
		}
		if (!sk_ASN1_INTEGER_push(nnums, aint))
			goto merr;
	}
	return 1;

merr:
	X509V3error(ERR_R_MALLOC_FAILURE);

err:
	sk_ASN1_INTEGER_pop_free(nnums, ASN1_STRING_free);
	return 0;
}

static int
i2r_certpol(X509V3_EXT_METHOD *method, STACK_OF(POLICYINFO) *pol, BIO *out,
    int indent)
{
	int i;
	POLICYINFO *pinfo;

	/* First print out the policy OIDs */
	for (i = 0; i < sk_POLICYINFO_num(pol); i++) {
		pinfo = sk_POLICYINFO_value(pol, i);
		BIO_printf(out, "%*sPolicy: ", indent, "");
		i2a_ASN1_OBJECT(out, pinfo->policyid);
		BIO_puts(out, "\n");
		if (pinfo->qualifiers)
			print_qualifiers(out, pinfo->qualifiers, indent + 2);
	}
	return 1;
}

static void
print_qualifiers(BIO *out, STACK_OF(POLICYQUALINFO) *quals, int indent)
{
	POLICYQUALINFO *qualinfo;
	int i;

	for (i = 0; i < sk_POLICYQUALINFO_num(quals); i++) {
		qualinfo = sk_POLICYQUALINFO_value(quals, i);
		switch (OBJ_obj2nid(qualinfo->pqualid)) {
		case NID_id_qt_cps:
			BIO_printf(out, "%*sCPS: %.*s\n", indent, "",
			    qualinfo->d.cpsuri->length,
			    qualinfo->d.cpsuri->data);
			break;

		case NID_id_qt_unotice:
			BIO_printf(out, "%*sUser Notice:\n", indent, "");
			print_notice(out, qualinfo->d.usernotice, indent + 2);
			break;

		default:
			BIO_printf(out, "%*sUnknown Qualifier: ",
			    indent + 2, "");

			i2a_ASN1_OBJECT(out, qualinfo->pqualid);
			BIO_puts(out, "\n");
			break;
		}
	}
}

static void
print_notice(BIO *out, USERNOTICE *notice, int indent)
{
	int i;

	if (notice->noticeref) {
		NOTICEREF *ref;
		ref = notice->noticeref;
		BIO_printf(out, "%*sOrganization: %.*s\n", indent, "",
		    ref->organization->length, ref->organization->data);
		BIO_printf(out, "%*sNumber%s: ", indent, "",
		    sk_ASN1_INTEGER_num(ref->noticenos) > 1 ? "s" : "");
		for (i = 0; i < sk_ASN1_INTEGER_num(ref->noticenos); i++) {
			ASN1_INTEGER *num;
			char *tmp;
			num = sk_ASN1_INTEGER_value(ref->noticenos, i);
			if (i)
				BIO_puts(out, ", ");
			tmp = i2s_ASN1_INTEGER(NULL, num);
			BIO_puts(out, tmp);
			free(tmp);
		}
		BIO_puts(out, "\n");
	}
	if (notice->exptext)
		BIO_printf(out, "%*sExplicit Text: %.*s\n", indent, "",
		    notice->exptext->length, notice->exptext->data);
}
