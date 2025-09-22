/* $OpenBSD: a_type.c,v 1.28 2025/05/10 05:54:38 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <string.h>

#include <openssl/asn1t.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "err_local.h"

typedef struct {
	ASN1_INTEGER *num;
	ASN1_OCTET_STRING *value;
} ASN1_int_octetstring;

static const ASN1_TEMPLATE ASN1_INT_OCTETSTRING_seq_tt[] = {
	{
		.offset = offsetof(ASN1_int_octetstring, num),
		.field_name = "num",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(ASN1_int_octetstring, value),
		.field_name = "value",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM ASN1_INT_OCTETSTRING_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ASN1_INT_OCTETSTRING_seq_tt,
	.tcount = sizeof(ASN1_INT_OCTETSTRING_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(ASN1_int_octetstring),
	.sname = "ASN1_INT_OCTETSTRING",
};

ASN1_TYPE *
ASN1_TYPE_new(void)
{
	return (ASN1_TYPE *)ASN1_item_new(&ASN1_ANY_it);
}
LCRYPTO_ALIAS(ASN1_TYPE_new);

void
ASN1_TYPE_free(ASN1_TYPE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ASN1_ANY_it);
}
LCRYPTO_ALIAS(ASN1_TYPE_free);

int
ASN1_TYPE_get(const ASN1_TYPE *a)
{
	/* Special non-pointer types. */
	if (a->type == V_ASN1_BOOLEAN || a->type == V_ASN1_NULL)
		return a->type;

	if (a->value.ptr != NULL)
		return a->type;

	return 0;
}
LCRYPTO_ALIAS(ASN1_TYPE_get);

void
ASN1_TYPE_set(ASN1_TYPE *a, int type, void *value)
{
	if (a->value.ptr != NULL) {
		ASN1_TYPE **tmp_a = &a;
		ASN1_primitive_free((ASN1_VALUE **)tmp_a, NULL);
	}
	a->type = type;
	if (type == V_ASN1_BOOLEAN)
		a->value.boolean = value ? 0xff : 0;
	else
		a->value.ptr = value;
}
LCRYPTO_ALIAS(ASN1_TYPE_set);

int
ASN1_TYPE_set1(ASN1_TYPE *a, int type, const void *value)
{
	if (!value || (type == V_ASN1_BOOLEAN)) {
		void *p = (void *)value;
		ASN1_TYPE_set(a, type, p);
	} else if (type == V_ASN1_OBJECT) {
		ASN1_OBJECT *odup;
		odup = OBJ_dup(value);
		if (!odup)
			return 0;
		ASN1_TYPE_set(a, type, odup);
	} else {
		ASN1_STRING *sdup;
		sdup = ASN1_STRING_dup(value);
		if (!sdup)
			return 0;
		ASN1_TYPE_set(a, type, sdup);
	}
	return 1;
}
LCRYPTO_ALIAS(ASN1_TYPE_set1);

/* Returns 0 if they are equal, != 0 otherwise. */
int
ASN1_TYPE_cmp(const ASN1_TYPE *a, const ASN1_TYPE *b)
{
	int result = -1;

	if (!a || !b || a->type != b->type)
		return -1;

	switch (a->type) {
	case V_ASN1_OBJECT:
		result = OBJ_cmp(a->value.object, b->value.object);
		break;
	case V_ASN1_BOOLEAN:
		result = a->value.boolean - b->value.boolean;
		break;
	case V_ASN1_NULL:
		result = 0;	/* They do not have content. */
		break;

	case V_ASN1_INTEGER:
	case V_ASN1_ENUMERATED:
	case V_ASN1_BIT_STRING:
	case V_ASN1_OCTET_STRING:
	case V_ASN1_SEQUENCE:
	case V_ASN1_SET:
	case V_ASN1_NUMERICSTRING:
	case V_ASN1_PRINTABLESTRING:
	case V_ASN1_T61STRING:
	case V_ASN1_VIDEOTEXSTRING:
	case V_ASN1_IA5STRING:
	case V_ASN1_UTCTIME:
	case V_ASN1_GENERALIZEDTIME:
	case V_ASN1_GRAPHICSTRING:
	case V_ASN1_VISIBLESTRING:
	case V_ASN1_GENERALSTRING:
	case V_ASN1_UNIVERSALSTRING:
	case V_ASN1_BMPSTRING:
	case V_ASN1_UTF8STRING:
	case V_ASN1_OTHER:
	default:
		result = ASN1_STRING_cmp((ASN1_STRING *)a->value.ptr,
		    (ASN1_STRING *)b->value.ptr);
		break;
	}

	return result;
}
LCRYPTO_ALIAS(ASN1_TYPE_cmp);

