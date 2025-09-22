/* $OpenBSD: x509name.c,v 1.36 2025/05/10 05:54:39 tb Exp $ */
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

#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

#include "bytestring.h"
#include "err_local.h"
#include "x509_local.h"

int
X509_NAME_get_text_by_NID(X509_NAME *name, int nid, char *buf, int len)
{
	ASN1_OBJECT *obj;

	obj = OBJ_nid2obj(nid);
	if (obj == NULL)
		return (-1);
	return (X509_NAME_get_text_by_OBJ(name, obj, buf, len));
}
LCRYPTO_ALIAS(X509_NAME_get_text_by_NID);

int
X509_NAME_get_text_by_OBJ(X509_NAME *name, const ASN1_OBJECT *obj, char *buf,
    int len)
{
	unsigned char *text = NULL;
	ASN1_STRING *data;
	int i, text_len;
	int ret = -1;
	CBS cbs;

	i = X509_NAME_get_index_by_OBJ(name, obj, -1);
	if (i < 0)
		goto err;
	data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i));
	/*
	 * Fail if we cannot encode as UTF-8, or if the UTF-8 encoding of the
	 * string contains a 0 byte, because mortal callers seldom handle the
	 * length difference correctly.
	 */
	if ((text_len = ASN1_STRING_to_UTF8(&text, data)) < 0)
		goto err;
	CBS_init(&cbs, text, text_len);
	if (CBS_contains_zero_byte(&cbs))
		goto err;
	/* We still support the "pass NULL to find out how much" API */
	if (buf != NULL) {
		if (len <= 0 || !CBS_write_bytes(&cbs, buf, len - 1, NULL))
			goto err;
		/* It must be a C string */
		buf[text_len] = '\0';
	}
	ret = text_len;

 err:
	free(text);
	return (ret);
}
LCRYPTO_ALIAS(X509_NAME_get_text_by_OBJ);

int
X509_NAME_entry_count(const X509_NAME *name)
{
	if (name == NULL)
		return (0);
	return (sk_X509_NAME_ENTRY_num(name->entries));
}
LCRYPTO_ALIAS(X509_NAME_entry_count);

int
X509_NAME_get_index_by_NID(const X509_NAME *name, int nid, int lastpos)
{
	ASN1_OBJECT *obj;

	obj = OBJ_nid2obj(nid);
	if (obj == NULL)
		return (-2);
	return (X509_NAME_get_index_by_OBJ(name, obj, lastpos));
}
LCRYPTO_ALIAS(X509_NAME_get_index_by_NID);

/* NOTE: you should be passing -1, not 0 as lastpos */
int
X509_NAME_get_index_by_OBJ(const X509_NAME *name, const ASN1_OBJECT *obj,
    int lastpos)
{
	int n;
	X509_NAME_ENTRY *ne;
	STACK_OF(X509_NAME_ENTRY) *sk;

	if (name == NULL)
		return (-1);
	if (lastpos < 0)
		lastpos = -1;
	sk = name->entries;
	n = sk_X509_NAME_ENTRY_num(sk);
	for (lastpos++; lastpos < n; lastpos++) {
		ne = sk_X509_NAME_ENTRY_value(sk, lastpos);
		if (OBJ_cmp(ne->object, obj) == 0)
			return (lastpos);
	}
	return (-1);
}
LCRYPTO_ALIAS(X509_NAME_get_index_by_OBJ);

X509_NAME_ENTRY *
X509_NAME_get_entry(const X509_NAME *name, int loc)
{
	if (name == NULL || sk_X509_NAME_ENTRY_num(name->entries) <= loc ||
	    loc < 0)
		return (NULL);
	else
		return (sk_X509_NAME_ENTRY_value(name->entries, loc));
}
LCRYPTO_ALIAS(X509_NAME_get_entry);

