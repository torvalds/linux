/* $OpenBSD: expirecallback.c,v 1.4 2024/08/23 12:56:26 anton Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2020-2021 Bob Beck <beck@openbsd.org>
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

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "x509_verify.h"

#define MODE_MODERN_VFY		0
#define MODE_MODERN_VFY_DIR	1
#define MODE_LEGACY_VFY		2
#define MODE_VERIFY		3

static int verbose = 1;

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
		    X509_get_subject_name(current_cert), 0,
		    XN_FLAG_ONELINE);
		fprintf(stderr, "\n");
	}

	verify_err = X509_STORE_CTX_get_error(xsc);
	if (verify_err != X509_V_OK) {
		if (verify_err == X509_V_ERR_CERT_HAS_EXPIRED)
			fprintf(stderr, "IGNORING ");
		fprintf(stderr, "verify error at depth %d: %s\n",
		    X509_STORE_CTX_get_error_depth(xsc),
		    X509_verify_cert_error_string(verify_err));
	}

	/*
	 * Ignore expired certs, in the way people are told to do it
	 * by OpenSSL
	 */

	if (verify_err == X509_V_ERR_CERT_HAS_EXPIRED)
		return 1;

	return ok;
}

static void
verify_cert(const char *roots_dir, const char *roots_file,
    const char *bundle_file, int *chains, int *error, int *error_depth,
    int mode)
{
	STACK_OF(X509) *roots = NULL, *bundle = NULL;
	X509_STORE_CTX *xsc = NULL;
	X509_STORE *store = NULL;
	X509 *leaf = NULL;
	int use_dir;
	int ret;

	*chains = 0;
	*error = 0;
	*error_depth = 0;

	use_dir = (mode == MODE_MODERN_VFY_DIR);

	if (!use_dir && !certs_from_file(roots_file, &roots))
		errx(1, "failed to load roots from '%s'", roots_file);
	if (!certs_from_file(bundle_file, &bundle))
		errx(1, "failed to load bundle from '%s'", bundle_file);
	if (sk_X509_num(bundle) < 1)
		errx(1, "not enough certs in bundle");
	leaf = sk_X509_shift(bundle);

	if ((xsc = X509_STORE_CTX_new()) == NULL)
		errx(1, "X509_STORE_CTX");
	if (use_dir && (store = X509_STORE_new()) == NULL)
		errx(1, "X509_STORE");
	if (!X509_STORE_CTX_init(xsc, store, leaf, bundle)) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to init store context");
	}

	if (use_dir) {
		if (!X509_STORE_load_locations(store, NULL, roots_dir))
			errx(1, "failed to set by_dir directory of %s", roots_dir);
	}
	if (mode == MODE_LEGACY_VFY)
		X509_STORE_CTX_set_flags(xsc, X509_V_FLAG_LEGACY_VERIFY);
	else
		X509_VERIFY_PARAM_clear_flags(X509_STORE_CTX_get0_param(xsc),
		    X509_V_FLAG_LEGACY_VERIFY);

	if (verbose)
		X509_STORE_CTX_set_verify_cb(xsc, verify_cert_cb);
	if (!use_dir)
		X509_STORE_CTX_set0_trusted_stack(xsc, roots);

	ret = X509_verify_cert(xsc);

	*error = X509_STORE_CTX_get_error(xsc);
	*error_depth = X509_STORE_CTX_get_error_depth(xsc);

	if (ret == 1) {
		*chains = 1; /* XXX */
		goto done;
	}

	if (*error == 0)
		errx(1, "Error unset on failure!");

	fprintf(stderr, "failed to verify at %d: %s\n",
	    *error_depth, X509_verify_cert_error_string(*error));

 done:
	sk_X509_pop_free(roots, X509_free);
	sk_X509_pop_free(bundle, X509_free);
	X509_STORE_free(store);
	X509_STORE_CTX_free(xsc);
	X509_free(leaf);
}

