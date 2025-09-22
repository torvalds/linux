/* $OpenBSD: exdata_test.c,v 1.3 2024/10/02 14:12:21 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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
#include <string.h>

#include <openssl/crypto.h>

static int ex_new_calls;
static int ex_free_calls;
static int ex_dup_calls;

static int
ex_new(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx, long argl,
    void *argp)
{
	long *arg = argp;

	if (ptr != NULL) {
		fprintf(stderr, "FAIL: ex_new() called with ptr != NULL\n");
		return 0;
	}
	if (argl != 1234 || *arg != 1234) {
		fprintf(stderr, "FAIL: ex_new() with bad arguments\n");
		return 0;
	}

	ex_new_calls++;

	return 1;
}

static int
ex_dup(CRYPTO_EX_DATA *to, CRYPTO_EX_DATA *from, void *from_d,
    int idx, long argl, void *argp)
{
	long *arg = argp;

	if (argl != 1234 || *arg != 1234) {
		fprintf(stderr, "FAIL: ex_dup() with bad arguments\n");
		return 0;
	}

	ex_dup_calls++;

	return 1;
}

static void
ex_free(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx,
    long argl, void *argp)
{
	long *arg = argp;

	if (argl != 1234 || *arg != 1234) {
		fprintf(stderr, "FAIL: ex_free() with bad arguments\n");
		return;
	}

	ex_free_calls++;
}

struct exdata {
	CRYPTO_EX_DATA exdata;
	int val;
};

static int
ex_data_test(void)
{
	struct exdata exdata1, exdata2, exdata3, exdata4;
	void *argp;
	long argl;
	int idx1, idx2;
	int failed = 1;

	memset(&exdata1, 0, sizeof(exdata1));
	memset(&exdata2, 0, sizeof(exdata2));
	memset(&exdata3, 0, sizeof(exdata3));
	memset(&exdata4, 0, sizeof(exdata4));

	argl = 1234;
	argp = &argl;

	if ((idx1 = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_RSA, argl, argp,
	    ex_new, ex_dup, ex_free)) < 0) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index failed\n");
		goto failure;
	}
	if (idx1 == 0) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index() returned 0 "
		    "(reserved for internal use)\n");
		goto failure;
	}

	if ((idx2 = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_RSA, 0, NULL,
	    NULL, NULL, NULL)) < 0) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index failed\n");
		goto failure;
	}
	if (idx1 == idx2) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index() returned the "
		    "same value\n");
		goto failure;
	}
	if (idx2 < idx1) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index() returned "
		    "idx2 < idx1\n");
		goto failure;
	}

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_RSA, &exdata1, &exdata1.exdata)) {
		fprintf(stderr, "FAIL: CRYPTO_new_ex_data() failed\n");
		goto failure;
	}

	if (!CRYPTO_set_ex_data(&exdata1.exdata, idx2, &idx2)) {
		fprintf(stderr, "FAIL: CRYPTO_set_ex_data() failed with index 2\n");
		goto failure;
	}
	if (!CRYPTO_set_ex_data(&exdata1.exdata, idx1, &idx1)) {
		fprintf(stderr, "FAIL: CRYPTO_set_ex_data() failed with index 1\n");
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata1.exdata, idx1) != &idx1) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed with index 1\n");
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata1.exdata, idx2) != &idx2) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed with index 2\n");
		goto failure;
	}

	if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_RSA, &exdata2.exdata,
	    &exdata1.exdata)) {
		fprintf(stderr, "FAIL: CRYPTO_dup_ex_data() failed\n");
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata2.exdata, idx1) != &idx1) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed after dup\n");
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata2.exdata, idx2) != &idx2) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed after dup\n");
		goto failure;
	}

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, &exdata1, &exdata1.exdata);
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, &exdata2, &exdata2.exdata);

	if (ex_new_calls != 1) {
		fprintf(stderr, "FAIL: got %d ex_new calls, want %d\n",
		    ex_new_calls, 1);
		goto failure;
	}
	if (ex_dup_calls != 1) {
		fprintf(stderr, "FAIL: got %d ex_dup calls, want %d\n",
		    ex_dup_calls, 1);
		goto failure;
	}
	if (ex_free_calls != 2) {
		fprintf(stderr, "FAIL: got %d ex_free calls, want %d\n",
		    ex_free_calls, 2);
		goto failure;
	}

	/* The current implementation allows for data to be set without new. */
	if (!CRYPTO_set_ex_data(&exdata3.exdata, idx1, &idx1)) {
		fprintf(stderr, "FAIL: CRYPTO_set_ex_data() failed with index "
		    "1 (without new)\n");
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata3.exdata, idx1) != &idx1) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed with index "
		    "1 (without new)\n");
		goto failure;
	}

	/* And indexes can be used without allocation. */
	if (!CRYPTO_set_ex_data(&exdata3.exdata, idx2 + 1, &idx2)) {
		fprintf(stderr, "FAIL: CRYPTO_set_ex_data() failed with index "
		    "%d (unallocated)\n", idx2 + 1);
		goto failure;
	}
	if (CRYPTO_get_ex_data(&exdata3.exdata, idx2 + 1) != &idx2) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_data() failed with index "
		    "%d\n", idx2 + 1);
		goto failure;
	}

	/* And new can be called without getting any index first. */
	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_BIO, &exdata4, &exdata4.exdata)) {
		fprintf(stderr, "FAIL: CRYPTO_new_ex_data() failed with "
		    "uninitialised index\n");
		goto failure;
	}

	/* And dup can be called after new or without new... */
	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_RSA, &exdata1, &exdata1.exdata)) {
		fprintf(stderr, "FAIL: CRYPTO_new_ex_data() failed\n");
		goto failure;
	}
	if (!CRYPTO_dup_ex_data(CRYPTO_EX_INDEX_RSA, &exdata2.exdata,
	    &exdata1.exdata)) {
		fprintf(stderr, "FAIL: CRYPTO_dup_ex_data() after new failed\n");
		goto failure;
	}

	failed = 0;

 failure:
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, &exdata1, &exdata1.exdata);
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, &exdata2, &exdata2.exdata);
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, &exdata3, &exdata3.exdata);
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_BIO, &exdata4, &exdata4.exdata);

	return failed;
}

static int
ex_new_index_test(void)
{
	int failed = 1;
	int idx;

	if ((idx = CRYPTO_get_ex_new_index(-1, 0, NULL, NULL, NULL,
	    NULL)) > 0) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index() succeeded with "
		    "negative class\n");
		goto failure;
	}
	if ((idx = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX__COUNT, 0,
	    NULL, NULL, NULL, NULL)) > 0) {
		fprintf(stderr, "FAIL: CRYPTO_get_ex_new_index() succeeded with "
		    "class exceeding maximum\n");
		goto failure;
	}

	failed = 0;

 failure:
	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= ex_data_test();
	failed |= ex_new_index_test();

	/* Force a clean up. */
	CRYPTO_cleanup_all_ex_data();

	return failed;
}
