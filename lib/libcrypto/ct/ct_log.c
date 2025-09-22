/*	$OpenBSD: ct_log.c,v 1.10 2025/05/10 05:54:38 tb Exp $ */
/* Author: Adam Eijdenberg <adam.eijdenberg@gmail.com>. */
/* ====================================================================
 * Copyright (c) 1998-2016 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/conf.h>
#include <openssl/ct.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "conf_local.h"
#include "crypto_local.h"
#include "err_local.h"


/*
 * Information about a CT log server.
 */
struct ctlog_st {
	char *name;
	uint8_t log_id[CT_V1_HASHLEN];
	EVP_PKEY *public_key;
};

/*
 * A store for multiple CTLOG instances.
 * It takes ownership of any CTLOG instances added to it.
 */
struct ctlog_store_st {
	STACK_OF(CTLOG) *logs;
};

/* The context when loading a CT log list from a CONF file. */
typedef struct ctlog_store_load_ctx_st {
	CTLOG_STORE *log_store;
	CONF *conf;
	size_t invalid_log_entries;
} CTLOG_STORE_LOAD_CTX;

/*
 * Creates an empty context for loading a CT log store.
 * It should be populated before use.
 */
static CTLOG_STORE_LOAD_CTX *ctlog_store_load_ctx_new(void);

/*
 * Deletes a CT log store load context.
 * Does not delete any of the fields.
 */
static void ctlog_store_load_ctx_free(CTLOG_STORE_LOAD_CTX *ctx);

static CTLOG_STORE_LOAD_CTX *
ctlog_store_load_ctx_new(void)
{
	CTLOG_STORE_LOAD_CTX *ctx = calloc(1, sizeof(*ctx));

	if (ctx == NULL)
		CTerror(ERR_R_MALLOC_FAILURE);

	return ctx;
}

static void
ctlog_store_load_ctx_free(CTLOG_STORE_LOAD_CTX  *ctx)
{
	free(ctx);
}

/* Converts a log's public key into a SHA256 log ID */
static int
ct_v1_log_id_from_pkey(EVP_PKEY *pkey, unsigned char log_id[CT_V1_HASHLEN])
{
	int ret = 0;
	unsigned char *pkey_der = NULL;
	int pkey_der_len = i2d_PUBKEY(pkey, &pkey_der);

	if (pkey_der_len <= 0) {
		CTerror(CT_R_LOG_KEY_INVALID);
		goto err;
	}

	SHA256(pkey_der, pkey_der_len, log_id);
	ret = 1;
 err:
	free(pkey_der);
	return ret;
}

