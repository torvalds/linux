/*	$OpenBSD: crypto_init.c,v 1.26 2025/06/11 07:41:12 tb Exp $ */
/*
 * Copyright (c) 2018 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* OpenSSL style init */

#include <pthread.h>
#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509v3.h>

#include "crypto_internal.h"
#include "err_local.h"
#include "x509_issuer_cache.h"

int OpenSSL_config(const char *);
int OpenSSL_no_config(void);

static pthread_once_t crypto_init_once = PTHREAD_ONCE_INIT;
static pthread_t crypto_init_thread;
static int crypto_init_cleaned_up;

void openssl_init_crypto_constructor(void)  __attribute__((constructor));

#ifndef HAVE_CRYPTO_CPU_CAPS_INIT
void
crypto_cpu_caps_init(void)
{
}
#endif

/*
 * This function is invoked as a constructor when the library is loaded. The
 * code run from here must not allocate memory or trigger signals. The only
 * safe code is to read data and update global variables.
 */
void
openssl_init_crypto_constructor(void)
{
	crypto_cpu_caps_init();
}

/*
 * This is used by various configure scripts to check availability of libcrypto,
 * so we need to keep it.
 */
void
OPENSSL_init(void)
{
}
LCRYPTO_ALIAS(OPENSSL_init);

static void
OPENSSL_init_crypto_internal(void)
{
	crypto_init_thread = pthread_self();

	ERR_load_crypto_strings();
}

int
OPENSSL_init_crypto(uint64_t opts, const void *settings)
{
	if (crypto_init_cleaned_up) {
		CRYPTOerror(ERR_R_INIT_FAIL);
		return 0;
	}

	if (pthread_equal(pthread_self(), crypto_init_thread))
		return 1; /* don't recurse */

	if (pthread_once(&crypto_init_once, OPENSSL_init_crypto_internal) != 0)
		return 0;

	if ((opts & OPENSSL_INIT_NO_LOAD_CONFIG) &&
	    (OpenSSL_no_config() == 0))
		return 0;

	if ((opts & OPENSSL_INIT_LOAD_CONFIG) &&
	    (OpenSSL_config(NULL) == 0))
		return 0;

	return 1;
}
LCRYPTO_ALIAS(OPENSSL_init_crypto);

void
OPENSSL_cleanup(void)
{
	/* This currently calls init... */
	ERR_free_strings();

	CRYPTO_cleanup_all_ex_data();
	EVP_cleanup();

	X509_VERIFY_PARAM_table_cleanup();

	x509_issuer_cache_free();

	crypto_init_cleaned_up = 1;
}
LCRYPTO_ALIAS(OPENSSL_cleanup);

void
OpenSSL_add_all_ciphers(void)
{
}
LCRYPTO_ALIAS(OpenSSL_add_all_ciphers);

void
OpenSSL_add_all_digests(void)
{
}
LCRYPTO_ALIAS(OpenSSL_add_all_digests);

void
OPENSSL_add_all_algorithms_noconf(void)
{
}
LCRYPTO_ALIAS(OPENSSL_add_all_algorithms_noconf);

void
OPENSSL_add_all_algorithms_conf(void)
{
	OPENSSL_config(NULL);
}
LCRYPTO_ALIAS(OPENSSL_add_all_algorithms_conf);
