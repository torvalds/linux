/* $OpenBSD: x509_vpm.c,v 1.56 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2004.
 */
/* ====================================================================
 * Copyright (c) 2004 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/lhash.h>
#include <openssl/stack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

/* X509_VERIFY_PARAM functions */

int X509_VERIFY_PARAM_set1_email(X509_VERIFY_PARAM *param, const char *email,
    size_t emaillen);
int X509_VERIFY_PARAM_set1_ip(X509_VERIFY_PARAM *param, const unsigned char *ip,
    size_t iplen);

#define SET_HOST 0
#define ADD_HOST 1

static void
str_free(char *s)
{
	free(s);
}

static STACK_OF(OPENSSL_STRING) *
sk_OPENSSL_STRING_deep_copy(const STACK_OF(OPENSSL_STRING) *sk)
{
	STACK_OF(OPENSSL_STRING) *new;
	char *copy = NULL;
	int i;

	if ((new = sk_OPENSSL_STRING_new_null()) == NULL)
		goto err;

	for (i = 0; i < sk_OPENSSL_STRING_num(sk); i++) {
		if ((copy = strdup(sk_OPENSSL_STRING_value(sk, i))) == NULL)
			goto err;
		if (sk_OPENSSL_STRING_push(new, copy) <= 0)
			goto err;
		copy = NULL;
	}

	return new;

 err:
	sk_OPENSSL_STRING_pop_free(new, str_free);
	free(copy);

	return NULL;
}

static int
x509_param_set_hosts_internal(X509_VERIFY_PARAM *param, int mode,
    const char *name, size_t namelen)
{
	char *copy;

	if (name != NULL && namelen == 0)
		namelen = strlen(name);
	/*
	 * Refuse names with embedded NUL bytes.
	 */
	if (name && memchr(name, '\0', namelen))
		return 0;

	if (mode == SET_HOST && param->hosts) {
		sk_OPENSSL_STRING_pop_free(param->hosts, str_free);
		param->hosts = NULL;
	}
	if (name == NULL || namelen == 0)
		return 1;
	copy = strndup(name, namelen);
	if (copy == NULL)
		return 0;

	if (param->hosts == NULL &&
	   (param->hosts = sk_OPENSSL_STRING_new_null()) == NULL) {
		free(copy);
		return 0;
	}

	if (!sk_OPENSSL_STRING_push(param->hosts, copy)) {
		free(copy);
		if (sk_OPENSSL_STRING_num(param->hosts) == 0) {
			sk_OPENSSL_STRING_free(param->hosts);
			param->hosts = NULL;
		}
		return 0;
	}

	return 1;
}

static void
x509_verify_param_zero(X509_VERIFY_PARAM *param)
{
	if (!param)
		return;

	free(param->name);
	param->name = NULL;
	param->purpose = 0;
	param->trust = 0;
	/*param->inh_flags = X509_VP_FLAG_DEFAULT;*/
	param->inh_flags = 0;
	param->flags = 0;
	param->depth = -1;
	sk_ASN1_OBJECT_pop_free(param->policies, ASN1_OBJECT_free);
	param->policies = NULL;
	sk_OPENSSL_STRING_pop_free(param->hosts, str_free);
	param->hosts = NULL;
	free(param->peername);
	param->peername = NULL;
	free(param->email);
	param->email = NULL;
	param->emaillen = 0;
	free(param->ip);
	param->ip = NULL;
	param->iplen = 0;
	param->poisoned = 0;
}

X509_VERIFY_PARAM *
X509_VERIFY_PARAM_new(void)
{
	X509_VERIFY_PARAM *param;

	param = calloc(1, sizeof(X509_VERIFY_PARAM));
	if (param == NULL)
		return NULL;
	x509_verify_param_zero(param);
	return param;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_new);

void
X509_VERIFY_PARAM_free(X509_VERIFY_PARAM *param)
{
	if (param == NULL)
		return;
	x509_verify_param_zero(param);
	free(param);
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_free);

/*
 * This function determines how parameters are "inherited" from one structure
 * to another. There are several different ways this can happen.
 *
 * 1. If a child structure needs to have its values initialized from a parent
 *    they are simply copied across. For example SSL_CTX copied to SSL.
 * 2. If the structure should take on values only if they are currently unset.
 *    For example the values in an SSL structure will take appropriate value
 *    for SSL servers or clients but only if the application has not set new
 *    ones.
 *
 * The "inh_flags" field determines how this function behaves.
 *
 * Normally any values which are set in the default are not copied from the
 * destination and verify flags are ORed together.
 *
 * If X509_VP_FLAG_DEFAULT is set then anything set in the source is copied
 * to the destination. Effectively the values in "to" become default values
 * which will be used only if nothing new is set in "from".
 *
 * If X509_VP_FLAG_OVERWRITE is set then all value are copied across whether
 * they are set or not. Flags is still Ored though.
 *
 * If X509_VP_FLAG_RESET_FLAGS is set then the flags value is copied instead
 * of ORed.
 *
 * If X509_VP_FLAG_LOCKED is set then no values are copied.
 *
 * If X509_VP_FLAG_ONCE is set then the current inh_flags setting is zeroed
 * after the next call.
 */

