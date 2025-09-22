/*	$OpenBSD: x509_extensions_test.c,v 1.3 2024/06/17 05:04:54 tb Exp $ */

/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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
#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#define ASN1_BOOLEAN_TRUE	0xff
#define ASN1_BOOLEAN_FALSE	0x00

#define X509V3_EXT_CRITICAL	1
#define X509V3_EXT_NONCRITICAL	0

static BASIC_CONSTRAINTS *
create_basic_constraints(int ca)
{
	BASIC_CONSTRAINTS *bc;

	if ((bc = BASIC_CONSTRAINTS_new()) == NULL)
		errx(1, "BASIC_CONSTRAINTS_new");

	bc->ca = ca ? ASN1_BOOLEAN_TRUE : ASN1_BOOLEAN_FALSE;

	return bc;
}

static X509_EXTENSION *
ext_create_basic_constraints(int ca, int critical)
{
	X509_EXTENSION *ext;
	BASIC_CONSTRAINTS *bc;

	bc = create_basic_constraints(ca);
	if ((ext = X509V3_EXT_i2d(NID_basic_constraints, critical, bc)) == NULL)
		errx(1, "X509V3_EXT_i2d");
	BASIC_CONSTRAINTS_free(bc);

	return ext;
}

static int
test_x509v3_add1_i2d_empty_stack(STACK_OF(X509_EXTENSION) **extensions)
{
	unsigned long error;
	int op, got;
	int nid = NID_basic_constraints;
	int failed = 1;

	if (X509v3_get_ext_count(*extensions) != 0) {
		fprintf(stderr, "%s: FAIL: need empty stack\n", __func__);
		goto err;
	}

	ERR_clear_error();

	op = X509V3_ADD_REPLACE_EXISTING;

	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}

	error = ERR_get_error();
	if (ERR_GET_REASON(error) != X509V3_R_EXTENSION_NOT_FOUND) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		    "pushed %d for empty stack, want %d.\n", __func__,
		    ERR_GET_REASON(error), X509V3_R_EXTENSION_NOT_FOUND);
		goto err;
	}
	if ((error = ERR_get_error()) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		    "expected exactly one error.\n", __func__);
		goto err;
	}

	op = X509V3_ADD_REPLACE_EXISTING | X509V3_ADD_SILENT;

	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 0) {
		fprintf(stderr, "%s: FAIL: silent X509V3_ADD_REPLACE_EXISTING "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}
	if ((error = ERR_get_error()) != 0) {
		fprintf(stderr, "%s: FAIL: silent X509V3_ADD_REPLACE_EXISTING "
		    "added error %d, want %d.\n", __func__,
		    ERR_GET_REASON(error), 0);
		goto err;
	}

	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}

	error = ERR_get_error();
	if (ERR_GET_REASON(error) != X509V3_R_EXTENSION_NOT_FOUND) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "pushed %d for empty stack, want %d.\n", __func__,
		    ERR_GET_REASON(error), X509V3_R_EXTENSION_NOT_FOUND);
		goto err;
	}

	if ((error = ERR_get_error()) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected exactly one error.\n", __func__);
		goto err;
	}

	failed = 0;

 err:

	return failed;
}

