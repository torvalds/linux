/* $OpenBSD: tasn_typ.c,v 1.21 2025/08/22 14:07:34 tb Exp $ */
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
#include <stdio.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>

/* Declarations for string types */

const ASN1_ITEM ASN1_NULL_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_NULL,
	.sname = "ASN1_NULL",
};
LCRYPTO_ALIAS(ASN1_NULL_it);

ASN1_NULL *
d2i_ASN1_NULL(ASN1_NULL **a, const unsigned char **in, long len)
{
	return (ASN1_NULL *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_NULL_it);
}
LCRYPTO_ALIAS(d2i_ASN1_NULL);

int
i2d_ASN1_NULL(ASN1_NULL *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_NULL_it);
}
LCRYPTO_ALIAS(i2d_ASN1_NULL);

ASN1_NULL *
ASN1_NULL_new(void)
{
	return (ASN1_NULL *)ASN1_item_new(&ASN1_NULL_it);
}
LCRYPTO_ALIAS(ASN1_NULL_new);

void
ASN1_NULL_free(ASN1_NULL *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_NULL_it);
}
LCRYPTO_ALIAS(ASN1_NULL_free);


const ASN1_ITEM ASN1_UTF8STRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_UTF8STRING,
	.sname = "ASN1_UTF8STRING",
};
LCRYPTO_ALIAS(ASN1_UTF8STRING_it);

ASN1_UTF8STRING *
d2i_ASN1_UTF8STRING(ASN1_UTF8STRING **a, const unsigned char **in, long len)
{
	return (ASN1_UTF8STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_UTF8STRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_UTF8STRING);

int
i2d_ASN1_UTF8STRING(ASN1_UTF8STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_UTF8STRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_UTF8STRING);

ASN1_UTF8STRING *
ASN1_UTF8STRING_new(void)
{
	return (ASN1_UTF8STRING *)ASN1_item_new(&ASN1_UTF8STRING_it);
}
LCRYPTO_ALIAS(ASN1_UTF8STRING_new);

void
ASN1_UTF8STRING_free(ASN1_UTF8STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_UTF8STRING_it);
}
LCRYPTO_ALIAS(ASN1_UTF8STRING_free);


const ASN1_ITEM ASN1_PRINTABLESTRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_PRINTABLESTRING,
	.sname = "ASN1_PRINTABLESTRING",
};
LCRYPTO_ALIAS(ASN1_PRINTABLESTRING_it);

ASN1_PRINTABLESTRING *
d2i_ASN1_PRINTABLESTRING(ASN1_PRINTABLESTRING **a, const unsigned char **in,
    long len)
{
	return (ASN1_PRINTABLESTRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_PRINTABLESTRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_PRINTABLESTRING);

int
i2d_ASN1_PRINTABLESTRING(ASN1_PRINTABLESTRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_PRINTABLESTRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_PRINTABLESTRING);

ASN1_PRINTABLESTRING *
ASN1_PRINTABLESTRING_new(void)
{
	return (ASN1_PRINTABLESTRING *)ASN1_item_new(&ASN1_PRINTABLESTRING_it);
}
LCRYPTO_ALIAS(ASN1_PRINTABLESTRING_new);

void
ASN1_PRINTABLESTRING_free(ASN1_PRINTABLESTRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_PRINTABLESTRING_it);
}
LCRYPTO_ALIAS(ASN1_PRINTABLESTRING_free);


const ASN1_ITEM ASN1_T61STRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_T61STRING,
	.sname = "ASN1_T61STRING",
};
LCRYPTO_ALIAS(ASN1_T61STRING_it);

ASN1_T61STRING *
d2i_ASN1_T61STRING(ASN1_T61STRING **a, const unsigned char **in, long len)
{
	return (ASN1_T61STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_T61STRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_T61STRING);

int
i2d_ASN1_T61STRING(ASN1_T61STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_T61STRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_T61STRING);

ASN1_T61STRING *
ASN1_T61STRING_new(void)
{
	return (ASN1_T61STRING *)ASN1_item_new(&ASN1_T61STRING_it);
}
LCRYPTO_ALIAS(ASN1_T61STRING_new);

void
ASN1_T61STRING_free(ASN1_T61STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_T61STRING_it);
}
LCRYPTO_ALIAS(ASN1_T61STRING_free);


