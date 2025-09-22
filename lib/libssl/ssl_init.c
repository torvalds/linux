/* $OpenBSD: ssl_init.c,v 1.6 2023/11/22 15:53:53 tb Exp $ */
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

#include <openssl/objects.h>

#include "ssl_local.h"

static pthread_t ssl_init_thread;

int
SSL_library_init(void)
{
	return OPENSSL_init_ssl(0, NULL);
}
LSSL_ALIAS(SSL_library_init);

static void
OPENSSL_init_ssl_internal(void)
{
	ssl_init_thread = pthread_self();
	SSL_load_error_strings();
}

int
OPENSSL_init_ssl(uint64_t opts, const void *settings)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	if (pthread_equal(pthread_self(), ssl_init_thread))
		return 1; /* don't recurse */

	OPENSSL_init_crypto(opts, settings);

	if (pthread_once(&once, OPENSSL_init_ssl_internal) != 0)
		return 0;

	return 1;
}
LSSL_ALIAS(OPENSSL_init_ssl);