CTLOG_STORE *
CTLOG_STORE_new(void)
{
	CTLOG_STORE *ret = calloc(1, sizeof(*ret));

	if (ret == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	ret->logs = sk_CTLOG_new_null();
	if (ret->logs == NULL)
		goto err;

	return ret;
 err:
	free(ret);
	return NULL;
}
LCRYPTO_ALIAS(CTLOG_STORE_new);

void
CTLOG_STORE_free(CTLOG_STORE *store)
{
	if (store != NULL) {
		sk_CTLOG_pop_free(store->logs, CTLOG_free);
		free(store);
	}
}
LCRYPTO_ALIAS(CTLOG_STORE_free);

static int
ctlog_new_from_conf(CTLOG **ct_log, const CONF *conf, const char *section)
{
	const char *description = NCONF_get_string(conf, section,
	    "description");
	char *pkey_base64;

	if (description == NULL) {
		CTerror(CT_R_LOG_CONF_MISSING_DESCRIPTION);
		return 0;
	}

	pkey_base64 = NCONF_get_string(conf, section, "key");
	if (pkey_base64 == NULL) {
		CTerror(CT_R_LOG_CONF_MISSING_KEY);
		return 0;
	}

	return CTLOG_new_from_base64(ct_log, pkey_base64, description);
}

int
CTLOG_STORE_load_default_file(CTLOG_STORE *store)
{
	return CTLOG_STORE_load_file(store, CTLOG_FILE);
}
LCRYPTO_ALIAS(CTLOG_STORE_load_default_file);

/*
 * Called by CONF_parse_list, which stops if this returns <= 0,
 * Otherwise, one bad log entry would stop loading of any of
 * the following log entries.
 * It may stop parsing and returns -1 on any internal (malloc) error.
 */
static int
ctlog_store_load_log(const char *log_name, int log_name_len, void *arg)
{
	CTLOG_STORE_LOAD_CTX *load_ctx = arg;
	CTLOG *ct_log = NULL;
	/* log_name may not be null-terminated, so fix that before using it */
	char *tmp;
	int ret = 0;

	/* log_name will be NULL for empty list entries */
	if (log_name == NULL)
		return 1;

	tmp = strndup(log_name, log_name_len);
	if (tmp == NULL)
		goto mem_err;

	ret = ctlog_new_from_conf(&ct_log, load_ctx->conf, tmp);
	free(tmp);

	if (ret < 0) {
		/* Propagate any internal error */
		return ret;
	}
	if (ret == 0) {
		/* If we can't load this log, record that fact and skip it */
		++load_ctx->invalid_log_entries;
		return 1;
	}

	if (!sk_CTLOG_push(load_ctx->log_store->logs, ct_log)) {
		goto mem_err;
	}
	return 1;

 mem_err:
	CTLOG_free(ct_log);
	CTerror(ERR_R_MALLOC_FAILURE);
	return -1;
}

int
CTLOG_STORE_load_file(CTLOG_STORE *store, const char *file)
{
	int ret = 0;
	char *enabled_logs;
	CTLOG_STORE_LOAD_CTX* load_ctx = ctlog_store_load_ctx_new();

	if (load_ctx == NULL)
		return 0;
	load_ctx->log_store = store;
	load_ctx->conf = NCONF_new(NULL);
	if (load_ctx->conf == NULL)
		goto end;

	if (NCONF_load(load_ctx->conf, file, NULL) <= 0) {
		CTerror(CT_R_LOG_CONF_INVALID);
		goto end;
	}

	enabled_logs = NCONF_get_string(load_ctx->conf, NULL, "enabled_logs");
	if (enabled_logs == NULL) {
		CTerror(CT_R_LOG_CONF_INVALID);
		goto end;
	}

	if (!CONF_parse_list(enabled_logs, ',', 1, ctlog_store_load_log, load_ctx) ||
	    load_ctx->invalid_log_entries > 0) {
		CTerror(CT_R_LOG_CONF_INVALID);
		goto end;
	}

	ret = 1;
 end:
	NCONF_free(load_ctx->conf);
	ctlog_store_load_ctx_free(load_ctx);
	return ret;
}
LCRYPTO_ALIAS(CTLOG_STORE_load_file);

/*
 * Initialize a new CTLOG object.
 * Takes ownership of the public key.
 * Copies the name.
 */
CTLOG *
CTLOG_new(EVP_PKEY *public_key, const char *name)
{
	CTLOG *ret = calloc(1, sizeof(*ret));

	if (ret == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	ret->name = strdup(name);
	if (ret->name == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (ct_v1_log_id_from_pkey(public_key, ret->log_id) != 1)
		goto err;

	ret->public_key = public_key;
	return ret;
 err:
	CTLOG_free(ret);
	return NULL;
}
LCRYPTO_ALIAS(CTLOG_new);

/* Frees CT log and associated structures */
void
CTLOG_free(CTLOG *log)
{
	if (log != NULL) {
		free(log->name);
		EVP_PKEY_free(log->public_key);
		free(log);
	}
}
LCRYPTO_ALIAS(CTLOG_free);

const char *
CTLOG_get0_name(const CTLOG *log)
{
	return log->name;
}
LCRYPTO_ALIAS(CTLOG_get0_name);

void
CTLOG_get0_log_id(const CTLOG *log, const uint8_t **log_id, size_t *log_id_len)
{
	*log_id = log->log_id;
	*log_id_len = CT_V1_HASHLEN;
}
LCRYPTO_ALIAS(CTLOG_get0_log_id);

EVP_PKEY *
CTLOG_get0_public_key(const CTLOG *log)
{
	return log->public_key;
}
LCRYPTO_ALIAS(CTLOG_get0_public_key);

/*
 * Given a log ID, finds the matching log.
 * Returns NULL if no match found.
 */
const CTLOG *
CTLOG_STORE_get0_log_by_id(const CTLOG_STORE *store, const uint8_t *log_id,
    size_t log_id_len)
{
	int i;

	for (i = 0; i < sk_CTLOG_num(store->logs); ++i) {
		const CTLOG *log = sk_CTLOG_value(store->logs, i);
		if (memcmp(log->log_id, log_id, log_id_len) == 0)
			return log;
	}

	return NULL;
}
LCRYPTO_ALIAS(CTLOG_STORE_get0_log_by_id);
