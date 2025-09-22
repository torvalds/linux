/* $OpenBSD: x509_lu.c,v 1.68 2025/05/10 05:54:39 tb Exp $ */
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

#include <openssl/lhash.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

static int X509_OBJECT_up_ref_count(X509_OBJECT *a);

static X509_LOOKUP *
X509_LOOKUP_new(const X509_LOOKUP_METHOD *method)
{
	X509_LOOKUP *lu;

	if ((lu = calloc(1, sizeof(*lu))) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	lu->method = method;

	if (method->new_item != NULL && !method->new_item(lu)) {
		free(lu);
		return NULL;
	}

	return lu;
}

void
X509_LOOKUP_free(X509_LOOKUP *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->method != NULL && ctx->method->free != NULL)
		ctx->method->free(ctx);
	free(ctx);
}
LCRYPTO_ALIAS(X509_LOOKUP_free);

int
X509_LOOKUP_ctrl(X509_LOOKUP *ctx, int cmd, const char *argc, long argl,
    char **ret)
{
	if (ctx->method == NULL)
		return -1;
	if (ctx->method->ctrl == NULL)
		return 1;
	return ctx->method->ctrl(ctx, cmd, argc, argl, ret);
}
LCRYPTO_ALIAS(X509_LOOKUP_ctrl);

static int
X509_LOOKUP_by_subject(X509_LOOKUP *ctx, X509_LOOKUP_TYPE type, X509_NAME *name,
    X509_OBJECT *ret)
{
	if (ctx->method == NULL || ctx->method->get_by_subject == NULL)
		return 0;
	return ctx->method->get_by_subject(ctx, type, name, ret);
}

static int
x509_object_cmp(const X509_OBJECT * const *a, const X509_OBJECT * const *b)
{
	int ret;

	if ((ret = (*a)->type - (*b)->type) != 0)
		return ret;

	switch ((*a)->type) {
	case X509_LU_X509:
		return X509_subject_name_cmp((*a)->data.x509, (*b)->data.x509);
	case X509_LU_CRL:
		return X509_CRL_cmp((*a)->data.crl, (*b)->data.crl);
	}
	return 0;
}

X509_STORE *
X509_STORE_new(void)
{
	X509_STORE *store;

	if ((store = calloc(1, sizeof(*store))) == NULL)
		goto err;

	if ((store->objs = sk_X509_OBJECT_new(x509_object_cmp)) == NULL)
		goto err;
	if ((store->get_cert_methods = sk_X509_LOOKUP_new_null()) == NULL)
		goto err;
	if ((store->param = X509_VERIFY_PARAM_new()) == NULL)
		goto err;

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_X509_STORE, store,
	    &store->ex_data))
		goto err;

	store->references = 1;

	return store;

 err:
	X509error(ERR_R_MALLOC_FAILURE);
	X509_STORE_free(store);

	return NULL;
}
LCRYPTO_ALIAS(X509_STORE_new);