const ASN1_ITEM ASN1_IA5STRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_IA5STRING,
	.sname = "ASN1_IA5STRING",
};
LCRYPTO_ALIAS(ASN1_IA5STRING_it);

ASN1_IA5STRING *
d2i_ASN1_IA5STRING(ASN1_IA5STRING **a, const unsigned char **in, long len)
{
	return (ASN1_IA5STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_IA5STRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_IA5STRING);

int
i2d_ASN1_IA5STRING(ASN1_IA5STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_IA5STRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_IA5STRING);

ASN1_IA5STRING *
ASN1_IA5STRING_new(void)
{
	return (ASN1_IA5STRING *)ASN1_item_new(&ASN1_IA5STRING_it);
}
LCRYPTO_ALIAS(ASN1_IA5STRING_new);

void
ASN1_IA5STRING_free(ASN1_IA5STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_IA5STRING_it);
}
LCRYPTO_ALIAS(ASN1_IA5STRING_free);


const ASN1_ITEM ASN1_GENERALSTRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_GENERALSTRING,
	.sname = "ASN1_GENERALSTRING",
};
LCRYPTO_ALIAS(ASN1_GENERALSTRING_it);

ASN1_GENERALSTRING *
d2i_ASN1_GENERALSTRING(ASN1_GENERALSTRING **a, const unsigned char **in,
    long len)
{
	return (ASN1_GENERALSTRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_GENERALSTRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_GENERALSTRING);

int
i2d_ASN1_GENERALSTRING(ASN1_GENERALSTRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_GENERALSTRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_GENERALSTRING);

ASN1_GENERALSTRING *
ASN1_GENERALSTRING_new(void)
{
	return (ASN1_GENERALSTRING *)ASN1_item_new(&ASN1_GENERALSTRING_it);
}
LCRYPTO_ALIAS(ASN1_GENERALSTRING_new);

void
ASN1_GENERALSTRING_free(ASN1_GENERALSTRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_GENERALSTRING_it);
}
LCRYPTO_ALIAS(ASN1_GENERALSTRING_free);


const ASN1_ITEM ASN1_UTCTIME_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_UTCTIME,
	.sname = "ASN1_UTCTIME",
};
LCRYPTO_ALIAS(ASN1_UTCTIME_it);

ASN1_UTCTIME *
d2i_ASN1_UTCTIME(ASN1_UTCTIME **a, const unsigned char **in, long len)
{
	return (ASN1_UTCTIME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_UTCTIME_it);
}
LCRYPTO_ALIAS(d2i_ASN1_UTCTIME);

int
i2d_ASN1_UTCTIME(ASN1_UTCTIME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_UTCTIME_it);
}
LCRYPTO_ALIAS(i2d_ASN1_UTCTIME);

ASN1_UTCTIME *
ASN1_UTCTIME_new(void)
{
	return (ASN1_UTCTIME *)ASN1_item_new(&ASN1_UTCTIME_it);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_new);

void
ASN1_UTCTIME_free(ASN1_UTCTIME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_UTCTIME_it);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_free);


const ASN1_ITEM ASN1_GENERALIZEDTIME_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_GENERALIZEDTIME,
	.sname = "ASN1_GENERALIZEDTIME",
};
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_it);

ASN1_GENERALIZEDTIME *
d2i_ASN1_GENERALIZEDTIME(ASN1_GENERALIZEDTIME **a, const unsigned char **in,
    long len)
{
	return (ASN1_GENERALIZEDTIME *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_GENERALIZEDTIME_it);
}
LCRYPTO_ALIAS(d2i_ASN1_GENERALIZEDTIME);

int
i2d_ASN1_GENERALIZEDTIME(ASN1_GENERALIZEDTIME *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_GENERALIZEDTIME_it);
}
LCRYPTO_ALIAS(i2d_ASN1_GENERALIZEDTIME);

