/*	$OpenBSD: verify.c,v 1.1.1.1 2021/08/30 17:27:45 tb Exp $ */
/*
 * Copyright (c) 2021 Theo Buehler <tb@openbsd.org>
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

/* Based on https://github.com/noxxi/libressl-tests */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>
#include <openssl/ssl.h>

struct peer_config {
	const char *name;
	int server;
	const char *cert;
	const char *key;
	const char *ca_file;
};

struct ssl_wildcard_test_data {
	const char *description;
	struct peer_config client_config;
	struct peer_config server_config;
	long verify_result;
};

static const struct ssl_wildcard_test_data ssl_wildcard_tests[] = {
	{
		.description = "unusual wildcard cert, no CA given to client",
		.client_config = {
			.name = "client",
			.server = 0,
			.cert = NULL,
			.ca_file = NULL,
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.cert = "server-unusual-wildcard.pem",
			.key = "server-unusual-wildcard.pem",
		},
		/* OpenSSL returns X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE */
		.verify_result = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY,
	},

	{
		.description = "unusual wildcard cert, CA given to client",
		.client_config = {
			.name = "client",
			.server = 0,
			.cert = NULL,
			.ca_file = "caR.pem",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.cert = "server-unusual-wildcard.pem",
			.key = "server-unusual-wildcard.pem",
		},
		.verify_result = X509_V_OK,
	},

	{
		.description = "common wildcard cert, no CA given to client",
		.client_config = {
			.name = "client",
			.server = 0,
			.cert = NULL,
			.ca_file = NULL,
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.cert = "server-common-wildcard.pem",
			.key = "server-common-wildcard.pem",
		},
		/* OpenSSL returns X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE */
		.verify_result = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY,
	},

	{
		.description = "common wildcard cert, CA given to client",
		.client_config = {
			.name = "client",
			.server = 0,
			.cert = NULL,
			.ca_file = "caR.pem",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.cert = "server-common-wildcard.pem",
			.key = "server-common-wildcard.pem",
		},
		.verify_result = X509_V_OK,
	},

	{
		.description = "server sends all chain certificates",
		.client_config = {
			.name = "client",
			.server = 0,
			.cert = NULL,
			.ca_file = "caR.pem",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.cert = "server-subca-chainS.pem",
			.key = "server-subca-chainS.pem",
			.ca_file = "subcaR.pem"
		},
		.verify_result = X509_V_OK,
	},
};

static const size_t N_SSL_WILDCARD_TESTS =
    sizeof(ssl_wildcard_tests) / sizeof(ssl_wildcard_tests[0]);

