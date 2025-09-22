/* $OpenBSD: x509_alt.c,v 1.20 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 1999-2003 The OpenSSL Project.  All rights reserved.
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

#include <openssl/conf.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_internal.h"

static GENERAL_NAMES *v2i_subject_alt(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static GENERAL_NAMES *v2i_issuer_alt(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static int copy_email(X509V3_CTX *ctx, GENERAL_NAMES *gens, int move_p);
static int copy_issuer(X509V3_CTX *ctx, GENERAL_NAMES *gens);
static int do_othername(GENERAL_NAME *gen, const char *value, X509V3_CTX *ctx);
static int do_dirname(GENERAL_NAME *gen, const char *value, X509V3_CTX *ctx);

static const X509V3_EXT_METHOD x509v3_ext_subject_alt_name = {
	.ext_nid = NID_subject_alt_name,
	.ext_flags = 0,
	.it = &GENERAL_NAMES_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = (X509V3_EXT_I2V)i2v_GENERAL_NAMES,
	.v2i = (X509V3_EXT_V2I)v2i_subject_alt,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_subject_alt_name(void)
{
	return &x509v3_ext_subject_alt_name;
}

static const X509V3_EXT_METHOD x509v3_ext_issuer_alt_name = {
	.ext_nid = NID_issuer_alt_name,
	.ext_flags = 0,
	.it = &GENERAL_NAMES_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = (X509V3_EXT_I2V)i2v_GENERAL_NAMES,
	.v2i = (X509V3_EXT_V2I)v2i_issuer_alt,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_issuer_alt_name(void)
{
	return &x509v3_ext_issuer_alt_name;
}

static const X509V3_EXT_METHOD x509v3_ext_certificate_issuer = {
	.ext_nid = NID_certificate_issuer,
	.ext_flags = 0,
	.it = &GENERAL_NAMES_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = (X509V3_EXT_I2V)i2v_GENERAL_NAMES,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_certificate_issuer(void)
{
	return &x509v3_ext_certificate_issuer;
}

STACK_OF(CONF_VALUE) *
i2v_GENERAL_NAMES(X509V3_EXT_METHOD *method, GENERAL_NAMES *gens,
    STACK_OF(CONF_VALUE) *ret)
{
	STACK_OF(CONF_VALUE) *free_ret = NULL;
	GENERAL_NAME *gen;
	int i;

	if (ret == NULL) {
		if ((free_ret = ret = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
		if ((gen = sk_GENERAL_NAME_value(gens, i)) == NULL)
			goto err;
		if ((ret = i2v_GENERAL_NAME(method, gen, ret)) == NULL)
			goto err;
	}

	return ret;

 err:
	sk_CONF_VALUE_pop_free(free_ret, X509V3_conf_free);

	return NULL;
}
LCRYPTO_ALIAS(i2v_GENERAL_NAMES);

STACK_OF(CONF_VALUE) *
i2v_GENERAL_NAME(X509V3_EXT_METHOD *method, GENERAL_NAME *gen,
    STACK_OF(CONF_VALUE) *ret)
{
	STACK_OF(CONF_VALUE) *free_ret = NULL;
	unsigned char *p;
	char oline[256], htmp[5];
	int i;

	if (ret == NULL) {
		if ((free_ret = ret = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	switch (gen->type) {
	case GEN_OTHERNAME:
		if (!X509V3_add_value("othername", "<unsupported>", &ret))
			goto err;
		break;

	case GEN_X400:
		if (!X509V3_add_value("X400Name", "<unsupported>", &ret))
			goto err;
		break;

	case GEN_EDIPARTY:
		if (!X509V3_add_value("EdiPartyName", "<unsupported>", &ret))
			goto err;
		break;

	case GEN_EMAIL:
		if (!X509V3_add_value_uchar("email", gen->d.ia5->data, &ret))
			goto err;
		break;

	case GEN_DNS:
		if (!X509V3_add_value_uchar("DNS", gen->d.ia5->data, &ret))
			goto err;
		break;

	case GEN_URI:
		if (!X509V3_add_value_uchar("URI", gen->d.ia5->data, &ret))
			goto err;
		break;

	case GEN_DIRNAME:
		if (X509_NAME_oneline(gen->d.dirn, oline, 256) == NULL)
			goto err;
		if (!X509V3_add_value("DirName", oline, &ret))
			goto err;
		break;

	case GEN_IPADD: /* XXX */
		p = gen->d.ip->data;
		if (gen->d.ip->length == 4)
			(void) snprintf(oline, sizeof oline,
			    "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
		else if (gen->d.ip->length == 16) {
			oline[0] = 0;
			for (i = 0; i < 8; i++) {
				(void) snprintf(htmp, sizeof htmp,
				    "%X", p[0] << 8 | p[1]);
				p += 2;
				strlcat(oline, htmp, sizeof(oline));
				if (i != 7)
					strlcat(oline, ":", sizeof(oline));
			}
		} else {
			if (!X509V3_add_value("IP Address", "<invalid>", &ret))
				goto err;
			break;
		}
		if (!X509V3_add_value("IP Address", oline, &ret))
			goto err;
		break;

	case GEN_RID:
		if (!i2t_ASN1_OBJECT(oline, 256, gen->d.rid))
			goto err;
		if (!X509V3_add_value("Registered ID", oline, &ret))
			goto err;
		break;
	}

	return ret;

 err:
	sk_CONF_VALUE_pop_free(free_ret, X509V3_conf_free);

	return NULL;
}
LCRYPTO_ALIAS(i2v_GENERAL_NAME);