ASN1_GENERALIZEDTIME *
ASN1_GENERALIZEDTIME_new(void)
{
	return (ASN1_GENERALIZEDTIME *)ASN1_item_new(&ASN1_GENERALIZEDTIME_it);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_new);

void
ASN1_GENERALIZEDTIME_free(ASN1_GENERALIZEDTIME *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_GENERALIZEDTIME_it);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_free);


const ASN1_ITEM ASN1_VISIBLESTRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_VISIBLESTRING,
	.sname = "ASN1_VISIBLESTRING",
};
LCRYPTO_ALIAS(ASN1_VISIBLESTRING_it);

ASN1_VISIBLESTRING *
d2i_ASN1_VISIBLESTRING(ASN1_VISIBLESTRING **a, const unsigned char **in,
    long len)
{
	return (ASN1_VISIBLESTRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_VISIBLESTRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_VISIBLESTRING);

int
i2d_ASN1_VISIBLESTRING(ASN1_VISIBLESTRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_VISIBLESTRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_VISIBLESTRING);

ASN1_VISIBLESTRING *
ASN1_VISIBLESTRING_new(void)
{
	return (ASN1_VISIBLESTRING *)ASN1_item_new(&ASN1_VISIBLESTRING_it);
}
LCRYPTO_ALIAS(ASN1_VISIBLESTRING_new);

void
ASN1_VISIBLESTRING_free(ASN1_VISIBLESTRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_VISIBLESTRING_it);
}
LCRYPTO_ALIAS(ASN1_VISIBLESTRING_free);


const ASN1_ITEM ASN1_UNIVERSALSTRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_UNIVERSALSTRING,
	.sname = "ASN1_UNIVERSALSTRING",
};
LCRYPTO_ALIAS(ASN1_UNIVERSALSTRING_it);

ASN1_UNIVERSALSTRING *
d2i_ASN1_UNIVERSALSTRING(ASN1_UNIVERSALSTRING **a, const unsigned char **in,
    long len)
{
	return (ASN1_UNIVERSALSTRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_UNIVERSALSTRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_UNIVERSALSTRING);

int
i2d_ASN1_UNIVERSALSTRING(ASN1_UNIVERSALSTRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_UNIVERSALSTRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_UNIVERSALSTRING);

ASN1_UNIVERSALSTRING *
ASN1_UNIVERSALSTRING_new(void)
{
	return (ASN1_UNIVERSALSTRING *)ASN1_item_new(&ASN1_UNIVERSALSTRING_it);
}
LCRYPTO_ALIAS(ASN1_UNIVERSALSTRING_new);

void
ASN1_UNIVERSALSTRING_free(ASN1_UNIVERSALSTRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_UNIVERSALSTRING_it);
}
LCRYPTO_ALIAS(ASN1_UNIVERSALSTRING_free);


const ASN1_ITEM ASN1_BMPSTRING_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_BMPSTRING,
	.sname = "ASN1_BMPSTRING",
};
LCRYPTO_ALIAS(ASN1_BMPSTRING_it);

ASN1_BMPSTRING *
d2i_ASN1_BMPSTRING(ASN1_BMPSTRING **a, const unsigned char **in, long len)
{
	return (ASN1_BMPSTRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_BMPSTRING_it);
}
LCRYPTO_ALIAS(d2i_ASN1_BMPSTRING);

int
i2d_ASN1_BMPSTRING(ASN1_BMPSTRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_BMPSTRING_it);
}
LCRYPTO_ALIAS(i2d_ASN1_BMPSTRING);

ASN1_BMPSTRING *
ASN1_BMPSTRING_new(void)
{
	return (ASN1_BMPSTRING *)ASN1_item_new(&ASN1_BMPSTRING_it);
}
LCRYPTO_ALIAS(ASN1_BMPSTRING_new);

void
ASN1_BMPSTRING_free(ASN1_BMPSTRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_BMPSTRING_it);
}
LCRYPTO_ALIAS(ASN1_BMPSTRING_free);

