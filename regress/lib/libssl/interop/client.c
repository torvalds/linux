/*	$OpenBSD: client.c,v 1.11 2022/07/07 13:12:57 tb Exp $	*/
/*
 * Copyright (c) 2018-2019 Alexander Bluhm <bluhm@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "util.h"

void __dead usage(void);

void __dead
usage(void)
{
	fprintf(stderr, "usage: client [-Lsv] [-C CA] [-c crt -k key] "
	    "[-l ciphers] [-V version] host port\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	const SSL_METHOD *method;
	SSL_CTX *ctx;
	SSL *ssl;
	BIO *bio;
	SSL_SESSION *session = NULL;
	int ch, error, listciphers = 0, sessionreuse = 0, verify = 0;
	int version = 0;
	char buf[256];
	char *ca = NULL, *crt = NULL, *key = NULL, *ciphers = NULL;
	char *host_port, *host = "127.0.0.1", *port = "0";

	while ((ch = getopt(argc, argv, "C:c:k:Ll:p:sV:v")) != -1) {
		switch (ch) {
		case 'C':
			ca = optarg;
			break;
		case 'c':
			crt = optarg;
			break;
		case 'k':
			key = optarg;
			break;
		case 'L':
			listciphers = 1;
			break;
		case 'l':
			ciphers = optarg;
			break;
		case 's':
			/* multiple reueses are possible */
			sessionreuse++;
			break;
		case 'V':
			if (strcmp(optarg, "TLS1") == 0) {
				version = TLS1_VERSION;
			} else if (strcmp(optarg, "TLS1_1") == 0) {
				version = TLS1_1_VERSION;
			} else if (strcmp(optarg, "TLS1_2") == 0) {
				version = TLS1_2_VERSION;
#ifdef TLS1_3_VERSION
			} else if (strcmp(optarg, "TLS1_3") == 0) {
				version = TLS1_3_VERSION;
#endif
			} else {
				errx(1, "unknown protocol version: %s", optarg);
			}
			break;
		case 'v':
			verify = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 2) {
		host = argv[0];
		port = argv[1];
	} else if (!listciphers) {
		usage();
	}
	if (asprintf(&host_port, strchr(host, ':') ? "[%s]:%s" : "%s:%s",
	    host, port) == -1)
		err(1, "asprintf host port");
	if ((crt == NULL && key != NULL) || (crt != NULL && key == NULL))
		errx(1, "certificate and private key must be used together");

	SSL_library_init();
	SSL_load_error_strings();
	print_version();

	/* setup method and context */
#if OPENSSL_VERSION_NUMBER >= 0x1010000f
	method = TLS_client_method();
	if (method == NULL)
		err_ssl(1, "TLS_client_method");
#else
	switch (version) {
	case TLS1_VERSION:
		method = TLSv1_client_method();
		break;
	case TLS1_1_VERSION:
		method = TLSv1_1_client_method();
		break;
	case TLS1_2_VERSION:
		method = TLSv1_2_client_method();
		break;
#ifdef TLS1_3_VERSION
	case TLS1_3_VERSION:
		err(1, "TLS1_3 not supported");
#endif
	default:
		method = SSLv23_client_method();
		break;
	}
	if (method == NULL)
		err_ssl(1, "SSLv23_client_method");
#endif
	ctx = SSL_CTX_new(method);
	if (ctx == NULL)
		err_ssl(1, "SSL_CTX_new");

#if OPENSSL_VERSION_NUMBER >= 0x1010000f
	if (version) {
		if (SSL_CTX_set_min_proto_version(ctx, version) != 1)
			err_ssl(1, "SSL_CTX_set_min_proto_version");
		if (SSL_CTX_set_max_proto_version(ctx, version) != 1)
			err_ssl(1, "SSL_CTX_set_max_proto_version");
	}