int
GENERAL_NAME_print(BIO *out, GENERAL_NAME *gen)
{
	unsigned char *p;
	int i;

	switch (gen->type) {
	case GEN_OTHERNAME:
		BIO_printf(out, "othername:<unsupported>");
		break;

	case GEN_X400:
		BIO_printf(out, "X400Name:<unsupported>");
		break;

	case GEN_EDIPARTY:
		/* Maybe fix this: it is supported now */
		BIO_printf(out, "EdiPartyName:<unsupported>");
		break;

	case GEN_EMAIL:
		BIO_printf(out, "email:%.*s", gen->d.ia5->length,
		    gen->d.ia5->data);
		break;

	case GEN_DNS:
		BIO_printf(out, "DNS:%.*s", gen->d.ia5->length,
		    gen->d.ia5->data);
		break;

	case GEN_URI:
		BIO_printf(out, "URI:%.*s", gen->d.ia5->length,
		    gen->d.ia5->data);
		break;

	case GEN_DIRNAME:
		BIO_printf(out, "DirName: ");
		X509_NAME_print_ex(out, gen->d.dirn, 0, XN_FLAG_ONELINE);
		break;

	case GEN_IPADD:
		p = gen->d.ip->data;
		if (gen->d.ip->length == 4)
			BIO_printf(out, "IP Address:%d.%d.%d.%d",
			    p[0], p[1], p[2], p[3]);
		else if (gen->d.ip->length == 16) {
			BIO_printf(out, "IP Address");
			for (i = 0; i < 8; i++) {
				BIO_printf(out, ":%X", p[0] << 8 | p[1]);
				p += 2;
			}
			BIO_puts(out, "\n");
		} else {
			BIO_printf(out, "IP Address:<invalid>");
			break;
		}
		break;

	case GEN_RID:
		BIO_printf(out, "Registered ID");
		i2a_ASN1_OBJECT(out, gen->d.rid);
		break;
	}
	return 1;
}
LCRYPTO_ALIAS(GENERAL_NAME_print);

static GENERAL_NAMES *
v2i_issuer_alt(X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	GENERAL_NAMES *gens = NULL;
	CONF_VALUE *cnf;
	int i;

	if ((gens = sk_GENERAL_NAME_new_null()) == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		cnf = sk_CONF_VALUE_value(nval, i);
		if (name_cmp(cnf->name, "issuer") == 0 && cnf->value != NULL &&
		    strcmp(cnf->value, "copy") == 0) {
			if (!copy_issuer(ctx, gens))
				goto err;
		} else {
			GENERAL_NAME *gen;
			if ((gen = v2i_GENERAL_NAME(method, ctx, cnf)) == NULL)
				goto err;
			if (sk_GENERAL_NAME_push(gens, gen) == 0) {
				GENERAL_NAME_free(gen);
				goto err;
			}
		}
	}
	return gens;

err:
	sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
	return NULL;
}

