/*	$OpenBSD: bio_chain.c,v 1.16 2023/08/07 11:00:54 tb Exp $	*/
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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
#include <string.h>

#include <openssl/bio.h>

#include "bio_local.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define CHAIN_POP_LEN		5
#define LINK_CHAIN_A_LEN	8
#define LINK_CHAIN_B_LEN	5

static BIO *
BIO_prev(BIO *bio)
{
	if (bio == NULL)
		return NULL;

	return bio->prev_bio;
}

static void bio_chain_destroy(BIO **, size_t);

static int
bio_chain_create(const BIO_METHOD *meth, BIO *chain[], size_t len)
{
	BIO *prev;
	size_t i;

	memset(chain, 0, len * sizeof(BIO *));

	prev = NULL;
	for (i = 0; i < len; i++) {
		if ((chain[i] = BIO_new(meth)) == NULL) {
			fprintf(stderr, "BIO_new failed\n");
			goto err;
		}
		if ((prev = BIO_push(prev, chain[i])) == NULL) {
			fprintf(stderr, "BIO_push failed\n");
			goto err;
		}
	}

	return 1;

 err:
	bio_chain_destroy(chain, len);

	return 0;
}

static void
bio_chain_destroy(BIO *chain[], size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		BIO_free(chain[i]);

	memset(chain, 0, len * sizeof(BIO *));
}

static int
bio_chain_pop_test(void)
{
	BIO *bio[CHAIN_POP_LEN];
	BIO *prev, *next;
	size_t i, j;
	int failed = 1;

	for (i = 0; i < nitems(bio); i++) {
		memset(bio, 0, sizeof(bio));
		prev = NULL;

		if (!bio_chain_create(BIO_s_null(), bio, nitems(bio)))
			goto err;

		/* Check that the doubly-linked list was set up as expected. */
		if (BIO_prev(bio[0]) != NULL) {
			fprintf(stderr,
			    "i = %zu: first BIO has predecessor\n", i);
			goto err;
		}
		if (BIO_next(bio[nitems(bio) - 1]) != NULL) {
			fprintf(stderr, "i = %zu: last BIO has successor\n", i);
			goto err;
		}
		for (j = 0; j < nitems(bio); j++) {
			if (j > 0) {
				if (BIO_prev(bio[j]) != bio[j - 1]) {
					fprintf(stderr, "i = %zu: "
					    "BIO_prev(bio[%zu]) != bio[%zu]\n",
					    i, j, j - 1);
					goto err;
				}
			}
			if (j < nitems(bio) - 1) {
				if (BIO_next(bio[j]) != bio[j + 1]) {
					fprintf(stderr, "i = %zu: "
					    "BIO_next(bio[%zu]) != bio[%zu]\n",
					    i, j, j + 1);
					goto err;
				}
			}
		}

		/* Drop the ith bio from the chain. */
		next = BIO_pop(bio[i]);

		if (BIO_prev(bio[i]) != NULL || BIO_next(bio[i]) != NULL) {
			fprintf(stderr,
			    "BIO_pop() didn't isolate bio[%zu]\n", i);
			goto err;
		}

		if (i < nitems(bio) - 1) {
			if (next != bio[i + 1]) {
				fprintf(stderr, "BIO_pop(bio[%zu]) did not "
				    "return bio[%zu]\n", i, i + 1);
				goto err;
			}
		} else {
			if (next != NULL) {
				fprintf(stderr, "i = %zu: "
				    "BIO_pop(last) != NULL\n", i);
				goto err;
			}
		}

		/*
		 * Walk the remainder of the chain and see if the doubly linked
		 * list checks out.
		 */
		if (i == 0) {
			prev = bio[1];
			j = 2;
		} else {
			prev = bio[0];
			j = 1;
		}

		for (; j < nitems(bio); j++) {
			if (j == i)
				continue;
			if (BIO_next(prev) != bio[j]) {
				fprintf(stderr, "i = %zu, j = %zu: "
				    "BIO_next(prev) != bio[%zu]\n", i, j, j);
				goto err;
			}
			if (BIO_prev(bio[j]) != prev) {
				fprintf(stderr, "i = %zu, j = %zu: "
				    "BIO_prev(bio[%zu]) != prev\n", i, j, j);
				goto err;
			}
			prev = bio[j];
		}

		if (BIO_next(prev) != NULL) {
			fprintf(stderr, "i = %zu: BIO_next(prev) != NULL\n", i);
			goto err;
		}

		bio_chain_destroy(bio, nitems(bio));
	}

	failed = 0;

 err:
	bio_chain_destroy(bio, nitems(bio));

	return failed;
}

static void
walk(BIO *(*step)(BIO *), BIO *start, BIO **end, size_t *len)
{
	BIO *current = NULL;
	BIO *next = start;

	*len = 0;
	while (next != NULL) {
		current = next;
		next = step(current);
		(*len)++;
	}
	*end = current;
}

