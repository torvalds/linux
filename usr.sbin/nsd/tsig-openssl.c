/*
 * tsig-openssl.h -- Interface to OpenSSL for TSIG support.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#if defined(HAVE_SSL)

#ifdef HAVE_OPENSSL_CORE_NAMES_H
#include <openssl/core_names.h>
#endif
#include "tsig-openssl.h"
#include "tsig.h"
#include "util.h"

static void *create_context(region_type *region);
static void init_context(void *context,
			 tsig_algorithm_type *algorithm,
			 tsig_key_type *key);
static void update(void *context, const void *data, size_t size);
static void final(void *context, uint8_t *digest, size_t *size);

#ifdef HAVE_EVP_MAC_CTX_NEW
struct tsig_openssl_data {
	/* the MAC for the algorithm, 'hmac' */
	EVP_MAC* mac;
	/* the digest name for creating the EVP_MAC_CTX with, 'sha256' */
	const char* digest;
};

struct tsig_openssl_context {
	/* the evp mac context, if notNULL it has algo and key set. */
	EVP_MAC_CTX* hmac_ctx;
	/* the size of destination buffers */
	size_t outsize;
};

static void
cleanup_tsig_openssl_data(void *data)
{
	struct tsig_openssl_data* d = (struct tsig_openssl_data*)data;
	EVP_MAC_free(d->mac);
	d->mac = NULL;
}
#endif

static int
tsig_openssl_init_algorithm(region_type* region,
	const char* digest, const char* name, const char* wireformat)
{
	tsig_algorithm_type* algorithm;
#ifndef HAVE_EVP_MAC_CTX_NEW
	const EVP_MD *hmac_algorithm;

	hmac_algorithm = EVP_get_digestbyname(digest);
	if (!hmac_algorithm) {
		/* skip but don't error */
		return 0;
	}
#else
	struct tsig_openssl_data* data;
	EVP_MAC_CTX* hmac_ctx;
	OSSL_PARAM params[3];
	data = region_alloc(region, sizeof(*data));
	data->digest = digest;
	data->mac = EVP_MAC_fetch(NULL, "hmac", NULL);
	if(!data->mac) {
		log_msg(LOG_ERR, "could not fetch MAC implementation 'hmac' with EVP_MAC_fetch");
		return 0;
	}
	/* this context is created to see what size the output is */
	hmac_ctx = EVP_MAC_CTX_new(data->mac);
	if(!hmac_ctx) {
		EVP_MAC_free(data->mac);
		return 0;
	}
	params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
		(char*)digest, 0);
	params[1] = OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY,
		"", 1);
	params[2] = OSSL_PARAM_construct_end();
#ifdef HAVE_EVP_MAC_CTX_SET_PARAMS
	if(EVP_MAC_CTX_set_params(hmac_ctx, params) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_CTX_set_params");
		EVP_MAC_CTX_free(hmac_ctx);
		EVP_MAC_free(data->mac);
		return 0;
	}
#else
	if(EVP_MAC_set_ctx_params(hmac_ctx, params) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_set_ctx_params");
		EVP_MAC_CTX_free(hmac_ctx);
		EVP_MAC_free(data->mac);
		return 0;
	}
#endif
#endif

	algorithm = (tsig_algorithm_type *) region_alloc(
		region, sizeof(tsig_algorithm_type));
	algorithm->short_name = name;
	algorithm->wireformat_name
		= dname_parse(region, wireformat);
	if (!algorithm->wireformat_name) {
		log_msg(LOG_ERR, "cannot parse %s algorithm", wireformat);
#ifdef HAVE_EVP_MAC_CTX_NEW
		EVP_MAC_CTX_free(hmac_ctx);
		EVP_MAC_free(data->mac);
#endif
		return 0;
	}
#ifdef HAVE_EVP_MAC_CTX_GET_MAC_SIZE
	algorithm->maximum_digest_size = EVP_MAC_CTX_get_mac_size(hmac_ctx);
#elif !defined(HAVE_EVP_MAC_CTX_NEW)
	algorithm->maximum_digest_size = EVP_MD_size(hmac_algorithm);
#else
	algorithm->maximum_digest_size = EVP_MAC_size(hmac_ctx);
#endif
	if(algorithm->maximum_digest_size < 20)
		algorithm->maximum_digest_size = EVP_MAX_MD_SIZE;
#ifndef HAVE_EVP_MAC_CTX_NEW
	algorithm->data = hmac_algorithm;
#else
	algorithm->data = data;
	region_add_cleanup(region, cleanup_tsig_openssl_data, data);
#endif
	algorithm->hmac_create_context = create_context;
	algorithm->hmac_init_context = init_context;
	algorithm->hmac_update = update;
	algorithm->hmac_final = final;
	tsig_add_algorithm(algorithm);

#ifdef HAVE_EVP_MAC_CTX_NEW
	EVP_MAC_CTX_free(hmac_ctx);
#endif
	return 1;
}

