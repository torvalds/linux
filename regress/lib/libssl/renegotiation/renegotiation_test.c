/* $OpenBSD: renegotiation_test.c,v 1.3 2025/03/12 14:07:35 jsing Exp $ */
/*
 * Copyright (c) 2020,2025 Joel Sing <jsing@openbsd.org>
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

int tls_client_alert;
int tls_server_alert;

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
		if (tls_client_alert >> 8 == SSL3_AL_FATAL ||
		    tls_server_alert >> 8 == SSL3_AL_FATAL) {
			ERR_clear_error();
			return 0;
		}
		if (tls_client_alert >> 8 == SSL3_AL_WARNING ||
		    tls_server_alert >> 8 == SSL3_AL_WARNING) {
			ERR_clear_error();
			return 1;
		}
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

struct tls_reneg_test {
	const unsigned char *desc;
	int ssl_max_proto_version;
	long ssl_client_options;
	long ssl_server_options;
	int renegotiate_client;
	int renegotiate_server;
	int client_ignored;
	int want_client_alert;
	int want_server_alert;
	int want_failure;
};

static const struct tls_reneg_test tls_reneg_tests[] = {
	{
		.desc = "TLSv1.2 - Renegotiation permitted, no renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
	},
	{
		.desc = "TLSv1.2 - Renegotiation permitted, server initiated "
		    "renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.renegotiate_server = 1,
	},
	{
		.desc = "TLSv1.2 - Renegotiation permitted, client initiated "
		    "renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.renegotiate_client = 1,
	},
	{
		.desc = "TLSv1.2 - Renegotiation permitted, server and client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.renegotiate_client = 1,
		.renegotiate_server = 1,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation not permitted, server "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_NO_CLIENT_RENEGOTIATION,
		.renegotiate_server = 1,
		.want_client_alert = SSL3_AL_FATAL << 8 | SSL_AD_NO_RENEGOTIATION,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation not permitted, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_NO_CLIENT_RENEGOTIATION,
		.renegotiate_client = 1,
		.want_client_alert = SSL3_AL_FATAL << 8 | SSL_AD_NO_RENEGOTIATION,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation not permitted, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_NO_RENEGOTIATION,
		.renegotiate_client = 1,
		.want_client_alert = SSL3_AL_FATAL << 8 | SSL_AD_NO_RENEGOTIATION,
	},
	{
		.desc = "TLSv1.2 - Server renegotiation not permitted, server "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_client_options = SSL_OP_NO_RENEGOTIATION,
		.renegotiate_server = 1,
		.client_ignored = 1,
		.want_server_alert = SSL3_AL_WARNING << 8 | SSL_AD_NO_RENEGOTIATION,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation permitted, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_NO_RENEGOTIATION |
		    SSL_OP_ALLOW_CLIENT_RENEGOTIATION,
		.renegotiate_client = 1,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation permitted, server "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_ALLOW_CLIENT_RENEGOTIATION,
		.renegotiate_server = 1,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation permitted, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_ALLOW_CLIENT_RENEGOTIATION,
		.renegotiate_client = 1,
	},
	{
		.desc = "TLSv1.2 - Client renegotiation disabled, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_client_options = SSL_OP_NO_RENEGOTIATION,
		.renegotiate_client = 1,
		.want_failure = 1,
	},
	{
		.desc = "TLSv1.2 - Server renegotiation disabled, server "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_2_VERSION,
		.ssl_server_options = SSL_OP_NO_RENEGOTIATION,
		.renegotiate_server = 1,
		.want_failure = 1,
	},
	{
		.desc = "TLSv1.3 - No renegotiation supported, no renegotiation",
		.ssl_max_proto_version = TLS1_3_VERSION,
	},
	{
		.desc = "TLSv1.3 - No renegotiation supported, server "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_3_VERSION,
		.renegotiate_server = 1,
		.want_failure = 1,
	},
	{
		.desc = "TLSv1.3 - No renegotiation supported, client "
		    "initiated renegotiation",
		.ssl_max_proto_version = TLS1_3_VERSION,
		.renegotiate_client = 1,
		.want_failure = 1,
	},
};

#define N_TLS_RENEG_TESTS (sizeof(tls_reneg_tests) / sizeof(*tls_reneg_tests))

static void
tls_client_info_callback(const SSL *ssl, int where, int value)
{
	if (where == SSL_CB_READ_ALERT) {
		fprintf(stderr, "INFO: client read %s alert - %s\n",
		    SSL_alert_type_string_long(value),
		    SSL_alert_desc_string_long(value));
		tls_client_alert = value;
	}
}

static void
tls_server_info_callback(const SSL *ssl, int where, int value)
{
	if (where == SSL_CB_READ_ALERT) {
		fprintf(stderr, "INFO: server read %s alert - %s\n",
		    SSL_alert_type_string_long(value),
		    SSL_alert_desc_string_long(value));
		tls_server_alert = value;
	}
}

static int
tls_check_reneg(SSL *client, SSL *server, int client_pending,
    int server_pending, long client_num_reneg, long server_num_reneg)
{
	if (debug) {
		fprintf(stderr, "DEBUG: client - pending = %d, num reneg = %ld\n",
		    SSL_renegotiate_pending(client), SSL_num_renegotiations(client));
		fprintf(stderr, "DEBUG: server - pending = %d, num reneg = %ld\n",
		    SSL_renegotiate_pending(server), SSL_num_renegotiations(server));
	}

	if (SSL_renegotiate_pending(client) != client_pending) {
		fprintf(stderr, "FAIL: client SSL_renegotiate_pending() = %d, want %d\n",
		    SSL_renegotiate_pending(client), client_pending);
		return 0;
	}
	if (SSL_renegotiate_pending(server) != server_pending) {
		fprintf(stderr, "FAIL: server SSL_renegotiate_pending() = %d, want %d\n",
		    SSL_renegotiate_pending(server), server_pending);
		return 0;
	}
	if (SSL_num_renegotiations(client) != client_num_reneg) {
		fprintf(stderr, "FAIL: client SSL_num_renegotiations() = %ld, want %ld\n",
		    SSL_num_renegotiations(client), client_num_reneg);
		return 0;
	}
	if (SSL_num_renegotiations(server) != server_num_reneg) {
		fprintf(stderr, "FAIL: server SSL_num_renegotiations() = %ld, want %ld\n",
		    SSL_num_renegotiations(server), server_num_reneg);
		return 0;
	}
	return 1;
}

static int
tls_reneg_test(const struct tls_reneg_test *trt)
{
	BIO *client_wbio = NULL, *server_wbio = NULL;
	SSL *client = NULL, *server = NULL;
	int failed = 1;

	fprintf(stderr, "\n== Testing %s... ==\n", trt->desc);

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

	SSL_set_options(client, trt->ssl_client_options);
	SSL_set_info_callback(client, tls_client_info_callback);

	if ((server = tls_server(client_wbio, server_wbio)) == NULL)
		goto failure;

	SSL_set_options(server, trt->ssl_server_options);
	SSL_set_info_callback(server, tls_server_info_callback);

	if (!SSL_set_max_proto_version(server, trt->ssl_max_proto_version))
		goto failure;

	tls_client_alert = 0;
	tls_server_alert = 0;

	if (!do_client_server_loop(client, do_connect, server, do_accept)) {
		fprintf(stderr, "FAIL: client and server handshake failed\n");
		goto failure;
	}

	if (!do_client_server_loop(client, do_write, server, do_read)) {
		fprintf(stderr, "FAIL: client write and server read failed\n");
		goto failure;
	}

	if (!do_client_server_loop(client, do_read, server, do_write)) {
		fprintf(stderr, "FAIL: client read and server write failed\n");
		goto failure;
	}

	if (!tls_check_reneg(client, server, 0, 0, 0, 0))
		goto failure;

	if (trt->renegotiate_server) {
		/*
		 * Trigger renegotiation from the server - this results in the
		 * server sending a HelloRequest, then waiting for the client to
		 * respond with a ClientHello.
		 */
		if (!SSL_renegotiate(server)) {
			if (!trt->want_failure) {
				fprintf(stderr, "FAIL: server renegotiation failed\n");
				goto failure;
			}
			goto done;
		}
		if (trt->want_failure) {
			fprintf(stderr, "FAIL: server renegotiation should have failed\n");
			goto failure;
		}

		if (!tls_check_reneg(client, server, 0, 1, 0, 0))
			goto failure;

		if (!do_client_server_loop(client, do_read, server, do_write)) {
			fprintf(stderr, "FAIL: client read and server write failed\n");
			goto failure;
		}

		if (!tls_check_reneg(client, server, (trt->client_ignored == 0), 1,
		    (trt->client_ignored == 0), 1))
			goto failure;

		if (!do_client_server_loop(client, do_write, server, do_read)) {
			if (!trt->want_client_alert && !trt->want_server_alert) {
				fprintf(stderr, "FAIL: client write and server read failed\n");
				goto failure;
			}
			if (tls_client_alert != trt->want_client_alert) {
				fprintf(stderr, "FAIL: client alert = %x, want %x\n",
				    tls_client_alert, trt->want_client_alert);
				goto failure;
			}
			if (tls_server_alert != trt->want_server_alert) {
				fprintf(stderr, "FAIL: server alert = %x, want %x\n",
				    tls_server_alert, trt->want_server_alert);
				goto failure;
			}
			goto done;
		}
		if (tls_client_alert != trt->want_client_alert) {
			fprintf(stderr, "FAIL: client alert = %x, want %x\n",
			    tls_client_alert, trt->want_client_alert);
			goto failure;
		}
		if (tls_server_alert != trt->want_server_alert) {
			fprintf(stderr, "FAIL: server alert = %x, want %x\n",
			    tls_server_alert, trt->want_server_alert);
			goto failure;
		}

		if (!tls_check_reneg(client, server, 0, (trt->client_ignored != 0),
		    (trt->client_ignored == 0), 1))
			goto failure;
	}

	SSL_clear_num_renegotiations(client);
	SSL_clear_num_renegotiations(server);

	tls_client_alert = 0;
	tls_server_alert = 0;

	if (trt->renegotiate_client) {
		/*
		 * Trigger renegotiation from the client - this results in the
		 * client sending a ClientHello.
		 */
		if (!SSL_renegotiate(client)) {
			if (!trt->want_failure) {
				fprintf(stderr, "FAIL: client renegotiation failed\n");
				goto failure;
			}
			goto done;
		}
		if (trt->want_failure) {
			fprintf(stderr, "FAIL: client renegotiation should have failed\n");
			goto failure;
		}

		if (!tls_check_reneg(client, server, 1, 0, 0, 0))
			goto failure;

		if (!do_client_server_loop(client, do_read, server, do_write)) {
			fprintf(stderr, "FAIL: client read and server write failed\n");
			goto failure;
		}

		if (!tls_check_reneg(client, server, 1, 0, 1, 0))
			goto failure;

		if (!do_client_server_loop(client, do_write, server, do_read)) {
			if (!trt->want_client_alert && !trt->want_server_alert) {
				fprintf(stderr, "FAIL: client write and server read failed\n");
				goto failure;
			}
			if (tls_client_alert != trt->want_client_alert) {
				fprintf(stderr, "FAIL: client alert = %x, want %x\n",
				    tls_client_alert, trt->want_client_alert);
				goto failure;
			}
			if (tls_server_alert != trt->want_server_alert) {
				fprintf(stderr, "FAIL: server alert = %x, want %x\n",
				    tls_server_alert, trt->want_server_alert);
				goto failure;
			}
			goto done;
		}
		if (tls_client_alert != trt->want_client_alert) {
			fprintf(stderr, "FAIL: client alert = %x, want %x\n",
			    tls_client_alert, trt->want_client_alert);
			goto failure;
		}
		if (tls_server_alert != trt->want_server_alert) {
			fprintf(stderr, "FAIL: server alert = %x, want %x\n",
			    tls_server_alert, trt->want_server_alert);
			goto failure;
		}

		if (!tls_check_reneg(client, server, 0, 0, 1, 0))
			goto failure;
	}

	if (!do_client_server_loop(client, do_shutdown, server, do_shutdown)) {
		fprintf(stderr, "FAIL: client and server shutdown failed\n");
		goto failure;
	}

 done:
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

	for (i = 0; i < N_TLS_RENEG_TESTS; i++)
		failed |= tls_reneg_test(&tls_reneg_tests[i]);

	return failed;
}
