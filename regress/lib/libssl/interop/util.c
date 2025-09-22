/*	$OpenBSD: util.c,v 1.3 2018/11/09 06:30:41 bluhm Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>

#include "util.h"

void
print_version(void)
{
#ifdef OPENSSL_VERSION_NUMBER
	printf("OPENSSL_VERSION_NUMBER: %#08lx\n", OPENSSL_VERSION_NUMBER);
#endif
#ifdef LIBRESSL_VERSION_NUMBER
	printf("LIBRESSL_VERSION_NUMBER: %#08lx\n", LIBRESSL_VERSION_NUMBER);
#endif
#ifdef LIBRESSL_VERSION_TEXT
	printf("LIBRESSL_VERSION_TEXT: %s\n", LIBRESSL_VERSION_TEXT);
#endif
#if OPENSSL_VERSION_NUMBER >= 0x1010000f
	printf("OpenSSL_version_num: %#08lx\n", OpenSSL_version_num());
	printf("OpenSSL_version OPENSSL_VERSION: %s\n",
	    OpenSSL_version(OPENSSL_VERSION));
	printf("OpenSSL_version OPENSSL_CFLAGS: %s\n",
	    OpenSSL_version(OPENSSL_CFLAGS));
	printf("OpenSSL_version OPENSSL_BUILT_ON: %s\n",
	    OpenSSL_version(OPENSSL_BUILT_ON));
	printf("OpenSSL_version OPENSSL_PLATFORM: %s\n",
	    OpenSSL_version(OPENSSL_PLATFORM));
	printf("OpenSSL_version OPENSSL_DIR: %s\n",
	    OpenSSL_version(OPENSSL_DIR));
	printf("OpenSSL_version OPENSSL_ENGINES_DIR: %s\n",
	    OpenSSL_version(OPENSSL_ENGINES_DIR));
#endif
	printf("SSLeay: %#08lx\n", SSLeay());
	printf("SSLeay_version SSLEAY_VERSION: %s\n",
	    SSLeay_version(SSLEAY_VERSION));
	printf("SSLeay_version SSLEAY_CFLAGS: %s\n",
	    SSLeay_version(SSLEAY_CFLAGS));
	printf("SSLeay_version SSLEAY_BUILT_ON: %s\n",
	    SSLeay_version(SSLEAY_BUILT_ON));
	printf("SSLeay_version SSLEAY_PLATFORM: %s\n",
	    SSLeay_version(SSLEAY_PLATFORM));
	printf("SSLeay_version SSLEAY_DIR: %s\n",
	    SSLeay_version(SSLEAY_DIR));
}

void
print_ciphers(STACK_OF(SSL_CIPHER) *cstack)
{
	const SSL_CIPHER *cipher;
	int i;

	for (i = 0; (cipher = sk_SSL_CIPHER_value(cstack, i)) != NULL; i++)
		printf("cipher %s\n", SSL_CIPHER_get_name(cipher));
	if (fflush(stdout) != 0)
		err(1, "fflush stdout");
}

void
print_sockname(BIO *bio)
{
	struct sockaddr_storage ss;
	socklen_t slen;
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int fd;

	if (BIO_get_fd(bio, &fd) <= 0)
		err_ssl(1, "BIO_get_fd");
	slen = sizeof(ss);
	if (getsockname(fd, (struct sockaddr *)&ss, &slen) == -1)
		err(1, "getsockname");
	if (getnameinfo((struct sockaddr *)&ss, ss.ss_len, host,
	    sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV))
		errx(1, "getnameinfo");
	printf("sock: %s %s\n", host, port);
	if (fflush(stdout) != 0)
		err(1, "fflush stdout");
}

void
print_peername(BIO *bio)
{
	struct sockaddr_storage ss;
	socklen_t slen;
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int fd;

	if (BIO_get_fd(bio, &fd) <= 0)
		err_ssl(1, "BIO_get_fd");
	slen = sizeof(ss);
	if (getpeername(fd, (struct sockaddr *)&ss, &slen) == -1)
		err(1, "getpeername");
	if (getnameinfo((struct sockaddr *)&ss, ss.ss_len, host,
	    sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV))
		errx(1, "getnameinfo");
	printf("peer: %s %s\n", host, port);
	if (fflush(stdout) != 0)
		err(1, "fflush stdout");
}

void
err_ssl(int eval, const char *fmt, ...)
{
	va_list ap;

	ERR_print_errors_fp(stderr);
	va_start(ap, fmt);
	verrx(eval, fmt, ap);
	va_end(ap);
}

int
verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	printf("verify: %s\n", preverify_ok ? "pass" : "fail");
	if (fflush(stdout) != 0)
		err(1, "fflush stdout");

	return preverify_ok;
}
