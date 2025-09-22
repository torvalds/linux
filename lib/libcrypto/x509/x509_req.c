/* $OpenBSD: x509_req.c,v 1.44 2025/05/10 05:54:39 tb Exp $ */
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

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "x509_local.h"

X509_REQ *
X509_to_X509_REQ(X509 *x509, EVP_PKEY *signing_key, const EVP_MD *signing_md)
{
	X509_REQ *req;
	X509_NAME *subject;
	EVP_PKEY *public_key;

	if ((req = X509_REQ_new()) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((subject = X509_get_subject_name(x509)) == NULL)
		goto err;
	if (!X509_REQ_set_subject_name(req, subject))
		goto err;

	if ((public_key = X509_get0_pubkey(x509)) == NULL)
		goto err;
	if (!X509_REQ_set_pubkey(req, public_key))
		goto err;

	if (signing_key != NULL) {
		if (!X509_REQ_sign(req, signing_key, signing_md))
			goto err;
	}

	return req;

 err:
	X509_REQ_free(req);

	return NULL;
}
LCRYPTO_ALIAS(X509_to_X509_REQ);

EVP_PKEY *
X509_REQ_get_pubkey(X509_REQ *req)
{
	if (req == NULL || req->req_info == NULL)
		return NULL;
	return X509_PUBKEY_get(req->req_info->pubkey);
}
LCRYPTO_ALIAS(X509_REQ_get_pubkey);

EVP_PKEY *
X509_REQ_get0_pubkey(X509_REQ *req)
{
	if (req == NULL || req->req_info == NULL)
		return NULL;
	return X509_PUBKEY_get0(req->req_info->pubkey);
}
LCRYPTO_ALIAS(X509_REQ_get0_pubkey);

int
X509_REQ_check_private_key(X509_REQ *req, EVP_PKEY *pkey)
{
	EVP_PKEY *req_pubkey = NULL;
	int ret;

	if ((req_pubkey = X509_REQ_get0_pubkey(req)) == NULL)
		return 0;

	if ((ret = EVP_PKEY_cmp(req_pubkey, pkey)) == 1)
		return 1;

	switch (ret) {
	case 0:
		X509error(X509_R_KEY_VALUES_MISMATCH);
		return 0;
	case -1:
		X509error(X509_R_KEY_TYPE_MISMATCH);
		return 0;
	case -2:
#ifndef OPENSSL_NO_EC
		if (pkey->type == EVP_PKEY_EC) {
			X509error(ERR_R_EC_LIB);
			return 0;
		}
#endif
#ifndef OPENSSL_NO_DH
		if (pkey->type == EVP_PKEY_DH) {
			/* No idea */
			X509error(X509_R_CANT_CHECK_DH_KEY);
			return 0;
		}
#endif
		X509error(X509_R_UNKNOWN_KEY_TYPE);
		return 0;
	}

	return 0;
}
LCRYPTO_ALIAS(X509_REQ_check_private_key);

int
X509_REQ_extension_nid(int nid)
{
	return nid == NID_ext_req || nid == NID_ms_ext_req;
}
LCRYPTO_ALIAS(X509_REQ_extension_nid);

STACK_OF(X509_EXTENSION) *
X509_REQ_get_extensions(X509_REQ *req)
{
	X509_ATTRIBUTE *attr;
	ASN1_TYPE *ext = NULL;
	int idx;

	if (req == NULL || req->req_info == NULL)
		return NULL;

	if ((idx = X509_REQ_get_attr_by_NID(req, NID_ext_req, -1)) == -1)
		idx = X509_REQ_get_attr_by_NID(req, NID_ms_ext_req, -1);
	if (idx == -1)
		return NULL;

	if ((attr = X509_REQ_get_attr(req, idx)) == NULL)
		return NULL;
	if ((ext = X509_ATTRIBUTE_get0_type(attr, 0)) == NULL)
		return NULL;

	return ASN1_TYPE_unpack_sequence(&X509_EXTENSIONS_it, ext);
}
LCRYPTO_ALIAS(X509_REQ_get_extensions);

/*
 * Add a STACK_OF extensions to a certificate request: allow alternative OIDs
 * in case we want to create a non-standard one.
 */

int
X509_REQ_add_extensions_nid(X509_REQ *req, STACK_OF(X509_EXTENSION) *exts,
    int nid)
{
	unsigned char *ext = NULL;
	int extlen;
	int ret;

	if ((extlen = i2d_X509_EXTENSIONS(exts, &ext)) <= 0)
		return 0;

	ret = X509_REQ_add1_attr_by_NID(req, nid, V_ASN1_SEQUENCE, ext, extlen);
	free(ext);

	return ret;
}
LCRYPTO_ALIAS(X509_REQ_add_extensions_nid);

/* This is the normal usage: use the "official" OID */
int
X509_REQ_add_extensions(X509_REQ *req, STACK_OF(X509_EXTENSION) *exts)
{
	return X509_REQ_add_extensions_nid(req, exts, NID_ext_req);
}
LCRYPTO_ALIAS(X509_REQ_add_extensions);

/* Request attribute functions */

int
X509_REQ_get_attr_count(const X509_REQ *req)
{
	return sk_X509_ATTRIBUTE_num(req->req_info->attributes);
}
LCRYPTO_ALIAS(X509_REQ_get_attr_count);

int
X509_REQ_get_attr_by_NID(const X509_REQ *req, int nid, int lastpos)
{
	return X509at_get_attr_by_NID(req->req_info->attributes, nid, lastpos);
}
LCRYPTO_ALIAS(X509_REQ_get_attr_by_NID);

int
X509_REQ_get_attr_by_OBJ(const X509_REQ *req, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509at_get_attr_by_OBJ(req->req_info->attributes, obj, lastpos);
}
LCRYPTO_ALIAS(X509_REQ_get_attr_by_OBJ);

X509_ATTRIBUTE *
X509_REQ_get_attr(const X509_REQ *req, int loc)
{
	return sk_X509_ATTRIBUTE_value(req->req_info->attributes, loc);
}
LCRYPTO_ALIAS(X509_REQ_get_attr);

X509_ATTRIBUTE *
X509_REQ_delete_attr(X509_REQ *req, int loc)
{
	return sk_X509_ATTRIBUTE_delete(req->req_info->attributes, loc);
}
LCRYPTO_ALIAS(X509_REQ_delete_attr);

int
X509_REQ_add1_attr(X509_REQ *req, X509_ATTRIBUTE *attr)
{
	if (X509at_add1_attr(&req->req_info->attributes, attr))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_REQ_add1_attr);

int
X509_REQ_add1_attr_by_OBJ(X509_REQ *req, const ASN1_OBJECT *obj, int type,
    const unsigned char *bytes, int len)
{
	if (X509at_add1_attr_by_OBJ(&req->req_info->attributes, obj,
	    type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_REQ_add1_attr_by_OBJ);

int
X509_REQ_add1_attr_by_NID(X509_REQ *req, int nid, int type,
    const unsigned char *bytes, int len)
{
	if (X509at_add1_attr_by_NID(&req->req_info->attributes, nid,
	    type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_REQ_add1_attr_by_NID);

int
X509_REQ_add1_attr_by_txt(X509_REQ *req, const char *attrname, int type,
    const unsigned char *bytes, int len)
{
	if (X509at_add1_attr_by_txt(&req->req_info->attributes, attrname,
	    type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_REQ_add1_attr_by_txt);

int
i2d_re_X509_REQ_tbs(X509_REQ *req, unsigned char **pp)
{
	req->req_info->enc.modified = 1;
	return i2d_X509_REQ_INFO(req->req_info, pp);
}
LCRYPTO_ALIAS(i2d_re_X509_REQ_tbs);
