/*	$OpenBSD: ssl_methods.c,v 1.4 2021/04/04 20:21:43 tb Exp $ */
/*
 * Copyright (c) 2020 Theo Buehler <tb@openbsd.org>
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

#include <stdio.h>

#include <openssl/ssl.h>

struct ssl_method_test_data {
	const SSL_METHOD *(*method)(void);
	const char *name;
	int server;
	int dtls;
};

struct ssl_method_test_data ssl_method_tests[] = {
	{
		.method = SSLv23_method,
		.name = "SSLv23_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = SSLv23_server_method,
		.name = "SSLv23_server_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = SSLv23_client_method,
		.name = "SSLv23_client_method",
		.server = 0,
		.dtls = 0,
	},

	{
		.method = TLSv1_method,
		.name = "TLSv1_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_server_method,
		.name = "TLSv1_server_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_client_method,
		.name = "TLSv1_client_method",
		.server = 0,
		.dtls = 0,
	},

	{
		.method = TLSv1_1_method,
		.name = "TLSv1_1_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_1_server_method,
		.name = "TLSv1_1_server_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_1_client_method,
		.name = "TLSv1_1_client_method",
		.server = 0,
		.dtls = 0,
	},

	{
		.method = TLSv1_2_method,
		.name = "TLSv1_2_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_2_server_method,
		.name = "TLSv1_2_server_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLSv1_2_client_method,
		.name = "TLSv1_2_client_method",
		.server = 0,
		.dtls = 0,
	},

	{
		.method = TLS_method,
		.name = "TLS_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLS_server_method,
		.name = "TLS_server_method",
		.server = 1,
		.dtls = 0,
	},
	{
		.method = TLS_client_method,
		.name = "TLS_client_method",
		.server = 0,
		.dtls = 0,
	},

	{
		.method = DTLSv1_method,
		.name = "DTLSv1_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLSv1_server_method,
		.name = "DTLSv1_server_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLSv1_client_method,
		.name = "DTLSv1_client_method",
		.server = 0,
		.dtls = 1,
	},

	{
		.method = DTLSv1_2_method,
		.name = "DTLSv1_2_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLSv1_2_server_method,
		.name = "DTLSv1_2_server_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLSv1_2_client_method,
		.name = "DTLSv1_2_client_method",
		.server = 0,
		.dtls = 1,
	},

	{
		.method = DTLS_method,
		.name = "DTLS_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLS_server_method,
		.name = "DTLS_server_method",
		.server = 1,
		.dtls = 1,
	},
	{
		.method = DTLS_client_method,
		.name = "DTLS_client_method",
		.server = 0,
		.dtls = 1,
	},
};

#define N_METHOD_TESTS (sizeof(ssl_method_tests) / sizeof(ssl_method_tests[0]))

int test_client_or_server_method(struct ssl_method_test_data *);
int test_dtls_method(struct ssl_method_test_data *);

int
test_client_or_server_method(struct ssl_method_test_data *testcase)
{
	SSL_CTX *ssl_ctx;
	SSL *ssl = NULL;
	int failed = 1;

	if ((ssl_ctx = SSL_CTX_new(testcase->method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new returned NULL\n");
		goto err;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		fprintf(stderr, "SSL_new returned NULL\n");
		goto err;
	}

	if (SSL_is_server(ssl) != testcase->server) {
		fprintf(stderr, "%s: SSL_is_server: want %d, got %d\n",
		    testcase->name, testcase->server, SSL_is_server(ssl));
		goto err;
	}

	failed = 0;

 err:
	SSL_free(ssl);
	SSL_CTX_free(ssl_ctx);

	return failed;
}

int
test_dtls_method(struct ssl_method_test_data *testcase)
{
	SSL_CTX *ssl_ctx;
	SSL *ssl = NULL;
	int failed = 1;

	if ((ssl_ctx = SSL_CTX_new(testcase->method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new returned NULL\n");
		goto err;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL) {
		fprintf(stderr, "SSL_new returned NULL\n");
		goto err;
	}

	if (SSL_is_dtls(ssl) != testcase->dtls) {
		fprintf(stderr, "%s: SSL_is_dtls: want %d, got %d\n",
		    testcase->name, testcase->dtls, SSL_is_dtls(ssl));
		goto err;
	}

	failed = 0;

 err:
	SSL_free(ssl);
	SSL_CTX_free(ssl_ctx);

	return failed;
}

int
main(int argc, char **argv)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_METHOD_TESTS; i++) {
		failed |= test_client_or_server_method(&ssl_method_tests[i]);
		failed |= test_dtls_method(&ssl_method_tests[i]);
	}

	if (failed == 0)
		printf("PASS %s\n", __FILE__);

	return failed;
}
