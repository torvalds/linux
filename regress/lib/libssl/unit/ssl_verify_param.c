/*	$OpenBSD: ssl_verify_param.c,v 1.1 2023/05/24 08:54:59 tb Exp $ */

/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <stdio.h>

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

unsigned int X509_VERIFY_PARAM_get_hostflags(X509_VERIFY_PARAM *param);

static int
ssl_verify_param_flags_inherited(void)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	X509_VERIFY_PARAM *param;
	unsigned int defaultflags = 0;
	unsigned int newflags = X509_CHECK_FLAG_NEVER_CHECK_SUBJECT;
	unsigned int flags;
	int failed = 1;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "SSL_CTX_new");

	if ((param = SSL_CTX_get0_param(ssl_ctx)) == NULL) {
		fprintf(stderr, "FAIL: no verify param on ssl_ctx\n");
		goto failure;
	}

	if ((flags = X509_VERIFY_PARAM_get_hostflags(param)) != defaultflags) {
		fprintf(stderr, "FAIL: SSL_CTX default hostflags, "
		    "want: %x, got: %x\n", defaultflags, flags);
		goto failure;
	}

	X509_VERIFY_PARAM_set_hostflags(param, newflags);

	if ((flags = X509_VERIFY_PARAM_get_hostflags(param)) != newflags) {
		fprintf(stderr, "FAIL: SSL_CTX new hostflags, "
		    "want: %x, got: %x\n", newflags, flags);
		goto failure;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "SSL_new");

	if ((param = SSL_get0_param(ssl)) == NULL) {
		fprintf(stderr, "FAIL: no verify param on ssl\n");
		goto failure;
	}

	if ((flags = X509_VERIFY_PARAM_get_hostflags(param)) != newflags) {
		fprintf(stderr, "FAIL: SSL inherited hostflags, "
		    "want: %x, got: %x\n", newflags, flags);
		goto failure;
	}

	SSL_set_hostflags(ssl, defaultflags);

	if ((flags = X509_VERIFY_PARAM_get_hostflags(param)) != defaultflags) {
		fprintf(stderr, "FAIL: SSL set hostflags, "
		    "want: %x, got: %x\n", defaultflags, flags);
		goto failure;
	}

	failed = 0;

 failure:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= ssl_verify_param_flags_inherited();

	return failed;
}
