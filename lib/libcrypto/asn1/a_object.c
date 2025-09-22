/* $OpenBSD: a_object.c,v 1.56 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "err_local.h"

const ASN1_ITEM ASN1_OBJECT_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = V_ASN1_OBJECT,
	.sname = "ASN1_OBJECT",
};
LCRYPTO_ALIAS(ASN1_OBJECT_it);

ASN1_OBJECT *
ASN1_OBJECT_new(void)
{
	ASN1_OBJECT *a;

	if ((a = calloc(1, sizeof(ASN1_OBJECT))) == NULL) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}
	a->flags = ASN1_OBJECT_FLAG_DYNAMIC;

	return a;
}
LCRYPTO_ALIAS(ASN1_OBJECT_new);

void
ASN1_OBJECT_free(ASN1_OBJECT *a)
{
	if (a == NULL)
		return;
	if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC_STRINGS) {
		free((void *)a->sn);
		free((void *)a->ln);
		a->sn = a->ln = NULL;
	}
	if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC_DATA) {
		freezero((void *)a->data, a->length);
		a->data = NULL;
		a->length = 0;
	}
	if (a->flags & ASN1_OBJECT_FLAG_DYNAMIC)
		free(a);
}
LCRYPTO_ALIAS(ASN1_OBJECT_free);

ASN1_OBJECT *
ASN1_OBJECT_create(int nid, unsigned char *data, int len,
    const char *sn, const char *ln)
{
	ASN1_OBJECT o;

	o.sn = sn;
	o.ln = ln;
	o.data = data;
	o.nid = nid;
	o.length = len;
	o.flags = ASN1_OBJECT_FLAG_DYNAMIC | ASN1_OBJECT_FLAG_DYNAMIC_STRINGS |
	    ASN1_OBJECT_FLAG_DYNAMIC_DATA;
	return (OBJ_dup(&o));
}
LCRYPTO_ALIAS(ASN1_OBJECT_create);

static int
oid_add_arc(CBB *cbb, uint64_t arc)
{
	int started = 0;
	uint8_t val;
	int i;

	for (i = (sizeof(arc) * 8) / 7; i >= 0; i--) {
		val = (arc >> (i * 7)) & 0x7f;
		if (!started && i != 0 && val == 0)
			continue;
		if (i > 0)
			val |= 0x80;
		if (!CBB_add_u8(cbb, val))
			return 0;
		started = 1;
	}

	return 1;
}

static int
oid_parse_arc(CBS *cbs, uint64_t *out_arc)
{
	uint64_t arc = 0;
	uint8_t val;

	do {
		if (!CBS_get_u8(cbs, &val))
			return 0;
		if (arc == 0 && val == 0x80)
			return 0;
		if (out_arc != NULL && arc > (UINT64_MAX >> 7))
			return 0;
		arc = (arc << 7) | (val & 0x7f);
	} while (val & 0x80);

	if (out_arc != NULL)
		*out_arc = arc;

	return 1;
}

static int
oid_add_arc_txt(CBB *cbb, uint64_t arc, int first)
{
	const char *fmt = ".%llu";
	char s[22]; /* Digits in decimal representation of 2^64-1, plus '.' and NUL. */
	int n;

	if (first)
		fmt = "%llu";
	n = snprintf(s, sizeof(s), fmt, (unsigned long long)arc);
	if (n < 0 || (size_t)n >= sizeof(s))
		return 0;
	if (!CBB_add_bytes(cbb, s, n))
		return 0;

	return 1;
}