static int
test_x509v3_add1_i2d_single_nid(STACK_OF(X509_EXTENSION) **extensions)
{
	BASIC_CONSTRAINTS *bc = NULL;
	unsigned long error;
	int crit, got, nid, op;
	int failed = 1;

	if (X509v3_get_ext_count(*extensions) != 0) {
		fprintf(stderr, "%s: FAIL: need an empty stack.\n", __func__);
		goto err;
	}

	/*
	 * Add basic ca constraints.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	op = X509V3_ADD_DEFAULT;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DEFAULT failed to add "
		    "basic constraints to empty stack: want %d, got %d.\n",
		    __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 1) {
		fprintf(stderr, "%s: FAIL: expected 1 extension, have %d.\n",
		    __func__, got);
		goto err;
	}

	/*
	 * Can't delete or replace non-existent extension.
	 */

	nid = NID_policy_constraints;
	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE non-existent "
		    "want %d, got %d,\n", __func__, 0, got);
		goto err;
	}
	nid = NID_policy_constraints;
	op = X509V3_ADD_REPLACE_EXISTING;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING non-existent "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}

	/*
	 * X509V3_ADD_DEFAULT refuses to add second basic constraints extension.
	 */

	ERR_clear_error();

	nid = NID_basic_constraints;
	bc = create_basic_constraints(0);
	op = X509V3_ADD_DEFAULT;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DEFAULT second constraints "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	error = ERR_get_error();
	if (ERR_GET_REASON(error) != X509V3_R_EXTENSION_EXISTS) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DEFAULT second constraints "
		    " pushed %d, want %d.\n", __func__,
		    ERR_GET_REASON(error), X509V3_R_EXTENSION_EXISTS);
		goto err;
	}

	if ((got = X509v3_get_ext_count(*extensions)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DEFAULT second contraints "
		    "expected 1 extension, have %d.\n", __func__, got);
		goto err;
	}

	/*
	 * We can replace existing basic constraints using X509V3_ADD_REPLACE.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(0);
	op = X509V3_ADD_REPLACE;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "want %d, got %d.\n", __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected 1 extension, have %d.\n", __func__, got);
		goto err;
	}

	/* Check that the extension was actually replaced. */
	nid = NID_basic_constraints;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, NULL)) == NULL) {
		if (crit != -1)
			errx(1, "X509V3_get_d2i");
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected basic constraints\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_FALSE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected cA = false in basic constraints\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * X509V3_ADD_KEEP_EXISTING existing does what it is supposed to do
	 * if basic constraints are already present.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	op = X509V3_ADD_KEEP_EXISTING;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_KEEP_EXISTING "
		    "want %d, got %d.\n", __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Check we still have non-ca basic constraints.
	 */

	nid = NID_basic_constraints;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, NULL)) == NULL) {
		if (crit != -1)
			errx(1, "X509V3_get_d2i");
		fprintf(stderr, "%s: FAIL: X509V3_ADD_KEEP_EXISTING "
		   "expected basic constraints\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_FALSE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_KEEP_EXISTING "
		   "expected non-ca basic constraints\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * X509V3_ADD_REPLACE_EXISTING also works.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	op = X509V3_ADD_REPLACE_EXISTING;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		    "want %d, got %d.\n", __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Check we again have ca basic constraints.
	 */

	nid = NID_basic_constraints;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, NULL)) == NULL) {
		if (crit != -1)
			errx(1, "X509V3_get_d2i");
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		   "expected basic constraints\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_TRUE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE_EXISTING "
		   "expected ca basic constraints\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * And X509V3_ADD_DELETE now works.
	 */

	nid = NID_basic_constraints;
	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}

	if ((got = X509v3_get_ext_count(*extensions)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected 0 extensions, have %d.\n", __func__, got);
		goto err;
	}

	/*
	 * X509V3_ADD_REPLACE adds the extension to empty stack as it should.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(0);
	op = X509V3_ADD_REPLACE;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, 1, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE on empty stack "
		    "want %d, got %d.\n", __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected 1 extension, have %d.\n", __func__, got);
		goto err;
	}

	/*
	 * And X509V3_ADD_DELETE works again.
	 */

	nid = NID_basic_constraints;
	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE after add replace "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}

	if ((got = X509v3_get_ext_count(*extensions)) != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected 0 extensions, have %d.\n", __func__, got);
		goto err;
	}

	failed = 0;

 err:
	BASIC_CONSTRAINTS_free(bc);

	return failed;
}

