/*	$Id: test-cert.c,v 1.26 2025/07/15 09:26:19 tb Exp $ */
/*
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>

#include "extern.h"

int outformats;
int verbose;
int filemode = 1;
int experimental;

int
main(int argc, char *argv[])
{
	int		 c, i, verb = 0, ta = 0;
	struct cert	*p;

	ERR_load_crypto_strings();
	OpenSSL_add_all_ciphers();
	OpenSSL_add_all_digests();
	x509_init_oid();

	while ((c = getopt(argc, argv, "tv")) != -1)
		switch (c) {
		case 't':
			ta = 1;
			break;
		case 'v':
			verb++;
			break;
		default:
			errx(1, "bad argument %c", c);
		}

	argv += optind;
	argc -= optind;

	if (argc == 0)
		errx(1, "argument missing");

	if (ta) {
		if (argc % 2)
			errx(1, "need even number of arguments");

		for (i = 0; i < argc; i += 2) {
			const char	*cert_path = argv[i];
			const char	*tal_path = argv[i + 1];
			char		*buf;
			size_t		 len;
			struct tal	*tal;

			buf = load_file(tal_path, &len);
			tal = tal_parse(tal_path, buf, len);
			free(buf);
			if (tal == NULL)
				break;

			buf = load_file(cert_path, &len);
			p = cert_parse(cert_path, buf, len);
			free(buf);
			if (p == NULL)
				break;
			p = ta_parse(cert_path, p, tal->pkey, tal->pkeysz);
			tal_free(tal);
			if (p == NULL)
				break;

			if (verb)
				cert_print(p);
			cert_free(p);
		}
	} else {
		for (i = 0; i < argc; i++) {
			char		*buf;
			size_t		 len;

			buf = load_file(argv[i], &len);
			p = cert_parse(argv[i], buf, len);
			free(buf);
			if (p == NULL)
				break;
			if (verb)
				cert_print(p);
			cert_free(p);
		}
	}

	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_free_strings();

	if (i < argc)
		errx(1, "test failed for %s", argv[i]);

	printf("OK\n");
	return 0;
}

time_t
get_current_time(void)
{
	return time(NULL);
}
