/* $OpenBSD: ssl_versions.c,v 1.20 2023/07/02 17:21:33 beck Exp $ */
/*
 * Copyright (c) 2016, 2017 Joel Sing <jsing@openbsd.org>
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

#include <openssl/ssl.h>

#include "ssl_local.h"

struct version_range_test {
	const long options;
	const uint16_t minver;
	const uint16_t maxver;
	const uint16_t want_minver;
	const uint16_t want_maxver;
};

static struct version_range_test version_range_tests[] = {
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1_3,
		.minver = TLS1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = SSL_OP_NO_TLSv1_1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
		    SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
		    SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_3_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
		    SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3,
		.minver = TLS1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_2_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_2_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_3_VERSION,
		.maxver = TLS1_3_VERSION,
		.want_minver = TLS1_3_VERSION,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_1_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
	},
};

#define N_VERSION_RANGE_TESTS \
    (sizeof(version_range_tests) / sizeof(*version_range_tests))

static int
test_ssl_enabled_version_range(void)
{
	struct version_range_test *vrt;
	uint16_t minver, maxver;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int failed = 1;
	size_t i;

	fprintf(stderr, "INFO: starting enabled version range tests...\n");

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new() returned NULL\n");
		goto failure;
	}
	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		fprintf(stderr, "SSL_new() returned NULL\n");
		goto failure;
	}

	failed = 0;

	for (i = 0; i < N_VERSION_RANGE_TESTS; i++) {
		vrt = &version_range_tests[i];

		SSL_clear_options(ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
		    SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);
		SSL_set_options(ssl, vrt->options);

		minver = maxver = 0xffff;
		ssl->min_tls_version = vrt->minver;
		ssl->max_tls_version = vrt->maxver;

		if (ssl_enabled_tls_version_range(ssl, &minver, &maxver) != 1) {
			if (vrt->want_minver != 0 || vrt->want_maxver != 0) {
				fprintf(stderr, "FAIL: test %zu - failed but "
				    "wanted non-zero versions\n", i);
				failed++;
			}
			continue;
		}
		if (minver != vrt->want_minver) {
			fprintf(stderr, "FAIL: test %zu - got minver %x, "
			    "want %x\n", i, minver, vrt->want_minver);
			failed++;
		}
		if (maxver != vrt->want_maxver) {
			fprintf(stderr, "FAIL: test %zu - got maxver %x, "
			    "want %x\n", i, maxver, vrt->want_maxver);
			failed++;
		}
	}

 failure:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return (failed);
}

struct shared_version_test {
	const SSL_METHOD *(*ssl_method)(void);
	const long options;
	const uint16_t minver;
	const uint16_t maxver;
	const uint16_t peerver;
	const uint16_t want_maxver;
};

static struct shared_version_test shared_version_tests[] = {
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = SSL2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = SSL3_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_3_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = 0x7f12,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = SSL_OP_NO_TLSv1,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_1_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLSv1_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLSv1_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLSv1_1_method,
		.options = 0,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_2_VERSION,
		.want_maxver = DTLS1_2_VERSION,
	},
	{
		.ssl_method = DTLS_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = 0xfefc,	/* DTLSv1.3, probably. */
		.want_maxver = DTLS1_2_VERSION,
	},
	{
		.ssl_method = DTLSv1_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_1_VERSION,
		.peerver = DTLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLSv1_2_method,
		.options = 0,
		.minver = TLS1_2_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_2_VERSION,
		.want_maxver = DTLS1_2_VERSION,
	},
	{
		.ssl_method = DTLSv1_method,
		.options = 0,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_1_VERSION,
		.peerver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.options = SSL_OP_NO_DTLSv1,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.options = SSL_OP_NO_DTLSv1,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_2_VERSION,
		.want_maxver = DTLS1_2_VERSION,
	},
	{
		.ssl_method = DTLS_method,
		.options = SSL_OP_NO_DTLSv1_2,
		.minver = TLS1_1_VERSION,
		.maxver = TLS1_2_VERSION,
		.peerver = DTLS1_2_VERSION,
		.want_maxver = 0,
	},
};

#define N_SHARED_VERSION_TESTS \
    (sizeof(shared_version_tests) / sizeof(*shared_version_tests))

