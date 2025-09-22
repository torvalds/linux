/* $OpenBSD: x509_conf.c,v 1.31 2025/06/02 12:18:21 jsg Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999-2002 The OpenSSL Project.  All rights reserved.
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
/* extension creation utilities */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/conf.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "conf_local.h"
#include "err_local.h"
#include "x509_local.h"

static int v3_check_critical(const char **value);
static int v3_check_generic(const char **value);
static X509_EXTENSION *do_ext_nconf(CONF *conf, X509V3_CTX *ctx, int nid,
    int crit, const char *value);
static X509_EXTENSION *v3_generic_extension(const char *ext, const char *value,
    int crit, int type, X509V3_CTX *ctx);
static X509_EXTENSION *do_ext_i2d(const X509V3_EXT_METHOD *method, int nid,
    int crit, void *ext_struct);
static unsigned char *generic_asn1(const char *value, X509V3_CTX *ctx,
    long *ext_len);

X509_EXTENSION *
X509V3_EXT_nconf(CONF *conf, X509V3_CTX *ctx, const char *name,
    const char *value)
{
	int crit;
	int ext_type;
	X509_EXTENSION *ret;

	crit = v3_check_critical(&value);
	if ((ext_type = v3_check_generic(&value)))
		return v3_generic_extension(name, value, crit, ext_type, ctx);
	ret = do_ext_nconf(conf, ctx, OBJ_sn2nid(name), crit, value);
	if (!ret) {
		X509V3error(X509V3_R_ERROR_IN_EXTENSION);
		ERR_asprintf_error_data("name=%s, value=%s", name, value);
	}
	return ret;
}
LCRYPTO_ALIAS(X509V3_EXT_nconf);

X509_EXTENSION *
X509V3_EXT_nconf_nid(CONF *conf, X509V3_CTX *ctx, int nid, const char *value)
{
	int crit;
	int ext_type;

	crit = v3_check_critical(&value);
	if ((ext_type = v3_check_generic(&value)))
		return v3_generic_extension(OBJ_nid2sn(nid),
		    value, crit, ext_type, ctx);
	return do_ext_nconf(conf, ctx, nid, crit, value);
}
LCRYPTO_ALIAS(X509V3_EXT_nconf_nid);

static X509_EXTENSION *
do_ext_nconf(CONF *conf, X509V3_CTX *ctx, int nid, int crit, const char *value)
{
	const X509V3_EXT_METHOD *method;
	X509_EXTENSION *ext;
	void *ext_struct;

	if (nid == NID_undef) {
		X509V3error(X509V3_R_UNKNOWN_EXTENSION_NAME);
		return NULL;
	}
	if (!(method = X509V3_EXT_get_nid(nid))) {
		X509V3error(X509V3_R_UNKNOWN_EXTENSION);
		return NULL;
	}
	/* Now get internal extension representation based on type */
	if (method->v2i) {
		STACK_OF(CONF_VALUE) *nval;

		if (*value == '@')
			nval = NCONF_get_section(conf, value + 1);
		else
			nval = X509V3_parse_list(value);
		if (sk_CONF_VALUE_num(nval) <= 0) {
			X509V3error(X509V3_R_INVALID_EXTENSION_STRING);
			ERR_asprintf_error_data("name=%s,section=%s",
			    OBJ_nid2sn(nid), value);
			if (*value != '@')
				sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
			return NULL;
		}
		ext_struct = method->v2i(method, ctx, nval);
		if (*value != '@')
			sk_CONF_VALUE_pop_free(nval, X509V3_conf_free);
	} else if (method->s2i) {
		ext_struct = method->s2i(method, ctx, value);
	} else if (method->r2i) {
		if (ctx->db == NULL) {
			X509V3error(X509V3_R_NO_CONFIG_DATABASE);
			return NULL;
		}
		ext_struct = method->r2i(method, ctx, value);
	} else {
		X509V3error(X509V3_R_EXTENSION_SETTING_NOT_SUPPORTED);
		ERR_asprintf_error_data("name=%s", OBJ_nid2sn(nid));
		return NULL;
	}
	if (ext_struct == NULL)
		return NULL;

	ext = do_ext_i2d(method, nid, crit, ext_struct);
	if (method->it)
		ASN1_item_free(ext_struct, method->it);
	else
		method->ext_free(ext_struct);
	return ext;
}