X509_NAME_ENTRY *
X509_NAME_delete_entry(X509_NAME *name, int loc)
{
	X509_NAME_ENTRY *ret;
	int i, n, set_prev, set_next;
	STACK_OF(X509_NAME_ENTRY) *sk;

	if (name == NULL || sk_X509_NAME_ENTRY_num(name->entries) <= loc ||
	    loc < 0)
		return (NULL);
	sk = name->entries;
	ret = sk_X509_NAME_ENTRY_delete(sk, loc);
	n = sk_X509_NAME_ENTRY_num(sk);
	name->modified = 1;
	if (loc == n)
		return (ret);

	/* else we need to fixup the set field */
	if (loc != 0)
		set_prev = (sk_X509_NAME_ENTRY_value(sk, loc - 1))->set;
	else
		set_prev = ret->set - 1;
	set_next = sk_X509_NAME_ENTRY_value(sk, loc)->set;

	/* set_prev is the previous set
	 * set is the current set
	 * set_next is the following
	 * prev  1 1	1 1	1 1	1 1
	 * set   1	1	2	2
	 * next  1 1	2 2	2 2	3 2
	 * so basically only if prev and next differ by 2, then
	 * re-number down by 1 */
	if (set_prev + 1 < set_next)
		for (i = loc; i < n; i++)
			sk_X509_NAME_ENTRY_value(sk, i)->set--;
	return (ret);
}
LCRYPTO_ALIAS(X509_NAME_delete_entry);

int
X509_NAME_add_entry_by_OBJ(X509_NAME *name, const ASN1_OBJECT *obj, int type,
    const unsigned char *bytes, int len, int loc, int set)
{
	X509_NAME_ENTRY *ne;
	int ret;

	ne = X509_NAME_ENTRY_create_by_OBJ(NULL, obj, type, bytes, len);
	if (!ne)
		return 0;
	ret = X509_NAME_add_entry(name, ne, loc, set);
	X509_NAME_ENTRY_free(ne);
	return ret;
}
LCRYPTO_ALIAS(X509_NAME_add_entry_by_OBJ);

int
X509_NAME_add_entry_by_NID(X509_NAME *name, int nid, int type,
    const unsigned char *bytes, int len, int loc, int set)
{
	X509_NAME_ENTRY *ne;
	int ret;

	ne = X509_NAME_ENTRY_create_by_NID(NULL, nid, type, bytes, len);
	if (!ne)
		return 0;
	ret = X509_NAME_add_entry(name, ne, loc, set);
	X509_NAME_ENTRY_free(ne);
	return ret;
}
LCRYPTO_ALIAS(X509_NAME_add_entry_by_NID);

int
X509_NAME_add_entry_by_txt(X509_NAME *name, const char *field, int type,
    const unsigned char *bytes, int len, int loc, int set)
{
	X509_NAME_ENTRY *ne;
	int ret;

	ne = X509_NAME_ENTRY_create_by_txt(NULL, field, type, bytes, len);
	if (!ne)
		return 0;
	ret = X509_NAME_add_entry(name, ne, loc, set);
	X509_NAME_ENTRY_free(ne);
	return ret;
}
LCRYPTO_ALIAS(X509_NAME_add_entry_by_txt);

/* if set is -1, append to previous set, 0 'a new one', and 1,
 * prepend to the guy we are about to stomp on. */
int
X509_NAME_add_entry(X509_NAME *name, const X509_NAME_ENTRY *ne, int loc,
    int set)
{
	X509_NAME_ENTRY *new_name = NULL;
	int n, i, inc;
	STACK_OF(X509_NAME_ENTRY) *sk;

	if (name == NULL)
		return (0);
	sk = name->entries;
	n = sk_X509_NAME_ENTRY_num(sk);
	if (loc > n)
		loc = n;
	else if (loc < 0)
		loc = n;
	inc = (set == 0);
	name->modified = 1;

	if (set == -1) {
		if (loc == 0) {
			set = 0;
			inc = 1;
		} else
			set = sk_X509_NAME_ENTRY_value(sk, loc - 1)->set;
	} else /* if (set >= 0) */ {
		if (loc >= n) {
			if (loc != 0)
				set = sk_X509_NAME_ENTRY_value(sk, loc - 1)->set + 1;
			else
				set = 0;
		} else
			set = sk_X509_NAME_ENTRY_value(sk, loc)->set;
	}

	/* OpenSSL has ASN1-generated X509_NAME_ENTRY_dup() without const. */
	if ((new_name = X509_NAME_ENTRY_dup((X509_NAME_ENTRY *)ne)) == NULL)
		goto err;
	new_name->set = set;
	if (!sk_X509_NAME_ENTRY_insert(sk, new_name, loc)) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if (inc) {
		n = sk_X509_NAME_ENTRY_num(sk);
		for (i = loc + 1; i < n; i++)
			sk_X509_NAME_ENTRY_value(sk, i)->set += 1;
	}
	return (1);

err:
	if (new_name != NULL)
		X509_NAME_ENTRY_free(new_name);
	return (0);
}
LCRYPTO_ALIAS(X509_NAME_add_entry);

