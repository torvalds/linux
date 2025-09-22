/* $OpenBSD: x509_ia5.c,v 1.3 2025/05/10 05:54:39 tb Exp $ */
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
#include <openssl/conf.h>
#include <openssl/x509v3.h>

#include "err_local.h"

static char *i2s_ASN1_IA5STRING(X509V3_EXT_METHOD *method, ASN1_IA5STRING *ia5);
static ASN1_IA5STRING *s2i_ASN1_IA5STRING(X509V3_EXT_METHOD *method,
    X509V3_CTX *ctx, char *str);

static const X509V3_EXT_METHOD x509v3_ext_netscape_base_url = {
	.ext_nid = NID_netscape_base_url,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_base_url(void)
{
	return &x509v3_ext_netscape_base_url;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_revocation_url = {
	.ext_nid = NID_netscape_revocation_url,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_revocation_url(void)
{
	return &x509v3_ext_netscape_revocation_url;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_ca_revocation_url = {
	.ext_nid = NID_netscape_ca_revocation_url,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_ca_revocation_url(void)
{
	return &x509v3_ext_netscape_ca_revocation_url;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_renewal_url = {
	.ext_nid = NID_netscape_renewal_url,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_renewal_url(void)
{
	return &x509v3_ext_netscape_renewal_url;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_ca_policy_url = {
	.ext_nid = NID_netscape_ca_policy_url,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_ca_policy_url(void)
{
	return &x509v3_ext_netscape_ca_policy_url;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_ssl_server_name = {
	.ext_nid = NID_netscape_ssl_server_name,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_ssl_server_name(void)
{
	return &x509v3_ext_netscape_ssl_server_name;
}

static const X509V3_EXT_METHOD x509v3_ext_netscape_comment = {
	.ext_nid = NID_netscape_comment,
	.ext_flags = 0,
	.it = &ASN1_IA5STRING_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = (X509V3_EXT_I2S)i2s_ASN1_IA5STRING,
	.s2i = (X509V3_EXT_S2I)s2i_ASN1_IA5STRING,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_netscape_comment(void)
{
	return &x509v3_ext_netscape_comment;
}

static char *
i2s_ASN1_IA5STRING(X509V3_EXT_METHOD *method, ASN1_IA5STRING *ia5)
{
	char *tmp;

	if (!ia5 || !ia5->length)
		return NULL;
	if (!(tmp = malloc(ia5->length + 1))) {
		X509V3error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	memcpy(tmp, ia5->data, ia5->length);
	tmp[ia5->length] = 0;
	return tmp;
}

static ASN1_IA5STRING *
s2i_ASN1_IA5STRING(X509V3_EXT_METHOD *method, X509V3_CTX *ctx, char *str)
{
	ASN1_IA5STRING *ia5;
	if (!str) {
		X509V3error(X509V3_R_INVALID_NULL_ARGUMENT);
		return NULL;
	}
	if (!(ia5 = ASN1_IA5STRING_new()))
		goto err;
	if (!ASN1_STRING_set((ASN1_STRING *)ia5, (unsigned char*)str,
	    strlen(str))) {
		ASN1_IA5STRING_free(ia5);
		goto err;
	}
	return ia5;

err:
	X509V3error(ERR_R_MALLOC_FAILURE);
	return NULL;
}