/* Append subject altname of issuer to issuer alt name of subject */

static int
copy_issuer(X509V3_CTX *ctx, GENERAL_NAMES *gens)
{
	GENERAL_NAMES *ialt = NULL;
	GENERAL_NAME *gen = NULL;
	X509_EXTENSION *ext;
	int i;
	int ret = 0;

	if (ctx && (ctx->flags == CTX_TEST))
		return 1;
	if (!ctx || !ctx->issuer_cert) {
		X509V3error(X509V3_R_NO_ISSUER_DETAILS);
		goto err;
	}
	i = X509_get_ext_by_NID(ctx->issuer_cert, NID_subject_alt_name, -1);
	if (i < 0)
		return 1;
	if (!(ext = X509_get_ext(ctx->issuer_cert, i)) ||
	    !(ialt = X509V3_EXT_d2i(ext))) {
		X509V3error(X509V3_R_ISSUER_DECODE_ERROR);
		goto err;
	}

	for (i = 0; i < sk_GENERAL_NAME_num(ialt); i++) {
		GENERAL_NAME *val = sk_GENERAL_NAME_value(ialt, i);

		if ((gen = GENERAL_NAME_dup(val)) == NULL)
			goto err;
		if (!sk_GENERAL_NAME_push(gens, gen)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		gen = NULL;
	}

	ret = 1;

 err:
	sk_GENERAL_NAME_pop_free(ialt, GENERAL_NAME_free);
	GENERAL_NAME_free(gen);

	return ret;
}

static GENERAL_NAMES *
v2i_subject_alt(X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	GENERAL_NAMES *gens = NULL;
	CONF_VALUE *cnf;
	int i;

	if (!(gens = sk_GENERAL_NAME_new_null())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		cnf = sk_CONF_VALUE_value(nval, i);
		if (!name_cmp(cnf->name, "email") && cnf->value &&
		    !strcmp(cnf->value, "copy")) {
			if (!copy_email(ctx, gens, 0))
				goto err;
		} else if (!name_cmp(cnf->name, "email") && cnf->value &&
		    !strcmp(cnf->value, "move")) {
			if (!copy_email(ctx, gens, 1))
				goto err;
		} else {
			GENERAL_NAME *gen;
			if (!(gen = v2i_GENERAL_NAME(method, ctx, cnf)))
				goto err;
			if (sk_GENERAL_NAME_push(gens, gen) == 0) {
				GENERAL_NAME_free(gen);
				goto err;
			}
		}
	}
	return gens;

err:
	sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
	return NULL;
}

/* Copy any email addresses in a certificate or request to
 * GENERAL_NAMES
 */

static int
copy_email(X509V3_CTX *ctx, GENERAL_NAMES *gens, int move_p)
{
	X509_NAME *nm;
	ASN1_IA5STRING *email = NULL;
	X509_NAME_ENTRY *ne;
	GENERAL_NAME *gen = NULL;
	int i;

	if (ctx != NULL && ctx->flags == CTX_TEST)
		return 1;
	if (!ctx || (!ctx->subject_cert && !ctx->subject_req)) {
		X509V3error(X509V3_R_NO_SUBJECT_DETAILS);
		goto err;
	}
	/* Find the subject name */
	if (ctx->subject_cert)
		nm = X509_get_subject_name(ctx->subject_cert);
	else
		nm = X509_REQ_get_subject_name(ctx->subject_req);

	/* Now add any email address(es) to STACK */
	i = -1;
	while ((i = X509_NAME_get_index_by_NID(nm,
	    NID_pkcs9_emailAddress, i)) >= 0) {
		ne = X509_NAME_get_entry(nm, i);
		email = ASN1_STRING_dup(X509_NAME_ENTRY_get_data(ne));
		if (move_p) {
			X509_NAME_delete_entry(nm, i);
			X509_NAME_ENTRY_free(ne);
			i--;
		}
		if (!email || !(gen = GENERAL_NAME_new())) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		gen->d.ia5 = email;
		email = NULL;
		gen->type = GEN_EMAIL;
		if (!sk_GENERAL_NAME_push(gens, gen)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		gen = NULL;
	}

	return 1;

err:
	GENERAL_NAME_free(gen);
	ASN1_IA5STRING_free(email);
	return 0;
}

GENERAL_NAMES *
v2i_GENERAL_NAMES(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *nval)
{
	GENERAL_NAME *gen;
	GENERAL_NAMES *gens = NULL;
	CONF_VALUE *cnf;
	int i;

	if (!(gens = sk_GENERAL_NAME_new_null())) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		cnf = sk_CONF_VALUE_value(nval, i);
		if (!(gen = v2i_GENERAL_NAME(method, ctx, cnf)))
			goto err;
		if (sk_GENERAL_NAME_push(gens, gen) == 0) {
			GENERAL_NAME_free(gen);
			goto err;
		}
	}
	return gens;

err:
	sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
	return NULL;
}
LCRYPTO_ALIAS(v2i_GENERAL_NAMES);

GENERAL_NAME *
v2i_GENERAL_NAME(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    CONF_VALUE *cnf)
{
	return v2i_GENERAL_NAME_ex(NULL, method, ctx, cnf, 0);
}
LCRYPTO_ALIAS(v2i_GENERAL_NAME);

GENERAL_NAME *
a2i_GENERAL_NAME(GENERAL_NAME *out, const X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, int gen_type, const char *value, int is_nc)
{
	char is_string = 0;
	GENERAL_NAME *gen = NULL;

	if (!value) {
		X509V3error(X509V3_R_MISSING_VALUE);
		return NULL;
	}

	if (out)
		gen = out;
	else {
		gen = GENERAL_NAME_new();
		if (gen == NULL) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			return NULL;
		}
	}

	switch (gen_type) {
	case GEN_URI:
	case GEN_EMAIL:
	case GEN_DNS:
		is_string = 1;
		break;

	case GEN_RID:
		{
			ASN1_OBJECT *obj;
			if (!(obj = OBJ_txt2obj(value, 0))) {
				X509V3error(X509V3_R_BAD_OBJECT);
				ERR_asprintf_error_data("value=%s", value);
				goto err;
			}
			gen->d.rid = obj;
		}
		break;

	case GEN_IPADD:
		if (is_nc)
			gen->d.ip = a2i_IPADDRESS_NC(value);
		else
			gen->d.ip = a2i_IPADDRESS(value);
		if (gen->d.ip == NULL) {
			X509V3error(X509V3_R_BAD_IP_ADDRESS);
			ERR_asprintf_error_data("value=%s", value);
			goto err;
		}
		break;

	case GEN_DIRNAME:
		if (!do_dirname(gen, value, ctx)) {
			X509V3error(X509V3_R_DIRNAME_ERROR);
			goto err;
		}
		break;

	case GEN_OTHERNAME:
		if (!do_othername(gen, value, ctx)) {
			X509V3error(X509V3_R_OTHERNAME_ERROR);
			goto err;
		}
		break;

	default:
		X509V3error(X509V3_R_UNSUPPORTED_TYPE);
		goto err;
	}

	if (is_string) {
		if (!(gen->d.ia5 = ASN1_IA5STRING_new()) ||
		    !ASN1_STRING_set(gen->d.ia5, value, strlen(value))) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
	}

	gen->type = gen_type;

	return gen;

err:
	if (out == NULL)
		GENERAL_NAME_free(gen);
	return NULL;
}
LCRYPTO_ALIAS(a2i_GENERAL_NAME);

