/* $OpenBSD: x509_ocsp.c,v 1.5 2025/05/10 05:54:39 tb Exp $ */
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

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_OCSP

#include <openssl/asn1.h>
#include <openssl/conf.h>
#include <openssl/ocsp.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "ocsp_local.h"

/* OCSP extensions and a couple of CRL entry extensions
 */

static int i2r_ocsp_crlid(const X509V3_EXT_METHOD *method, void *nonce,
    BIO *out, int indent);
static int i2r_ocsp_acutoff(const X509V3_EXT_METHOD *method, void *nonce,
    BIO *out, int indent);
static int i2r_object(const X509V3_EXT_METHOD *method, void *obj, BIO *out,
    int indent);

static void *ocsp_nonce_new(void);
static int i2d_ocsp_nonce(void *a, unsigned char **pp);
static void *d2i_ocsp_nonce(void *a, const unsigned char **pp, long length);
static void ocsp_nonce_free(void *a);
static int i2r_ocsp_nonce(const X509V3_EXT_METHOD *method, void *nonce,
    BIO *out, int indent);

static int i2r_ocsp_nocheck(const X509V3_EXT_METHOD *method,
    void *nocheck, BIO *out, int indent);
static void *s2i_ocsp_nocheck(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    const char *str);
static int i2r_ocsp_serviceloc(const X509V3_EXT_METHOD *method, void *in,
    BIO *bp, int ind);

