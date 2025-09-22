/* $OpenBSD: tlstest.c,v 1.2 2023/07/02 17:21:33 beck Exp $ */
/*
 * Copyright (c) 2020, 2021 Joel Sing <jsing@openbsd.org>
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

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

const char *server_ca_file;
const char *server_cert_file;
const char *server_key_file;

int debug = 0;

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

static SSL *
tls_client(BIO *rbio, BIO *wbio)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "client context");

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "client ssl");

	BIO_up_ref(rbio);
	BIO_up_ref(wbio);

	SSL_set_bio(ssl, rbio, wbio);

	SSL_CTX_free(ssl_ctx);

	return ssl;
}

static SSL *
tls_server(BIO *rbio, BIO *wbio)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "server context");

	SSL_CTX_set_dh_auto(ssl_ctx, 2);

	if (SSL_CTX_use_certificate_file(ssl_ctx, server_cert_file,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load server certificate");
		goto failure;
	}
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, server_key_file,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load server private key");
		goto failure;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "server ssl");

	BIO_up_ref(rbio);
	BIO_up_ref(wbio);

	SSL_set_bio(ssl, rbio, wbio);

 failure:
	SSL_CTX_free(ssl_ctx);

	return ssl;
}

static int
ssl_error(SSL *ssl, const char *name, const char *desc, int ssl_ret)
{
	int ssl_err;

	ssl_err = SSL_get_error(ssl, ssl_ret);

	if (ssl_err == SSL_ERROR_WANT_READ) {
		return 1;
	} else if (ssl_err == SSL_ERROR_WANT_WRITE) {
		return 1;
	} else if (ssl_err == SSL_ERROR_SYSCALL && errno == 0) {
		/* Yup, this is apparently a thing... */
	} else {
		fprintf(stderr, "FAIL: %s %s failed - ssl err = %d, errno = %d\n",
		    name, desc, ssl_err, errno);
		ERR_print_errors_fp(stderr);
		return 0;
	}

	return 1;
}

static int
do_connect(SSL *ssl, const char *name, int *done)
{
	int ssl_ret;

	if ((ssl_ret = SSL_connect(ssl)) == 1) {
		fprintf(stderr, "INFO: %s connect done\n", name);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "connect", ssl_ret);
}

static int
do_accept(SSL *ssl, const char *name, int *done)
{
	int ssl_ret;

	if ((ssl_ret = SSL_accept(ssl)) == 1) {
		fprintf(stderr, "INFO: %s accept done\n", name);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "accept", ssl_ret);
}

static int
do_read(SSL *ssl, const char *name, int *done)
{
	uint8_t buf[512];
	int ssl_ret;

	if ((ssl_ret = SSL_read(ssl, buf, sizeof(buf))) > 0) {
		fprintf(stderr, "INFO: %s read done\n", name);
		if (debug > 1)
			hexdump(buf, ssl_ret);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "read", ssl_ret);
}

static int
do_write(SSL *ssl, const char *name, int *done)
{
	const uint8_t buf[] = "Hello, World!\n";
	int ssl_ret;

	if ((ssl_ret = SSL_write(ssl, buf, sizeof(buf))) > 0) {
		fprintf(stderr, "INFO: %s write done\n", name);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "write", ssl_ret);
}

static int
do_shutdown(SSL *ssl, const char *name, int *done)
{
	int ssl_ret;

	ssl_ret = SSL_shutdown(ssl);
	if (ssl_ret == 1) {
		fprintf(stderr, "INFO: %s shutdown done\n", name);
		*done = 1;
		return 1;
	}
	return ssl_error(ssl, name, "shutdown", ssl_ret);
}

typedef int (*ssl_func)(SSL *ssl, const char *name, int *done);

static int
do_client_server_loop(SSL *client, ssl_func client_func, SSL *server,
    ssl_func server_func)
{
	int client_done = 0, server_done = 0;
	int i = 0;

	do {
		if (!client_done) {
			if (debug)
				fprintf(stderr, "DEBUG: client loop\n");
			if (!client_func(client, "client", &client_done))
				return 0;
		}
		if (!server_done) {
			if (debug)
				fprintf(stderr, "DEBUG: server loop\n");
			if (!server_func(server, "server", &server_done))
				return 0;
		}
	} while (i++ < 100 && (!client_done || !server_done));

	if (!client_done || !server_done)
		fprintf(stderr, "FAIL: gave up\n");

	return client_done && server_done;
}

struct tls_test {
	const unsigned char *desc;
	const SSL_METHOD *(*client_method)(void);
	uint16_t client_min_version;
	uint16_t client_max_version;
	const char *client_ciphers;
	const SSL_METHOD *(*server_method)(void);
	uint16_t server_min_version;
	uint16_t server_max_version;
	const char *server_ciphers;
};