GENERAL_NAME *
v2i_GENERAL_NAME_ex(GENERAL_NAME *out, const X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, CONF_VALUE *cnf, int is_nc)
{
	uint8_t *bytes = NULL;
	char *name, *value;
	GENERAL_NAME *ret;
	size_t len = 0;
	int type;
	CBS cbs;

	name = cnf->name;
	value = cnf->value;

	if (!value) {
		X509V3error(X509V3_R_MISSING_VALUE);
		return NULL;
	}

	if (!name_cmp(name, "email"))
		type = GEN_EMAIL;
	else if (!name_cmp(name, "URI"))
		type = GEN_URI;
	else if (!name_cmp(name, "DNS"))
		type = GEN_DNS;
	else if (!name_cmp(name, "RID"))
		type = GEN_RID;
	else if (!name_cmp(name, "IP"))
		type = GEN_IPADD;
	else if (!name_cmp(name, "dirName"))
		type = GEN_DIRNAME;
	else if (!name_cmp(name, "otherName"))
		type = GEN_OTHERNAME;
	else {
		X509V3error(X509V3_R_UNSUPPORTED_OPTION);
		ERR_asprintf_error_data("name=%s", name);
		return NULL;
	}

	ret = a2i_GENERAL_NAME(out, method, ctx, type, value, is_nc);
	if (ret == NULL)
		return NULL;

	/*
	 * Validate what we have for sanity.
	 */

	if (is_nc) {
		struct x509_constraints_name *constraints_name = NULL;

		if (!x509_constraints_validate(ret, &constraints_name, NULL)) {
			X509V3error(X509V3_R_BAD_OBJECT);
			ERR_asprintf_error_data("name=%s", name);
			goto err;
		}
		x509_constraints_name_free(constraints_name);
		return ret;
	}

	type = x509_constraints_general_to_bytes(ret, &bytes, &len);
	CBS_init(&cbs, bytes, len);
	switch (type) {
	case GEN_DNS:
		if (!x509_constraints_valid_sandns(&cbs)) {
			X509V3error(X509V3_R_BAD_OBJECT);
			ERR_asprintf_error_data("name=%s value='%.*s'", name,
			    (int)len, bytes);
			goto err;
		}
		break;
	case GEN_URI:
		if (!x509_constraints_uri_host(bytes, len, NULL)) {
			X509V3error(X509V3_R_BAD_OBJECT);
			ERR_asprintf_error_data("name=%s value='%.*s'", name,
			    (int)len, bytes);
			goto err;
		}
		break;
	case GEN_EMAIL:
		if (!x509_constraints_parse_mailbox(&cbs, NULL)) {
			X509V3error(X509V3_R_BAD_OBJECT);
			ERR_asprintf_error_data("name=%s value='%.*s'", name,
			    (int)len, bytes);
			goto err;
		}
		break;
	case GEN_IPADD:
		if (len != 4 && len != 16) {
			X509V3error(X509V3_R_BAD_IP_ADDRESS);
			ERR_asprintf_error_data("name=%s len=%zu", name, len);
			goto err;
		}
		break;
	default:
		break;
	}
	return ret;
 err:
	if (out == NULL)
		GENERAL_NAME_free(ret);
	return NULL;
}
LCRYPTO_ALIAS(v2i_GENERAL_NAME_ex);