static X509_EXTENSION *
do_ext_i2d(const X509V3_EXT_METHOD *method, int nid, int crit,
    void *ext_struct)
{
	unsigned char *ext_der = NULL;
	int ext_len;
	ASN1_OCTET_STRING *ext_oct = NULL;
	X509_EXTENSION *ext;

	/* Convert internal representation to DER */
	if (method->it != NULL) {
		ext_der = NULL;
		ext_len = ASN1_item_i2d(ext_struct, &ext_der, method->it);
		if (ext_len < 0)
			goto err;
	} else {
		unsigned char *p;

		if ((ext_len = method->i2d(ext_struct, NULL)) <= 0)
			goto err;
		if ((ext_der = calloc(1, ext_len)) == NULL)
			goto err;
		p = ext_der;
		if (method->i2d(ext_struct, &p) != ext_len)
			goto err;
	}
	if ((ext_oct = ASN1_OCTET_STRING_new()) == NULL)
		goto err;
	ASN1_STRING_set0(ext_oct, ext_der, ext_len);
	ext_der = NULL;
	ext_len = 0;

	ext = X509_EXTENSION_create_by_NID(NULL, nid, crit, ext_oct);
	if (ext == NULL)
		goto err;
	ASN1_OCTET_STRING_free(ext_oct);

	return ext;

 err:
	free(ext_der);
	ASN1_OCTET_STRING_free(ext_oct);
	X509V3error(ERR_R_MALLOC_FAILURE);

	return NULL;
}

/* Given an internal structure, nid and critical flag create an extension */
X509_EXTENSION *
X509V3_EXT_i2d(int nid, int crit, void *ext_struct)
{
	const X509V3_EXT_METHOD *method;

	if (!(method = X509V3_EXT_get_nid(nid))) {
		X509V3error(X509V3_R_UNKNOWN_EXTENSION);
		return NULL;
	}
	return do_ext_i2d(method, nid, crit, ext_struct);
}
LCRYPTO_ALIAS(X509V3_EXT_i2d);

/* Check the extension string for critical flag */
static int
v3_check_critical(const char **value)
{
	const char *p = *value;

	if ((strlen(p) < 9) || strncmp(p, "critical,", 9))
		return 0;
	p += 9;
	while (isspace((unsigned char)*p))
		p++;
	*value = p;
	return 1;
}

/* Check extension string for generic extension and return the type */
static int
v3_check_generic(const char **value)
{
	int gen_type = 0;
	const char *p = *value;

	if ((strlen(p) >= 4) && !strncmp(p, "DER:", 4)) {
		p += 4;
		gen_type = 1;
	} else if ((strlen(p) >= 5) && !strncmp(p, "ASN1:", 5)) {
		p += 5;
		gen_type = 2;
	} else
		return 0;

	while (isspace((unsigned char)*p))
		p++;
	*value = p;
	return gen_type;
}

/* Create a generic extension: for now just handle DER type */
static X509_EXTENSION *
v3_generic_extension(const char *name, const char *value, int crit, int gen_type,
    X509V3_CTX *ctx)
{
	unsigned char *ext_der = NULL;
	long ext_len = 0;
	ASN1_OBJECT *obj = NULL;
	ASN1_OCTET_STRING *oct = NULL;
	X509_EXTENSION *ext = NULL;

	if ((obj = OBJ_txt2obj(name, 0)) == NULL) {
		X509V3error(X509V3_R_EXTENSION_NAME_ERROR);
		ERR_asprintf_error_data("name=%s", name);
		goto err;
	}

	if (gen_type == 1)
		ext_der = string_to_hex(value, &ext_len);
	else if (gen_type == 2)
		ext_der = generic_asn1(value, ctx, &ext_len);
	else {
		ERR_asprintf_error_data("Unexpected generic extension type %d", gen_type);
		goto err;
	}

	if (ext_der == NULL) {
		X509V3error(X509V3_R_EXTENSION_VALUE_ERROR);
		ERR_asprintf_error_data("value=%s", value);
		goto err;
	}

	if ((oct = ASN1_OCTET_STRING_new()) == NULL) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	ASN1_STRING_set0(oct, ext_der, ext_len);
	ext_der = NULL;
	ext_len = 0;

	ext = X509_EXTENSION_create_by_OBJ(NULL, obj, crit, oct);

 err:
	ASN1_OBJECT_free(obj);
	ASN1_OCTET_STRING_free(oct);
	free(ext_der);

	return ext;
}