X509_OBJECT *
X509_OBJECT_new(void)
{
	X509_OBJECT *obj;

	if ((obj = calloc(1, sizeof(*obj))) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	obj->type = X509_LU_NONE;

	return obj;
}
LCRYPTO_ALIAS(X509_OBJECT_new);

void
X509_OBJECT_free(X509_OBJECT *a)
{
	if (a == NULL)
		return;

	switch (a->type) {
	case X509_LU_X509:
		X509_free(a->data.x509);
		break;
	case X509_LU_CRL:
		X509_CRL_free(a->data.crl);
		break;
	}

	free(a);
}
LCRYPTO_ALIAS(X509_OBJECT_free);

static X509_OBJECT *
x509_object_dup(const X509_OBJECT *obj)
{
	X509_OBJECT *copy;

	if ((copy = X509_OBJECT_new()) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	copy->type = obj->type;
	copy->data = obj->data;

	X509_OBJECT_up_ref_count(copy);

	return copy;
}

void
X509_STORE_free(X509_STORE *store)
{
	if (store == NULL)
		return;

	if (CRYPTO_add(&store->references, -1, CRYPTO_LOCK_X509_STORE) > 0)
		return;

	sk_X509_LOOKUP_pop_free(store->get_cert_methods, X509_LOOKUP_free);
	sk_X509_OBJECT_pop_free(store->objs, X509_OBJECT_free);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_X509_STORE, store, &store->ex_data);
	X509_VERIFY_PARAM_free(store->param);
	free(store);
}
LCRYPTO_ALIAS(X509_STORE_free);

int
X509_STORE_up_ref(X509_STORE *store)
{
	return CRYPTO_add(&store->references, 1, CRYPTO_LOCK_X509_STORE) > 1;
}
LCRYPTO_ALIAS(X509_STORE_up_ref);

X509_LOOKUP *
X509_STORE_add_lookup(X509_STORE *store, const X509_LOOKUP_METHOD *method)
{
	STACK_OF(X509_LOOKUP) *sk;
	X509_LOOKUP *lu;
	int i;

	sk = store->get_cert_methods;
	for (i = 0; i < sk_X509_LOOKUP_num(sk); i++) {
		lu = sk_X509_LOOKUP_value(sk, i);
		if (method == lu->method) {
			return lu;
		}
	}

	if ((lu = X509_LOOKUP_new(method)) == NULL)
		return NULL;

	lu->store_ctx = store;
	if (sk_X509_LOOKUP_push(store->get_cert_methods, lu) <= 0) {
		X509error(ERR_R_MALLOC_FAILURE);
		X509_LOOKUP_free(lu);
		return NULL;
	}

	return lu;
}
LCRYPTO_ALIAS(X509_STORE_add_lookup);

X509_OBJECT *
X509_STORE_CTX_get_obj_by_subject(X509_STORE_CTX *vs, X509_LOOKUP_TYPE type,
    X509_NAME *name)
{
	X509_OBJECT *obj;

	if ((obj = X509_OBJECT_new()) == NULL)
		return NULL;
	if (!X509_STORE_CTX_get_by_subject(vs, type, name, obj)) {
		X509_OBJECT_free(obj);
		return NULL;
	}

	return obj;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_obj_by_subject);

int
X509_STORE_CTX_get_by_subject(X509_STORE_CTX *vs, X509_LOOKUP_TYPE type,
    X509_NAME *name, X509_OBJECT *ret)
{
	X509_STORE *ctx = vs->store;
	X509_LOOKUP *lu;
	X509_OBJECT stmp, *tmp;
	int i;

	if (ctx == NULL)
		return 0;

	memset(&stmp, 0, sizeof(stmp));

	CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);
	tmp = X509_OBJECT_retrieve_by_subject(ctx->objs, type, name);
	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);

	if (tmp == NULL || type == X509_LU_CRL) {
		for (i = 0; i < sk_X509_LOOKUP_num(ctx->get_cert_methods); i++) {
			lu = sk_X509_LOOKUP_value(ctx->get_cert_methods, i);
			if (X509_LOOKUP_by_subject(lu, type, name, &stmp) != 0) {
				tmp = &stmp;
				break;
			}
		}
		if (tmp == NULL)
			return 0;
	}

	if (!X509_OBJECT_up_ref_count(tmp))
		return 0;

	*ret = *tmp;

	return 1;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_by_subject);

/* Add obj to the store. Takes ownership of obj. */
static int
X509_STORE_add_object(X509_STORE *store, X509_OBJECT *obj)
{
	int ret = 0;

	CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);

	if (X509_OBJECT_retrieve_match(store->objs, obj) != NULL) {
		/* Object is already present in the store. That's fine. */
		ret = 1;
		goto out;
	}

	if (sk_X509_OBJECT_push(store->objs, obj) <= 0) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto out;
	}

	obj = NULL;
	ret = 1;

 out:
	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
	X509_OBJECT_free(obj);

	return ret;
}