static int
do_othername(GENERAL_NAME *gen, const char *value, X509V3_CTX *ctx)
{
	char *objtmp = NULL, *p;
	int objlen;

	if (!(p = strchr(value, ';')))
		return 0;
	if (!(gen->d.otherName = OTHERNAME_new()))
		return 0;
	/* Free this up because we will overwrite it.
	 * no need to free type_id because it is static
	 */
	ASN1_TYPE_free(gen->d.otherName->value);
	if (!(gen->d.otherName->value = ASN1_generate_v3(p + 1, ctx)))
		return 0;
	objlen = p - value;
	objtmp = malloc(objlen + 1);
	if (objtmp) {
		strlcpy(objtmp, value, objlen + 1);
		gen->d.otherName->type_id = OBJ_txt2obj(objtmp, 0);
		free(objtmp);
	} else
		gen->d.otherName->type_id = NULL;
	if (!gen->d.otherName->type_id)
		return 0;
	return 1;
}

static int
do_dirname(GENERAL_NAME *gen, const char *value, X509V3_CTX *ctx)
{
	int ret;
	STACK_OF(CONF_VALUE) *sk;
	X509_NAME *nm;

	if (!(nm = X509_NAME_new()))
		return 0;
	sk = X509V3_get0_section(ctx, value);
	if (!sk) {
		X509V3error(X509V3_R_SECTION_NOT_FOUND);
		ERR_asprintf_error_data("section=%s", value);
		X509_NAME_free(nm);
		return 0;
	}
	/* FIXME: should allow other character types... */
	ret = X509V3_NAME_from_section(nm, sk, MBSTRING_ASC);
	if (!ret)
		X509_NAME_free(nm);
	gen->d.dirn = nm;

	return ret;
}