static int
oid_parse_arc_txt(CBS *cbs, uint64_t *out_arc, char *separator, int first)
{
	uint64_t arc = 0;
	int digits = 0;
	uint8_t val;

	if (!first) {
		if (!CBS_get_u8(cbs, &val))
			return 0;
		if ((*separator == 0 && val != '.' && val != ' ') ||
		    (*separator != 0 && val != *separator)) {
			ASN1error(ASN1_R_INVALID_SEPARATOR);
			return 0;
		}
		*separator = val;
	}

	while (CBS_len(cbs) > 0) {
		if (!CBS_peek_u8(cbs, &val))
			return 0;
		if (val == '.' || val == ' ')
			break;

		if (!CBS_get_u8(cbs, &val))
			return 0;
		if (val < '0' || val > '9') {
			/* For the first arc we treat this as the separator. */
			if (first) {
				ASN1error(ASN1_R_INVALID_SEPARATOR);
				return 0;
			}
			ASN1error(ASN1_R_INVALID_DIGIT);
			return 0;
		}
		val -= '0';

		if (digits > 0 && arc == 0 && val == 0) {
			ASN1error(ASN1_R_INVALID_NUMBER);
			return 0;
		}
		digits++;

		if (arc > UINT64_MAX / 10) {
			ASN1error(ASN1_R_TOO_LONG);
			return 0;
		}
		arc = arc * 10 + val;
	}

	if (digits < 1) {
		ASN1error(ASN1_R_INVALID_NUMBER);
		return 0;
	}

	*out_arc = arc;

	return 1;
}

static int
a2c_ASN1_OBJECT_internal(CBB *cbb, CBS *cbs)
{
	uint64_t arc, si1, si2;
	char separator = 0;

	if (!oid_parse_arc_txt(cbs, &si1, &separator, 1))
		return 0;

	if (CBS_len(cbs) == 0) {
		ASN1error(ASN1_R_MISSING_SECOND_NUMBER);
		return 0;
	}

	if (!oid_parse_arc_txt(cbs, &si2, &separator, 0))
		return 0;

	/*
	 * X.690 section 8.19 - the first two subidentifiers are encoded as
	 * (x * 40) + y, with x being limited to [0,1,2]. The second
	 * subidentifier cannot exceed 39 for x < 2.
	 */
	if (si1 > 2) {
		ASN1error(ASN1_R_FIRST_NUM_TOO_LARGE);
		return 0;
	}
	if ((si1 < 2 && si2 >= 40) || si2 > UINT64_MAX - si1 * 40) {
		ASN1error(ASN1_R_SECOND_NUMBER_TOO_LARGE);
		return 0;
	}
	arc = si1 * 40 + si2;

	if (!oid_add_arc(cbb, arc))
		return 0;

	while (CBS_len(cbs) > 0) {
		if (!oid_parse_arc_txt(cbs, &arc, &separator, 0))
			return 0;
		if (!oid_add_arc(cbb, arc))
			return 0;
	}

	return 1;
}

static int
c2a_ASN1_OBJECT(CBS *cbs, CBB *cbb)
{
	uint64_t arc, si1, si2;

	/*
	 * X.690 section 8.19 - the first two subidentifiers are encoded as
	 * (x * 40) + y, with x being limited to [0,1,2].
	 */
	if (!oid_parse_arc(cbs, &arc))
		return 0;
	if ((si1 = arc / 40) > 2)
		si1 = 2;
	si2 = arc - si1 * 40;

	if (!oid_add_arc_txt(cbb, si1, 1))
		return 0;
	if (!oid_add_arc_txt(cbb, si2, 0))
		return 0;

	while (CBS_len(cbs) > 0) {
		if (!oid_parse_arc(cbs, &arc))
			return 0;
		if (!oid_add_arc_txt(cbb, arc, 0))
			return 0;
	}

	/* NUL terminate. */
	if (!CBB_add_u8(cbb, 0))
		return 0;

	return 1;
}

int
a2d_ASN1_OBJECT(unsigned char *out, int out_len, const char *in, int in_len)
{
	uint8_t *data = NULL;
	size_t data_len;
	CBS cbs;
	CBB cbb;
	int ret = 0;

	memset(&cbb, 0, sizeof(cbb));

	if (in_len == -1)
		in_len = strlen(in);
	if (in_len <= 0)
		goto err;

	CBS_init(&cbs, in, in_len);

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!a2c_ASN1_OBJECT_internal(&cbb, &cbs))
		goto err;
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	if (data_len > INT_MAX)
		goto err;

	if (out != NULL) {
		if (out_len <= 0 || (size_t)out_len < data_len) {
			ASN1error(ASN1_R_BUFFER_TOO_SMALL);
			goto err;
		}
		memcpy(out, data, data_len);
	}

	ret = (int)data_len;

 err:
	CBB_cleanup(&cbb);
	free(data);

	return ret;
}
LCRYPTO_ALIAS(a2d_ASN1_OBJECT);