const ASN1_ITEM ASN1_ANY_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_ANY,
	.sname = "ASN1_ANY",
};
LCRYPTO_ALIAS(ASN1_ANY_it);


/* Just swallow an ASN1_SEQUENCE in an ASN1_STRING */

const ASN1_ITEM ASN1_SEQUENCE_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_SEQUENCE,
	.sname = "ASN1_SEQUENCE",
};
LCRYPTO_ALIAS(ASN1_SEQUENCE_it);


/* Multistring types */


const ASN1_ITEM ASN1_PRINTABLE_it = {
	.itype = ASN1_ITYPE_MSTRING,
	.utype = B_ASN1_PRINTABLE,
	.templates = NULL,
	.tcount = 0,
	.funcs = NULL,
	.size = sizeof(ASN1_STRING),
	.sname = "ASN1_PRINTABLE",
};
LCRYPTO_ALIAS(ASN1_PRINTABLE_it);

ASN1_STRING *
d2i_ASN1_PRINTABLE(ASN1_STRING **a, const unsigned char **in, long len)
{
	return (ASN1_STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_PRINTABLE_it);
}
LCRYPTO_ALIAS(d2i_ASN1_PRINTABLE);

int
i2d_ASN1_PRINTABLE(ASN1_STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_PRINTABLE_it);
}
LCRYPTO_ALIAS(i2d_ASN1_PRINTABLE);

ASN1_STRING *
ASN1_PRINTABLE_new(void)
{
	return (ASN1_STRING *)ASN1_item_new(&ASN1_PRINTABLE_it);
}
LCRYPTO_ALIAS(ASN1_PRINTABLE_new);

void
ASN1_PRINTABLE_free(ASN1_STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_PRINTABLE_it);
}
LCRYPTO_ALIAS(ASN1_PRINTABLE_free);


const ASN1_ITEM DISPLAYTEXT_it = {
	.itype = ASN1_ITYPE_MSTRING,
	.utype = B_ASN1_DISPLAYTEXT,
	.templates = NULL,
	.tcount = 0,
	.funcs = NULL,
	.size = sizeof(ASN1_STRING),
	.sname = "DISPLAYTEXT",
};
LCRYPTO_ALIAS(DISPLAYTEXT_it);

ASN1_STRING *
d2i_DISPLAYTEXT(ASN1_STRING **a, const unsigned char **in, long len)
{
	return (ASN1_STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DISPLAYTEXT_it);
}
LCRYPTO_ALIAS(d2i_DISPLAYTEXT);

int
i2d_DISPLAYTEXT(ASN1_STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DISPLAYTEXT_it);
}
LCRYPTO_ALIAS(i2d_DISPLAYTEXT);

ASN1_STRING *
DISPLAYTEXT_new(void)
{
	return (ASN1_STRING *)ASN1_item_new(&DISPLAYTEXT_it);
}
LCRYPTO_ALIAS(DISPLAYTEXT_new);

void
DISPLAYTEXT_free(ASN1_STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &DISPLAYTEXT_it);
}
LCRYPTO_ALIAS(DISPLAYTEXT_free);


const ASN1_ITEM DIRECTORYSTRING_it = {
	.itype = ASN1_ITYPE_MSTRING,
	.utype = B_ASN1_DIRECTORYSTRING,
	.templates = NULL,
	.tcount = 0,
	.funcs = NULL,
	.size = sizeof(ASN1_STRING),
	.sname = "DIRECTORYSTRING",
};
LCRYPTO_ALIAS(DIRECTORYSTRING_it);

ASN1_STRING *
d2i_DIRECTORYSTRING(ASN1_STRING **a, const unsigned char **in, long len)
{
	return (ASN1_STRING *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &DIRECTORYSTRING_it);
}
LCRYPTO_ALIAS(d2i_DIRECTORYSTRING);

int
i2d_DIRECTORYSTRING(ASN1_STRING *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &DIRECTORYSTRING_it);
}
LCRYPTO_ALIAS(i2d_DIRECTORYSTRING);

