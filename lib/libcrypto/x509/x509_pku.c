/* $OpenBSD: x509_pku.c,v 1.5 2024/07/13 15:08:58 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>

static int i2r_PKEY_USAGE_PERIOD(X509V3_EXT_METHOD *method,
    PKEY_USAGE_PERIOD *usage, BIO *out, int indent);

static const X509V3_EXT_METHOD x509v3_ext_private_key_usage_period = {
	.ext_nid = NID_private_key_usage_period,
	.ext_flags = 0,
	.it = &PKEY_USAGE_PERIOD_it,
	.ext_new = NULL,
	.ext_free = NULL,
	.d2i = NULL,
	.i2d = NULL,
	.i2s = NULL,
	.s2i = NULL,
	.i2v = NULL,
	.v2i = NULL,
	.i2r = (X509V3_EXT_I2R)i2r_PKEY_USAGE_PERIOD,
	.r2i = NULL,
	.usr_data = NULL,
};

const X509V3_EXT_METHOD *
x509v3_ext_method_private_key_usage_period(void)
{
	return &x509v3_ext_private_key_usage_period;
}

static const ASN1_TEMPLATE PKEY_USAGE_PERIOD_seq_tt[] = {
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKEY_USAGE_PERIOD, notBefore),
		.field_name = "notBefore",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(PKEY_USAGE_PERIOD, notAfter),
		.field_name = "notAfter",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
};

const ASN1_ITEM PKEY_USAGE_PERIOD_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKEY_USAGE_PERIOD_seq_tt,
	.tcount = sizeof(PKEY_USAGE_PERIOD_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKEY_USAGE_PERIOD),
	.sname = "PKEY_USAGE_PERIOD",
};
LCRYPTO_ALIAS(PKEY_USAGE_PERIOD_it);


PKEY_USAGE_PERIOD *
d2i_PKEY_USAGE_PERIOD(PKEY_USAGE_PERIOD **a, const unsigned char **in, long len)
{
	return (PKEY_USAGE_PERIOD *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKEY_USAGE_PERIOD_it);
}
LCRYPTO_ALIAS(d2i_PKEY_USAGE_PERIOD);

int
i2d_PKEY_USAGE_PERIOD(PKEY_USAGE_PERIOD *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKEY_USAGE_PERIOD_it);
}
LCRYPTO_ALIAS(i2d_PKEY_USAGE_PERIOD);

PKEY_USAGE_PERIOD *
PKEY_USAGE_PERIOD_new(void)
{
	return (PKEY_USAGE_PERIOD *)ASN1_item_new(&PKEY_USAGE_PERIOD_it);
}
LCRYPTO_ALIAS(PKEY_USAGE_PERIOD_new);

void
PKEY_USAGE_PERIOD_free(PKEY_USAGE_PERIOD *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKEY_USAGE_PERIOD_it);
}
LCRYPTO_ALIAS(PKEY_USAGE_PERIOD_free);

static int
i2r_PKEY_USAGE_PERIOD(X509V3_EXT_METHOD *method, PKEY_USAGE_PERIOD *usage,
    BIO *out, int indent)
{
	BIO_printf(out, "%*s", indent, "");
	if (usage->notBefore) {
		BIO_write(out, "Not Before: ", 12);
		ASN1_GENERALIZEDTIME_print(out, usage->notBefore);
		if (usage->notAfter)
			BIO_write(out, ", ", 2);
	}
	if (usage->notAfter) {
		BIO_write(out, "Not After: ", 11);
		ASN1_GENERALIZEDTIME_print(out, usage->notAfter);
	}
	return 1;
}
