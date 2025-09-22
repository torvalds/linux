/* $OpenBSD: conf_lib.c,v 1.26 2025/05/10 05:54:38 tb Exp $ */
/* Written by Richard Levitte (richard@levitte.org) for the OpenSSL
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
#include <openssl/crypto.h>
#include <openssl/conf.h>
#include <openssl/lhash.h>

#include "conf_local.h"
#include "err_local.h"

static const CONF_METHOD *default_CONF_method = NULL;

/* Init a 'CONF' structure from an old LHASH */

void
CONF_set_nconf(CONF *conf, LHASH_OF(CONF_VALUE) *hash)
{
	if (default_CONF_method == NULL)
		default_CONF_method = NCONF_default();
	default_CONF_method->init(conf);
	conf->data = hash;
}

CONF *
NCONF_new(const CONF_METHOD *meth)
{
	CONF *ret;

	if (meth == NULL)
		meth = NCONF_default();

	ret = meth->create(meth);
	if (ret == NULL) {
		CONFerror(ERR_R_MALLOC_FAILURE);
		return (NULL);
	}

	return ret;
}
LCRYPTO_ALIAS(NCONF_new);

void
NCONF_free(CONF *conf)
{
	if (conf == NULL)
		return;
	conf->meth->destroy(conf);
}
LCRYPTO_ALIAS(NCONF_free);

int
NCONF_load(CONF *conf, const char *file, long *eline)
{
	if (conf == NULL) {
		CONFerror(CONF_R_NO_CONF);
		return 0;
	}

	return conf->meth->load(conf, file, eline);
}
LCRYPTO_ALIAS(NCONF_load);

int
NCONF_load_bio(CONF *conf, BIO *bp, long *eline)
{
	if (conf == NULL) {
		CONFerror(CONF_R_NO_CONF);
		return 0;
	}

	return conf->meth->load_bio(conf, bp, eline);
}
LCRYPTO_ALIAS(NCONF_load_bio);

STACK_OF(CONF_VALUE) *
NCONF_get_section(const CONF *conf, const char *section)
{
	CONF_VALUE *v;

	if (conf == NULL) {
		CONFerror(CONF_R_NO_CONF);
		return NULL;
	}

	if (section == NULL) {
		CONFerror(CONF_R_NO_SECTION);
		return NULL;
	}

	if ((v = _CONF_get_section(conf, section)) == NULL)
		return NULL;

	return (STACK_OF(CONF_VALUE) *)v->value;
}
LCRYPTO_ALIAS(NCONF_get_section);

char *
NCONF_get_string(const CONF *conf, const char *group, const char *name)
{
	char *s = _CONF_get_string(conf, group, name);

        /* Since we may get a value from an environment variable even
           if conf is NULL, let's check the value first */
	if (s)
		return s;

	if (conf == NULL) {
		CONFerror(CONF_R_NO_CONF_OR_ENVIRONMENT_VARIABLE);
		return NULL;
	}
	CONFerror(CONF_R_NO_VALUE);
	ERR_asprintf_error_data("group=%s name=%s",
	    group ? group : "", name);
	return NULL;
}
LCRYPTO_ALIAS(NCONF_get_string);

int
NCONF_get_number_e(const CONF *conf, const char *group, const char *name,
    long *result)
{
	char *str;

	if (result == NULL) {
		CONFerror(ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}

	str = NCONF_get_string(conf, group, name);

	if (str == NULL)
		return 0;

	for (*result = 0; conf->meth->is_number(conf, *str); ) {
		*result = (*result) * 10 + conf->meth->to_int(conf, *str);
		str++;
	}

	return 1;
}
LCRYPTO_ALIAS(NCONF_get_number_e);
