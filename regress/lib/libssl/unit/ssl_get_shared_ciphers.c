/*	$OpenBSD: ssl_get_shared_ciphers.c,v 1.13 2024/08/31 12:47:24 jsing Exp $ */
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

struct peer_config {
	const char *name;
	int server;
	uint16_t max_version;
	uint16_t min_version;
	const char *ciphers;
};

struct ssl_shared_ciphers_test_data {
	const char *description;
	struct peer_config client_config;
	struct peer_config server_config;
	const char *shared_ciphers;
	const char *shared_ciphers_without_aesni;
};

char *server_cert;
char *server_key;

static const struct ssl_shared_ciphers_test_data ssl_shared_ciphers_tests[] = {
	{
		.description = "TLSv1.3 defaults",
		.client_config = {
			.name = "client",
			.server = 0,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_3_VERSION,
			.ciphers =
			    "TLS_AES_256_GCM_SHA384:"
			    "TLS_CHACHA20_POLY1305_SHA256:"
			    "TLS_AES_128_GCM_SHA256",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_3_VERSION,
			.ciphers =
			    "TLS_AES_256_GCM_SHA384:"
			    "TLS_CHACHA20_POLY1305_SHA256:"
			    "TLS_AES_128_GCM_SHA256",
		},
		.shared_ciphers =
		    "TLS_AES_256_GCM_SHA384:"
		    "TLS_CHACHA20_POLY1305_SHA256:"
		    "TLS_AES_128_GCM_SHA256",
	},

	{
		.description = "TLSv1.3, client without ChaCha",
		.client_config = {
			.name = "client",
			.server = 0,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_3_VERSION,
			.ciphers =
			    "TLS_AES_256_GCM_SHA384:"
			    "TLS_AES_128_GCM_SHA256",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_3_VERSION,
			.ciphers =
			    "TLS_AES_256_GCM_SHA384:"
			    "TLS_CHACHA20_POLY1305_SHA256:"
			    "TLS_AES_128_GCM_SHA256",
		},
		.shared_ciphers =
		    "TLS_AES_256_GCM_SHA384:"
		    "TLS_AES_128_GCM_SHA256",
	},

	{
		.description = "TLSv1.2",
		.client_config = {
			.name = "client",
			.server = 0,
			.max_version = TLS1_2_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384:"
			    "ECDHE-ECDSA-AES256-GCM-SHA384:"
			    "ECDHE-RSA-AES256-SHA384:"
			    "ECDHE-ECDSA-AES256-SHA384:"
			    "ECDHE-RSA-AES256-SHA:"
			    "ECDHE-ECDSA-AES256-SHA",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.max_version = TLS1_2_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384:"
			    "ECDHE-ECDSA-AES256-GCM-SHA384:"
			    "ECDHE-RSA-AES256-SHA384:"
			    "ECDHE-ECDSA-AES256-SHA384:"
			    "ECDHE-RSA-AES256-SHA:"
			    "ECDHE-ECDSA-AES256-SHA",
		},
		.shared_ciphers =
		    "ECDHE-RSA-AES256-GCM-SHA384:"
		    "ECDHE-ECDSA-AES256-GCM-SHA384:"
		    "ECDHE-RSA-AES256-SHA384:"
		    "ECDHE-ECDSA-AES256-SHA384:"
		    "ECDHE-RSA-AES256-SHA:"
		    "ECDHE-ECDSA-AES256-SHA",
	},

	{
		.description = "TLSv1.2, server without ECDSA",
		.client_config = {
			.name = "client",
			.server = 0,
			.max_version = TLS1_2_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384:"
			    "ECDHE-ECDSA-AES256-GCM-SHA384:"
			    "ECDHE-RSA-AES256-SHA384:"
			    "ECDHE-ECDSA-AES256-SHA384:"
			    "ECDHE-RSA-AES256-SHA:"
			    "ECDHE-ECDSA-AES256-SHA",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.max_version = TLS1_2_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384:"
			    "ECDHE-RSA-AES256-SHA384:"
			    "ECDHE-RSA-AES256-SHA",
		},
		.shared_ciphers =
		    "ECDHE-RSA-AES256-GCM-SHA384:"
		    "ECDHE-RSA-AES256-SHA384:"
		    "ECDHE-RSA-AES256-SHA",
	},

	{
		.description = "TLSv1.3 ciphers are prepended",
		.client_config = {
			.name = "client",
			.server = 0,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384",
		},
		.server_config = {
			.name = "server",
			.server = 1,
			.max_version = TLS1_3_VERSION,
			.min_version = TLS1_2_VERSION,
			.ciphers =
			    "ECDHE-RSA-AES256-GCM-SHA384",
		},
		.shared_ciphers =
		    "TLS_AES_256_GCM_SHA384:"
		    "TLS_CHACHA20_POLY1305_SHA256:"
		    "TLS_AES_128_GCM_SHA256:"
		    "ECDHE-RSA-AES256-GCM-SHA384",
		.shared_ciphers_without_aesni =
		    "TLS_CHACHA20_POLY1305_SHA256:"
		    "TLS_AES_256_GCM_SHA384:"
		    "TLS_AES_128_GCM_SHA256:"
		    "ECDHE-RSA-AES256-GCM-SHA384",
	},
};