#endif

	/* load client certificate */
	if (crt != NULL) {
		if (SSL_CTX_use_certificate_file(ctx, crt,
		    SSL_FILETYPE_PEM) <= 0)
			err_ssl(1, "SSL_CTX_use_certificate_file");
		if (SSL_CTX_use_PrivateKey_file(ctx, key,
		    SSL_FILETYPE_PEM) <= 0)
			err_ssl(1, "SSL_CTX_use_PrivateKey_file");
		if (SSL_CTX_check_private_key(ctx) <= 0)
			err_ssl(1, "SSL_CTX_check_private_key");
	}

	/* verify server certificate */
	if (ca != NULL) {
		if (SSL_CTX_load_verify_locations(ctx, ca, NULL) <= 0)
			err_ssl(1, "SSL_CTX_load_verify_locations");
	}
	SSL_CTX_set_verify(ctx, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
	    verify_callback);

	if (sessionreuse) {
		SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
	}

	if (ciphers) {
		if (SSL_CTX_set_cipher_list(ctx, ciphers) <= 0)
			err_ssl(1, "SSL_CTX_set_cipher_list");
	}

	if (listciphers) {
		STACK_OF(SSL_CIPHER) *supported_ciphers;

#if OPENSSL_VERSION_NUMBER < 0x1010000f
#define SSL_get1_supported_ciphers SSL_get_ciphers
#endif
		ssl = SSL_new(ctx);
		if (ssl == NULL)
			err_ssl(1, "SSL_new");
		supported_ciphers = SSL_get1_supported_ciphers(ssl);
		if (supported_ciphers == NULL)
			err_ssl(1, "SSL_get1_supported_ciphers");
		print_ciphers(supported_ciphers);

#if OPENSSL_VERSION_NUMBER >= 0x1010000f
		sk_SSL_CIPHER_free(supported_ciphers);
#endif
		return 0;
	}

	do {
		/* setup bio for socket operations */
		bio = BIO_new_connect(host_port);
		if (bio == NULL)
			err_ssl(1, "BIO_new_connect");

		/* connect */
		if (BIO_do_connect(bio) <= 0)
			err_ssl(1, "BIO_do_connect");
		printf("connect ");
		print_sockname(bio);
		printf("connect ");
		print_peername(bio);

		/* do ssl client handshake */
		ssl = SSL_new(ctx);
		if (ssl == NULL)
			err_ssl(1, "SSL_new");
		SSL_set_bio(ssl, bio, bio);
		/* resuse session if possible */
		if (session != NULL) {
			if (SSL_set_session(ssl, session) <= 0)
				err_ssl(1, "SSL_set_session");
		}
		if ((error = SSL_connect(ssl)) <= 0)
			err_ssl(1, "SSL_connect %d", error);
		printf("session %d: %s\n", sessionreuse,
		    SSL_session_reused(ssl) ? "reuse" : "new");
		if (fflush(stdout) != 0)
			err(1, "fflush stdout");

		/* print session statistics */
		if (sessionreuse) {
			session = SSL_get1_session(ssl);
			if (session == NULL)
				err_ssl(1, "SSL1_get_session");
		} else {
			session = SSL_get_session(ssl);
			if (session == NULL)
				err_ssl(1, "SSL_get_session");
		}
		if (SSL_SESSION_print_fp(stdout, session) <= 0)
			err_ssl(1, "SSL_SESSION_print_fp");

		/* read server greeting and write client hello over TLS */
		if ((error = SSL_read(ssl, buf, 9)) <= 0)
			err_ssl(1, "SSL_read %d", error);
		if (error != 9)
			errx(1, "read not 9 bytes greeting: %d", error);
		buf[9] = '\0';
		printf("<<< %s", buf);
		if (fflush(stdout) != 0)
			err(1, "fflush stdout");
		strlcpy(buf, "hello\n", sizeof(buf));
		printf(">>> %s", buf);
		if (fflush(stdout) != 0)
			err(1, "fflush stdout");
		if ((error = SSL_write(ssl, buf, 6)) <= 0)
			err_ssl(1, "SSL_write %d", error);
		if (error != 6)
			errx(1, "write not 6 bytes hello: %d", error);

		/* shutdown connection */
		if ((error = SSL_shutdown(ssl)) < 0)
			err_ssl(1, "SSL_shutdown unidirectional %d", error);
		if (error <= 0) {
			if ((error = SSL_shutdown(ssl)) <= 0)
				err_ssl(1, "SSL_shutdown bidirectional %d",
				    error);
		}

		SSL_free(ssl);
	} while (sessionreuse--);

	SSL_CTX_free(ctx);

	printf("success\n");

	return 0;
}