static int
test_ssl_max_shared_version(void)
{
	struct shared_version_test *svt;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	uint16_t maxver;
	int failed = 0;
	size_t i;

	failed = 0;

	fprintf(stderr, "INFO: starting max shared version tests...\n");

	for (i = 0; i < N_SHARED_VERSION_TESTS; i++) {
		svt = &shared_version_tests[i];

		if ((ssl_ctx = SSL_CTX_new(svt->ssl_method())) == NULL) {
			fprintf(stderr, "SSL_CTX_new() returned NULL\n");
			failed++;
			goto err;
		}
		if ((ssl = SSL_new(ssl_ctx)) == NULL) {
			fprintf(stderr, "SSL_new() returned NULL\n");
			failed++;
			goto err;
		}

		SSL_clear_options(ssl, SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
		    SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3);
		SSL_set_options(ssl, svt->options);

		maxver = 0;
		ssl->min_tls_version = svt->minver;
		ssl->max_tls_version = svt->maxver;

		if (!ssl_max_shared_version(ssl, svt->peerver, &maxver)) {
			if (svt->want_maxver != 0) {
				fprintf(stderr, "FAIL: test %zu - failed but "
				    "wanted non-zero shared version (peer %x)\n",
				    i, svt->peerver);
				failed++;
			}
			SSL_CTX_free(ssl_ctx);
			SSL_free(ssl);
			ssl_ctx = NULL;
			ssl = NULL;
			continue;
		}
		if (maxver != svt->want_maxver) {
			fprintf(stderr, "FAIL: test %zu - got shared "
			    "version %x, want %x\n", i, maxver,
			    svt->want_maxver);
			failed++;
		}

		SSL_CTX_free(ssl_ctx);
		SSL_free(ssl);
		ssl_ctx = NULL;
		ssl = NULL;
	}

 err:
	SSL_CTX_free(ssl_ctx);
	SSL_free(ssl);

	return (failed);
}

struct min_max_version_test {
	const SSL_METHOD *(*ssl_method)(void);
	const uint16_t minver;
	const uint16_t maxver;
	const uint16_t want_minver;
	const uint16_t want_maxver;
	const int want_min_fail;
	const int want_max_fail;
};

static struct min_max_version_test min_max_version_tests[] = {
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = 0,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.minver = TLS1_VERSION,
		.maxver = 0,
		.want_minver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = TLS1_3_VERSION,
		.want_minver = 0,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_VERSION,
		.want_maxver = TLS1_2_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = TLS1_1_VERSION,
		.maxver = 0,
		.want_minver = TLS1_1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.minver = TLS1_2_VERSION,
		.maxver = 0,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0x0300,
		.maxver = 0,
		.want_minver = TLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0x0305,
		.maxver = 0,
		.want_min_fail = 1,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = 0x0305,
		.want_minver = 0,
		.want_maxver = TLS1_3_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = TLS1_1_VERSION,
		.want_minver = 0,
		.want_maxver = TLS1_1_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = TLS1_VERSION,
		.want_minver = 0,
		.want_maxver = TLS1_VERSION,
	},
	{
		.ssl_method = TLS_method,
		.minver = 0,
		.maxver = 0x0300,
		.want_max_fail = 1,
	},
	{
		.ssl_method = TLS_method,
		.minver = TLS1_2_VERSION,
		.maxver = TLS1_1_VERSION,
		.want_minver = TLS1_2_VERSION,
		.want_maxver = 0,
		.want_max_fail = 1,
	},
	{
		.ssl_method = TLSv1_1_method,
		.minver = 0,
		.maxver = 0,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.ssl_method = TLSv1_1_method,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = TLS1_1_VERSION,
		.want_maxver = TLS1_1_VERSION,
	},
	{
		.ssl_method = TLSv1_1_method,
		.minver = TLS1_2_VERSION,
		.maxver = 0,
		.want_minver = 0,
		.want_maxver = 0,
		.want_min_fail = 1,
	},
	{
		.ssl_method = TLSv1_1_method,
		.minver = 0,
		.maxver = TLS1_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
		.want_max_fail = 1,
	},
	{
		.ssl_method = DTLS_method,
		.minver = 0,
		.maxver = 0,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.minver = 0,
		.maxver = DTLS1_VERSION,
		.want_minver = 0,
		.want_maxver = DTLS1_VERSION,
	},
	{
		.ssl_method = DTLS_method,
		.minver = DTLS1_VERSION,
		.maxver = 0,
		.want_minver = DTLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLS_method,
		.minver = DTLS1_VERSION,
		.maxver = DTLS1_2_VERSION,
		.want_minver = DTLS1_VERSION,
		.want_maxver = DTLS1_2_VERSION,
	},
	{
		.ssl_method = DTLSv1_method,
		.minver = 0,
		.maxver = 0,
		.want_minver = 0,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLSv1_method,
		.minver = DTLS1_VERSION,
		.maxver = 0,
		.want_minver = DTLS1_VERSION,
		.want_maxver = 0,
	},
	{
		.ssl_method = DTLSv1_method,
		.minver = 0,
		.maxver = DTLS1_VERSION,
		.want_minver = 0,
		.want_maxver = DTLS1_VERSION,
	},
	{
		.ssl_method = DTLSv1_method,
		.minver = 0,
		.maxver = DTLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = DTLS1_VERSION,
	},
	{
		.ssl_method = DTLSv1_method,
		.minver = TLS1_VERSION,
		.maxver = TLS1_2_VERSION,
		.want_minver = 0,
		.want_maxver = 0,
		.want_min_fail = 1,
		.want_max_fail = 1,
	},
};

#define N_MIN_MAX_VERSION_TESTS \
    (sizeof(min_max_version_tests) / sizeof(*min_max_version_tests))