static SSL_CTX *
peer_config_to_ssl_ctx(const struct peer_config *config)
{
	SSL_CTX *ctx;

	if ((ctx = SSL_CTX_new(TLS_method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new(%s) failed\n", config->name);
		goto err;
	}

	if (config->server) {
		if (!SSL_CTX_use_certificate_file(ctx, config->cert,
		    SSL_FILETYPE_PEM)) {
			fprintf(stderr, "use_certificate_file(%s) failed\n",
			    config->name);
			goto err;
		}
		if (config->key != NULL && !SSL_CTX_use_PrivateKey_file(ctx,
		    config->key, SSL_FILETYPE_PEM)) {
			fprintf(stderr, "use_PrivateKey_file(%s) failed\n",
			    config->name);
			goto err;
		}
	}

	if (config->ca_file != NULL) {
		if (!SSL_CTX_load_verify_locations(ctx, config->ca_file, NULL)) {
			fprintf(stderr, "load_verify_locations(%s) failed\n",
			    config->name);
			goto err;
		}
	}

	return ctx;

 err:
	SSL_CTX_free(ctx);
	return NULL;
}

/* Connect client and server via a pair of "nonblocking" memory BIOs. */
static int
connect_peers(SSL *client_ssl, SSL *server_ssl, const char *description)
{
	BIO *client_wbio = NULL, *server_wbio = NULL;
	int ret = 0;

	if ((client_wbio = BIO_new(BIO_s_mem())) == NULL) {
		fprintf(stderr, "%s: failed to create client BIO\n",
		    description);
		goto err;
	}
	if ((server_wbio = BIO_new(BIO_s_mem())) == NULL) {
		fprintf(stderr, "%s: failed to create server BIO\n",
		    description);
		goto err;
	}
	if (BIO_set_mem_eof_return(client_wbio, -1) <= 0) {
		fprintf(stderr, "%s: failed to set client eof return\n",
		    description);
		goto err;
	}
	if (BIO_set_mem_eof_return(server_wbio, -1) <= 0) {
		fprintf(stderr, "%s: failed to set server eof return\n",
		    description);
		goto err;
	}

	/* Avoid double free. SSL_set_bio() takes ownership of the BIOs. */
	BIO_up_ref(client_wbio);
	BIO_up_ref(server_wbio);

	SSL_set_bio(client_ssl, server_wbio, client_wbio);
	SSL_set_bio(server_ssl, client_wbio, server_wbio);
	client_wbio = NULL;
	server_wbio = NULL;

	ret = 1;

 err:
	BIO_free(client_wbio);
	BIO_free(server_wbio);

	return ret;
}

static int
push_data_to_peer(SSL *ssl, int *ret, int (*func)(SSL *), const char *func_name,
    const char *description)
{
	int ssl_err = 0;

	if (*ret == 1)
		return 1;

	/*
	 * Do SSL_connect/SSL_accept/SSL_shutdown once and loop while hitting
	 * WANT_WRITE.  If done or on WANT_READ hand off to peer.
	 */

	do {
		if ((*ret = func(ssl)) <= 0)
			ssl_err = SSL_get_error(ssl, *ret);
	} while (*ret <= 0 && ssl_err == SSL_ERROR_WANT_WRITE);

	/* Ignore erroneous error - see SSL_shutdown(3)... */
	if (func == SSL_shutdown && ssl_err == SSL_ERROR_SYSCALL)
		return 1;

	if (*ret <= 0 && ssl_err != SSL_ERROR_WANT_READ) {
		fprintf(stderr, "%s: %s failed\n", description, func_name);
		ERR_print_errors_fp(stderr);
		return 0;
	}

	return 1;
}

/*
 * Alternate between loops of SSL_connect() and SSL_accept() as long as only
 * WANT_READ and WANT_WRITE situations are encountered. A function is repeated
 * until WANT_READ is returned or it succeeds, then it's the other function's
 * turn to make progress. Succeeds if SSL_connect() and SSL_accept() return 1.
 */
static int
handshake(SSL *client_ssl, SSL *server_ssl, const char *description)
{
	int loops = 0, client_ret = 0, server_ret = 0;

	while (loops++ < 10 && (client_ret <= 0 || server_ret <= 0)) {
		if (!push_data_to_peer(client_ssl, &client_ret, SSL_connect,
		    "SSL_connect", description))
			return 0;

		if (!push_data_to_peer(server_ssl, &server_ret, SSL_accept,
		    "SSL_accept", description))
			return 0;
	}

	if (client_ret != 1 || server_ret != 1) {
		fprintf(stderr, "%s: failed\n", __func__);
		return 0;
	}

	return 1;
}

static int
shutdown_peers(SSL *client_ssl, SSL *server_ssl, const char *description)
{
	int loops = 0, client_ret = 0, server_ret = 0;

	while (loops++ < 10 && (client_ret <= 0 || server_ret <= 0)) {
		if (!push_data_to_peer(client_ssl, &client_ret, SSL_shutdown,
		    "client shutdown", description))
			return 0;

		if (!push_data_to_peer(server_ssl, &server_ret, SSL_shutdown,
		    "server shutdown", description))
			return 0;
	}

	if (client_ret != 1 || server_ret != 1) {
		fprintf(stderr, "%s: failed\n", __func__);
		return 0;
	}

	return 1;
}

static int
test_ssl_wildcards(const struct ssl_wildcard_test_data *test)
{
	SSL_CTX *client_ctx = NULL, *server_ctx = NULL;
	SSL *client_ssl = NULL, *server_ssl = NULL;
	long verify_result;
	int failed = 1;

	if ((client_ctx = peer_config_to_ssl_ctx(&test->client_config)) == NULL)
		goto err;
	if ((server_ctx = peer_config_to_ssl_ctx(&test->server_config)) == NULL)
		goto err;

	if ((client_ssl = SSL_new(client_ctx)) == NULL) {
		fprintf(stderr, "%s: failed to create client SSL\n",
		    test->description);
		goto err;
	}
	if ((server_ssl = SSL_new(server_ctx)) == NULL) {
		fprintf(stderr, "%s: failed to create server SSL\n",
		    test->description);
		goto err;
	}

	if (!connect_peers(client_ssl, server_ssl, test->description))
		goto err;

	if (!handshake(client_ssl, server_ssl, test->description))
		goto err;

	verify_result = SSL_get_verify_result(client_ssl);

	if (test->verify_result == verify_result) {
		failed = 0;
		fprintf(stderr, "%s: ok\n", test->description);
	} else
		fprintf(stderr, "%s: verify_result: want %ld, got %ld\n",
		    test->description, test->verify_result, verify_result);

	if (!shutdown_peers(client_ssl, server_ssl, test->description))
		goto err;

 err:
	SSL_CTX_free(client_ctx);
	SSL_CTX_free(server_ctx);
	SSL_free(client_ssl);
	SSL_free(server_ssl);

	return failed;
}

int
main(int argc, char **argv)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_SSL_WILDCARD_TESTS; i++)
		failed |= test_ssl_wildcards(&ssl_wildcard_tests[i]);

	if (failed == 0)
		printf("PASS %s\n", __FILE__);

	return failed;
}
