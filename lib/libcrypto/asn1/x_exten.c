/* $OpenBSD: x_exten.c,v 1.22 2024/07/08 14:48:49 beck Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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

#include <stddef.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>

#include "x509_local.h"

static const ASN1_TEMPLATE X509_EXTENSION_seq_tt[] = {
	{
		.offset = offsetof(X509_EXTENSION, object),
		.field_name = "object",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_EXTENSION, critical),
		.field_name = "critical",
		.item = &ASN1_BOOLEAN_it,
	},
	{
		.offset = offsetof(X509_EXTENSION, value),
		.field_name = "value",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM X509_EXTENSION_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_EXTENSION_seq_tt,
	.tcount = sizeof(X509_EXTENSION_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(X509_EXTENSION),
	.sname = "X509_EXTENSION",
};
LCRYPTO_ALIAS(X509_EXTENSION_it);

static const ASN1_TEMPLATE X509_EXTENSIONS_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "Extension",
	.item = &X509_EXTENSION_it,
};

const ASN1_ITEM X509_EXTENSIONS_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &X509_EXTENSIONS_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "X509_EXTENSIONS",
};
LCRYPTO_ALIAS(X509_EXTENSIONS_it);


X509_EXTENSION *
d2i_X509_EXTENSION(X509_EXTENSION **a, const unsigned char **in, long len)
{
	return (X509_EXTENSION *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_EXTENSION_it);
}
LCRYPTO_ALIAS(d2i_X509_EXTENSION);

int
i2d_X509_EXTENSION(X509_EXTENSION *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_EXTENSION_it);
}
LCRYPTO_ALIAS(i2d_X509_EXTENSION);

X509_EXTENSION *
X509_EXTENSION_new(void)
{
	return (X509_EXTENSION *)ASN1_item_new(&X509_EXTENSION_it);
}
LCRYPTO_ALIAS(X509_EXTENSION_new);

void
X509_EXTENSION_free(X509_EXTENSION *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_EXTENSION_it);
}
LCRYPTO_ALIAS(X509_EXTENSION_free);

X509_EXTENSIONS *
d2i_X509_EXTENSIONS(X509_EXTENSIONS **a, const unsigned char **in, long len)
{
	return (X509_EXTENSIONS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_EXTENSIONS_it);
}
LCRYPTO_ALIAS(d2i_X509_EXTENSIONS);

int
i2d_X509_EXTENSIONS(X509_EXTENSIONS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_EXTENSIONS_it);
}
LCRYPTO_ALIAS(i2d_X509_EXTENSIONS);

X509_EXTENSION *
X509_EXTENSION_dup(X509_EXTENSION *x)
{
	return ASN1_item_dup(&X509_EXTENSION_it, x);
}
LCRYPTO_ALIAS(X509_EXTENSION_dup);