struct verify_cert_test {
	const char *id;
	int want_chains;
	int want_error;
	int want_error_depth;
	int want_legacy_error;
	int want_legacy_error_depth;
	int failing;
};

struct verify_cert_test verify_cert_tests[] = {
	{
		.id = "2a",
		.want_chains = 1,
		.want_error = 0,
		.want_error_depth = 0,
		.want_legacy_error = 0,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "8a",
		.want_chains = 1,
		.want_error = X509_V_ERR_CERT_HAS_EXPIRED,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_CERT_HAS_EXPIRED,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "9a",
		.want_chains = 1,
		.want_error = X509_V_ERR_CERT_HAS_EXPIRED,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY,
		.want_legacy_error_depth = 0,
		.failing = 1,
	},
};

#define N_VERIFY_CERT_TESTS \
    (sizeof(verify_cert_tests) / sizeof(*verify_cert_tests))

static int
verify_cert_test(const char *certs_path, int mode)
{
	char *roots_file, *bundle_file, *roots_dir;
	struct verify_cert_test *vct;
	int chains, error, error_depth;
	int failed = 0;
	size_t i;

	for (i = 0; i < N_VERIFY_CERT_TESTS; i++) {
		vct = &verify_cert_tests[i];

		if (asprintf(&roots_file, "%s/%s/roots.pem", certs_path,
		    vct->id) == -1)
			errx(1, "asprintf");
		if (asprintf(&bundle_file, "%s/%s/bundle.pem", certs_path,
		    vct->id) == -1)
			errx(1, "asprintf");
		if (asprintf(&roots_dir, "./%s/roots", vct->id) == -1)
			errx(1, "asprintf");

		fprintf(stderr, "== Test %zu (%s)\n", i, vct->id);
		verify_cert(roots_dir, roots_file, bundle_file, &chains, &error,
		    &error_depth, mode);

		if ((mode == MODE_VERIFY && chains == vct->want_chains) ||
		    (chains == 0 && vct->want_chains == 0) ||
		    (chains == 1 && vct->want_chains > 0)) {
			fprintf(stderr, "INFO: Succeeded with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (mode == MODE_LEGACY_VFY && vct->failing)
				failed |= 1;
		} else {
			fprintf(stderr, "FAIL: Failed with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (!vct->failing)
				failed |= 1;
		}

		if (mode == MODE_LEGACY_VFY) {
			if (error != vct->want_legacy_error) {
				fprintf(stderr, "FAIL: Got legacy error %d, "
				    "want %d\n", error, vct->want_legacy_error);
				failed |= 1;
			}
			if (error_depth != vct->want_legacy_error_depth) {
				fprintf(stderr, "FAIL: Got legacy error depth "
				    "%d, want %d\n", error_depth,
				    vct->want_legacy_error_depth);
				failed |= 1;
			}
		} else if (mode == MODE_MODERN_VFY || mode == MODE_MODERN_VFY_DIR) {
			if (error != vct->want_error) {
				fprintf(stderr, "FAIL: Got error %d, want %d\n",
				    error, vct->want_error);
				failed |= 1;
			}
			if (error_depth != vct->want_error_depth) {
				fprintf(stderr, "FAIL: Got error depth %d, want"
				    " %d\n", error_depth, vct->want_error_depth);
				failed |= 1;
			}
		}

		fprintf(stderr, "\n");

		free(roots_file);
		free(bundle_file);
		free(roots_dir);
	}

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <certs_path>\n", argv[0]);
		exit(1);
	}

	fprintf(stderr, "\n\nTesting legacy x509_vfy\n");
	failed |= verify_cert_test(argv[1], MODE_LEGACY_VFY);
	fprintf(stderr, "\n\nTesting modern x509_vfy\n");
	failed |= verify_cert_test(argv[1], MODE_MODERN_VFY);
	fprintf(stderr, "\n\nTesting modern x509_vfy by_dir\n");
	failed |= verify_cert_test(argv[1], MODE_MODERN_VFY_DIR);

	return (failed);
}