static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_CrlID = {
	.ext_nid = NID_id_pkix_OCSP_CrlID,
	.ext_flags = 0,
	.it = &OCSP_CRLID_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_crlid,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_CrlID(void)
{
	return &x509v3_ext_id_pkix_OCSP_CrlID;
}

static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_archiveCutoff = {
	.ext_nid = NID_id_pkix_OCSP_archiveCutoff,
	.ext_flags = 0,
	.it = &ASN1_GENERALIZEDTIME_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_acutoff,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_archiveCutoff(void)
{
	return &x509v3_ext_id_pkix_OCSP_archiveCutoff;
}

static const X509V3_EXT_METHOD x509v3_ext_invalidity_date = {
	.ext_nid = NID_invalidity_date,
	.ext_flags = 0,
	.it = &ASN1_GENERALIZEDTIME_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_acutoff,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_invalidity_date(void)
{
	return &x509v3_ext_invalidity_date;
}

static const X509V3_EXT_METHOD x509v3_ext_hold_instruction_code = {
	.ext_nid = NID_hold_instruction_code,
	.ext_flags = 0,
	.it = &ASN1_OBJECT_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_object,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_hold_instruction_code(void)
{
	return &x509v3_ext_hold_instruction_code;
}

static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_Nonce = {
	.ext_nid = NID_id_pkix_OCSP_Nonce,
	.ext_flags = 0,
	.it = NULL,
	.ext_new = ocsp_nonce_new,
	.ext_free = ocsp_nonce_free,
	.d2i = d2i_ocsp_nonce,
	.i2d = i2d_ocsp_nonce,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_nonce,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_Nonce(void)
{
	return &x509v3_ext_id_pkix_OCSP_Nonce;
}

static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_noCheck = {
	.ext_nid = NID_id_pkix_OCSP_noCheck,
	.ext_flags = 0,
	.it = &ASN1_NULL_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = s2i_ocsp_nocheck,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_nocheck,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_noCheck(void)
{
	return &x509v3_ext_id_pkix_OCSP_noCheck;
}

static const X509V3_EXT_METHOD x509v3_ext_id_pkix_OCSP_serviceLocator = {
	.ext_nid = NID_id_pkix_OCSP_serviceLocator,
	.ext_flags = 0,
	.it = &OCSP_SERVICELOC_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = i2r_ocsp_serviceloc,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_id_pkix_OCSP_serviceLocator(void)
{
	return &x509v3_ext_id_pkix_OCSP_serviceLocator;
}

static int
i2r_ocsp_crlid(const X509V3_EXT_METHOD *method, void *in, BIO *bp, int ind)
{
	OCSP_CRLID *a = in;
	if (a->crlUrl) {
		if (BIO_printf(bp, "%*scrlUrl: ", ind, "") <= 0)
			goto err;
		if (!ASN1_STRING_print(bp, (ASN1_STRING*)a->crlUrl))
			goto err;
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
	}
	if (a->crlNum) {
		if (BIO_printf(bp, "%*scrlNum: ", ind, "") <= 0)
			goto err;
		if (i2a_ASN1_INTEGER(bp, a->crlNum) <= 0)
			goto err;
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
	}
	if (a->crlTime) {
		if (BIO_printf(bp, "%*scrlTime: ", ind, "") <= 0)
			goto err;
		if (!ASN1_GENERALIZEDTIME_print(bp, a->crlTime))
			goto err;
		if (BIO_write(bp, "\n", 1) <= 0)
			goto err;
	}
	return 1;

err:
	return 0;
}

static int
i2r_ocsp_acutoff(const X509V3_EXT_METHOD *method, void *cutoff, BIO *bp,
    int ind)
{
	if (BIO_printf(bp, "%*s", ind, "") <= 0)
		return 0;
	if (!ASN1_GENERALIZEDTIME_print(bp, cutoff))
		return 0;
	return 1;
}

static int
i2r_object(const X509V3_EXT_METHOD *method, void *oid, BIO *bp, int ind)
{
	if (BIO_printf(bp, "%*s", ind, "") <= 0)
		return 0;
	if (i2a_ASN1_OBJECT(bp, oid) <= 0)
		return 0;
	return 1;
}

/* OCSP nonce. This is needs special treatment because it doesn't have
 * an ASN1 encoding at all: it just contains arbitrary data.
 */

static void *
ocsp_nonce_new(void)
{
	return ASN1_OCTET_STRING_new();
}

static int
i2d_ocsp_nonce(void *a, unsigned char **pp)
{
	ASN1_OCTET_STRING *os = a;

	if (pp) {
		memcpy(*pp, os->data, os->length);
		*pp += os->length;
	}
	return os->length;
}

static void *
d2i_ocsp_nonce(void *a, const unsigned char **pp, long length)
{
	ASN1_OCTET_STRING *os, **pos;

	pos = a;
	if (pos == NULL || *pos == NULL) {
		os = ASN1_OCTET_STRING_new();
		if (os == NULL)
			goto err;
	} else
		os = *pos;
	if (ASN1_OCTET_STRING_set(os, *pp, length) == 0)
		goto err;

	*pp += length;

	if (pos != NULL)
		*pos = os;
	return os;

err:
	if (pos == NULL || *pos != os)
		ASN1_OCTET_STRING_free(os);
	OCSPerror(ERR_R_MALLOC_FAILURE);
	return NULL;
}

static void
ocsp_nonce_free(void *a)
{
	ASN1_OCTET_STRING_free(a);
}

static int
i2r_ocsp_nonce(const X509V3_EXT_METHOD *method, void *nonce, BIO *out,
    int indent)
{
	if (BIO_printf(out, "%*s", indent, "") <= 0)
		return 0;
	if (i2a_ASN1_STRING(out, nonce, V_ASN1_OCTET_STRING) <= 0)
		return 0;
	return 1;
}

/* Nocheck is just a single NULL. Don't print anything and always set it */

static int
i2r_ocsp_nocheck(const X509V3_EXT_METHOD *method, void *nocheck, BIO *out,
    int indent)
{
	return 1;
}

static void *
s2i_ocsp_nocheck(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx,
    const char *str)
{
	return ASN1_NULL_new();
}

static int
i2r_ocsp_serviceloc(const X509V3_EXT_METHOD *method, void *in, BIO *bp, int ind)
{
	int i;
	OCSP_SERVICELOC *a = in;
	ACCESS_DESCRIPTION *ad;

	if (BIO_printf(bp, "%*sIssuer: ", ind, "") <= 0)
		goto err;
	if (X509_NAME_print_ex(bp, a->issuer, 0, XN_FLAG_ONELINE) <= 0)
		goto err;
	for (i = 0; i < sk_ACCESS_DESCRIPTION_num(a->locator); i++) {
		ad = sk_ACCESS_DESCRIPTION_value(a->locator, i);
		if (BIO_printf(bp, "\n%*s", (2 * ind), "") <= 0)
			goto err;
		if (i2a_ASN1_OBJECT(bp, ad->method) <= 0)
			goto err;
		if (BIO_puts(bp, " - ") <= 0)
			goto err;
		if (GENERAL_NAME_print(bp, ad->location) <= 0)
			goto err;
	}
	return 1;

err:
	return 0;
}
#endif