int
tsig_openssl_init(region_type *region)
{
	int count = 0;
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
	OpenSSL_add_all_digests();
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
#endif

	count += tsig_openssl_init_algorithm(region,
	    "md5", "hmac-md5","hmac-md5.sig-alg.reg.int.");
	count += tsig_openssl_init_algorithm(region,
	    "sha1", "hmac-sha1", "hmac-sha1.");
	count += tsig_openssl_init_algorithm(region,
	    "sha224", "hmac-sha224", "hmac-sha224.");
	count += tsig_openssl_init_algorithm(region,
	    "sha256", "hmac-sha256", "hmac-sha256.");
	count += tsig_openssl_init_algorithm(region,
	    "sha384", "hmac-sha384", "hmac-sha384.");
	count += tsig_openssl_init_algorithm(region,
	    "sha512", "hmac-sha512", "hmac-sha512.");

	return count;
}

static void
cleanup_context(void *data)
{
#ifndef HAVE_EVP_MAC_CTX_NEW
	HMAC_CTX *context = (HMAC_CTX *) data;
#ifdef HAVE_HMAC_CTX_NEW
	HMAC_CTX_free(context);
#else
	HMAC_CTX_cleanup(context);
	free(context);
#endif
#else
	struct tsig_openssl_context* c = (struct tsig_openssl_context*)data;
	EVP_MAC_CTX_free(c->hmac_ctx);
	c->hmac_ctx = NULL;
#endif
}

static void *
create_context(region_type *region)
{
#ifndef HAVE_EVP_MAC_CTX_NEW
#ifdef HAVE_HMAC_CTX_NEW
	HMAC_CTX *context = HMAC_CTX_new();
#else
	HMAC_CTX *context = (HMAC_CTX *) malloc(sizeof(HMAC_CTX));
#endif
	region_add_cleanup(region, cleanup_context, context);
#ifdef HAVE_HMAC_CTX_RESET
	HMAC_CTX_reset(context);
#else
	HMAC_CTX_init(context);
#endif
#else
	struct tsig_openssl_context* context = region_alloc(region,
		sizeof(*context));
	memset(context, 0, sizeof(*context));
	region_add_cleanup(region, cleanup_context, context);
#endif
	return context;
}

static void
init_context(void *context,
			  tsig_algorithm_type *algorithm,
			  tsig_key_type *key)
{
#ifndef HAVE_EVP_MAC_CTX_NEW
	HMAC_CTX *ctx = (HMAC_CTX *) context;
	const EVP_MD *md = (const EVP_MD *) algorithm->data;
	HMAC_Init_ex(ctx, key->data, key->size, md, NULL);
#else
	OSSL_PARAM params[3];
	struct tsig_openssl_data* algo_data = (struct tsig_openssl_data*)
		algorithm->data;
	struct tsig_openssl_context* c = (struct tsig_openssl_context*)context;
	if(c->hmac_ctx) {
		EVP_MAC_CTX_free(c->hmac_ctx);
	}
	c->hmac_ctx = EVP_MAC_CTX_new(algo_data->mac);
	if(!c->hmac_ctx) {
		log_msg(LOG_ERR, "could not EVP_MAC_CTX_new");
		return;
	}
	params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
		(char*)algo_data->digest, 0);
	params[1] = OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY,
		key->data, key->size);
	params[2] = OSSL_PARAM_construct_end();
#ifdef HAVE_EVP_MAC_CTX_SET_PARAMS
	if(EVP_MAC_CTX_set_params(c->hmac_ctx, params) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_CTX_set_params");
		EVP_MAC_CTX_free(c->hmac_ctx);
		c->hmac_ctx = NULL;
		return;
	}
#else
	if(EVP_MAC_set_ctx_params(hmac_ctx, params) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_set_ctx_params");
		EVP_MAC_CTX_free(c->hmac_ctx);
		c->hmac_ctx = NULL;
		return;
	}
#endif
	c->outsize = algorithm->maximum_digest_size;
#endif
}

static void
update(void *context, const void *data, size_t size)
{
#ifndef HAVE_EVP_MAC_CTX_NEW
	HMAC_CTX *ctx = (HMAC_CTX *) context;
	HMAC_Update(ctx, (unsigned char *) data, (int) size);
#else
	struct tsig_openssl_context* c = (struct tsig_openssl_context*)context;
	if(EVP_MAC_update(c->hmac_ctx, data, size) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_update");
	}
#endif
}

static void
final(void *context, uint8_t *digest, size_t *size)
{
#ifndef HAVE_EVP_MAC_CTX_NEW
	HMAC_CTX *ctx = (HMAC_CTX *) context;
	unsigned len = (unsigned) *size;
	HMAC_Final(ctx, digest, &len);
	*size = (size_t) len;
#else
	struct tsig_openssl_context* c = (struct tsig_openssl_context*)context;
	if(EVP_MAC_final(c->hmac_ctx, digest, size, c->outsize) <= 0) {
		log_msg(LOG_ERR, "could not EVP_MAC_final");
	}
#endif
}

void
tsig_openssl_finalize()
{
#ifdef HAVE_EVP_CLEANUP
	EVP_cleanup();
#endif
}

#endif /* defined(HAVE_SSL) */
