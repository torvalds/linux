/* $OpenBSD: ts_req_utils.c,v 1.10 2025/05/10 05:54:39 tb Exp $ */
/* Written by Zoltan Glozik (zglozik@stones.com) for the OpenSSL
 * project 2002.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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

#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "ts_local.h"

int
TS_REQ_set_version(TS_REQ *a, long version)
{
	return ASN1_INTEGER_set(a->version, version);
}
LCRYPTO_ALIAS(TS_REQ_set_version);

long
TS_REQ_get_version(const TS_REQ *a)
{
	return ASN1_INTEGER_get(a->version);
}
LCRYPTO_ALIAS(TS_REQ_get_version);

int
TS_REQ_set_msg_imprint(TS_REQ *a, TS_MSG_IMPRINT *msg_imprint)
{
	TS_MSG_IMPRINT *new_msg_imprint;

	if (a->msg_imprint == msg_imprint)
		return 1;
	new_msg_imprint = TS_MSG_IMPRINT_dup(msg_imprint);
	if (new_msg_imprint == NULL) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	TS_MSG_IMPRINT_free(a->msg_imprint);
	a->msg_imprint = new_msg_imprint;
	return 1;
}
LCRYPTO_ALIAS(TS_REQ_set_msg_imprint);

TS_MSG_IMPRINT *
TS_REQ_get_msg_imprint(TS_REQ *a)
{
	return a->msg_imprint;
}
LCRYPTO_ALIAS(TS_REQ_get_msg_imprint);

int
TS_MSG_IMPRINT_set_algo(TS_MSG_IMPRINT *a, X509_ALGOR *alg)
{
	X509_ALGOR *new_alg;

	if (a->hash_algo == alg)
		return 1;
	new_alg = X509_ALGOR_dup(alg);
	if (new_alg == NULL) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	X509_ALGOR_free(a->hash_algo);
	a->hash_algo = new_alg;
	return 1;
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_set_algo);

X509_ALGOR *
TS_MSG_IMPRINT_get_algo(TS_MSG_IMPRINT *a)
{
	return a->hash_algo;
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_get_algo);

int
TS_MSG_IMPRINT_set_msg(TS_MSG_IMPRINT *a, unsigned char *d, int len)
{
	return ASN1_OCTET_STRING_set(a->hashed_msg, d, len);
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_set_msg);

ASN1_OCTET_STRING *
TS_MSG_IMPRINT_get_msg(TS_MSG_IMPRINT *a)
{
	return a->hashed_msg;
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_get_msg);

int
TS_REQ_set_policy_id(TS_REQ *a, const ASN1_OBJECT *policy)
{
	ASN1_OBJECT *new_policy;

	if (a->policy_id == policy)
		return 1;
	new_policy = OBJ_dup(policy);
	if (new_policy == NULL) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ASN1_OBJECT_free(a->policy_id);
	a->policy_id = new_policy;
	return 1;
}
LCRYPTO_ALIAS(TS_REQ_set_policy_id);

ASN1_OBJECT *
TS_REQ_get_policy_id(TS_REQ *a)
{
	return a->policy_id;
}
LCRYPTO_ALIAS(TS_REQ_get_policy_id);

int
TS_REQ_set_nonce(TS_REQ *a, const ASN1_INTEGER *nonce)
{
	ASN1_INTEGER *new_nonce;

	if (a->nonce == nonce)
		return 1;
	new_nonce = ASN1_INTEGER_dup(nonce);
	if (new_nonce == NULL) {
		TSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	ASN1_INTEGER_free(a->nonce);
	a->nonce = new_nonce;
	return 1;
}
LCRYPTO_ALIAS(TS_REQ_set_nonce);

const ASN1_INTEGER *
TS_REQ_get_nonce(const TS_REQ *a)
{
	return a->nonce;
}
LCRYPTO_ALIAS(TS_REQ_get_nonce);

int
TS_REQ_set_cert_req(TS_REQ *a, int cert_req)
{
	a->cert_req = cert_req ? 0xFF : 0x00;
	return 1;
}
LCRYPTO_ALIAS(TS_REQ_set_cert_req);

int
TS_REQ_get_cert_req(const TS_REQ *a)
{
	return a->cert_req ? 1 : 0;
}
LCRYPTO_ALIAS(TS_REQ_get_cert_req);

STACK_OF(X509_EXTENSION) *TS_REQ_get_exts(TS_REQ *a)
{
	return a->extensions;
}
LCRYPTO_ALIAS(TS_REQ_get_exts);

void
TS_REQ_ext_free(TS_REQ *a)
{
	if (!a)
		return;
	sk_X509_EXTENSION_pop_free(a->extensions, X509_EXTENSION_free);
	a->extensions = NULL;
}
LCRYPTO_ALIAS(TS_REQ_ext_free);

int
TS_REQ_get_ext_count(TS_REQ *a)
{
	return X509v3_get_ext_count(a->extensions);
}
LCRYPTO_ALIAS(TS_REQ_get_ext_count);

int
TS_REQ_get_ext_by_NID(TS_REQ *a, int nid, int lastpos)
{
	return X509v3_get_ext_by_NID(a->extensions, nid, lastpos);
}
LCRYPTO_ALIAS(TS_REQ_get_ext_by_NID);

int
TS_REQ_get_ext_by_OBJ(TS_REQ *a, const ASN1_OBJECT *obj, int lastpos)
{
	return X509v3_get_ext_by_OBJ(a->extensions, obj, lastpos);
}
LCRYPTO_ALIAS(TS_REQ_get_ext_by_OBJ);

int
TS_REQ_get_ext_by_critical(TS_REQ *a, int crit, int lastpos)
{
	return X509v3_get_ext_by_critical(a->extensions, crit, lastpos);
}
LCRYPTO_ALIAS(TS_REQ_get_ext_by_critical);

X509_EXTENSION *
TS_REQ_get_ext(TS_REQ *a, int loc)
{
	return X509v3_get_ext(a->extensions, loc);
}
LCRYPTO_ALIAS(TS_REQ_get_ext);

X509_EXTENSION *
TS_REQ_delete_ext(TS_REQ *a, int loc)
{
	return X509v3_delete_ext(a->extensions, loc);
}
LCRYPTO_ALIAS(TS_REQ_delete_ext);

int
TS_REQ_add_ext(TS_REQ *a, X509_EXTENSION *ex, int loc)
{
	return X509v3_add_ext(&a->extensions, ex, loc) != NULL;
}
LCRYPTO_ALIAS(TS_REQ_add_ext);

void *
TS_REQ_get_ext_d2i(TS_REQ *a, int nid, int *crit, int *idx)
{
	return X509V3_get_d2i(a->extensions, nid, crit, idx);
}
LCRYPTO_ALIAS(TS_REQ_get_ext_d2i);
