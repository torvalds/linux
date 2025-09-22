/*	$OpenBSD: ocsp_test.c,v 1.7 2023/07/07 19:54:36 bcook Exp $	*/
/*
 * Copyright (c) 2016 Bob Beck <beck@openbsd.org>
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
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/ocsp.h>

static int
tcp_connect(char *host, char *port)
{
	int error, sd = -1;
	struct addrinfo hints, *res, *r;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (BIO_sock_init() != 1) {
		perror("BIO_sock_init()");
		exit(-1);
	}

	error = getaddrinfo(host, port, &hints, &res);
	if (error != 0) {
		perror("getaddrinfo()");
		exit(-1);
	}

	for (r = res; r != NULL; r = r->ai_next) {
		sd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (sd == -1)
			continue;

		if (connect(sd, r->ai_addr, r->ai_addrlen) == 0)
			break;

		close(sd);
	}

	freeaddrinfo(res);

	return sd;
}

int
main(int argc, char *argv[])
{
	int sd, ocsp_status;
	const unsigned char *p;
	long len;
	OCSP_RESPONSE *rsp = NULL;
	OCSP_BASICRESP *br = NULL;
	X509_STORE     *st = NULL;
	STACK_OF(X509) *ch = NULL;
	char *host, *port;
#ifdef _PATH_SSL_CA_FILE
	char *cafile = _PATH_SSL_CA_FILE;
#else
	char *cafile = "/etc/ssl/cert.pem";
#endif

	SSL *ssl;
	SSL_CTX *ctx;

	SSL_library_init();
	SSL_load_error_strings();

	ctx = SSL_CTX_new(SSLv23_client_method());

	if (!SSL_CTX_load_verify_locations(ctx, cafile, NULL)) {
		printf("failed to load %s\n", cafile);
		exit(-1);
	}

	if (argc != 3)
		errx(-1, "need a host and port to connect to");
	else {
		host = argv[1];
		port = argv[2];
	}

	sd = tcp_connect(host, port);

	ssl = SSL_new(ctx);

	SSL_set_fd(ssl, (int) sd);
	SSL_set_tlsext_status_type(ssl, TLSEXT_STATUSTYPE_ocsp);

	if (SSL_connect(ssl) <= 0) {
		printf("SSL connect error\n");
		exit(-1);
	}

	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		printf("Certificate doesn't verify from host %s port %s\n", host, port);
		exit(-1);
	}

	/* ==== VERIFY OCSP RESPONSE ==== */


	len = SSL_get_tlsext_status_ocsp_resp(ssl, &p);

	if (!p) {
		printf("No OCSP response received for %s port %s\n", host, port);
		exit(-1);
	}

	rsp = d2i_OCSP_RESPONSE(NULL, &p, len);
	if (!rsp) {
		puts("Invalid OCSP response");
		exit(-1);
	}

	ocsp_status = OCSP_response_status(rsp);
	if (ocsp_status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		printf("Invalid OCSP response status: %s (%d)",
		       OCSP_response_status_str(ocsp_status), ocsp_status);
		exit(-1);
	}

	br = OCSP_response_get1_basic(rsp);
	if (!br) {
		puts("Invalid OCSP response");
		exit(-1);
	}

	ch = SSL_get_peer_cert_chain(ssl);
	st = SSL_CTX_get_cert_store(ctx);

	if (OCSP_basic_verify(br, ch, st, 0) <= 0) {
		puts("OCSP response verification failed");
		exit(-1);
	}

	printf("OCSP validated from %s %s\n", host, port);

	return 0;
}