int
X509_STORE_add_cert(X509_STORE *store, X509 *x)
{
	X509_OBJECT *obj;

	if (x == NULL)
		return 0;

	if ((obj = X509_OBJECT_new()) == NULL)
		return 0;

	if (!X509_up_ref(x)) {
		X509_OBJECT_free(obj);
		return 0;
	}

	obj->type = X509_LU_X509;
	obj->data.x509 = x;

	return X509_STORE_add_object(store, obj);
}
LCRYPTO_ALIAS(X509_STORE_add_cert);

int
X509_STORE_add_crl(X509_STORE *store, X509_CRL *x)
{
	X509_OBJECT *obj;

	if (x == NULL)
		return 0;

	if ((obj = X509_OBJECT_new()) == NULL)
		return 0;

	if (!X509_CRL_up_ref(x)) {
		X509_OBJECT_free(obj);
		return 0;
	}

	obj->type = X509_LU_CRL;
	obj->data.crl = x;

	return X509_STORE_add_object(store, obj);
}
LCRYPTO_ALIAS(X509_STORE_add_crl);

static int
X509_OBJECT_up_ref_count(X509_OBJECT *a)
{
	switch (a->type) {
	case X509_LU_X509:
		return X509_up_ref(a->data.x509);
	case X509_LU_CRL:
		return X509_CRL_up_ref(a->data.crl);
	}
	return 1;
}

X509_LOOKUP_TYPE
X509_OBJECT_get_type(const X509_OBJECT *a)
{
	return a->type;
}
LCRYPTO_ALIAS(X509_OBJECT_get_type);

static int
x509_object_idx_cnt(STACK_OF(X509_OBJECT) *h, X509_LOOKUP_TYPE type,
    X509_NAME *name, int *pnmatch)
{
	X509_OBJECT stmp;
	X509 x509_s;
	X509_CINF cinf_s;
	X509_CRL crl_s;
	X509_CRL_INFO crl_info_s;
	int idx;

	stmp.type = type;
	switch (type) {
	case X509_LU_X509:
		stmp.data.x509 = &x509_s;
		x509_s.cert_info = &cinf_s;
		cinf_s.subject = name;
		break;
	case X509_LU_CRL:
		stmp.data.crl = &crl_s;
		crl_s.crl = &crl_info_s;
		crl_info_s.issuer = name;
		break;
	default:
		return -1;
	}

	idx = sk_X509_OBJECT_find(h, &stmp);
	if (idx >= 0 && pnmatch) {
		int tidx;
		const X509_OBJECT *tobj, *pstmp;

		*pnmatch = 1;
		pstmp = &stmp;
		for (tidx = idx + 1; tidx < sk_X509_OBJECT_num(h); tidx++) {
			tobj = sk_X509_OBJECT_value(h, tidx);
			if (x509_object_cmp(&tobj, &pstmp))
				break;
			(*pnmatch)++;
		}
	}
	return idx;
}

int
X509_OBJECT_idx_by_subject(STACK_OF(X509_OBJECT) *h, X509_LOOKUP_TYPE type,
    X509_NAME *name)
{
	return x509_object_idx_cnt(h, type, name, NULL);
}
LCRYPTO_ALIAS(X509_OBJECT_idx_by_subject);

X509_OBJECT *
X509_OBJECT_retrieve_by_subject(STACK_OF(X509_OBJECT) *h, X509_LOOKUP_TYPE type,
    X509_NAME *name)
{
	int idx;

	idx = X509_OBJECT_idx_by_subject(h, type, name);
	if (idx == -1)
		return NULL;
	return sk_X509_OBJECT_value(h, idx);
}
LCRYPTO_ALIAS(X509_OBJECT_retrieve_by_subject);

X509 *
X509_OBJECT_get0_X509(const X509_OBJECT *xo)
{
	if (xo != NULL && xo->type == X509_LU_X509)
		return xo->data.x509;
	return NULL;
}
LCRYPTO_ALIAS(X509_OBJECT_get0_X509);

