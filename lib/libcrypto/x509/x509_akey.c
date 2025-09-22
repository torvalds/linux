/* $OpenBSD: x509_akey.c,v 1.4 2025/05/10 05:54:39 tb Exp $ */
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

static STACK_OF(CONF_VALUE) *i2v_AUTHORITY_KEYID(X509V3_EXT_METHOD *method,
    AUTHORITY_KEYID *akeyid, STACK_OF(CONF_VALUE) *extlist);
static AUTHORITY_KEYID *v2i_AUTHORITY_KEYID(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *values);

static const X509V3_EXT_METHOD x509v3_ext_authority_key_identifier = {
	.ext_nid = NID_authority_key_identifier,
	.ext_flags = X509V3_EXT_MULTILINE,
	.it = &AUTHORITY_KEYID_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = (X509V3_EXT_I2V)i2v_AUTHORITY_KEYID,
	.v2i = (X509V3_EXT_V2I)v2i_AUTHORITY_KEYID,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_authority_key_identifier(void)
{
	return &x509v3_ext_authority_key_identifier;
}

static STACK_OF(CONF_VALUE) *
i2v_AUTHORITY_KEYID(X509V3_EXT_METHOD *method, AUTHORITY_KEYID *akeyid,
    STACK_OF(CONF_VALUE) *extlist)
{
	STACK_OF(CONF_VALUE) *free_extlist = NULL;
	char *tmpstr = NULL;

	if (extlist == NULL) {
		if ((free_extlist = extlist = sk_CONF_VALUE_new_null()) == NULL)
			return NULL;
	}

	if (akeyid->keyid != NULL) {
		if ((tmpstr = hex_to_string(akeyid->keyid->data,
		    akeyid->keyid->length)) == NULL)
			goto err;
		if (!X509V3_add_value("keyid", tmpstr, &extlist))
			goto err;
		free(tmpstr);
		tmpstr = NULL;
	}

	if (akeyid->issuer != NULL) {
		if ((extlist = i2v_GENERAL_NAMES(NULL, akeyid->issuer,
		    extlist)) == NULL)
			goto err;
	}

	if (akeyid->serial != NULL) {
		if ((tmpstr = hex_to_string(akeyid->serial->data,
		    akeyid->serial->length)) == NULL)
			goto err;
		if (!X509V3_add_value("serial", tmpstr, &extlist))
			goto err;
		free(tmpstr);
		tmpstr = NULL;
	}

	if (sk_CONF_VALUE_num(extlist) <= 0)
		goto err;

	return extlist;

 err:
	free(tmpstr);
	sk_CONF_VALUE_pop_free(free_extlist, X509V3_conf_free);

	return NULL;
}

/*
 * Currently two options:
 * keyid: use the issuers subject keyid, the value 'always' means its is
 * an error if the issuer certificate doesn't have a key id.
 * issuer: use the issuers cert issuer and serial number. The default is
 * to only use this if keyid is not present. With the option 'always'
 * this is always included.
 */
static AUTHORITY_KEYID *
v2i_AUTHORITY_KEYID(X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    STACK_OF(CONF_VALUE) *values)
{
	char keyid = 0, issuer = 0;
	int i;
	CONF_VALUE *cnf;
	ASN1_OCTET_STRING *ikeyid = NULL;
	X509_NAME *isname = NULL;
	STACK_OF(GENERAL_NAME) *gens = NULL;
	GENERAL_NAME *gen = NULL;
	ASN1_INTEGER *serial = NULL;
	X509_EXTENSION *ext;
	X509 *cert;
	AUTHORITY_KEYID *akeyid = NULL;

	for (i = 0; i < sk_CONF_VALUE_num(values); i++) {
		cnf = sk_CONF_VALUE_value(values, i);
		if (!strcmp(cnf->name, "keyid")) {
			keyid = 1;
			if (cnf->value && !strcmp(cnf->value, "always"))
				keyid = 2;
		} else if (!strcmp(cnf->name, "issuer")) {
			issuer = 1;
			if (cnf->value && !strcmp(cnf->value, "always"))
				issuer = 2;
		} else {
			X509V3error(X509V3_R_UNKNOWN_OPTION);
			ERR_asprintf_error_data("name=%s", cnf->name);
			return NULL;
		}
	}

	if (!ctx || !ctx->issuer_cert) {
		if (ctx && (ctx->flags == CTX_TEST))
			return AUTHORITY_KEYID_new();
		X509V3error(X509V3_R_NO_ISSUER_CERTIFICATE);
		return NULL;
	}

	cert = ctx->issuer_cert;

	if (keyid) {
		i = X509_get_ext_by_NID(cert, NID_subject_key_identifier, -1);
		if ((i >= 0)  && (ext = X509_get_ext(cert, i)))
			ikeyid = X509V3_EXT_d2i(ext);
		if (keyid == 2 && !ikeyid) {
			X509V3error(X509V3_R_UNABLE_TO_GET_ISSUER_KEYID);
			return NULL;
		}
	}

	if ((issuer && !ikeyid) || (issuer == 2)) {
		isname = X509_NAME_dup(X509_get_issuer_name(cert));
		serial = ASN1_INTEGER_dup(X509_get_serialNumber(cert));
		if (!isname || !serial) {
			X509V3error(X509V3_R_UNABLE_TO_GET_ISSUER_DETAILS);
			goto err;
		}
	}

	if (!(akeyid = AUTHORITY_KEYID_new()))
		goto err;

	if (isname) {
		if (!(gens = sk_GENERAL_NAME_new_null()) ||
		    !(gen = GENERAL_NAME_new()) ||
		    !sk_GENERAL_NAME_push(gens, gen)) {
			X509V3error(ERR_R_MALLOC_FAILURE);
			goto err;
		}
		gen->type = GEN_DIRNAME;
		gen->d.dirn = isname;
	}

	akeyid->issuer = gens;
	akeyid->serial = serial;
	akeyid->keyid = ikeyid;

	return akeyid;

 err:
	AUTHORITY_KEYID_free(akeyid);
	GENERAL_NAME_free(gen);
	sk_GENERAL_NAME_free(gens);
	X509_NAME_free(isname);
	ASN1_INTEGER_free(serial);
	ASN1_OCTET_STRING_free(ikeyid);
	return NULL;
}
