/* $OpenBSD: conf_api.c,v 1.26 2025/03/08 09:35:53 tb Exp $ */
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

/* Part of the code in here was originally in conf.c, which is now removed */

#ifndef CONF_DEBUG
# undef NDEBUG /* avoid conflicting definitions */
# define NDEBUG
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/conf.h>

#include "conf_local.h"

static void value_free_hash_doall_arg(CONF_VALUE *a,
    LHASH_OF(CONF_VALUE) *conf);
static void value_free_stack_doall(CONF_VALUE *a);
static IMPLEMENT_LHASH_DOALL_ARG_FN(value_free_hash, CONF_VALUE,
    LHASH_OF(CONF_VALUE))
static IMPLEMENT_LHASH_DOALL_FN(value_free_stack, CONF_VALUE)

/* Up until OpenSSL 0.9.5a, this was get_section */
CONF_VALUE *
_CONF_get_section(const CONF *conf, const char *section)
{
	CONF_VALUE *v, vv;

	if ((conf == NULL) || (section == NULL))
		return (NULL);
	vv.name = NULL;
	vv.section = (char *)section;
	v = lh_CONF_VALUE_retrieve(conf->data, &vv);
	return (v);
}

int
_CONF_add_string(CONF *conf, CONF_VALUE *section, CONF_VALUE *value)
{
	CONF_VALUE *v = NULL;
	STACK_OF(CONF_VALUE) *ts;

	ts = (STACK_OF(CONF_VALUE) *)section->value;

	value->section = section->section;
	if (!sk_CONF_VALUE_push(ts, value)) {
		return 0;
	}

	v = lh_CONF_VALUE_insert(conf->data, value);
	if (v != NULL) {
		(void)sk_CONF_VALUE_delete_ptr(ts, v);
		free(v->name);
		free(v->value);
		free(v);
	}
	return 1;
}

char *
_CONF_get_string(const CONF *conf, const char *section, const char *name)
{
	CONF_VALUE *v, vv;

	if (name == NULL)
		return (NULL);
	if (conf != NULL) {
		if (section != NULL) {
			vv.name = (char *)name;
			vv.section = (char *)section;
			v = lh_CONF_VALUE_retrieve(conf->data, &vv);
			if (v != NULL)
				return (v->value);
		}
		vv.section = "default";
		vv.name = (char *)name;
		v = lh_CONF_VALUE_retrieve(conf->data, &vv);
		if (v != NULL)
			return (v->value);
		else
			return (NULL);
	} else
		return (NULL);
}

static unsigned long
conf_value_hash(const CONF_VALUE *v)
{
	return (lh_strhash(v->section) << 2) ^ lh_strhash(v->name);
}

static IMPLEMENT_LHASH_HASH_FN(conf_value, CONF_VALUE)

static int
conf_value_cmp(const CONF_VALUE *a, const CONF_VALUE *b)
{
	int i;

	if (a->section != b->section) {
		i = strcmp(a->section, b->section);
		if (i)
			return (i);
	}
	if ((a->name != NULL) && (b->name != NULL)) {
		i = strcmp(a->name, b->name);
		return (i);
	} else if (a->name == b->name)
		return (0);
	else
		return ((a->name == NULL)?-1 : 1);
}

static IMPLEMENT_LHASH_COMP_FN(conf_value, CONF_VALUE)

int
_CONF_new_data(CONF *conf)
{
	if (conf == NULL) {
		return 0;
	}
	if (conf->data == NULL)
		if ((conf->data = lh_CONF_VALUE_new()) == NULL) {
			return 0;
		}
	return 1;
}

void
_CONF_free_data(CONF *conf)
{
	if (conf == NULL || conf->data == NULL)
		return;

	lh_CONF_VALUE_doall_arg(conf->data,
	    LHASH_DOALL_ARG_FN(value_free_hash),
	    LHASH_OF(CONF_VALUE), conf->data);

	/* We now have only 'section' entries in the hash table.
	 * Due to problems with */

	lh_CONF_VALUE_doall(conf->data, LHASH_DOALL_FN(value_free_stack));
	lh_CONF_VALUE_free(conf->data);
}

static void
value_free_hash_doall_arg(CONF_VALUE *a, LHASH_OF(CONF_VALUE) *conf)
{
	if (a->name != NULL)
		(void)lh_CONF_VALUE_delete(conf, a);
}

static void
value_free_stack_doall(CONF_VALUE *a)
{
	CONF_VALUE *vv;
	STACK_OF(CONF_VALUE) *sk;
	int i;

	if (a->name != NULL)
		return;

	sk = (STACK_OF(CONF_VALUE) *)a->value;
	for (i = sk_CONF_VALUE_num(sk) - 1; i >= 0; i--) {
		vv = sk_CONF_VALUE_value(sk, i);
		free(vv->value);
		free(vv->name);
		free(vv);
	}
	if (sk != NULL)
		sk_CONF_VALUE_free(sk);
	free(a->section);
	free(a);
}

/* Up until OpenSSL 0.9.5a, this was new_section */
CONF_VALUE *
_CONF_new_section(CONF *conf, const char *section)
{
	STACK_OF(CONF_VALUE) *sk = NULL;
	CONF_VALUE *v = NULL, *vv;

	if ((sk = sk_CONF_VALUE_new_null()) == NULL)
		goto err;
	if ((v = calloc(1, sizeof(*v))) == NULL)
		goto err;
	if ((v->section = strdup(section)) == NULL)
		goto err;
	v->value = (char *)sk;

	vv = lh_CONF_VALUE_insert(conf->data, v);
	OPENSSL_assert(vv == NULL);
	if (lh_CONF_VALUE_error(conf->data))
		goto err;

	return v;

 err:
	sk_CONF_VALUE_free(sk);
	if (v != NULL)
		free(v->section);
	free(v);

	return NULL;
}