static unsigned char *
generic_asn1(const char *value, X509V3_CTX *ctx, long *ext_len)
{
	ASN1_TYPE *typ;
	unsigned char *ext_der = NULL;

	typ = ASN1_generate_v3(value, ctx);
	if (typ == NULL)
		return NULL;
	*ext_len = i2d_ASN1_TYPE(typ, &ext_der);
	ASN1_TYPE_free(typ);
	return ext_der;
}

/*
 * This is the main function: add a bunch of extensions based on a config file
 * section to an extension STACK.
 */

int
X509V3_EXT_add_nconf_sk(CONF *conf, X509V3_CTX *ctx, const char *section,
    STACK_OF(X509_EXTENSION) **sk)
{
	X509_EXTENSION *ext;
	STACK_OF(CONF_VALUE) *nval;
	CONF_VALUE *val;
	int i;

	if (!(nval = NCONF_get_section(conf, section)))
		return 0;
	for (i = 0; i < sk_CONF_VALUE_num(nval); i++) {
		val = sk_CONF_VALUE_value(nval, i);
		if (!(ext = X509V3_EXT_nconf(conf, ctx, val->name, val->value)))
			return 0;
		if (sk)
			X509v3_add_ext(sk, ext, -1);
		X509_EXTENSION_free(ext);
	}
	return 1;
}
LCRYPTO_ALIAS(X509V3_EXT_add_nconf_sk);

int
X509V3_EXT_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509 *cert)
{
	STACK_OF(X509_EXTENSION) **sk = NULL;

	if (cert)
		sk = &cert->cert_info->extensions;
	return X509V3_EXT_add_nconf_sk(conf, ctx, section, sk);
}
LCRYPTO_ALIAS(X509V3_EXT_add_nconf);

int
X509V3_EXT_CRL_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509_CRL *crl)
{
	STACK_OF(X509_EXTENSION) **sk = NULL;

	if (crl)
		sk = &crl->crl->extensions;
	return X509V3_EXT_add_nconf_sk(conf, ctx, section, sk);
}
LCRYPTO_ALIAS(X509V3_EXT_CRL_add_nconf);

int
X509V3_EXT_REQ_add_nconf(CONF *conf, X509V3_CTX *ctx, const char *section,
    X509_REQ *req)
{
	STACK_OF(X509_EXTENSION) *extlist = NULL, **sk = NULL;
	int i;

	if (req)
		sk = &extlist;
	i = X509V3_EXT_add_nconf_sk(conf, ctx, section, sk);
	if (!i || !sk)
		return i;
	i = X509_REQ_add_extensions(req, extlist);
	sk_X509_EXTENSION_pop_free(extlist, X509_EXTENSION_free);
	return i;
}
LCRYPTO_ALIAS(X509V3_EXT_REQ_add_nconf);

STACK_OF(CONF_VALUE) *
X509V3_get0_section(X509V3_CTX *ctx, const char *section)
{
	if (ctx->db == NULL) {
		X509V3error(X509V3_R_OPERATION_NOT_DEFINED);
		return NULL;
	}
	return NCONF_get_section(ctx->db, section);
}

void
X509V3_set_nconf(X509V3_CTX *ctx, CONF *conf)
{
	ctx->db = conf;
}
LCRYPTO_ALIAS(X509V3_set_nconf);

void
X509V3_set_ctx(X509V3_CTX *ctx, X509 *issuer, X509 *subj, X509_REQ *req,
    X509_CRL *crl, int flags)
{
	ctx->issuer_cert = issuer;
	ctx->subject_cert = subj;
	ctx->crl = crl;
	ctx->subject_req = req;
	ctx->flags = flags;
}
LCRYPTO_ALIAS(X509V3_set_ctx);

X509_EXTENSION *
X509V3_EXT_conf(LHASH_OF(CONF_VALUE) *conf, X509V3_CTX *ctx, const char *name,
    const char *value)
{
	CONF ctmp;

	CONF_set_nconf(&ctmp, conf);
	return X509V3_EXT_nconf(&ctmp, ctx, name, value);
}
LCRYPTO_ALIAS(X509V3_EXT_conf);

X509_EXTENSION *
X509V3_EXT_conf_nid(LHASH_OF(CONF_VALUE) *conf, X509V3_CTX *ctx, int nid,
    const char *value)
{
	CONF ctmp;

	CONF_set_nconf(&ctmp, conf);
	return X509V3_EXT_nconf_nid(&ctmp, ctx, nid, value);
}
LCRYPTO_ALIAS(X509V3_EXT_conf_nid);