X509_CRL *
X509_OBJECT_get0_X509_CRL(X509_OBJECT *xo)
{
	if (xo != NULL && xo->type == X509_LU_CRL)
		return xo->data.crl;
	return NULL;
}
LCRYPTO_ALIAS(X509_OBJECT_get0_X509_CRL);

static STACK_OF(X509) *
X509_get1_certs_from_cache(X509_STORE *store, X509_NAME *name)
{
	STACK_OF(X509) *sk = NULL;
	X509 *x = NULL;
	X509_OBJECT *obj;
	int i, idx, cnt;

	CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);

	idx = x509_object_idx_cnt(store->objs, X509_LU_X509, name, &cnt);
	if (idx < 0)
		goto err;

	if ((sk = sk_X509_new_null()) == NULL)
		goto err;

	for (i = 0; i < cnt; i++, idx++) {
		obj = sk_X509_OBJECT_value(store->objs, idx);

		x = obj->data.x509;
		if (!X509_up_ref(x)) {
			x = NULL;
			goto err;
		}
		if (!sk_X509_push(sk, x))
			goto err;
	}

	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);

	return sk;

 err:
	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
	sk_X509_pop_free(sk, X509_free);
	X509_free(x);

	return NULL;
}

STACK_OF(X509) *
X509_STORE_CTX_get1_certs(X509_STORE_CTX *ctx, X509_NAME *name)
{
	X509_STORE *store = ctx->store;
	STACK_OF(X509) *sk;
	X509_OBJECT *obj;

	if (store == NULL)
		return NULL;

	if ((sk = X509_get1_certs_from_cache(store, name)) != NULL)
		return sk;

	/* Nothing found: do lookup to possibly add new objects to cache. */
	obj = X509_STORE_CTX_get_obj_by_subject(ctx, X509_LU_X509, name);
	if (obj == NULL)
		return NULL;
	X509_OBJECT_free(obj);

	return X509_get1_certs_from_cache(store, name);
}
LCRYPTO_ALIAS(X509_STORE_CTX_get1_certs);

STACK_OF(X509_CRL) *
X509_STORE_CTX_get1_crls(X509_STORE_CTX *ctx, X509_NAME *name)
{
	X509_STORE *store = ctx->store;
	STACK_OF(X509_CRL) *sk = NULL;
	X509_CRL *x = NULL;
	X509_OBJECT *obj = NULL;
	int i, idx, cnt;

	if (store == NULL)
		return NULL;

	/* Always do lookup to possibly add new CRLs to cache */
	obj = X509_STORE_CTX_get_obj_by_subject(ctx, X509_LU_CRL, name);
	if (obj == NULL)
		return NULL;

	X509_OBJECT_free(obj);
	obj = NULL;

	CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);
	idx = x509_object_idx_cnt(store->objs, X509_LU_CRL, name, &cnt);
	if (idx < 0)
		goto err;

	if ((sk = sk_X509_CRL_new_null()) == NULL)
		goto err;

	for (i = 0; i < cnt; i++, idx++) {
		obj = sk_X509_OBJECT_value(store->objs, idx);

		x = obj->data.crl;
		if (!X509_CRL_up_ref(x)) {
			x = NULL;
			goto err;
		}
		if (!sk_X509_CRL_push(sk, x))
			goto err;
	}

	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
	return sk;

 err:
	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
	X509_CRL_free(x);
	sk_X509_CRL_pop_free(sk, X509_CRL_free);
	return NULL;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get1_crls);