ASN1_STRING *
DIRECTORYSTRING_new(void)
{
	return (ASN1_STRING *)ASN1_item_new(&DIRECTORYSTRING_it);
}
LCRYPTO_ALIAS(DIRECTORYSTRING_new);

void
DIRECTORYSTRING_free(ASN1_STRING *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &DIRECTORYSTRING_it);
}
LCRYPTO_ALIAS(DIRECTORYSTRING_free);

/* Three separate BOOLEAN type: normal, DEFAULT TRUE and DEFAULT FALSE */

const ASN1_ITEM ASN1_BOOLEAN_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_BOOLEAN,
	.size = -1,
	.sname = "ASN1_BOOLEAN",
};
LCRYPTO_ALIAS(ASN1_BOOLEAN_it);

int
i2d_ASN1_BOOLEAN(int a, unsigned char **out)
{
	return ASN1_item_ex_i2d((ASN1_VALUE **)&a, out,
	    &ASN1_BOOLEAN_it, -1, 0);
}

int
d2i_ASN1_BOOLEAN(int *a, const unsigned char **in, long len)
{
	ASN1_BOOLEAN abool;

	if (ASN1_item_ex_d2i((ASN1_VALUE **)&abool, in, len, &ASN1_BOOLEAN_it,
	    -1, 0, 0, NULL) <= 0)
		return -1;

	if (a != NULL)
		*a = abool;

	return abool;
}

const ASN1_ITEM ASN1_TBOOLEAN_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_BOOLEAN,
	.size = 1,
	.sname = "ASN1_TBOOLEAN",
};
LCRYPTO_ALIAS(ASN1_TBOOLEAN_it);

const ASN1_ITEM ASN1_FBOOLEAN_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_BOOLEAN,
	.size = 0,
	.sname = "ASN1_FBOOLEAN",
};
LCRYPTO_ALIAS(ASN1_FBOOLEAN_it);

/* Special, OCTET STRING with indefinite length constructed support */

const ASN1_ITEM ASN1_OCTET_STRING_NDEF_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_OCTET_STRING,
	.size = ASN1_TFLG_NDEF,
	.sname = "ASN1_OCTET_STRING_NDEF",
};

static const ASN1_TEMPLATE ASN1_SEQUENCE_ANY_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "ASN1_SEQUENCE_ANY",
	.item = &ASN1_ANY_it,
};

const ASN1_ITEM ASN1_SEQUENCE_ANY_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &ASN1_SEQUENCE_ANY_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "ASN1_SEQUENCE_ANY",
};
LCRYPTO_ALIAS(ASN1_SEQUENCE_ANY_it);

static const ASN1_TEMPLATE ASN1_SET_ANY_item_tt = {
	.flags = ASN1_TFLG_SET_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "ASN1_SET_ANY",
	.item = &ASN1_ANY_it,
};

const ASN1_ITEM ASN1_SET_ANY_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &ASN1_SET_ANY_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "ASN1_SET_ANY",
};
LCRYPTO_ALIAS(ASN1_SET_ANY_it);


ASN1_SEQUENCE_ANY *
d2i_ASN1_SEQUENCE_ANY(ASN1_SEQUENCE_ANY **a, const unsigned char **in, long len)
{
	return (ASN1_SEQUENCE_ANY *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_SEQUENCE_ANY_it);
}
LCRYPTO_ALIAS(d2i_ASN1_SEQUENCE_ANY);

int
i2d_ASN1_SEQUENCE_ANY(const ASN1_SEQUENCE_ANY *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_SEQUENCE_ANY_it);
}
LCRYPTO_ALIAS(i2d_ASN1_SEQUENCE_ANY);

ASN1_SEQUENCE_ANY *
d2i_ASN1_SET_ANY(ASN1_SEQUENCE_ANY **a, const unsigned char **in, long len)
{
	return (ASN1_SEQUENCE_ANY *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_SET_ANY_it);
}
LCRYPTO_ALIAS(d2i_ASN1_SET_ANY);

int
i2d_ASN1_SET_ANY(const ASN1_SEQUENCE_ANY *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_SET_ANY_it);
}
LCRYPTO_ALIAS(i2d_ASN1_SET_ANY);