X509_NAME_ENTRY *
X509_NAME_ENTRY_create_by_txt(X509_NAME_ENTRY **ne,
    const char *field, int type, const unsigned char *bytes, int len)
{
	ASN1_OBJECT *obj;
	X509_NAME_ENTRY *nentry;

	obj = OBJ_txt2obj(field, 0);
	if (obj == NULL) {
		X509error(X509_R_INVALID_FIELD_NAME);
		ERR_asprintf_error_data("name=%s", field);
		return (NULL);
	}
	nentry = X509_NAME_ENTRY_create_by_OBJ(ne, obj, type, bytes, len);
	ASN1_OBJECT_free(obj);
	return nentry;
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_create_by_txt);

X509_NAME_ENTRY *
X509_NAME_ENTRY_create_by_NID(X509_NAME_ENTRY **ne, int nid, int type,
    const unsigned char *bytes, int len)
{
	ASN1_OBJECT *obj;
	X509_NAME_ENTRY *nentry;

	obj = OBJ_nid2obj(nid);
	if (obj == NULL) {
		X509error(X509_R_UNKNOWN_NID);
		return (NULL);
	}
	nentry = X509_NAME_ENTRY_create_by_OBJ(ne, obj, type, bytes, len);
	ASN1_OBJECT_free(obj);
	return nentry;
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_create_by_NID);

X509_NAME_ENTRY *
X509_NAME_ENTRY_create_by_OBJ(X509_NAME_ENTRY **ne, const ASN1_OBJECT *obj,
    int type, const unsigned char *bytes, int len)
{
	X509_NAME_ENTRY *ret;

	if ((ne == NULL) || (*ne == NULL)) {
		if ((ret = X509_NAME_ENTRY_new()) == NULL)
			return (NULL);
	} else
		ret= *ne;

	if (!X509_NAME_ENTRY_set_object(ret, obj))
		goto err;
	if (!X509_NAME_ENTRY_set_data(ret, type, bytes, len))
		goto err;

	if ((ne != NULL) && (*ne == NULL))
		*ne = ret;
	return (ret);

err:
	if ((ne == NULL) || (ret != *ne))
		X509_NAME_ENTRY_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_create_by_OBJ);

int
X509_NAME_ENTRY_set_object(X509_NAME_ENTRY *ne, const ASN1_OBJECT *obj)
{
	if ((ne == NULL) || (obj == NULL)) {
		X509error(ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}
	ASN1_OBJECT_free(ne->object);
	ne->object = OBJ_dup(obj);
	return ((ne->object == NULL) ? 0 : 1);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_set_object);

int
X509_NAME_ENTRY_set_data(X509_NAME_ENTRY *ne, int type,
    const unsigned char *bytes, int len)
{
	int i;

	if ((ne == NULL) || ((bytes == NULL) && (len != 0)))
		return (0);
	if ((type > 0) && (type & MBSTRING_FLAG))
		return ASN1_STRING_set_by_NID(&ne->value, bytes, len, type,
		    OBJ_obj2nid(ne->object)) ? 1 : 0;
	if (len < 0)
		len = strlen((const char *)bytes);
	i = ASN1_STRING_set(ne->value, bytes, len);
	if (!i)
		return (0);
	if (type != V_ASN1_UNDEF) {
		if (type == V_ASN1_APP_CHOOSE)
			ne->value->type = ASN1_PRINTABLE_type(bytes, len);
		else
			ne->value->type = type;
	}
	return (1);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_set_data);

ASN1_OBJECT *
X509_NAME_ENTRY_get_object(const X509_NAME_ENTRY *ne)
{
	if (ne == NULL)
		return (NULL);
	return (ne->object);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_get_object);

ASN1_STRING *
X509_NAME_ENTRY_get_data(const X509_NAME_ENTRY *ne)
{
	if (ne == NULL)
		return (NULL);
	return (ne->value);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_get_data);

int
X509_NAME_ENTRY_set(const X509_NAME_ENTRY *ne)
{
	return (ne->set);
}
LCRYPTO_ALIAS(X509_NAME_ENTRY_set);