static int
walk_report(BIO *last, BIO *expected_last, size_t len, size_t expected_len,
    size_t i, size_t j, const char *fn, const char *description,
    const char *direction, const char *last_name)
{
	if (last != expected_last) {
		fprintf(stderr, "%s case (%zu, %zu) %s %s has unexpected %s\n",
		    fn, i, j, description, direction, last_name);
		return 0;
	}

	if (len != expected_len) {
		fprintf(stderr, "%s case (%zu, %zu) %s %s want %zu, got %zu\n",
		    fn, i, j, description, direction, expected_len, len);
		return 0;
	}

	return 1;
}

static int
walk_forward(BIO *start, BIO *expected_end, size_t expected_len,
    size_t i, size_t j, const char *fn, const char *description)
{
	BIO *end;
	size_t len;

	walk(BIO_next, start, &end, &len);

	return walk_report(end, expected_end, len, expected_len,
	    i, j, fn, description, "forward", "end");
}

static int
walk_backward(BIO *expected_start, BIO *end, size_t expected_len,
    size_t i, size_t j, const char *fn, const char *description)
{
	BIO *start;
	size_t len;

	walk(BIO_prev, end, &start, &len);

	return walk_report(start, expected_start, len, expected_len,
	    i, j, fn, description, "backward", "start");
}

static int
check_chain(BIO *start, BIO *end, size_t expected_len, size_t i, size_t j,
    const char *fn, const char *description)
{
	if (!walk_forward(start, end, expected_len, i, j, fn, description))
		return 0;

	if (!walk_backward(start, end, expected_len, i, j, fn, description))
		return 0;

	return 1;
}

/*
 * Link two linear chains of BIOs A[] and B[] together using either
 * BIO_push(A[i], B[j]) or BIO_set_next(A[i], B[j]).
 *
 * BIO_push() first walks the chain A[] to its end and then appends the tail
 * of chain B[] starting at B[j]. If j > 0, we get two chains
 *
 *     A[0] -- ... -- A[nitems(A) - 1] -- B[j] -- ... -- B[nitems(B) - 1]
 *                                      `- link created by BIO_push()
 *     B[0] -- ... -- B[j-1]
 *       |<-- oldhead -->|
 *
 * of lengths nitems(A) + nitems(B) - j and j, respectively.
 * If j == 0, the second chain (oldhead) is empty. One quirk of BIO_push() is
 * that the outcome of BIO_push(A[i], B[j]) apart from the return value is
 * independent of i.
 *
 * Prior to bio_lib.c r1.41, BIO_push(A[i], B[j]) would fail to dissociate the
 * two chains and leave B[j] with two parents for 0 < j < nitems(B).
 * B[j]->prev_bio would point at A[nitems(A) - 1], while both B[j - 1] and
 * A[nitems(A) - 1] would point at B[j]. In particular, BIO_free_all(A[0])
 * followed by BIO_free_all(B[0]) results in a double free of B[j].
 *
 * The result for BIO_set_next() is different: three chains are created.
 *
 *                                 |--- oldtail -->
 *     ... -- A[i-1] -- A[i] -- A[i+1] -- ...
 *                         \
 *                          \  link created by BIO_set_next()
 *     --- oldhead -->|      \
 *          ... -- B[j-1] -- B[j] -- B[j+1] -- ...
 *
 * After creating a new link, the new chain has length i + 1 + nitems(B) - j,
 * oldtail has length nitems(A) - i - 1 and oldhead has length j.
 *
 * Prior to bio_lib.c r1.40, BIO_set_next(A[i], B[j]) would result in both A[i]
 * and B[j - 1] pointing at B[j] while B[j] would point back at A[i]. Calling
 * BIO_free_all(A[0]) and BIO_free_all(B[0]) results in a double free of B[j].
 *
 * XXX: Should check that the callback is called on BIO_push() as expected.
 */