static const size_t N_SHARED_CIPHERS_TESTS =
    sizeof(ssl_shared_ciphers_tests) / sizeof(ssl_shared_ciphers_tests[0]);

static SSL_CTX *
peer_config_to_ssl_ctx(const struct peer_config *config)
{
	SSL_CTX *ctx;

	if ((ctx = SSL_CTX_new(TLS_method())) == NULL) {
		fprintf(stderr, "SSL_CTX_new(%s) failed\n", config->name);
		goto err;
	}
	if (!SSL_CTX_set_max_proto_version(ctx, config->max_version)) {
		fprintf(stderr, "max_proto_version(%s) failed\n", config->name);
		goto err;
	}
	if (!SSL_CTX_set_min_proto_version(ctx, config->min_version)) {
		fprintf(stderr, "min_proto_version(%s) failed\n", config->name);
		goto err;
	}
	if (!SSL_CTX_set_cipher_list(ctx, config->ciphers)) {
		fprintf(stderr, "set_cipher_list(%s) failed\n", config->name);
		goto err;
	}

	if (config->server) {
		if (!SSL_CTX_use_certificate_file(ctx, server_cert,
		    SSL_FILETYPE_PEM)) {
			fprintf(stderr, "use_certificate_file(%s) failed\n",
			    config->name);
			goto err;
		}
		if (!SSL_CTX_use_PrivateKey_file(ctx, server_key,
		    SSL_FILETYPE_PEM)) {
			fprintf(stderr, "use_PrivateKey_file(%s) failed\n",
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

/* from ssl_ciph.c */
static inline int
ssl_aes_is_accelerated(void)
{
	return (OPENSSL_cpu_caps() & CRYPTO_CPU_CAPS_ACCELERATED_AES) != 0;
}

static int
check_shared_ciphers(const struct ssl_shared_ciphers_test_data *test,
    const char *got)
{
	const char *want = test->shared_ciphers;
	int failed;

	if (!ssl_aes_is_accelerated() &&
	    test->shared_ciphers_without_aesni != NULL)
		want = test->shared_ciphers_without_aesni;

	failed = strcmp(want, got);

	if (failed)
		fprintf(stderr, "%s: want \"%s\", got \"%s\"\n",
		    test->description, want, got);

	return failed;
}

static int
test_get_shared_ciphers(const struct ssl_shared_ciphers_test_data *test)
{
	SSL_CTX *client_ctx = NULL, *server_ctx = NULL;
	SSL *client_ssl = NULL, *server_ssl = NULL;
	char buf[4096];
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

	if (SSL_get_shared_ciphers(server_ssl, buf, sizeof(buf)) == NULL) {
		fprintf(stderr, "%s: failed to get shared ciphers\n",
		    test->description);
		goto err;
	}

	if (!shutdown_peers(client_ssl, server_ssl, test->description))
		goto err;

	failed = check_shared_ciphers(test, buf);

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

	if (asprintf(&server_cert, "%s/server1-rsa.pem", CERTSDIR) == -1) {
		fprintf(stderr, "asprintf server_cert failed\n");
		failed = 1;
		goto err;
	}
	server_key = server_cert;

	for (i = 0; i < N_SHARED_CIPHERS_TESTS; i++)
		failed |= test_get_shared_ciphers(&ssl_shared_ciphers_tests[i]);

	if (failed == 0)
		printf("PASS %s\n", __FILE__);

 err:
	free(server_cert);

	return failed;
}
