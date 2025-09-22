/* $OpenBSD: verify.c,v 1.9 2020/10/26 12:11:47 beck Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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
#include <string.h>

#include <sys/stat.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

static int verbose = 0;
static int json = 0; /* print out json like bettertls expects resuls in */

static int
passwd_cb(char *buf, int size, int rwflag, void *u)
{
	memset(buf, 0, size);
	return (0);
}

static int
certs_from_file(const char *filename, STACK_OF(X509) **certs)
{
	STACK_OF(X509_INFO) *xis = NULL;
	STACK_OF(X509) *xs = NULL;
	BIO *bio = NULL;
	X509 *x;
	int i;

	if ((xs = sk_X509_new_null()) == NULL)
		errx(1, "failed to create X509 stack");
	if ((bio = BIO_new_file(filename, "r")) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to create bio");
	}
	if ((xis = PEM_X509_INFO_read_bio(bio, NULL, passwd_cb, NULL)) == NULL)
		errx(1, "failed to read PEM");

	for (i = 0; i < sk_X509_INFO_num(xis); i++) {
		if ((x = sk_X509_INFO_value(xis, i)->x509) == NULL)
			continue;
		if (!sk_X509_push(xs, x))
			errx(1, "failed to push X509");
		X509_up_ref(x);
	}

	*certs = xs;
	xs = NULL;

	sk_X509_INFO_pop_free(xis, X509_INFO_free);
	sk_X509_pop_free(xs, X509_free);
	BIO_free(bio);

	return 1;
}

static int
verify_cert_cb(int ok, X509_STORE_CTX *xsc)
{
	X509 *current_cert;
	int verify_err;

	current_cert = X509_STORE_CTX_get_current_cert(xsc);
	if (current_cert != NULL) {
		X509_NAME_print_ex_fp(stderr,
		    X509_get_subject_name(current_cert), 0, XN_FLAG_ONELINE);
		fprintf(stderr, "\n");
	}

	verify_err = X509_STORE_CTX_get_error(xsc);
	if (verify_err != X509_V_OK) {
		fprintf(stderr, "verify error at depth %d: %s\n",
		    X509_STORE_CTX_get_error_depth(xsc),
		    X509_verify_cert_error_string(verify_err));
	}

	return ok;
}

