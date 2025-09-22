/* $OpenBSD: quictest.c,v 1.1 2022/08/27 09:16:29 jsing Exp $ */
/*
 * Copyright (c) 2020, 2021, 2022 Joel Sing <jsing@openbsd.org>
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

struct quic_data {
	enum ssl_encryption_level_t rlevel;
	enum ssl_encryption_level_t wlevel;
	BIO *rbio;
	BIO *wbio;
};

static int
quic_set_read_secret(SSL *ssl, enum ssl_encryption_level_t level,
    const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len)
{
	struct quic_data *qd = SSL_get_app_data(ssl);

	qd->rlevel = level;

	return 1;
}

static int
quic_set_write_secret(SSL *ssl, enum ssl_encryption_level_t level,
    const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len)
{
	struct quic_data *qd = SSL_get_app_data(ssl);

	qd->wlevel = level;

	return 1;
}

static int
quic_read_handshake_data(SSL *ssl)
{
	struct quic_data *qd = SSL_get_app_data(ssl);
	uint8_t buf[2048];
	int ret;

	if ((ret = BIO_read(qd->rbio, buf, sizeof(buf))) > 0) {
		if (debug > 1) {
			fprintf(stderr, "== quic_read_handshake_data ==\n");
			hexdump(buf, ret);
		}
		if (!SSL_provide_quic_data(ssl, qd->rlevel, buf, ret))
			return -1;
	}

	return 1;
}

static int
quic_add_handshake_data(SSL *ssl, enum ssl_encryption_level_t level,
    const uint8_t *data, size_t len)
{
	struct quic_data *qd = SSL_get_app_data(ssl);
	int ret;

	if (debug > 1) {
		fprintf(stderr, "== quic_add_handshake_data\n");
		hexdump(data, len);
	}

	if ((ret = BIO_write(qd->wbio, data, len)) <= 0)
		return 0;

	return (size_t)ret == len;
}

static int
quic_flush_flight(SSL *ssl)
{
	return 1;
}

static int
quic_send_alert(SSL *ssl, enum ssl_encryption_level_t level, uint8_t alert)
{
	return 1;
}

const SSL_QUIC_METHOD quic_method = {
	.set_read_secret = quic_set_read_secret,
	.set_write_secret = quic_set_write_secret,
	.add_handshake_data = quic_add_handshake_data,
	.flush_flight = quic_flush_flight,
	.send_alert = quic_send_alert,
};

static SSL *
quic_client(struct quic_data *data)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "client context");

	if (!SSL_CTX_set_quic_method(ssl_ctx, &quic_method)) {
		fprintf(stderr, "FAIL: Failed to set QUIC method\n");
		goto failure;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "client ssl");

	SSL_set_connect_state(ssl);
	SSL_set_app_data(ssl, data);

 failure:
	SSL_CTX_free(ssl_ctx);

	return ssl;
}

static SSL *
quic_server(struct quic_data *data)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "server context");

	SSL_CTX_set_dh_auto(ssl_ctx, 2);

	if (SSL_CTX_use_certificate_file(ssl_ctx, server_cert_file,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load server certificate\n");
		goto failure;
	}
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, server_key_file,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load server private key\n");
		goto failure;
	}

	if (!SSL_CTX_set_quic_method(ssl_ctx, &quic_method)) {
		fprintf(stderr, "FAIL: Failed to set QUIC method\n");
		goto failure;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "server ssl");

	SSL_set_accept_state(ssl);
	SSL_set_app_data(ssl, data);

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
		if (quic_read_handshake_data(ssl) < 0)
			return 0;
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
do_handshake(SSL *ssl, const char *name, int *done)
{
	int ssl_ret;

	if ((ssl_ret = SSL_do_handshake(ssl)) == 1) {
		fprintf(stderr, "INFO: %s handshake done\n", name);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "handshake", ssl_ret);
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

static int
quictest(void)
{
	struct quic_data *client_data = NULL, *server_data = NULL;
	BIO *client_wbio = NULL, *server_wbio = NULL;
	SSL *client = NULL, *server = NULL;
	int failed = 1;

	if ((client_wbio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (BIO_set_mem_eof_return(client_wbio, -1) <= 0)
		goto failure;

	if ((server_wbio = BIO_new(BIO_s_mem())) == NULL)
		goto failure;
	if (BIO_set_mem_eof_return(server_wbio, -1) <= 0)
		goto failure;

	if ((client_data = calloc(1, sizeof(*client_data))) == NULL)
		goto failure;

	client_data->rbio = server_wbio;
	client_data->wbio = client_wbio;

	if ((client = quic_client(client_data)) == NULL)
		goto failure;

	if ((server_data = calloc(1, sizeof(*server_data))) == NULL)
		goto failure;

	server_data->rbio = client_wbio;
	server_data->wbio = server_wbio;

	if ((server = quic_server(server_data)) == NULL)
		goto failure;

	if (!do_client_server_loop(client, do_handshake, server, do_handshake)) {
		fprintf(stderr, "FAIL: client and server handshake failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	fprintf(stderr, "INFO: Done!\n");

	failed = 0;

 failure:
	BIO_free(client_wbio);
	BIO_free(server_wbio);

	free(client_data);
	free(server_data);

	SSL_free(client);
	SSL_free(server);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s keyfile certfile cafile\n",
		    argv[0]);
		exit(1);
	}

	server_key_file = argv[1];
	server_cert_file = argv[2];
	server_ca_file = argv[3];

	failed |= quictest();

	return failed;
}