/* Macro to test if a field should be copied from src to dest */
#define test_x509_verify_param_copy(field, def) \
	(to_overwrite || \
		((src->field != def) && (to_default || (dest->field == def))))

/* Macro to test and copy a field if necessary */
#define x509_verify_param_copy(field, def) \
	if (test_x509_verify_param_copy(field, def)) \
		dest->field = src->field

int
X509_VERIFY_PARAM_inherit(X509_VERIFY_PARAM *dest, const X509_VERIFY_PARAM *src)
{
	unsigned long inh_flags;
	int to_default, to_overwrite;

	if (!src)
		return 1;
	inh_flags = dest->inh_flags | src->inh_flags;

	if (inh_flags & X509_VP_FLAG_ONCE)
		dest->inh_flags = 0;

	if (inh_flags & X509_VP_FLAG_LOCKED)
		return 1;

	if (inh_flags & X509_VP_FLAG_DEFAULT)
		to_default = 1;
	else
		to_default = 0;

	if (inh_flags & X509_VP_FLAG_OVERWRITE)
		to_overwrite = 1;
	else
		to_overwrite = 0;

	x509_verify_param_copy(purpose, 0);
	x509_verify_param_copy(trust, 0);
	x509_verify_param_copy(depth, -1);

	/* If overwrite or check time not set, copy across */

	if (to_overwrite || !(dest->flags & X509_V_FLAG_USE_CHECK_TIME)) {
		dest->check_time = src->check_time;
		dest->flags &= ~X509_V_FLAG_USE_CHECK_TIME;
		/* Don't need to copy flag: that is done below */
	}

	if (inh_flags & X509_VP_FLAG_RESET_FLAGS)
		dest->flags = 0;

	dest->flags |= src->flags;

	if (test_x509_verify_param_copy(policies, NULL)) {
		if (!X509_VERIFY_PARAM_set1_policies(dest, src->policies))
			return 0;
	}

	x509_verify_param_copy(hostflags, 0);

	if (test_x509_verify_param_copy(hosts, NULL)) {
		if (dest->hosts) {
			sk_OPENSSL_STRING_pop_free(dest->hosts, str_free);
			dest->hosts = NULL;
		}
		if (src->hosts) {
			dest->hosts = sk_OPENSSL_STRING_deep_copy(src->hosts);
			if (dest->hosts == NULL)
				return 0;
		}
	}

	if (test_x509_verify_param_copy(email, NULL)) {
		if (!X509_VERIFY_PARAM_set1_email(dest, src->email,
		    src->emaillen))
			return 0;
	}

	if (test_x509_verify_param_copy(ip, NULL)) {
		if (!X509_VERIFY_PARAM_set1_ip(dest, src->ip, src->iplen))
			return 0;
	}

	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_inherit);

int
X509_VERIFY_PARAM_set1(X509_VERIFY_PARAM *to, const X509_VERIFY_PARAM *from)
{
	unsigned long save_flags = to->inh_flags;
	int ret;

	to->inh_flags |= X509_VP_FLAG_DEFAULT;
	ret = X509_VERIFY_PARAM_inherit(to, from);
	to->inh_flags = save_flags;
	return ret;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1);

static int
x509_param_set1_internal(char **pdest, size_t *pdestlen,  const char *src,
    size_t srclen, int nonul)
{
	char *tmp;

	if (src == NULL)
		return 0;

	if (srclen == 0) {
		srclen = strlen(src);
		if (srclen == 0)
			return 0;
		if ((tmp = strdup(src)) == NULL)
			return 0;
	} else {
		if (nonul && memchr(src, '\0', srclen))
			return 0;
		if ((tmp = malloc(srclen)) == NULL)
			return 0;
		memcpy(tmp, src, srclen);
	}

	if (*pdest)
		free(*pdest);
	*pdest = tmp;
	if (pdestlen)
		*pdestlen = srclen;
	return 1;
}

int
X509_VERIFY_PARAM_set1_name(X509_VERIFY_PARAM *param, const char *name)
{
	free(param->name);
	param->name = NULL;
	if (name == NULL)
		return 1;
	param->name = strdup(name);
	if (param->name)
		return 1;
	return 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_name);

int
X509_VERIFY_PARAM_set_flags(X509_VERIFY_PARAM *param, unsigned long flags)
{
	param->flags |= flags;
	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_flags);

int
X509_VERIFY_PARAM_clear_flags(X509_VERIFY_PARAM *param, unsigned long flags)
{
	param->flags &= ~flags;
	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_clear_flags);

