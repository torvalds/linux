/* $OpenBSD: policy.c,v 1.13 2024/08/23 12:56:26 anton Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2020-2023 Bob Beck <beck@openbsd.org>
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
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "x509_verify.h"

#define MODE_MODERN_VFY		0
#define MODE_MODERN_VFY_DIR	1
#define MODE_LEGACY_VFY		2

static int verbose = 1;

#define OID1 "1.2.840.113554.4.1.72585.2.1"
#define OID2 "1.2.840.113554.4.1.72585.2.2"
#define OID3 "1.2.840.113554.4.1.72585.2.3"
#define OID4 "1.2.840.113554.4.1.72585.2.4"
#define OID5 "1.2.840.113554.4.1.72585.2.5"

#ifndef CERTSDIR
#define CERTSDIR "."
#endif

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

	if (*certs == NULL) {
		if ((xs = sk_X509_new_null()) == NULL)
			errx(1, "failed to create X509 stack");
	} else {
		xs = *certs;
	}
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
		fprintf(stderr, "verify error at depth %d: %s\n",
		    X509_STORE_CTX_get_error_depth(xsc),
		    X509_verify_cert_error_string(verify_err));
	}

	return ok;
}

static void
verify_cert(const char *roots_file, const char *intermediate_file,
    const char *leaf_file, int *chains, int *error, int *error_depth,
    int mode, ASN1_OBJECT *policy_oid, ASN1_OBJECT *policy_oid2,
    int verify_flags)
{
	STACK_OF(X509) *roots = NULL, *bundle = NULL;
	X509_STORE_CTX *xsc = NULL;
	X509_STORE *store = NULL;
	X509 *leaf = NULL;
	int flags, ret;

	*chains = 0;
	*error = 0;
	*error_depth = 0;

	if (!certs_from_file(roots_file, &roots))
		errx(1, "failed to load roots from '%s'", roots_file);
	if (!certs_from_file(leaf_file, &bundle))
		errx(1, "failed to load leaf from '%s'", leaf_file);
	if (intermediate_file != NULL && !certs_from_file(intermediate_file,
	    &bundle))
		errx(1, "failed to load intermediate from '%s'",
		    intermediate_file);
	if (sk_X509_num(bundle) < 1)
		errx(1, "not enough certs in bundle");
	leaf = sk_X509_shift(bundle);

	if ((xsc = X509_STORE_CTX_new()) == NULL)
		errx(1, "X509_STORE_CTX");
	if (!X509_STORE_CTX_init(xsc, store, leaf, bundle)) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to init store context");
	}

	flags = X509_V_FLAG_POLICY_CHECK;
	flags |= verify_flags;
	if (mode == MODE_LEGACY_VFY)
		flags |= X509_V_FLAG_LEGACY_VERIFY;
	X509_STORE_CTX_set_flags(xsc, flags);

	if (verbose)
		X509_STORE_CTX_set_verify_cb(xsc, verify_cert_cb);
	X509_STORE_CTX_set0_trusted_stack(xsc, roots);

	if (policy_oid != NULL) {
		X509_VERIFY_PARAM *param = X509_STORE_CTX_get0_param(xsc);
		ASN1_OBJECT *copy = OBJ_dup(policy_oid);
		X509_VERIFY_PARAM_add0_policy(param, copy);
	}
	if (policy_oid2 != NULL) {
		X509_VERIFY_PARAM *param = X509_STORE_CTX_get0_param(xsc);
		ASN1_OBJECT *copy = OBJ_dup(policy_oid2);
		X509_VERIFY_PARAM_add0_policy(param, copy);
	}

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
	const char *root_file;
	const char *intermediate_file;
	const char *leaf_file;
	const char *policy_oid_to_check;
	const char *policy_oid_to_check2;
	int want_chains;
	int want_error;
	int want_error_depth;
	int want_legacy_error;
	int want_legacy_error_depth;
	int failing;
	int verify_flags;
};

struct verify_cert_test verify_cert_tests[] = {
	/*
	 * Comments here are from boringssl/crypto/x509/x509_test.cc
	 * certs were generated by
	 * boringssl/crypto/x509/test/make_policy_certs.go
	 */

	/* The chain is good for |oid1| and |oid2|, but not |oid3|. */
	{
		.id = "nothing  in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.want_chains = 1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
	},
	{
		.id = "1, in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
	},
	{
		.id = "2, in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID2,
		.want_chains = 1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
	},
	{
		.id = "3, in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "1 and 2, in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.policy_oid_to_check2 = OID2,
		.want_chains = 1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
	},
	{
		.id = "1 and 3, in 1 and 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.policy_oid_to_check2 = OID3,
		.want_chains = 1,
	},
	/*  The policy extension cannot be parsed. */
	{
		.id = "1 in invalid intermediate policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_invalid.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "invalid intermediate",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_invalid.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "1 in invalid policy in leaf",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_invalid.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "invalid leaf",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_invalid.pem",
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "invalid leaf without explicit policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_invalid.pem",
		.want_chains = 0,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	/*  There is a duplicate policy in the leaf policy extension. */
	{
		.id = "1 in duplicate policy extension in leaf",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_duplicate.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	/*  There is a duplicate policy in the intermediate policy extension. */
	{
		.id = "1 in duplicate policy extension in intermediate",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_duplicate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 0,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	/*
	 * Without |X509_V_FLAG_EXPLICIT_POLICY|, the policy tree is built and
	 * intersected with user-specified policies, but it is not required to result
	 * in any valid policies.
	 */
	{
		.id = "nothing with explicit_policy unset",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.want_chains = 1,
	},
	{
		.id = "oid3 with explicit_policy unset",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 1,
	},
	/*  However, a CA with policy constraints can require an explicit policy. */
	{
		.id = "oid1 with explicit_policy unset, intermediate requiring policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID1,
		.want_chains = 1,
	},
	{
		.id = "oid3 with explicit_policy unset, intermediate requiring policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/*
	 * requireExplicitPolicy applies even if the application does not configure a
	 * user-initial-policy-set. If the validation results in no policies, the
	 * chain is invalid.
	 */
	{
		.id = "nothing explict_policy unset, with intermediate requiring policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_none.pem",
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/* A leaf can also set requireExplicitPolicy but should work with none */
	{
		.id = "nothing explicit_policy unset, with leaf requiring policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_require.pem",
		.want_chains = 1,
	},
	/* A leaf can also set requireExplicitPolicy but should fail with policy */
	{
		.id = "oid3, explicit policy unset,  with leaf requiring policy",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_require.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/*
	 * requireExplicitPolicy is a count of certificates to skip. If the value is
	 * not zero by the end of the chain, it doesn't count.
	 */
	{
		.id = "oid3, with intermediate requiring explicit depth 1",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require1.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "oid3, with intermediate requiring explicit depth 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require2.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 1,
	},
	{
		.id = "oid3, with leaf requiring explicit depth 1",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_require1.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 1,
	},
	/*
	 * If multiple certificates specify the constraint, the more constrained value
	 * wins.
	 */
	{
		.id = "oid3, with leaf and intermediate requiring explicit depth 1",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require1.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_require1.pem",
		.policy_oid_to_check = OID3,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	{
		.id = "oid3, with leaf requiring explicit depth 1 and intermediate depth 2",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require2.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_require.pem",
		.policy_oid_to_check = OID3,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/*
	 * An intermediate that requires an explicit policy, but then specifies no
	 * policies should fail verification as a result.
	 */
	{
		.id = "oid1 with explicit_policy unset, intermediate requiring policy but specifying none",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require_no_policies.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/*
	 * A constrained intermediate's policy extension has a duplicate policy, which
	 * is invalid. Historically this, and the above case, leaked memory.
	 */
	{
		.id = "oid1 with explicit_policy unset, intermediate requiring policy but has duplicate",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_require_duplicate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf.pem",
		.policy_oid_to_check = OID3,
		.want_chains = 0,
		.want_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_INVALID_POLICY_EXTENSION,
		.want_legacy_error_depth = 0,
	},
	/*
	 * The leaf asserts anyPolicy, but the intermediate does not. The resulting
	 * valid policies are the intersection.(and vice versa)
	 */
	{
		.id = "oid1, with explicit_policy set, with leaf asserting any",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_any.pem",
		.policy_oid_to_check = OID1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 1,
	},
	{
		.id = "oid3, with explicit_policy set, with leaf asserting any",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_any.pem",
		.policy_oid_to_check = OID3,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 0,
		.want_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_error_depth = 0,
		.want_legacy_error = X509_V_ERR_NO_EXPLICIT_POLICY,
		.want_legacy_error_depth = 0,
	},
	/* Both assert anyPolicy. All policies are valid. */
	{
		.id = "oid1, with explicit_policy set, with leaf and intermediate asserting any",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_any.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_any.pem",
		.policy_oid_to_check = OID1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 1,
	},
	{
		.id = "oid3, with explicit_policy set, with leaf and intermediate asserting any",
		.root_file = CERTSDIR "/" "policy_root.pem",
		.intermediate_file = CERTSDIR "/" "policy_intermediate_any.pem",
		.leaf_file = CERTSDIR "/" "policy_leaf_any.pem",
		.policy_oid_to_check = OID1,
		.verify_flags = X509_V_FLAG_EXPLICIT_POLICY,
		.want_chains = 1,
	},
	/*
	 * BoringSSL tests just a trust anchor but behaves differently in this corner case.
	 * than libressl for reasons that have nothing to do with policy (because parital
	 * chains and legacy verifier horror)
	 */
};

#define N_VERIFY_CERT_TESTS \
    (sizeof(verify_cert_tests) / sizeof(*verify_cert_tests))

static int
verify_cert_test(int mode)
{
	ASN1_OBJECT *policy_oid, *policy_oid2;
	struct verify_cert_test *vct;
	int chains, error, error_depth;
	int failed = 0;
	size_t i;

	for (i = 0; i < N_VERIFY_CERT_TESTS; i++) {
		vct = &verify_cert_tests[i];
		policy_oid = vct->policy_oid_to_check ?
		    OBJ_txt2obj(vct->policy_oid_to_check, 1) : NULL;
		policy_oid2 = vct->policy_oid_to_check2 ?
		    OBJ_txt2obj(vct->policy_oid_to_check2, 1) : NULL;

		error = 0;
		error_depth = 0;

		fprintf(stderr, "== Test %zu (%s)\n", i, vct->id);
		verify_cert(vct->root_file, vct->intermediate_file,
		    vct->leaf_file, &chains, &error, &error_depth,
		    mode, policy_oid, policy_oid2, vct->verify_flags);

		if ((chains == 0 && vct->want_chains == 0) ||
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
		}
		fprintf(stderr, "\n");
		ASN1_OBJECT_free(policy_oid);
		ASN1_OBJECT_free(policy_oid2);
	}
	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	fprintf(stderr, "\n\nTesting legacy x509_vfy\n");
	failed |= verify_cert_test(MODE_LEGACY_VFY);
	fprintf(stderr, "\n\nTesting modern x509_vfy\n");
	failed |= verify_cert_test(MODE_MODERN_VFY);
	/* New verifier does not do policy goop at the moment */

	return (failed);
}