static int
link_chains_at(size_t i, size_t j, int use_bio_push)
{
	const char *fn = use_bio_push ? "BIO_push" : "BIO_set_next";
	BIO *A[LINK_CHAIN_A_LEN], *B[LINK_CHAIN_B_LEN];
	BIO *new_start, *new_end;
	BIO *oldhead_start, *oldhead_end, *oldtail_start, *oldtail_end;
	size_t new_len, oldhead_len, oldtail_len;
	int failed = 1;

	memset(A, 0, sizeof(A));
	memset(B, 0, sizeof(B));

	if (i >= nitems(A) || j >= nitems(B))
		goto err;

	/* Create two linear chains of BIOs. */
	if (!bio_chain_create(BIO_s_null(), A, nitems(A)))
		goto err;
	if (!bio_chain_create(BIO_s_null(), B, nitems(B)))
		goto err;

	/*
	 * Set our expectations. ... it's complicated.
	 */

	new_start = A[0];
	new_end = B[nitems(B) - 1];
	/* new_len depends on use_bio_push. It is set a few lines down. */

	oldhead_start = B[0];
	oldhead_end = BIO_prev(B[j]);
	oldhead_len = j;

	/* If we push B[0] or set next to B[0], the oldhead chain is empty. */
	if (j == 0) {
		oldhead_start = NULL;
		oldhead_end = NULL;
		oldhead_len = 0;
	}

	if (use_bio_push) {
		new_len = nitems(A) + nitems(B) - j;

		/* oldtail doesn't exist in the BIO_push() case. */
		oldtail_start = NULL;
		oldtail_end = NULL;
		oldtail_len = 0;
	} else {
		new_len = i + 1 + nitems(B) - j;

		oldtail_start = BIO_next(A[i]);
		oldtail_end = A[nitems(A) - 1];
		oldtail_len = nitems(A) - i - 1;

		/* If we set next on end of A[], the oldtail chain is empty. */
		if (i == nitems(A) - 1) {
			oldtail_start = NULL;
			oldtail_end = NULL;
			oldtail_len = 0;
		}
	}

	/* The two chains A[] and B[] are split into three disjoint pieces. */
	if (nitems(A) + nitems(B) != new_len + oldtail_len + oldhead_len) {
		fprintf(stderr, "%s case (%zu, %zu) inconsistent lengths: "
		    "%zu + %zu != %zu + %zu + %zu\n", fn, i, j,
		    nitems(A), nitems(B), new_len, oldtail_len, oldhead_len);
		goto err;
	}

	/*
	 * Now actually push or set next.
	 */

	if (use_bio_push) {
		if (BIO_push(A[i], B[j]) != A[i]) {
			fprintf(stderr, "BIO_push(A[%zu], B[%zu]) != A[%zu]\n",
			    i, j, i);
			goto err;
		}
	} else {
		BIO_set_next(A[i], B[j]);
	}

	/*
	 * Check that all the chains match our expectations.
	 */

	if (!check_chain(new_start, new_end, new_len, i, j, fn, "new chain"))
		goto err;

	if (!check_chain(oldhead_start, oldhead_end, oldhead_len, i, j, fn,
	    "oldhead"))
		goto err;

	if (!check_chain(oldtail_start, oldtail_end, oldtail_len, i, j, fn,
	    "oldtail"))
		goto err;

	/*
	 * All sanity checks passed. We can now free the chains
	 * with the BIO API without risk of leaks or double frees.
	 */

	BIO_free_all(new_start);
	BIO_free_all(oldhead_start);
	BIO_free_all(oldtail_start);

	memset(A, 0, sizeof(A));
	memset(B, 0, sizeof(B));

	failed = 0;

 err:
	bio_chain_destroy(A, nitems(A));
	bio_chain_destroy(B, nitems(B));

	return failed;
}

static int
link_chains(int use_bio_push)
{
	size_t i, j;
	int failure = 0;

	for (i = 0; i < LINK_CHAIN_A_LEN; i++) {
		for (j = 0; j < LINK_CHAIN_B_LEN; j++) {
			failure |= link_chains_at(i, j, use_bio_push);
		}
	}

	return failure;
}

static int
bio_push_link_test(void)
{
	int use_bio_push = 1;

	return link_chains(use_bio_push);
}

static int
bio_set_next_link_test(void)
{
	int use_bio_push = 0;

	return link_chains(use_bio_push);
}

static long
dup_leak_cb(BIO *bio, int cmd, const char *argp, int argi, long argl, long ret)
{
	if (argi == BIO_CTRL_DUP)
		return 0;

	return ret;
}

static int
bio_dup_chain_leak(void)
{
	BIO *bio[CHAIN_POP_LEN];
	BIO *dup;
	int failed = 1;

	if (!bio_chain_create(BIO_s_null(), bio, nitems(bio)))
		goto err;

	if ((dup = BIO_dup_chain(bio[0])) == NULL) {
		fprintf(stderr, "BIO_set_callback() failed\n");
		goto err;
	}

	BIO_set_callback(bio[CHAIN_POP_LEN - 1], dup_leak_cb);

	BIO_free_all(dup);
	if ((dup = BIO_dup_chain(bio[0])) != NULL) {
		fprintf(stderr, "BIO_dup_chain() succeeded unexpectedly\n");
		BIO_free_all(dup);
		goto err;
	}

	failed = 0;

 err:
	bio_chain_destroy(bio, nitems(bio));

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= bio_chain_pop_test();
	failed |= bio_push_link_test();
	failed |= bio_set_next_link_test();
	failed |= bio_dup_chain_leak();

	return failed;
}