X509_OBJECT *
X509_OBJECT_retrieve_match(STACK_OF(X509_OBJECT) *h, X509_OBJECT *x)
{
	int idx, i;
	X509_OBJECT *obj;

	idx = sk_X509_OBJECT_find(h, x);
	if (idx == -1)
		return NULL;
	if ((x->type != X509_LU_X509) && (x->type != X509_LU_CRL))
		return sk_X509_OBJECT_value(h, idx);
	for (i = idx; i < sk_X509_OBJECT_num(h); i++) {
		obj = sk_X509_OBJECT_value(h, i);
		if (x509_object_cmp((const X509_OBJECT **)&obj,
		    (const X509_OBJECT **)&x))
			return NULL;
		if (x->type == X509_LU_X509) {
			if (!X509_cmp(obj->data.x509, x->data.x509))
				return obj;
		} else if (x->type == X509_LU_CRL) {
			if (!X509_CRL_match(obj->data.crl, x->data.crl))
				return obj;
		} else
			return obj;
	}
	return NULL;
}
LCRYPTO_ALIAS(X509_OBJECT_retrieve_match);

/* Try to get issuer certificate from store. Due to limitations
 * of the API this can only retrieve a single certificate matching
 * a given subject name. However it will fill the cache with all
 * matching certificates, so we can examine the cache for all
 * matches.
 *
 * Return values are:
 *  1 lookup successful.
 *  0 certificate not found.
 * -1 some other error.
 */
int
X509_STORE_CTX_get1_issuer(X509 **out_issuer, X509_STORE_CTX *ctx, X509 *x)
{
	X509_NAME *xn;
	X509_OBJECT *obj, *pobj;
	X509 *issuer = NULL;
	int i, idx, ret;

	*out_issuer = NULL;

	xn = X509_get_issuer_name(x);
	obj = X509_STORE_CTX_get_obj_by_subject(ctx, X509_LU_X509, xn);
	if (obj == NULL)
		return 0;

	if ((issuer = X509_OBJECT_get0_X509(obj)) == NULL) {
		X509_OBJECT_free(obj);
		return 0;
	}
	if (!X509_up_ref(issuer)) {
		X509_OBJECT_free(obj);
		return -1;
	}

	/* If certificate matches all OK */
	if (ctx->check_issued(ctx, x, issuer)) {
		if (x509_check_cert_time(ctx, issuer, -1)) {
			*out_issuer = issuer;
			X509_OBJECT_free(obj);
			return 1;
		}
	}
	X509_free(issuer);
	issuer = NULL;
	X509_OBJECT_free(obj);
	obj = NULL;

	if (ctx->store == NULL)
		return 0;

	/* Else find index of first cert accepted by 'check_issued' */
	CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);
	idx = X509_OBJECT_idx_by_subject(ctx->store->objs, X509_LU_X509, xn);
	if (idx != -1) /* should be true as we've had at least one match */ {
		/* Look through all matching certs for suitable issuer */
		for (i = idx; i < sk_X509_OBJECT_num(ctx->store->objs); i++) {
			pobj = sk_X509_OBJECT_value(ctx->store->objs, i);
			/* See if we've run past the matches */
			if (pobj->type != X509_LU_X509)
				break;
			if (X509_NAME_cmp(xn,
			    X509_get_subject_name(pobj->data.x509)))
				break;
			if (ctx->check_issued(ctx, x, pobj->data.x509)) {
				issuer = pobj->data.x509;
				/*
				 * If times check, exit with match,
				 * otherwise keep looking. Leave last
				 * match in issuer so we return nearest
				 * match if no certificate time is OK.
				 */
				if (x509_check_cert_time(ctx, issuer, -1))
					break;
			}
		}
	}
	ret = 0;
	if (issuer != NULL) {
		if (!X509_up_ref(issuer)) {
			ret = -1;
		} else {
			*out_issuer = issuer;
			ret = 1;
		}
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
	return ret;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get1_issuer);

STACK_OF(X509_OBJECT) *
X509_STORE_get0_objects(X509_STORE *xs)
{
	return xs->objs;
}
LCRYPTO_ALIAS(X509_STORE_get0_objects);

