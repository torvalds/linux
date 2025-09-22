/* $OpenBSD: callback.c,v 1.5 2024/08/23 12:56:26 anton Exp $ */
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
FILE *output;

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
	X509 *current_cert, *issuer_cert;
	int verify_err, verify_depth;

	current_cert = X509_STORE_CTX_get_current_cert(xsc);
	issuer_cert = X509_STORE_CTX_get0_current_issuer(xsc);
	verify_depth =  X509_STORE_CTX_get_error_depth(xsc);
	verify_err = X509_STORE_CTX_get_error(xsc);
	fprintf(output, "depth %d error %d", verify_depth, verify_err);
	fprintf(output, " cert: ");
	if (current_cert != NULL) {
		X509_NAME_print_ex_fp(output,
		    X509_get_subject_name(current_cert), 0,
		    XN_FLAG_ONELINE);
	} else
		fprintf(output, "NULL");
	fprintf(output, " issuer: ");
	if (issuer_cert != NULL) {
		X509_NAME_print_ex_fp(output,
		    X509_get_subject_name(issuer_cert), 0,
		    XN_FLAG_ONELINE);
	} else
		fprintf(output, "NULL");
	fprintf(output, "\n");

	return ok;
}

static void
verify_cert(const char *roots_dir, const char *roots_file,
    const char *bundle_file, int *chains, int mode)
{
	STACK_OF(X509) *roots = NULL, *bundle = NULL;
	X509_STORE_CTX *xsc = NULL;
	X509_STORE *store = NULL;
	int verify_err, use_dir;
	X509 *leaf = NULL;

	*chains = 0;
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
	if (X509_verify_cert(xsc) == 1) {
		*chains = 1; /* XXX */
		goto done;
	}

	verify_err = X509_STORE_CTX_get_error(xsc);
	if (verify_err == 0)
		errx(1, "Error unset on failure!");

	fprintf(stderr, "failed to verify at %d: %s\n",
	    X509_STORE_CTX_get_error_depth(xsc),
	    X509_verify_cert_error_string(verify_err));

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
	int failing;
};

struct verify_cert_test verify_cert_tests[] = {
	{
		.id = "1a",
		.want_chains = 1,
	},
	{
		.id = "2a",
		.want_chains = 1,
	},
	{
		.id = "2b",
		.want_chains = 0,
	},
	{
		.id = "2c",
		.want_chains = 1,
	},
	{
		.id = "3a",
		.want_chains = 1,
	},
	{
		.id = "3b",
		.want_chains = 0,
	},
	{
		.id = "3c",
		.want_chains = 0,
	},
	{
		.id = "3d",
		.want_chains = 0,
	},
	{
		.id = "3e",
		.want_chains = 1,
	},
	{
		.id = "4a",
		.want_chains = 2,
	},
	{
		.id = "4b",
		.want_chains = 1,
	},
	{
		.id = "4c",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "4d",
		.want_chains = 1,
	},
	{
		.id = "4e",
		.want_chains = 1,
	},
	{
		.id = "4f",
		.want_chains = 2,
	},
	{
		.id = "4g",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "4h",
		.want_chains = 1,
	},
	{
		.id = "5a",
		.want_chains = 2,
	},
	{
		.id = "5b",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "5c",
		.want_chains = 1,
	},
	{
		.id = "5d",
		.want_chains = 1,
	},
	{
		.id = "5e",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "5f",
		.want_chains = 1,
	},
	{
		.id = "5g",
		.want_chains = 2,
	},
	{
		.id = "5h",
		.want_chains = 1,
	},
	{
		.id = "5i",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "6a",
		.want_chains = 1,
	},
	{
		.id = "6b",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "7a",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "7b",
		.want_chains = 1,
	},
	{
		.id = "8a",
		.want_chains = 0,
	},
	{
		.id = "9a",
		.want_chains = 0,
	},
	{
		.id = "10a",
		.want_chains = 1,
	},
	{
		.id = "10b",
		.want_chains = 1,
	},
	{
		.id = "11a",
		.want_chains = 1,
		.failing = 1,
	},
	{
		.id = "11b",
		.want_chains = 1,
	},
	{
		.id = "12a",
		.want_chains = 1,
	},
	{
		.id = "13a",
		.want_chains = 1,
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
	int failed = 0;
	int chains;
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

		fprintf(output, "== Test %zu (%s)\n", i, vct->id);
		fprintf(output, "== Legacy:\n");
		mode = MODE_LEGACY_VFY;
		verify_cert(roots_dir, roots_file, bundle_file, &chains, mode);
		if ((mode == MODE_VERIFY && chains == vct->want_chains) ||
		    (chains == 0 && vct->want_chains == 0) ||
		    (chains == 1 && vct->want_chains > 0)) {
			fprintf(output, "INFO: Succeeded with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (mode == MODE_LEGACY_VFY && vct->failing)
				failed |= 1;
		} else {
			fprintf(output, "FAIL: Failed with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (!vct->failing)
				failed |= 1;
		}
		fprintf(output, "\n");
		fprintf(output, "== Modern:\n");
		mode = MODE_MODERN_VFY;
		verify_cert(roots_dir, roots_file, bundle_file, &chains, mode);
		if ((mode == MODE_VERIFY && chains == vct->want_chains) ||
		    (chains == 0 && vct->want_chains == 0) ||
		    (chains == 1 && vct->want_chains > 0)) {
			fprintf(output, "INFO: Succeeded with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (mode == MODE_LEGACY_VFY && vct->failing)
				failed |= 1;
		} else {
			fprintf(output, "FAIL: Failed with %d chains%s\n",
			    chains, vct->failing ? " (legacy failure)" : "");
			if (!vct->failing)
				failed |= 1;
		}
		fprintf(output, "\n");

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

	output = fopen("callback.out", "w+");

	fprintf(stderr, "\n\nTesting legacy and modern X509_vfy\n");
	failed |= verify_cert_test(argv[1], MODE_LEGACY_VFY);
	return (failed);
}