unsigned long
X509_VERIFY_PARAM_get_flags(X509_VERIFY_PARAM *param)
{
	return param->flags;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get_flags);

int
X509_VERIFY_PARAM_set_purpose(X509_VERIFY_PARAM *param, int purpose)
{
	if (purpose < X509_PURPOSE_MIN || purpose > X509_PURPOSE_MAX) {
		X509V3error(X509V3_R_INVALID_PURPOSE);
		return 0;
	}

	param->purpose = purpose;
	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_purpose);

int
X509_VERIFY_PARAM_set_trust(X509_VERIFY_PARAM *param, int trust)
{
	if (trust < X509_TRUST_MIN || trust > X509_TRUST_MAX) {
		X509error(X509_R_INVALID_TRUST);
		return 0;
	}

	param->trust = trust;
	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_trust);

void
X509_VERIFY_PARAM_set_depth(X509_VERIFY_PARAM *param, int depth)
{
	param->depth = depth;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_depth);

void
X509_VERIFY_PARAM_set_auth_level(X509_VERIFY_PARAM *param, int auth_level)
{
	param->security_level = auth_level;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_auth_level);

time_t
X509_VERIFY_PARAM_get_time(const X509_VERIFY_PARAM *param)
{
	return param->check_time;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get_time);

void
X509_VERIFY_PARAM_set_time(X509_VERIFY_PARAM *param, time_t t)
{
	param->check_time = t;
	param->flags |= X509_V_FLAG_USE_CHECK_TIME;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_time);

int
X509_VERIFY_PARAM_add0_policy(X509_VERIFY_PARAM *param, ASN1_OBJECT *policy)
{
	if (param->policies == NULL)
		param->policies = sk_ASN1_OBJECT_new_null();
	if (param->policies == NULL)
		return 0;
	if (sk_ASN1_OBJECT_push(param->policies, policy) <= 0)
		return 0;
	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_add0_policy);

static STACK_OF(ASN1_OBJECT) *
sk_ASN1_OBJECT_deep_copy(const STACK_OF(ASN1_OBJECT) *sk)
{
	STACK_OF(ASN1_OBJECT) *objs;
	ASN1_OBJECT *obj = NULL;
	int i;

	if ((objs = sk_ASN1_OBJECT_new_null()) == NULL)
		goto err;

	for (i = 0; i < sk_ASN1_OBJECT_num(sk); i++) {
		if ((obj = OBJ_dup(sk_ASN1_OBJECT_value(sk, i))) == NULL)
			goto err;
		if (sk_ASN1_OBJECT_push(objs, obj) <= 0)
			goto err;
		obj = NULL;
	}

	return objs;

 err:
	sk_ASN1_OBJECT_pop_free(objs, ASN1_OBJECT_free);
	ASN1_OBJECT_free(obj);

	return NULL;
}

int
X509_VERIFY_PARAM_set1_policies(X509_VERIFY_PARAM *param,
    STACK_OF(ASN1_OBJECT) *policies)
{
	if (param == NULL)
		return 0;

	sk_ASN1_OBJECT_pop_free(param->policies, ASN1_OBJECT_free);
	param->policies = NULL;

	if (policies == NULL)
		return 1;

	if ((param->policies = sk_ASN1_OBJECT_deep_copy(policies)) == NULL)
		return 0;

	return 1;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_policies);

int
X509_VERIFY_PARAM_set1_host(X509_VERIFY_PARAM *param,
    const char *name, size_t namelen)
{
	if (x509_param_set_hosts_internal(param, SET_HOST, name, namelen))
		return 1;
	param->poisoned = 1;
	return 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_host);

int
X509_VERIFY_PARAM_add1_host(X509_VERIFY_PARAM *param,
    const char *name, size_t namelen)
{
	if (x509_param_set_hosts_internal(param, ADD_HOST, name, namelen))
		return 1;
	param->poisoned = 1;
	return 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_add1_host);

/* Public API in OpenSSL - nothing seems to use this. */
unsigned int
X509_VERIFY_PARAM_get_hostflags(X509_VERIFY_PARAM *param)
{
	return param->hostflags;
}

void
X509_VERIFY_PARAM_set_hostflags(X509_VERIFY_PARAM *param, unsigned int flags)
{
	param->hostflags = flags;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set_hostflags);

char *
X509_VERIFY_PARAM_get0_peername(X509_VERIFY_PARAM *param)
{
	return param->peername;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get0_peername);

int
X509_VERIFY_PARAM_set1_email(X509_VERIFY_PARAM *param,  const char *email,
    size_t emaillen)
{
	if (x509_param_set1_internal(&param->email, &param->emaillen,
	    email, emaillen, 1))
		return 1;
	param->poisoned = 1;
	return 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_email);