static const struct tls_test tls_tests[] = {
	{
		.desc = "Default client and server",
	},
	{
		.desc = "Default client and TLSv1.2 server",
		.server_max_version = TLS1_2_VERSION,
	},
	{
		.desc = "Default client and default server with ECDHE KEX",
		.server_ciphers = "ECDHE-RSA-AES128-SHA",
	},
	{
		.desc = "Default client and TLSv1.2 server with ECDHE KEX",
		.server_max_version = TLS1_2_VERSION,
		.server_ciphers = "ECDHE-RSA-AES128-SHA",
	},
	{
		.desc = "Default client and default server with DHE KEX",
		.server_ciphers = "DHE-RSA-AES128-SHA",
	},
	{
		.desc = "Default client and TLSv1.2 server with DHE KEX",
		.server_max_version = TLS1_2_VERSION,
		.server_ciphers = "DHE-RSA-AES128-SHA",
	},
	{
		.desc = "Default client and default server with RSA KEX",
		.server_ciphers = "AES128-SHA",
	},
	{
		.desc = "Default client and TLSv1.2 server with RSA KEX",
		.server_max_version = TLS1_2_VERSION,
		.server_ciphers = "AES128-SHA",
	},
	{
		.desc = "TLSv1.2 client and default server",
		.client_max_version = TLS1_2_VERSION,
	},
	{
		.desc = "TLSv1.2 client and default server with ECDHE KEX",
		.client_max_version = TLS1_2_VERSION,
		.client_ciphers = "ECDHE-RSA-AES128-SHA",
	},
	{
		.desc = "TLSv1.2 client and default server with DHE KEX",
		.server_max_version = TLS1_2_VERSION,
		.client_ciphers = "DHE-RSA-AES128-SHA",
	},
	{
		.desc = "TLSv1.2 client and default server with RSA KEX",
		.client_max_version = TLS1_2_VERSION,
		.client_ciphers = "AES128-SHA",
	},
};

#define N_TLS_TESTS (sizeof(tls_tests) / sizeof(*tls_tests))

static int
tlstest(const struct tls_test *tt)
{
	BIO *client_wbio = NULL, *server_wbio = NULL;
	SSL *client = NULL, *server = NULL;
	int failed = 1;

	fprintf(stderr, "\n== Testing %s... ==\n", tt->desc);

	if ((client_wbio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (BIO_set_mem_eof_return(client_wbio, -1) <= 0)
		goto failure;

	if ((server_wbio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (BIO_set_mem_eof_return(server_wbio, -1) <= 0)
		goto failure;

	if ((client = tls_client(server_wbio, client_wbio)) == NULL)
		goto failure;
	if (tt->client_min_version != 0) {
		if (!SSL_set_min_proto_version(client, tt->client_min_version))
			goto failure;
	}
	if (tt->client_max_version != 0) {
		if (!SSL_set_max_proto_version(client, tt->client_max_version))
			goto failure;
	}
	if (tt->client_ciphers != NULL) {
		if (!SSL_set_cipher_list(client, tt->client_ciphers))
			goto failure;
	}

	if ((server = tls_server(client_wbio, server_wbio)) == NULL)
		goto failure;
	if (tt->server_min_version != 0) {
		if (!SSL_set_min_proto_version(server, tt->server_min_version))
			goto failure;
	}
	if (tt->server_max_version != 0) {
		if (!SSL_set_max_proto_version(server, tt->server_max_version))
			goto failure;
	}
	if (tt->server_ciphers != NULL) {
		if (!SSL_set_cipher_list(server, tt->server_ciphers))
			goto failure;
	}

	if (!do_client_server_loop(client, do_connect, server, do_accept)) {
		fprintf(stderr, "FAIL: client and server handshake failed\n");
		goto failure;
	}

	if (!do_client_server_loop(client, do_write, server, do_read)) {
		fprintf(stderr, "FAIL: client write and server read I/O failed\n");
		goto failure;
	}

	if (!do_client_server_loop(client, do_read, server, do_write)) {
		fprintf(stderr, "FAIL: client read and server write I/O failed\n");
		goto failure;
	}

	if (!do_client_server_loop(client, do_shutdown, server, do_shutdown)) {
		fprintf(stderr, "FAIL: client and server shutdown failed\n");
		goto failure;
	}

	fprintf(stderr, "INFO: Done!\n");

	failed = 0;

 failure:
	BIO_free(client_wbio);
	BIO_free(server_wbio);

	SSL_free(client);
	SSL_free(server);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;
	size_t i;

	if (argc != 4) {
		fprintf(stderr, "usage: %s keyfile certfile cafile\n",
		    argv[0]);
		exit(1);
	}

	server_key_file = argv[1];
	server_cert_file = argv[2];
	server_ca_file = argv[3];

	for (i = 0; i < N_TLS_TESTS; i++)
		failed |= tlstest(&tls_tests[i]);

	return failed;
}