static int
test_x509v3_add1_i2d_add_append(STACK_OF(X509_EXTENSION) **extensions)
{
	BASIC_CONSTRAINTS *bc = NULL;
	int crit, got, idx, nid, op;
	int failed = 1;

	if (X509v3_get_ext_count(*extensions) != 0) {
		fprintf(stderr, "%s: FAIL: need empty stack.\n", __func__);
		goto err;
	}

	/*
	 * Let the toolkit add two basic constraints extensions.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	crit = 1;
	op = X509V3_ADD_APPEND;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, crit, op)) != 1) {
		fprintf(stderr, "%s: FAIL: first X509V3_ADD_APPEND "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	nid = NID_basic_constraints;
	bc = create_basic_constraints(0);
	crit = 1;
	op = X509V3_ADD_APPEND;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, crit, op)) != 1) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_APPEND "
		    "want %d, got %d.\n", __func__, 0, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 2) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_APPEND "
		    "expected 2 extensions, have %d.\n", __func__, got);
		goto err;
	}

	/*
	 * Inspect the extensions on the stack. First we should get the one
	 * with the ca bit set and it should be critical.
	 */

	nid = NID_basic_constraints;
	idx = -1;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_APPEND "
		    "expected basic constraints.\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_TRUE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_APPEND "
		    "expected ca basic constraints.\n", __func__);
		goto err;
	}
	if (crit != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_APPEND "
		    "expected critical basic constraints.\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/* Redo the exercise and get the basic constraints with ca bit unset. */
	nid = NID_basic_constraints;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_APPEND "
		    "expected basic constraints.\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_FALSE) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_APPEND "
		    "expected basic constraints to be non-ca.\n", __func__);
		goto err;
	}
	if (crit != 1) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_APPEND "
		    "expected critical basic constraints.\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Now X509V3_ADD_REPLACE non-critical ca constraints. They should
	 * replace the critical ca constraints we added before.
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	crit = 0;
	op = X509V3_ADD_REPLACE;
	if ((got = X509V3_add1_i2d(extensions, nid, bc, crit, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "want %d, got %d\n", __func__, 1, got);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * If we get basic constraints now, we get the non-critical one with the
	 * ca bit set.
	 */

	nid = NID_basic_constraints;
	idx = -1;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected basic constraints.\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_TRUE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected ca basic constraints.\n", __func__);
		goto err;
	}
	if (crit != 0) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected non-critical basic constraints.\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 2) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_REPLACE "
		    "expected 2 extensions, got %d.\n", __func__, got);
		goto err;
	}

	nid = NID_basic_constraints;
	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "want %d, got %d\n", __func__, 1, got);
		goto err;
	}

	if ((got = X509v3_get_ext_count(*extensions)) != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected 1 extension, got %d.\n", __func__, got);
		goto err;
	}

	/* The last deletion will have left the critical non-ca constraints. */
	nid = NID_basic_constraints;
	idx = -1;
	if ((bc = X509V3_get_d2i(*extensions, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected basic constraints.\n", __func__);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_FALSE) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected ca basic constraints.\n", __func__);
		goto err;
	}
	if (crit != 1) {
		fprintf(stderr, "%s: FAIL: X509V3_ADD_DELETE "
		    "expected critical basic constraints.\n", __func__);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/* Now delete the last extension. */
	nid = NID_basic_constraints;
	op = X509V3_ADD_DELETE;
	if ((got = X509V3_add1_i2d(extensions, nid, NULL, 0, op)) != 1) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_DELETE "
		    "want %d, got %d\n", __func__, 1, got);
		goto err;
	}

	if ((got = X509v3_get_ext_count(*extensions)) != 0) {
		fprintf(stderr, "%s: FAIL: second X509V3_ADD_DELETE "
		    "expected 0 extensions, got %d.\n", __func__, got);
		goto err;
	}

	failed = 0;

 err:
	BASIC_CONSTRAINTS_free(bc);

	return failed;
}