static void
verify_cert(X509_STORE *store, const char *roots_file, const char *bundle_file,
    const char *cert_file, int *ip, int *dns)
{
	STACK_OF(X509) *roots = NULL, *bundle = NULL, *cert = NULL;
	X509_STORE_CTX *xsc = NULL;
	X509_STORE_CTX *xscip = NULL;
	X509_VERIFY_PARAM *param, *paramip;
	X509 *leaf = NULL;
	unsigned long flags, flagsip;
	int verify_err;

	*ip = *dns = 0;

	if (!certs_from_file(roots_file, &roots))
		errx(1, "failed to load roots from '%s'", roots_file);
	if (!certs_from_file(bundle_file, &bundle))
		errx(1, "failed to load bundle from '%s'", bundle_file);
	if (!certs_from_file(cert_file, &cert))
		errx(1, "failed to load cert from '%s'", cert_file);
	if (sk_X509_num(cert) < 1)
		errx(1, "no certs in cert bundle %s", cert_file);
	leaf = sk_X509_shift(cert);

	if ((xsc = X509_STORE_CTX_new()) == NULL)
		errx(1, "X509_STORE_CTX");

	if (!X509_STORE_CTX_init(xsc, store, leaf, bundle)) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to init store context");
	}

	if (verbose)
		X509_STORE_CTX_set_verify_cb(xsc, verify_cert_cb);

	if ((param = X509_STORE_CTX_get0_param(xsc)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to get verify parameters");
	}
	flags = X509_VERIFY_PARAM_get_flags(param);
	X509_VERIFY_PARAM_set_flags(param, flags);
	X509_VERIFY_PARAM_set_time(param, 1600000000);
	X509_VERIFY_PARAM_set1_host(param, "localhost.local",
	    strlen("localhost.local"));

	X509_STORE_CTX_set0_trusted_stack(xsc, roots);

	if (X509_verify_cert(xsc) == 1)
		*dns = 1;
	verify_err = X509_STORE_CTX_get_error(xsc);
	if (verify_err == X509_V_OK && *dns == 0) {
		fprintf(stderr, "X509_V_OK on failure!\n");
		*dns = 1;
	}

	if ((xscip = X509_STORE_CTX_new()) == NULL)
		errx(1, "X509_STORE_CTX");

	if (!X509_STORE_CTX_init(xscip, store, leaf, bundle)) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to init store context");
	}

	if (verbose)
		X509_STORE_CTX_set_verify_cb(xscip, verify_cert_cb);

	if ((paramip = X509_STORE_CTX_get0_param(xscip)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to get verify parameters");
	}
	flagsip = X509_VERIFY_PARAM_get_flags(paramip);
	X509_VERIFY_PARAM_set_flags(paramip, flagsip);
	X509_VERIFY_PARAM_set_time(paramip, 1600000000);
	X509_VERIFY_PARAM_set1_ip_asc(paramip, "127.0.0.1");

	X509_STORE_CTX_set0_trusted_stack(xscip, roots);

	if (X509_verify_cert(xscip) == 1)
		*ip = 1;
	verify_err = X509_STORE_CTX_get_error(xscip);
	if (verify_err == X509_V_OK && *ip == 0) {
		fprintf(stderr, "X509_V_OK on failure!\n");
		*ip = 1;
	}

	sk_X509_pop_free(roots, X509_free);
	sk_X509_pop_free(bundle, X509_free);
	sk_X509_pop_free(cert, X509_free);
	X509_STORE_CTX_free(xsc);
	X509_STORE_CTX_free(xscip);
	X509_free(leaf);
}

static void
bettertls_cert_test(const char *certs_path)
{
	X509_STORE *store;
	char *roots_file, *bundle_file, *cert_file;
	int i;

	if ((store = X509_STORE_new()) == NULL)
		errx(1, "X509_STORE_new");

	X509_STORE_set_default_paths(store);

	if (asprintf(&roots_file, "%s/root.crt", certs_path) == -1)
		errx(1, "asprintf");

	for(i = 1;; i++) {
		int ip, dns;
		struct stat sb;
		if (asprintf(&cert_file, "%s/%d.crt", certs_path, i) == -1)
			errx(1, "asprintf");
		if (asprintf(&bundle_file, "%s/%d.chain", certs_path, i) == -1)
			errx(1, "asprintf");
		if (stat(cert_file, &sb) == -1)
			break;
		if (stat(bundle_file, &sb) == -1)
			break;
		verify_cert(store, roots_file, bundle_file, cert_file, &ip, &dns);
		/* Mmm. json. with my avocado toast */
		if (i > 1 && json)
			fprintf(stdout, ",");
		if (json)
			fprintf(stdout, "{\"id\":%d,\"dnsResult\":%s,\""
			    "ipResult\":%s}", i, dns ? "true" : "false",
			    ip ? "true" : "false");
		else
			fprintf(stdout, "%d,%s,%s\n", i, dns ? "OK" : "ERROR",
			    ip ? "OK" : "ERROR");
		free(bundle_file);
		free(cert_file);
	}
	free(bundle_file);
	free(cert_file);
	free(roots_file);
	X509_STORE_free(store);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <certs_path>\n", argv[0]);
		exit(1);
	}
	if (json)
		fprintf(stdout, "{\"testVersion\":1,\"date\":%lld,\"userAgent\""
		    ":\"LibreSSL OpenBSD 6.8\\n\",\"results\":[", time(NULL));

	bettertls_cert_test(argv[1]);

	if (json)
		fprintf(stdout, "],\"osVersion\":\"OpenBSD 6.7\\n\"}\n");

	return 0;
}