static int
i2t_ASN1_OBJECT_oid(const ASN1_OBJECT *aobj, CBB *cbb)
{
	CBS cbs;

	CBS_init(&cbs, aobj->data, aobj->length);

	return c2a_ASN1_OBJECT(&cbs, cbb);
}

static int
i2t_ASN1_OBJECT_name(const ASN1_OBJECT *aobj, CBB *cbb, const char **out_name)
{
	const char *name;
	int nid;

	*out_name = NULL;

	if ((nid = OBJ_obj2nid(aobj)) == NID_undef)
		return 0;

	if ((name = OBJ_nid2ln(nid)) == NULL)
		name = OBJ_nid2sn(nid);
	if (name == NULL)
		return 0;

	*out_name = name;

	if (!CBB_add_bytes(cbb, name, strlen(name)))
		return 0;

	/* NUL terminate. */
	if (!CBB_add_u8(cbb, 0))
		return 0;

	return 1;
}

static int
i2t_ASN1_OBJECT_cbb(const ASN1_OBJECT *aobj, CBB *cbb, int no_name)
{
	const char *name;

	if (!no_name) {
		if (i2t_ASN1_OBJECT_name(aobj, cbb, &name))
			return 1;
		if (name != NULL)
			return 0;
	}
	return i2t_ASN1_OBJECT_oid(aobj, cbb);
}

int
i2t_ASN1_OBJECT_internal(const ASN1_OBJECT *aobj, char *buf, int buf_len, int no_name)
{
	uint8_t *data = NULL;
	size_t data_len;
	CBB cbb;
	int ret = 0;

	if (buf_len < 0)
		return 0;
	if (buf_len > 0)
		buf[0] = '\0';

	if (aobj == NULL || aobj->data == NULL)
		return 0;

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!i2t_ASN1_OBJECT_cbb(aobj, &cbb, no_name))
		goto err;
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	ret = strlcpy(buf, data, buf_len);
 err:
	CBB_cleanup(&cbb);
	free(data);

	return ret;
}

int
i2t_ASN1_OBJECT(char *buf, int buf_len, const ASN1_OBJECT *aobj)
{
	return i2t_ASN1_OBJECT_internal(aobj, buf, buf_len, 0);
}
LCRYPTO_ALIAS(i2t_ASN1_OBJECT);

ASN1_OBJECT *
t2i_ASN1_OBJECT_internal(const char *oid)
{
	ASN1_OBJECT *aobj = NULL;
	uint8_t *data = NULL;
	size_t data_len;
	CBB cbb;
	CBS cbs;

	memset(&cbb, 0, sizeof(cbb));

	CBS_init(&cbs, oid, strlen(oid));

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!a2c_ASN1_OBJECT_internal(&cbb, &cbs))
		goto err;
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	if (data_len > INT_MAX)
		goto err;

	if ((aobj = ASN1_OBJECT_new()) == NULL)
		goto err;

	aobj->data = data;
	aobj->length = (int)data_len;
	aobj->flags |= ASN1_OBJECT_FLAG_DYNAMIC_DATA;
	data = NULL;

 err:
	CBB_cleanup(&cbb);
	free(data);

	return aobj;
}

int
i2a_ASN1_OBJECT(BIO *bp, const ASN1_OBJECT *aobj)
{
	uint8_t *data = NULL;
	size_t data_len;
	CBB cbb;
	int ret = -1;

	if (aobj == NULL || aobj->data == NULL)
		return BIO_write(bp, "NULL", 4);

	if (!CBB_init(&cbb, 0))
		goto err;
	if (!i2t_ASN1_OBJECT_cbb(aobj, &cbb, 0)) {
		ret = BIO_write(bp, "<INVALID>", 9);
		goto err;
	}
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	ret = BIO_write(bp, data, strlen(data));

 err:
	CBB_cleanup(&cbb);
	free(data);

	return ret;
}
LCRYPTO_ALIAS(i2a_ASN1_OBJECT);