int
X509_VERIFY_PARAM_set1_ip(X509_VERIFY_PARAM *param, const unsigned char *ip,
    size_t iplen)
{
	if (iplen != 4 && iplen != 16)
		goto err;
	if (x509_param_set1_internal((char **)&param->ip, &param->iplen,
		(char *)ip, iplen, 0))
		return 1;
 err:
	param->poisoned = 1;
	return 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_ip);

int
X509_VERIFY_PARAM_set1_ip_asc(X509_VERIFY_PARAM *param, const char *ipasc)
{
	unsigned char ipout[16];
	size_t iplen;

	iplen = (size_t)a2i_ipadd(ipout, ipasc);
	return X509_VERIFY_PARAM_set1_ip(param, ipout, iplen);
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_set1_ip_asc);

int
X509_VERIFY_PARAM_get_depth(const X509_VERIFY_PARAM *param)
{
	return param->depth;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get_depth);

const char *
X509_VERIFY_PARAM_get0_name(const X509_VERIFY_PARAM *param)
{
	return param->name;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get0_name);

/*
 * Default verify parameters: these are used for various applications and can
 * be overridden by the user specified table.
 */

static const X509_VERIFY_PARAM default_table[] = {
	{
		.name = "default",
		.flags = X509_V_FLAG_TRUSTED_FIRST,
		.depth = 100,
		.trust = 0,  /* XXX This is not the default trust value */
	},
	{
		.name = "pkcs7",
		.purpose = X509_PURPOSE_SMIME_SIGN,
		.trust = X509_TRUST_EMAIL,
		.depth = -1,
	},
	{
		.name = "smime_sign",
		.purpose = X509_PURPOSE_SMIME_SIGN,
		.trust = X509_TRUST_EMAIL,
		.depth =  -1,
	},
	{
		.name = "ssl_client",
		.purpose = X509_PURPOSE_SSL_CLIENT,
		.trust = X509_TRUST_SSL_CLIENT,
		.depth = -1,
	},
	{
		.name = "ssl_server",
		.purpose = X509_PURPOSE_SSL_SERVER,
		.trust = X509_TRUST_SSL_SERVER,
		.depth = -1,
	}
};

#define N_DEFAULT_VERIFY_PARAMS (sizeof(default_table) / sizeof(default_table[0]))

static STACK_OF(X509_VERIFY_PARAM) *param_table = NULL;

static int
param_cmp(const X509_VERIFY_PARAM * const *a,
    const X509_VERIFY_PARAM * const *b)
{
	return strcmp((*a)->name, (*b)->name);
}

int
X509_VERIFY_PARAM_add0_table(X509_VERIFY_PARAM *param)
{
	X509_VERIFY_PARAM *ptmp;
	int idx;

	if (param_table == NULL)
		param_table = sk_X509_VERIFY_PARAM_new(param_cmp);
	if (param_table == NULL)
		return 0;

	if ((idx = sk_X509_VERIFY_PARAM_find(param_table, param)) != -1) {
		ptmp = sk_X509_VERIFY_PARAM_value(param_table, idx);
		X509_VERIFY_PARAM_free(ptmp);
		(void)sk_X509_VERIFY_PARAM_delete(param_table, idx);
	}

	return sk_X509_VERIFY_PARAM_push(param_table, param) > 0;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_add0_table);

int
X509_VERIFY_PARAM_get_count(void)
{
	int num = N_DEFAULT_VERIFY_PARAMS;

	if (param_table != NULL)
		num += sk_X509_VERIFY_PARAM_num(param_table);

	return num;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get_count);

const X509_VERIFY_PARAM *
X509_VERIFY_PARAM_get0(int id)
{
	int num = N_DEFAULT_VERIFY_PARAMS;

	if (id < 0)
		return NULL;

	if (id < num)
		return &default_table[id];

	return sk_X509_VERIFY_PARAM_value(param_table, id - num);
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_get0);

const X509_VERIFY_PARAM *
X509_VERIFY_PARAM_lookup(const char *name)
{
	X509_VERIFY_PARAM param;
	size_t i;
	int idx;

	memset(&param, 0, sizeof(param));
	param.name = (char *)name;
	if ((idx = sk_X509_VERIFY_PARAM_find(param_table, &param)) != -1)
		return sk_X509_VERIFY_PARAM_value(param_table, idx);

	for (i = 0; i < N_DEFAULT_VERIFY_PARAMS; i++) {
		if (strcmp(default_table[i].name, name) == 0)
			return &default_table[i];
	}

	return NULL;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_lookup);

void
X509_VERIFY_PARAM_table_cleanup(void)
{
	sk_X509_VERIFY_PARAM_pop_free(param_table, X509_VERIFY_PARAM_free);
	param_table = NULL;
}
LCRYPTO_ALIAS(X509_VERIFY_PARAM_table_cleanup);
