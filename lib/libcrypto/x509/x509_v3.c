/* $OpenBSD: x509_v3.c,v 1.44 2025/05/10 05:54:39 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/stack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

int
X509v3_get_ext_count(const STACK_OF(X509_EXTENSION) *exts)
{
	if (exts == NULL)
		return 0;

	return sk_X509_EXTENSION_num(exts);
}
LCRYPTO_ALIAS(X509v3_get_ext_count);

int
X509v3_get_ext_by_NID(const STACK_OF(X509_EXTENSION) *exts, int nid, int lastpos)
{
	const ASN1_OBJECT *obj;

	if ((obj = OBJ_nid2obj(nid)) == NULL)
		return -2;

	return X509v3_get_ext_by_OBJ(exts, obj, lastpos);
}
LCRYPTO_ALIAS(X509v3_get_ext_by_NID);

int
X509v3_get_ext_by_OBJ(const STACK_OF(X509_EXTENSION) *exts,
    const ASN1_OBJECT *obj, int lastpos)
{
	if (++lastpos < 0)
		lastpos = 0;

	for (; lastpos < X509v3_get_ext_count(exts); lastpos++) {
		const X509_EXTENSION *ext = X509v3_get_ext(exts, lastpos);

		if (OBJ_cmp(ext->object, obj) == 0)
			return lastpos;
	}

	return -1;
}
LCRYPTO_ALIAS(X509v3_get_ext_by_OBJ);

int
X509v3_get_ext_by_critical(const STACK_OF(X509_EXTENSION) *exts, int critical,
    int lastpos)
{
	critical = (critical != 0);

	if (++lastpos < 0)
		lastpos = 0;

	for (; lastpos < X509v3_get_ext_count(exts); lastpos++) {
		const X509_EXTENSION *ext = X509v3_get_ext(exts, lastpos);

		if (X509_EXTENSION_get_critical(ext) == critical)
			return lastpos;
	}

	return -1;
}
LCRYPTO_ALIAS(X509v3_get_ext_by_critical);

X509_EXTENSION *
X509v3_get_ext(const STACK_OF(X509_EXTENSION) *exts, int loc)
{
	return sk_X509_EXTENSION_value(exts, loc);
}
LCRYPTO_ALIAS(X509v3_get_ext);

X509_EXTENSION *
X509v3_delete_ext(STACK_OF(X509_EXTENSION) *exts, int loc)
{
	return sk_X509_EXTENSION_delete(exts, loc);
}
LCRYPTO_ALIAS(X509v3_delete_ext);

STACK_OF(X509_EXTENSION) *
X509v3_add_ext(STACK_OF(X509_EXTENSION) **out_exts, X509_EXTENSION *ext, int loc)
{
	STACK_OF(X509_EXTENSION) *exts = NULL;
	X509_EXTENSION *new_ext = NULL;

	/*
	 * XXX - Nonsense from the poorly reviewed OpenSSL c755c5fd8ba (2005).
	 * This check should have been joined with the next check, i.e., if no
	 * stack was passed in, a new one should be created and returned.
	 */
	if (out_exts == NULL) {
		X509error(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if ((exts = *out_exts) == NULL)
		exts = sk_X509_EXTENSION_new_null();
	if (exts == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((new_ext = X509_EXTENSION_dup(ext)) == NULL)
		goto err;
	if (!sk_X509_EXTENSION_insert(exts, new_ext, loc))
		goto err;
	new_ext = NULL;

	*out_exts = exts;

	return exts;

 err:
	X509_EXTENSION_free(new_ext);
	if (out_exts != NULL && exts != *out_exts)
		sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);

	return NULL;
}
LCRYPTO_ALIAS(X509v3_add_ext);

X509_EXTENSION *
X509_EXTENSION_create_by_NID(X509_EXTENSION **out_ext, int nid, int critical,
    ASN1_OCTET_STRING *data)
{
	const ASN1_OBJECT *obj;

	if ((obj = OBJ_nid2obj(nid)) == NULL) {
		X509error(X509_R_UNKNOWN_NID);
		return NULL;
	}

	return X509_EXTENSION_create_by_OBJ(out_ext, obj, critical, data);
}
LCRYPTO_ALIAS(X509_EXTENSION_create_by_NID);

X509_EXTENSION *
X509_EXTENSION_create_by_OBJ(X509_EXTENSION **out_ext, const ASN1_OBJECT *obj,
    int critical, ASN1_OCTET_STRING *data)
{
	X509_EXTENSION *ext;

	if (out_ext == NULL || (ext = *out_ext) == NULL)
		ext = X509_EXTENSION_new();
	if (ext == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (!X509_EXTENSION_set_object(ext, obj))
		goto err;
	if (!X509_EXTENSION_set_critical(ext, critical))
		goto err;
	if (!X509_EXTENSION_set_data(ext, data))
		goto err;

	if (out_ext != NULL)
		*out_ext = ext;

	return ext;

 err:
	if (out_ext == NULL || ext != *out_ext)
		X509_EXTENSION_free(ext);

	return NULL;
}
LCRYPTO_ALIAS(X509_EXTENSION_create_by_OBJ);

int
X509_EXTENSION_set_object(X509_EXTENSION *ext, const ASN1_OBJECT *obj)
{
	if (ext == NULL || obj == NULL)
		return 0;

	ASN1_OBJECT_free(ext->object);
	return (ext->object = OBJ_dup(obj)) != NULL;
}
LCRYPTO_ALIAS(X509_EXTENSION_set_object);

int
X509_EXTENSION_set_critical(X509_EXTENSION *ext, int critical)
{
	if (ext == NULL)
		return 0;

	ext->critical = critical ? 0xFF : -1;

	return 1;
}
LCRYPTO_ALIAS(X509_EXTENSION_set_critical);

int
X509_EXTENSION_set_data(X509_EXTENSION *ext, ASN1_OCTET_STRING *data)
{
	if (ext == NULL)
		return 0;

	return ASN1_STRING_set(ext->value, data->data, data->length);
}
LCRYPTO_ALIAS(X509_EXTENSION_set_data);

ASN1_OBJECT *
X509_EXTENSION_get_object(X509_EXTENSION *ext)
{
	if (ext == NULL)
		return NULL;

	return ext->object;
}
LCRYPTO_ALIAS(X509_EXTENSION_get_object);

ASN1_OCTET_STRING *
X509_EXTENSION_get_data(X509_EXTENSION *ext)
{
	if (ext == NULL)
		return NULL;

	return ext->value;
}
LCRYPTO_ALIAS(X509_EXTENSION_get_data);

int
X509_EXTENSION_get_critical(const X509_EXTENSION *ext)
{
	if (ext == NULL)
		return 0;

	return ext->critical > 0;
}
LCRYPTO_ALIAS(X509_EXTENSION_get_critical);