int
c2i_ASN1_OBJECT_cbs(ASN1_OBJECT **out_aobj, CBS *content)
{
	ASN1_OBJECT *aobj = NULL;
	uint8_t *data = NULL;
	size_t data_len;
	CBS cbs;

	if (out_aobj == NULL)
		goto err;

	if (*out_aobj != NULL) {
		ASN1_OBJECT_free(*out_aobj);
		*out_aobj = NULL;
	}

	/* Parse and validate OID encoding per X.690 8.19.2. */
	CBS_dup(content, &cbs);
	if (CBS_len(&cbs) == 0) {
		ASN1error(ASN1_R_INVALID_OBJECT_ENCODING);
		goto err;
	}
	while (CBS_len(&cbs) > 0) {
		if (!oid_parse_arc(&cbs, NULL)) {
			ASN1error(ASN1_R_INVALID_OBJECT_ENCODING);
			goto err;
		}
	}

	if (!CBS_stow(content, &data, &data_len))
		goto err;

	if (data_len > INT_MAX)
		goto err;

	if ((aobj = ASN1_OBJECT_new()) == NULL)
		goto err;

	aobj->data = data;
	aobj->length = (int)data_len; /* XXX - change length to size_t. */
	aobj->flags |= ASN1_OBJECT_FLAG_DYNAMIC_DATA;

	*out_aobj = aobj;

	return 1;

 err:
	ASN1_OBJECT_free(aobj);
	free(data);

	return 0;
}

ASN1_OBJECT *
c2i_ASN1_OBJECT(ASN1_OBJECT **out_aobj, const unsigned char **pp, long len)
{
	ASN1_OBJECT *aobj = NULL;
	CBS content;

	if (out_aobj != NULL) {
		ASN1_OBJECT_free(*out_aobj);
		*out_aobj = NULL;
	}

	if (len < 0) {
		ASN1error(ASN1_R_LENGTH_ERROR);
		return NULL;
	}

	CBS_init(&content, *pp, len);

	if (!c2i_ASN1_OBJECT_cbs(&aobj, &content))
		return NULL;

	*pp = CBS_data(&content);

	if (out_aobj != NULL)
		*out_aobj = aobj;

	return aobj;
}

int
i2d_ASN1_OBJECT(const ASN1_OBJECT *a, unsigned char **pp)
{
	unsigned char *buf, *p;
	int objsize;

	if (a == NULL || a->data == NULL)
		return -1;

	objsize = ASN1_object_size(0, a->length, V_ASN1_OBJECT);

	if (pp == NULL)
		return objsize;

	if ((buf = *pp) == NULL)
		buf = calloc(1, objsize);
	if (buf == NULL)
		return -1;

	p = buf;
	ASN1_put_object(&p, 0, a->length, V_ASN1_OBJECT, V_ASN1_UNIVERSAL);
	memcpy(p, a->data, a->length);
	p += a->length;

	/* If buf was allocated, return it, otherwise return the advanced p. */
	if (*pp == NULL)
		p = buf;

	*pp = p;

	return objsize;
}
LCRYPTO_ALIAS(i2d_ASN1_OBJECT);

ASN1_OBJECT *
d2i_ASN1_OBJECT(ASN1_OBJECT **out_aobj, const unsigned char **pp, long length)
{
	ASN1_OBJECT *aobj = NULL;
	uint32_t tag_number;
	CBS cbs, content;

	if (out_aobj != NULL) {
		ASN1_OBJECT_free(*out_aobj);
		*out_aobj = NULL;
	}

	if (length < 0) {
		ASN1error(ASN1_R_LENGTH_ERROR);
		return NULL;
	}

	CBS_init(&cbs, *pp, length);

	if (!asn1_get_primitive(&cbs, 0, &tag_number, &content)) {
		ASN1error(ASN1_R_BAD_OBJECT_HEADER);
		return NULL;
	}
	if (tag_number != V_ASN1_OBJECT) {
		ASN1error(ASN1_R_EXPECTING_AN_OBJECT);
		return NULL;
	}

	if (!c2i_ASN1_OBJECT_cbs(&aobj, &content))
		return NULL;

	*pp = CBS_data(&cbs);

	if (out_aobj != NULL)
		*out_aobj = aobj;

	return aobj;
}
LCRYPTO_ALIAS(d2i_ASN1_OBJECT);