static STACK_OF(X509_OBJECT) *
sk_X509_OBJECT_deep_copy(const STACK_OF(X509_OBJECT) *objs)
{
	STACK_OF(X509_OBJECT) *copy = NULL;
	X509_OBJECT *obj = NULL;
	int i;

	if ((copy = sk_X509_OBJECT_new(x509_object_cmp)) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	for (i = 0; i < sk_X509_OBJECT_num(objs); i++) {
		if ((obj = x509_object_dup(sk_X509_OBJECT_value(objs, i))) == NULL)
			goto err;
		if (!sk_X509_OBJECT_push(copy, obj))
			goto err;
		obj = NULL;
	}

	return copy;

 err:
	X509_OBJECT_free(obj);
	sk_X509_OBJECT_pop_free(copy, X509_OBJECT_free);

	return NULL;
}

STACK_OF(X509_OBJECT) *
X509_STORE_get1_objects(X509_STORE *store)
{
	STACK_OF(X509_OBJECT) *objs;

	if (store == NULL) {
		X509error(ERR_R_PASSED_NULL_PARAMETER);
		return NULL;
	}

	CRYPTO_r_lock(CRYPTO_LOCK_X509_STORE);
	objs = sk_X509_OBJECT_deep_copy(store->objs);
	CRYPTO_r_unlock(CRYPTO_LOCK_X509_STORE);

	return objs;
}
LCRYPTO_ALIAS(X509_STORE_get1_objects);

void *
X509_STORE_get_ex_data(X509_STORE *xs, int idx)
{
	return CRYPTO_get_ex_data(&xs->ex_data, idx);
}
LCRYPTO_ALIAS(X509_STORE_get_ex_data);

int
X509_STORE_set_ex_data(X509_STORE *xs, int idx, void *data)
{
	return CRYPTO_set_ex_data(&xs->ex_data, idx, data);
}
LCRYPTO_ALIAS(X509_STORE_set_ex_data);

int
X509_STORE_set_flags(X509_STORE *ctx, unsigned long flags)
{
	return X509_VERIFY_PARAM_set_flags(ctx->param, flags);
}
LCRYPTO_ALIAS(X509_STORE_set_flags);

int
X509_STORE_set_depth(X509_STORE *ctx, int depth)
{
	X509_VERIFY_PARAM_set_depth(ctx->param, depth);
	return 1;
}
LCRYPTO_ALIAS(X509_STORE_set_depth);

int
X509_STORE_set_purpose(X509_STORE *ctx, int purpose)
{
	return X509_VERIFY_PARAM_set_purpose(ctx->param, purpose);
}
LCRYPTO_ALIAS(X509_STORE_set_purpose);

int
X509_STORE_set_trust(X509_STORE *ctx, int trust)
{
	return X509_VERIFY_PARAM_set_trust(ctx->param, trust);
}
LCRYPTO_ALIAS(X509_STORE_set_trust);

int
X509_STORE_set1_param(X509_STORE *ctx, X509_VERIFY_PARAM *param)
{
	return X509_VERIFY_PARAM_set1(ctx->param, param);
}
LCRYPTO_ALIAS(X509_STORE_set1_param);

X509_VERIFY_PARAM *
X509_STORE_get0_param(X509_STORE *ctx)
{
	return ctx->param;
}
LCRYPTO_ALIAS(X509_STORE_get0_param);

void
X509_STORE_set_verify(X509_STORE *store, X509_STORE_CTX_verify_fn verify)
{
	store->verify = verify;
}
LCRYPTO_ALIAS(X509_STORE_set_verify);

X509_STORE_CTX_verify_fn
X509_STORE_get_verify(X509_STORE *store)
{
	return store->verify;
}
LCRYPTO_ALIAS(X509_STORE_get_verify);

void
X509_STORE_set_verify_cb(X509_STORE *store, X509_STORE_CTX_verify_cb verify_cb)
{
	store->verify_cb = verify_cb;
}
LCRYPTO_ALIAS(X509_STORE_set_verify_cb);

X509_STORE_CTX_verify_cb
X509_STORE_get_verify_cb(X509_STORE *store)
{
	return store->verify_cb;
}
LCRYPTO_ALIAS(X509_STORE_get_verify_cb);