static int
test_x509v3_add1_i2d_invalid_operations(STACK_OF(X509_EXTENSION) **extensions)
{
	BASIC_CONSTRAINTS *bc = NULL;
	long error;
	int crit, got, nid, op;
	int failed = 1;

	if (X509v3_get_ext_count(*extensions) != 0) {
		fprintf(stderr, "%s: FAIL: need empty stack.\n", __func__);
		goto err;
	}

	/*
	 * Attempt to add a basic constraint extension with invalid operations
	 */

	nid = NID_basic_constraints;
	bc = create_basic_constraints(1);
	crit = 1;
	for (op = X509V3_ADD_DELETE + 1; op <= X509V3_ADD_OP_MASK; op++) {
		if ((got = X509V3_add1_i2d(extensions, nid, bc, crit, op)) != -1) {
			fprintf(stderr, "%s: FAIL: operation %d "
			    "want %d, got %d.\n", __func__, op, -1, got);
			goto err;
		}
		error = ERR_get_error();
		if (ERR_GET_REASON(error) != X509V3_R_UNSUPPORTED_OPTION) {
			fprintf(stderr, "%s: FAIL: invalid operation %d "
			    " pushed %d, want %d.\n", __func__, op,
			    ERR_GET_REASON(error), X509V3_R_EXTENSION_EXISTS);
			goto err;
		}
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	if ((got = X509v3_get_ext_count(*extensions)) != 0) {
		fprintf(stderr, "%s: FAIL: expected 0 extensions, have %d.\n",
		    __func__, got);
		goto err;
	}

	failed = 0;

 err:
	BASIC_CONSTRAINTS_free(bc);

	return failed;
}

static int
test_x509v3_add1_i2d(void)
{
	STACK_OF(X509_EXTENSION) *extensions;
	int failed = 0;

	if ((extensions = sk_X509_EXTENSION_new_null()) == NULL)
		errx(1, "sk_X509_EXTENSION_new_null");

	failed |= test_x509v3_add1_i2d_empty_stack(&extensions);
	failed |= test_x509v3_add1_i2d_single_nid(&extensions);
	failed |= test_x509v3_add1_i2d_add_append(&extensions);
	failed |= test_x509v3_add1_i2d_invalid_operations(&extensions);

	sk_X509_EXTENSION_pop_free(extensions, X509_EXTENSION_free);

	return failed;
}

static int
test_x509v3_get_d2i_null(void)
{
	X509_EXTENSION *ext;
	int crit, idx;
	int failed = 1;

	if ((ext = X509V3_get_d2i(NULL, NID_undef, NULL, NULL)) != NULL) {
		fprintf(stderr, "FAIL: %s: expected X509V3_get_d2i with three "
		    "NULL arguments to return NULL\n", __func__);
		goto err;
	}

	idx = -5;
	if (X509V3_get_d2i(NULL, NID_undef, &crit, &idx) != NULL) {
		/* Leaks whatever garbage libcrypto decoded. What to do... */
		fprintf(stderr, "FAIL: %s: expected X509V3_get_d2i NULL stack"
		    "to return NULL\n", __func__);
		goto err;
	}

	if (crit != -1 || idx != -1) {
		fprintf(stderr, "FAIL: %s: crit: want: %d, got: %d; "
		    "idx: want: %d, got: %d\n", __func__, -1, crit, -1, idx);
		goto err;
	}

	failed = 0;

 err:
	X509_EXTENSION_free(ext);

	return failed;
}

static int
test_x509v3_get_d2i_multiple_basic_constraints(void)
{
	STACK_OF(X509_EXTENSION) *exts = NULL;
	ASN1_BIT_STRING *abs = NULL;
	BASIC_CONSTRAINTS *bc = NULL;
	X509_EXTENSION *ext;
	int crit, idx;
	int ca, nid;
	int failed = 1;

	/*
	 * Create extension stack containing three basic constraints extensions:
	 * 1. critical CA basic constraints,
	 * 2. non-critical CA basic constraints,
	 * 3. critical non-CA basic constraints.
	 */

	if ((exts = sk_X509_EXTENSION_new_null()) == NULL)
		errx(1, "sk_X509_EXTENSION_new_null");

	ca = 1;
	ext = ext_create_basic_constraints(ca, X509V3_EXT_CRITICAL);

	if (sk_X509_EXTENSION_push(exts, ext) <= 0)
		errx(1, "sk_X509_EXTENSION_push");
	ext = NULL;

	ca = 1;
	ext = ext_create_basic_constraints(ca, X509V3_EXT_NONCRITICAL);

	if (sk_X509_EXTENSION_push(exts, ext) <= 0)
		errx(1, "sk_X509_EXTENSION_push");
	ext = NULL;

	ca = 0;
	ext = ext_create_basic_constraints(ca, X509V3_EXT_CRITICAL);

	if (sk_X509_EXTENSION_push(exts, ext) <= 0)
		errx(1, "sk_X509_EXTENSION_push");
	ext = NULL;

	/*
	 * There is no key usage in this stack, so we shouldn't find any.
	 */

	nid = NID_key_usage;
	if ((abs = X509V3_get_d2i(exts, nid, &crit, NULL)) != NULL) {
		fprintf(stderr, "FAIL: %s: found key usage extension\n",
		    __func__);
		goto err;
	}
	if (crit != -1) {
		fprintf(stderr, "FAIL: %s: key usage: crit: want %d, got %d\n",
		    __func__, -1, crit);
		goto err;
	}

	/*
	 * If we pass no idx and look for basic constraints,
	 * we should fail with crit == -2.
	 */

	nid = NID_basic_constraints;
	if ((bc = X509V3_get_d2i(exts, nid, &crit, NULL)) != NULL) {
		fprintf(stderr, "FAIL: %s (NULL idx): did not expect to find "
		    "basic constraints\n", __func__);
		goto err;
	}
	if (crit != -2) {
		fprintf(stderr, "FAIL: %s: basic constraints, no idx: \n"
		    "crit: want %d, got %d\n", __func__, -2, crit);
		goto err;
	}

	/*
	 * If we pass idx = -1 and look for basic constraints, we should find
	 * the first one: it is critical at idx = 0, with ca bit set to true.
	 */

	nid = NID_basic_constraints;
	idx = -1;
	if ((bc = X509V3_get_d2i(exts, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "FAIL: %s (idx %d): expected to find"
		    "basic constraints\n", __func__, -1);
		goto err;
	}
	if (crit != 1) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "crit: want %d, got %d\n", __func__, -1, 1, crit);
		goto err;
	}
	if (idx != 0) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "idx: want %d, got %d\n", __func__, -1, 0, idx);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_TRUE) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "cA bit: want %x, got %x\n", __func__, -1,
		    ASN1_BOOLEAN_TRUE, bc->ca);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Now pass idx = 0 and look for basic constraints, we should find
	 * the second one: non-critical at idx = 1, with ca bit set to true.
	 */

	nid = NID_basic_constraints;
	idx = 0;
	if ((bc = X509V3_get_d2i(exts, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "FAIL: %s (idx %d): expected to find"
		    "basic constraints\n", __func__, 0);
		goto err;
	}
	if (crit != 0) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "crit: want %d, got %d\n", __func__, 0, 0, crit);
		goto err;
	}
	if (idx != 1) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "idx: want %d, got %d\n", __func__, 0, 1, idx);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_TRUE) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "cA bit: want %x, got %x\n", __func__, 0,
		    ASN1_BOOLEAN_TRUE, bc->ca);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Now pass idx = 1 and look for basic constraints, we should find the
	 * third one: critical at idx = 2, with ca bit set to false.
	 */

	nid = NID_basic_constraints;
	idx = 1;
	if ((bc = X509V3_get_d2i(exts, nid, &crit, &idx)) == NULL) {
		fprintf(stderr, "FAIL: %s (idx %d): expected to find"
		    "basic constraints\n", __func__, 1);
		goto err;
	}
	if (crit != 1) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "crit: want %d, got %d\n", __func__, 1, 0, crit);
		goto err;
	}
	if (idx != 2) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "idx: want %d, got %d\n", __func__, 1, 2, idx);
		goto err;
	}
	if (bc->ca != ASN1_BOOLEAN_FALSE) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "cA bit: want %x, got %x\n", __func__, 1,
		    ASN1_BOOLEAN_FALSE, bc->ca);
		goto err;
	}
	BASIC_CONSTRAINTS_free(bc);
	bc = NULL;

	/*
	 * Finally, pass idx = 2 and we should find no basic constraints.
	 */

	nid = NID_basic_constraints;
	idx = 2;
	if ((bc = X509V3_get_d2i(exts, nid, &crit, &idx)) != NULL) {
		fprintf(stderr, "FAIL: %s (idx %d): expected to find"
		    "no basic constraints\n", __func__, 2);
		goto err;
	}
	if (crit != -1) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "crit: want %d, got %d\n", __func__, 2, -1, crit);
		goto err;
	}
	if (idx != -1) {
		fprintf(stderr, "FAIL: %s: basic constraints (idx %d): "
		    "idx: want %d, got %d\n", __func__, 2, -1, idx);
		goto err;
	}

	failed = 0;

 err:
	sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
	ASN1_BIT_STRING_free(abs);
	BASIC_CONSTRAINTS_free(bc);

	return failed;
}

static int
test_x509v3_get_d2i(void)
{
	int failed = 0;

	failed |= test_x509v3_get_d2i_null();
	failed |= test_x509v3_get_d2i_multiple_basic_constraints();

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_x509v3_add1_i2d();
	failed |= test_x509v3_get_d2i();

	return failed;
}
