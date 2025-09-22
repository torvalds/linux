/*	$OpenBSD: ct_x509v3.c,v 1.7 2024/07/13 15:08:58 tb Exp $ */
/*
 * Written by Rob Stradling (rob@comodo.com) and Stephen Henson
 * (steve@openssl.org) for the OpenSSL project 2014.
 */
/* ====================================================================
 * Copyright (c) 2014 The OpenSSL Project.  All rights reserved.
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

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <string.h>

#include "ct_local.h"

static char *
i2s_poison(const X509V3_EXT_METHOD *method, void *val)
{
	return strdup("NULL");
}

static void *
s2i_poison(const X509V3_EXT_METHOD *method, X509V3_CTX *ctx, const char *str)
{
	return ASN1_NULL_new();
}

static int
i2r_SCT_LIST(X509V3_EXT_METHOD *method, STACK_OF(SCT) *sct_list, BIO *out,
    int indent)
{
	SCT_LIST_print(sct_list, out, indent, "\n", NULL);
	return 1;
}

static int
set_sct_list_source(STACK_OF(SCT) *s, sct_source_t source)
{
	if (s != NULL) {
		int i;

		for (i = 0; i < sk_SCT_num(s); i++) {
			int res = SCT_set_source(sk_SCT_value(s, i), source);

			if (res != 1) {
				return 0;
			}
		}
	}
	return 1;
}

static STACK_OF(SCT) *
x509_ext_d2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp, long len)
{
	STACK_OF(SCT) *s = d2i_SCT_LIST(a, pp, len);

	if (set_sct_list_source(s, SCT_SOURCE_X509V3_EXTENSION) != 1) {
		SCT_LIST_free(s);
		*a = NULL;
		return NULL;
	}
	return s;
}

static STACK_OF(SCT) *
ocsp_ext_d2i_SCT_LIST(STACK_OF(SCT) **a, const unsigned char **pp, long len)
{
	STACK_OF(SCT) *s = d2i_SCT_LIST(a, pp, len);

	if (set_sct_list_source(s, SCT_SOURCE_OCSP_STAPLED_RESPONSE) != 1) {
		SCT_LIST_free(s);
		*a = NULL;
		return NULL;
	}
	return s;
}

/* X509v3 extension in certificates that contains SCTs */
static const X509V3_EXT_METHOD x509v3_ext_ct_precert_scts = {
	.ext_nid = NID_ct_precert_scts,
	.ext_flags = 0,
	.it = NULL,
	.ext_new = NULL,
	.ext_free = (X509V3_EXT_FREE)SCT_LIST_free,
	.d2i = (X509V3_EXT_D2I)x509_ext_d2i_SCT_LIST,
	.i2d = (X509V3_EXT_I2D)i2d_SCT_LIST,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = (X509V3_EXT_I2R)i2r_SCT_LIST,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_ct_precert_scts(void)
{
	return &x509v3_ext_ct_precert_scts;
}

/* X509v3 extension to mark a certificate as a pre-certificate */
static const X509V3_EXT_METHOD x509v3_ext_ct_precert_poison = {
	.ext_nid = NID_ct_precert_poison,
	.ext_flags = 0,
	.it = &ASN1_NULL_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = i2s_poison,
	.s2i = s2i_poison,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = NULL,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_ct_precert_poison(void)
{
	return &x509v3_ext_ct_precert_poison;
}

/* OCSP extension that contains SCTs */
static const X509V3_EXT_METHOD x509v3_ext_ct_cert_scts = {
	.ext_nid = NID_ct_cert_scts,
	.ext_flags = 0,
	.it = NULL,
	.ext_new = NULL,
	.ext_free = (X509V3_EXT_FREE)SCT_LIST_free,
	.d2i = (X509V3_EXT_D2I)ocsp_ext_d2i_SCT_LIST,
	.i2d = (X509V3_EXT_I2D)i2d_SCT_LIST,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = (X509V3_EXT_I2R)i2r_SCT_LIST,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_ct_cert_scts(void)
{
	return &x509v3_ext_ct_cert_scts;
}
