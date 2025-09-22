/* $OpenBSD: apitest.c,v 1.3 2024/09/07 16:39:29 tb Exp $ */
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

#ifndef CERTSDIR
#define CERTSDIR "."
#endif

const char *certs_path = CERTSDIR;

int debug = 0;

static int
ssl_ctx_use_ca_file(SSL_CTX *ssl_ctx, const char *ca_file)
{
	char *ca_path = NULL;
	int ret = 0;

	if (asprintf(&ca_path, "%s/%s", certs_path, ca_file) == -1)
		goto err;
	if (!SSL_CTX_load_verify_locations(ssl_ctx, ca_path, NULL)) {
		fprintf(stderr, "load_verify_locations(%s) failed\n", ca_path);
		goto err;
	}

	ret = 1;

 err:
	free(ca_path);

	return ret;
}

static int
ssl_ctx_use_keypair(SSL_CTX *ssl_ctx, const char *chain_file,
    const char *key_file)
{
	char *chain_path = NULL, *key_path = NULL;
	int ret = 0;

	if (asprintf(&chain_path, "%s/%s", certs_path, chain_file) == -1)
		goto err;
	if (SSL_CTX_use_certificate_chain_file(ssl_ctx, chain_path) != 1) {
		fprintf(stderr, "FAIL: Failed to load certificates\n");
		goto err;
	}
	if (asprintf(&key_path, "%s/%s", certs_path, key_file) == -1)
		goto err;
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load private key\n");
		goto err;
	}

	ret = 1;

 err:
	free(chain_path);
	free(key_path);

	return ret;
}

static SSL *
tls_client(BIO *rbio, BIO *wbio)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;

	if ((ssl_ctx = SSL_CTX_new(TLS_method())) == NULL)
		errx(1, "client context");

	SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);

	if (!ssl_ctx_use_ca_file(ssl_ctx, "ca-root-rsa.pem"))
		goto failure;
	if (!ssl_ctx_use_keypair(ssl_ctx, "client1-rsa-chain.pem",
	    "client1-rsa.pem"))
		goto failure;

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "client ssl");

	BIO_up_ref(rbio);
	BIO_up_ref(wbio);

	SSL_set_bio(ssl, rbio, wbio);

 failure:
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

	SSL_CTX_set_verify(ssl_ctx,
	    SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

	if (!ssl_ctx_use_ca_file(ssl_ctx, "ca-root-rsa.pem"))
		goto failure;
	if (!ssl_ctx_use_keypair(ssl_ctx, "server1-rsa-chain.pem",
	    "server1-rsa.pem"))
		goto failure;

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
ssl_get_peer_cert_chain_test(uint16_t tls_version)
{
	STACK_OF(X509) *peer_chain;
	X509 *peer_cert;
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

	if ((client = tls_client(server_wbio, client_wbio)) == NULL)
		goto failure;
	if (tls_version != 0) {
		if (!SSL_set_min_proto_version(client, tls_version))
			goto failure;
		if (!SSL_set_max_proto_version(client, tls_version))
			goto failure;
	}

	if ((server = tls_server(client_wbio, server_wbio)) == NULL)
		goto failure;
	if (tls_version != 0) {
		if (!SSL_set_min_proto_version(server, tls_version))
			goto failure;
		if (!SSL_set_max_proto_version(server, tls_version))
			goto failure;
	}

	if (!do_client_server_loop(client, do_connect, server, do_accept)) {
		fprintf(stderr, "FAIL: client and server handshake failed\n");
		goto failure;
	}

	if (tls_version != 0) {
		if (SSL_version(client) != tls_version) {
			fprintf(stderr, "FAIL: client got TLS version %x, "
			    "want %x\n", SSL_version(client), tls_version);
			goto failure;
		}
		if (SSL_version(server) != tls_version) {
			fprintf(stderr, "FAIL: server got TLS version %x, "
			    "want %x\n", SSL_version(server), tls_version);
			goto failure;
		}
	}

	/*
	 * Due to the wonders of API inconsistency, SSL_get_peer_cert_chain()
	 * includes the peer's leaf certificate when called by the client,
	 * however it does not when called by the server. Furthermore, the
	 * certificate returned by SSL_get_peer_certificate() has already
	 * had its reference count incremented and must be freed, where as
	 * the certificates returned from SSL_get_peer_cert_chain() must
	 * not be freed... *sigh*
	 */
	peer_cert = SSL_get_peer_certificate(client);
	peer_chain = SSL_get_peer_cert_chain(client);
	X509_free(peer_cert);

	if (peer_cert == NULL) {
		fprintf(stderr, "FAIL: client got no peer cert\n");
		goto failure;
	}
	if (sk_X509_num(peer_chain) != 2) {
		fprintf(stderr, "FAIL: client got peer cert chain with %d "
		    "certificates, want 2\n", sk_X509_num(peer_chain));
		goto failure;
	}
	if (X509_cmp(peer_cert, sk_X509_value(peer_chain, 0)) != 0) {
		fprintf(stderr, "FAIL: client got peer cert chain without peer "
		    "certificate\n");
		goto failure;
	}

	peer_cert = SSL_get_peer_certificate(server);
	peer_chain = SSL_get_peer_cert_chain(server);
	X509_free(peer_cert);

	if (peer_cert == NULL) {
		fprintf(stderr, "FAIL: server got no peer cert\n");
		goto failure;
	}
	if (sk_X509_num(peer_chain) != 1) {
		fprintf(stderr, "FAIL: server got peer cert chain with %d "
		    "certificates, want 1\n", sk_X509_num(peer_chain));
		goto failure;
	}
	if (X509_cmp(peer_cert, sk_X509_value(peer_chain, 0)) == 0) {
		fprintf(stderr, "FAIL: server got peer cert chain with peer "
		    "certificate\n");
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

static int
ssl_get_peer_cert_chain_tests(void)
{
	int failed = 0;

	fprintf(stderr, "\n== Testing SSL_get_peer_cert_chain()... ==\n");

	failed |= ssl_get_peer_cert_chain_test(0);
	failed |= ssl_get_peer_cert_chain_test(TLS1_3_VERSION);
	failed |= ssl_get_peer_cert_chain_test(TLS1_2_VERSION);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [certspath]\n", argv[0]);
		exit(1);
	}
	if (argc == 2)
		certs_path = argv[1];

	failed |= ssl_get_peer_cert_chain_tests();

	return failed;
}