int
ASN1_TYPE_set_octetstring(ASN1_TYPE *a, const unsigned char *data, int len)
{
	ASN1_STRING *os;

	if ((os = ASN1_OCTET_STRING_new()) == NULL)
		return (0);
	if (!ASN1_STRING_set(os, data, len)) {
		ASN1_OCTET_STRING_free(os);
		return (0);
	}
	ASN1_TYPE_set(a, V_ASN1_OCTET_STRING, os);
	return (1);
}
LCRYPTO_ALIAS(ASN1_TYPE_set_octetstring);

int
ASN1_TYPE_get_octetstring(const ASN1_TYPE *a, unsigned char *data, int max_len)
{
	int ret, num;
	unsigned char *p;

	if ((a->type != V_ASN1_OCTET_STRING) ||
	    (a->value.octet_string == NULL)) {
		ASN1error(ASN1_R_DATA_IS_WRONG);
		return (-1);
	}
	p = ASN1_STRING_data(a->value.octet_string);
	ret = ASN1_STRING_length(a->value.octet_string);
	if (ret < max_len)
		num = ret;
	else
		num = max_len;
	memcpy(data, p, num);
	return (ret);
}
LCRYPTO_ALIAS(ASN1_TYPE_get_octetstring);

int
ASN1_TYPE_set_int_octetstring(ASN1_TYPE *at, long num, const unsigned char *data,
    int len)
{
	ASN1_int_octetstring *ios;
	ASN1_STRING *sp = NULL;
	int ret = 0;

	if ((ios = (ASN1_int_octetstring *)ASN1_item_new(
	    &ASN1_INT_OCTETSTRING_it)) == NULL)
		goto err;
	if (!ASN1_INTEGER_set(ios->num, num))
		goto err;
	if (!ASN1_OCTET_STRING_set(ios->value, data, len))
		goto err;

	if ((sp = ASN1_item_pack(ios, &ASN1_INT_OCTETSTRING_it, NULL)) == NULL)
		goto err;

	ASN1_TYPE_set(at, V_ASN1_SEQUENCE, sp);
	sp = NULL;

	ret = 1;

 err:
	ASN1_item_free((ASN1_VALUE *)ios, &ASN1_INT_OCTETSTRING_it);
	ASN1_STRING_free(sp);

	return ret;
}
LCRYPTO_ALIAS(ASN1_TYPE_set_int_octetstring);

int
ASN1_TYPE_get_int_octetstring(const ASN1_TYPE *at, long *num, unsigned char *data,
    int max_len)
{
	ASN1_STRING *sp = at->value.sequence;
	ASN1_int_octetstring *ios = NULL;
	int ret = -1;
	int len;

	if (at->type != V_ASN1_SEQUENCE || sp == NULL)
		goto err;

	if ((ios = ASN1_item_unpack(sp, &ASN1_INT_OCTETSTRING_it)) == NULL)
		goto err;

	if (num != NULL)
		*num = ASN1_INTEGER_get(ios->num);
	if (data != NULL) {
		len = ASN1_STRING_length(ios->value);
		if (len > max_len)
			len = max_len;
		memcpy(data, ASN1_STRING_data(ios->value), len);
	}

	ret = ASN1_STRING_length(ios->value);

 err:
	ASN1_item_free((ASN1_VALUE *)ios, &ASN1_INT_OCTETSTRING_it);

	if (ret == -1)
		ASN1error(ASN1_R_DATA_IS_WRONG);

	return ret;
}
LCRYPTO_ALIAS(ASN1_TYPE_get_int_octetstring);

ASN1_TYPE *
ASN1_TYPE_pack_sequence(const ASN1_ITEM *it, void *s, ASN1_TYPE **t)
{
	ASN1_OCTET_STRING *oct;
	ASN1_TYPE *rt;

	if ((oct = ASN1_item_pack(s, it, NULL)) == NULL)
		return NULL;

	if (t != NULL && *t != NULL) {
		rt = *t;
	} else {
		if ((rt = ASN1_TYPE_new()) == NULL) {
			ASN1_OCTET_STRING_free(oct);
			return NULL;
		}
		if (t != NULL)
			*t = rt;
	}
	ASN1_TYPE_set(rt, V_ASN1_SEQUENCE, oct);
	return rt;
}

void *
ASN1_TYPE_unpack_sequence(const ASN1_ITEM *it, const ASN1_TYPE *t)
{
	if (t == NULL || t->type != V_ASN1_SEQUENCE || t->value.sequence == NULL)
		return NULL;
	return ASN1_item_unpack(t->value.sequence, it);
}

int
i2d_ASN1_TYPE(ASN1_TYPE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ASN1_ANY_it);
}
LCRYPTO_ALIAS(i2d_ASN1_TYPE);

ASN1_TYPE *
d2i_ASN1_TYPE(ASN1_TYPE **a, const unsigned char **in, long len)
{
	return (ASN1_TYPE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ASN1_ANY_it);
}
LCRYPTO_ALIAS(d2i_ASN1_TYPE);