static int
test_ssl_min_max_version(void)
{
	struct min_max_version_test *mmvt;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int failed = 0;
	size_t i;

	failed = 0;

	fprintf(stderr, "INFO: starting min max version tests...\n");

	for (i = 0; i < N_MIN_MAX_VERSION_TESTS; i++) {
		mmvt = &min_max_version_tests[i];

		if ((ssl_ctx = SSL_CTX_new(mmvt->ssl_method())) == NULL) {
			fprintf(stderr, "SSL_CTX_new() returned NULL\n");
			return 1;
		}

		if (!SSL_CTX_set_min_proto_version(ssl_ctx, mmvt->minver)) {
			if (!mmvt->want_min_fail) {
				fprintf(stderr, "FAIL: test %zu - failed to set "
				    "SSL_CTX min version\n", i);
				failed++;
			}
			goto next;
		}
		if (!SSL_CTX_set_max_proto_version(ssl_ctx, mmvt->maxver)) {
			if (!mmvt->want_max_fail) {
				fprintf(stderr, "FAIL: test %zu - failed to set "
				    "SSL_CTX min version\n", i);
				failed++;
			}
			goto next;
		}

		if (mmvt->want_min_fail) {
			fprintf(stderr, "FAIL: test %zu - successfully set "
			    "SSL_CTX min version, should have failed\n", i);
			failed++;
			goto next;
		}
		if (mmvt->want_max_fail) {
			fprintf(stderr, "FAIL: test %zu - successfully set "
			    "SSL_CTX max version, should have failed\n", i);
			failed++;
			goto next;
		}

		if (SSL_CTX_get_min_proto_version(ssl_ctx) != mmvt->want_minver) {
			fprintf(stderr, "FAIL: test %zu - got SSL_CTX min "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_CTX_get_min_proto_version(ssl_ctx), mmvt->want_minver);
			failed++;
			goto next;
		}
		if (SSL_CTX_get_max_proto_version(ssl_ctx) != mmvt->want_maxver) {
			fprintf(stderr, "FAIL: test %zu - got SSL_CTX max "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_CTX_get_max_proto_version(ssl_ctx), mmvt->want_maxver);
			failed++;
			goto next;
		}

		if ((ssl = SSL_new(ssl_ctx)) == NULL) {
			fprintf(stderr, "SSL_new() returned NULL\n");
			return 1;
		}

		if (SSL_get_min_proto_version(ssl) != mmvt->want_minver) {
			fprintf(stderr, "FAIL: test %zu - initial SSL min "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_get_min_proto_version(ssl), mmvt->want_minver);
			failed++;
			goto next;
		}
		if (SSL_get_max_proto_version(ssl) != mmvt->want_maxver) {
			fprintf(stderr, "FAIL: test %zu - initial SSL max "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_get_max_proto_version(ssl), mmvt->want_maxver);
			failed++;
			goto next;
		}

		if (!SSL_set_min_proto_version(ssl, mmvt->minver)) {
			if (mmvt->want_min_fail) {
				fprintf(stderr, "FAIL: test %zu - failed to set "
				    "SSL min version\n", i);
				failed++;
			}
			goto next;
		}
		if (!SSL_set_max_proto_version(ssl, mmvt->maxver)) {
			if (mmvt->want_max_fail) {
				fprintf(stderr, "FAIL: test %zu - failed to set "
				    "SSL min version\n", i);
				failed++;
			}
			goto next;
		}

		if (mmvt->want_min_fail) {
			fprintf(stderr, "FAIL: test %zu - successfully set SSL "
			    "min version, should have failed\n", i);
			failed++;
			goto next;
		}
		if (mmvt->want_max_fail) {
			fprintf(stderr, "FAIL: test %zu - successfully set SSL "
			    "max version, should have failed\n", i);
			failed++;
			goto next;
		}

		if (SSL_get_min_proto_version(ssl) != mmvt->want_minver) {
			fprintf(stderr, "FAIL: test %zu - got SSL min "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_get_min_proto_version(ssl), mmvt->want_minver);
			failed++;
			goto next;
		}
		if (SSL_get_max_proto_version(ssl) != mmvt->want_maxver) {
			fprintf(stderr, "FAIL: test %zu - got SSL max "
			    "version 0x%x, want 0x%x\n", i,
			    SSL_get_max_proto_version(ssl), mmvt->want_maxver);
			failed++;
			goto next;
		}

 next:
		SSL_CTX_free(ssl_ctx);
		SSL_free(ssl);

		ssl_ctx = NULL;
		ssl = NULL;
	}

	return (failed);
}

int
main(int argc, char **argv)
{
	int failed = 0;

	SSL_library_init();

	/* XXX - Test ssl_supported_version_range() */

	failed |= test_ssl_enabled_version_range();
	failed |= test_ssl_max_shared_version();
	failed |= test_ssl_min_max_version();

	if (failed == 0)
		printf("PASS %s\n", __FILE__);

	return (failed);
}
