/* $OpenBSD: shutdowntest.c,v 1.3 2024/01/30 14:46:46 jsing Exp $ */
/*
 * Copyright (c) 2020, 2021, 2024 Joel Sing <jsing@openbsd.org>
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
		return 1;
	} else {
		fprintf(stderr, "FAIL: %s %s failed - ssl err = %d, errno = %d\n",
		    name, desc, ssl_err, errno);
		ERR_print_errors_fp(stderr);
		return 0;
	}
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

	/* The astounding EOF condition. */
	if (ssl_ret == -1 &&
	    SSL_get_error(ssl, ssl_ret) == SSL_ERROR_SYSCALL && errno == 0) {
		fprintf(stderr, "INFO: %s shutdown encountered EOF\n", name);
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

static int
do_shutdown_loop(SSL *client, SSL *server)
{
	int client_done = 0, server_done = 0;
	int i = 0;

	do {
		if (!client_done) {
			if (debug)
				fprintf(stderr, "DEBUG: client loop\n");
			if (!do_shutdown(client, "client", &client_done))
				return 0;
			if (client_done)
				BIO_set_mem_eof_return(SSL_get_wbio(client), 0);
		}
		if (!server_done) {
			if (debug)
				fprintf(stderr, "DEBUG: server loop\n");
			if (!do_shutdown(server, "server", &server_done))
				return 0;
			if (server_done)
				BIO_set_mem_eof_return(SSL_get_wbio(server), 0);
		}
	} while (i++ < 100 && (!client_done || !server_done));

	if (!client_done || !server_done)
		fprintf(stderr, "FAIL: gave up\n");

	return client_done && server_done;
}

static void
ssl_msg_callback(int is_write, int version, int content_type, const void *buf,
    size_t len, SSL *ssl, void *arg)
{
	const uint8_t *msg = buf;
	int *close_notify = arg;

	if (is_write || content_type != SSL3_RT_ALERT)
		return;
	if (len == 2 && msg[0] == SSL3_AL_WARNING && msg[1] == SSL_AD_CLOSE_NOTIFY)
		*close_notify = 1;
}

struct shutdown_test {
	const unsigned char *desc;
	int client_quiet_shutdown;
	int client_set_shutdown;
	int want_client_shutdown;
	int want_client_close_notify;
	int server_quiet_shutdown;
	int server_set_shutdown;
	int want_server_shutdown;
	int want_server_close_notify;
};

static const struct shutdown_test shutdown_tests[] = {
	{
		.desc = "bidirectional shutdown",
		.want_client_close_notify = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_close_notify = 1,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "client quiet shutdown",
		.client_quiet_shutdown = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN,
	},
	{
		.desc = "server quiet shutdown",
		.server_quiet_shutdown = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "both quiet shutdown",
		.client_quiet_shutdown = 1,
		.server_quiet_shutdown = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "client set sent shutdown",
		.client_set_shutdown = SSL_SENT_SHUTDOWN,
		.want_client_close_notify = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN,
	},
	{
		.desc = "client set received shutdown",
		.client_set_shutdown = SSL_RECEIVED_SHUTDOWN,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_close_notify = 1,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "client set sent/received shutdown",
		.client_set_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN,
	},
	{
		.desc = "server set sent shutdown",
		.server_set_shutdown = SSL_SENT_SHUTDOWN,
		.want_client_shutdown = SSL_SENT_SHUTDOWN,
		.want_server_close_notify = 1,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "server set received shutdown",
		.server_set_shutdown = SSL_RECEIVED_SHUTDOWN,
		.want_client_close_notify = 1,
		.want_client_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
	{
		.desc = "server set sent/received shutdown",
		.server_set_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
		.want_client_shutdown = SSL_SENT_SHUTDOWN,
		.want_server_shutdown = SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN,
	},
};

#define N_TLS_TESTS (sizeof(shutdown_tests) / sizeof(*shutdown_tests))

static int
shutdown_test(uint16_t ssl_version, const char *ssl_version_name,
    const struct shutdown_test *st)
{
	BIO *client_wbio = NULL, *server_wbio = NULL;
	SSL *client = NULL, *server = NULL;
	int client_close_notify = 0, server_close_notify = 0;
	int shutdown, ssl_err;
	int failed = 1;

	fprintf(stderr, "\n== Testing %s, %s... ==\n", ssl_version_name,
	    st->desc);

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
	if (!SSL_set_min_proto_version(client, ssl_version))
		goto failure;
	if (!SSL_set_max_proto_version(client, ssl_version))
		goto failure;

	if ((server = tls_server(client_wbio, server_wbio)) == NULL)
		goto failure;
	if (!SSL_set_min_proto_version(server, ssl_version))
		goto failure;
	if (!SSL_set_max_proto_version(server, ssl_version))
		goto failure;

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

	/* Seemingly this is the only way to find out about alerts... */
	SSL_set_msg_callback(client, ssl_msg_callback);
	SSL_set_msg_callback_arg(client, &client_close_notify);
	SSL_set_msg_callback(server, ssl_msg_callback);
	SSL_set_msg_callback_arg(server, &server_close_notify);

	SSL_set_shutdown(client, st->client_set_shutdown);
	SSL_set_shutdown(server, st->server_set_shutdown);

	SSL_set_quiet_shutdown(client, st->client_quiet_shutdown);
	SSL_set_quiet_shutdown(server, st->server_quiet_shutdown);

	if (!do_shutdown_loop(client, server)) {
		fprintf(stderr, "FAIL: client and server shutdown failed\n");
		goto failure;
	}

	if ((shutdown = SSL_get_shutdown(client)) != st->want_client_shutdown) {
		fprintf(stderr, "FAIL: client shutdown flags = %x, want %x\n",
		    shutdown, st->want_client_shutdown);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(server)) != st->want_server_shutdown) {
		fprintf(stderr, "FAIL: server shutdown flags = %x, want %x\n",
		    shutdown, st->want_server_shutdown);
		goto failure;
	}

	if (client_close_notify != st->want_client_close_notify) {
		fprintf(stderr, "FAIL: client close notify = %d, want %d\n",
		    client_close_notify, st->want_client_close_notify);
		goto failure;
	}
	if (server_close_notify != st->want_server_close_notify) {
		fprintf(stderr, "FAIL: server close notify = %d, want %d\n",
		    server_close_notify, st->want_server_close_notify);
		goto failure;
	}

	if (st->want_client_close_notify) {
		if ((ssl_err = SSL_get_error(client, 0)) != SSL_ERROR_ZERO_RETURN) {
			fprintf(stderr, "FAIL: client ssl error = %d, want %d\n",
			    ssl_err, SSL_ERROR_ZERO_RETURN);
			goto failure;
		}
	}
	if (st->want_server_close_notify) {
		if ((ssl_err = SSL_get_error(server, 0)) != SSL_ERROR_ZERO_RETURN) {
			fprintf(stderr, "FAIL: server ssl error = %d, want %d\n",
			    ssl_err, SSL_ERROR_ZERO_RETURN);
			goto failure;
		}
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

static int
shutdown_sequence_test(uint16_t ssl_version, const char *ssl_version_name)
{
	BIO *client_wbio = NULL, *server_wbio = NULL;
	SSL *client = NULL, *server = NULL;
	int shutdown, ret;
	int failed = 1;

	fprintf(stderr, "\n== Testing %s, shutdown sequence... ==\n",
	    ssl_version_name);

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
	if (!SSL_set_min_proto_version(client, ssl_version))
		goto failure;
	if (!SSL_set_max_proto_version(client, ssl_version))
		goto failure;

	if ((server = tls_server(client_wbio, server_wbio)) == NULL)
		goto failure;
	if (!SSL_set_min_proto_version(server, ssl_version))
		goto failure;
	if (!SSL_set_max_proto_version(server, ssl_version))
		goto failure;

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

	/*
	 * Shutdown in lock step and check return value and shutdown flags.
	 *
	 * It is not documented, however some software relies on SSL_shutdown()
	 * to only send a close-notify on the first call, then indicate that a
	 * close-notify was received on a second (or later) call.
	 */

	if ((shutdown = SSL_get_shutdown(client)) != 0) {
		fprintf(stderr, "FAIL: client shutdown flags = %x, want %x\n",
		    shutdown, 0);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(server)) != 0) {
		fprintf(stderr, "FAIL: server shutdown flags = %x, want %x\n",
		    shutdown, 0);
		goto failure;
	}

	if ((ret = SSL_shutdown(client)) != 0) {
		fprintf(stderr, "FAIL: client SSL_shutdown() = %d, want %d\n",
		    ret, 0);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(client)) != SSL_SENT_SHUTDOWN) {
		fprintf(stderr, "FAIL: client shutdown flags = %x, want %x\n",
		    shutdown, SSL_SENT_SHUTDOWN);
		goto failure;
	}

	if ((ret = SSL_shutdown(server)) != 0) {
		fprintf(stderr, "FAIL: server SSL_shutdown() = %d, want %d\n",
		    ret, 0);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(server)) != SSL_SENT_SHUTDOWN) {
		fprintf(stderr, "FAIL: server shutdown flags = %x, want %x\n",
		    shutdown, SSL_SENT_SHUTDOWN);
		goto failure;
	}

	if ((ret = SSL_shutdown(client)) != 1) {
		fprintf(stderr, "FAIL: client SSL_shutdown() = %d, want %d\n",
		    ret, 0);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(client)) !=
	    (SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN)) {
		fprintf(stderr, "FAIL: client shutdown flags = %x, want %x\n",
		    shutdown, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
		goto failure;
	}

	if ((ret = SSL_shutdown(server)) != 1) {
		fprintf(stderr, "FAIL: server SSL_shutdown() = %d, want %d\n",
		    ret, 0);
		goto failure;
	}
	if ((shutdown = SSL_get_shutdown(server)) !=
	    (SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN)) {
		fprintf(stderr, "FAIL: server shutdown flags = %x, want %x\n",
		    shutdown, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
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

struct ssl_version {
	uint16_t version;
	const char *name;
};

struct ssl_version ssl_versions[] = {
	{
		.version = TLS1_2_VERSION,
		.name = SSL_TXT_TLSV1_2,
	},
	{
		.version = TLS1_3_VERSION,
		.name = SSL_TXT_TLSV1_3,
	},
};

#define N_SSL_VERSIONS (sizeof(ssl_versions) / sizeof(*ssl_versions))

int
main(int argc, char **argv)
{
	const struct ssl_version *sv;
	int failed = 0;
	size_t i, j;

	if (argc != 4) {
		fprintf(stderr, "usage: %s keyfile certfile cafile\n",
		    argv[0]);
		exit(1);
	}

	server_key_file = argv[1];
	server_cert_file = argv[2];
	server_ca_file = argv[3];

	for (i = 0; i < N_SSL_VERSIONS; i++) {
		sv = &ssl_versions[i];
		for (j = 0; j < N_TLS_TESTS; j++) {
			failed |= shutdown_test(sv->version, sv->name,
			    &shutdown_tests[j]);
		}
		failed |= shutdown_sequence_test(sv->version, sv->name);
	}

	return failed;
}
