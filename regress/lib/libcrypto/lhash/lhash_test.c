/*	$OpenBSD: lhash_test.c,v 1.2 2024/05/08 15:13:23 jsing Exp $	*/
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/lhash.h>

/*
 * Need to add test coverage for:
 *  - custom hash function
 *  - custom comparison function
 */

static void
test_doall_count(void *arg1, void *arg2)
{
	int *count = arg2;

	(*count)++;
}

static int
test_lhash(void)
{
	const char *a = "a", *b = "b", *c = "c", *d = "d";
	const char *a2 = "a", *b2 = "b";
	_LHASH *lh;
	int count;
	int failed = 1;

	if ((lh = lh_new(NULL, NULL)) == NULL)
		goto failure;

	/*
	 * Another amazing API... both a successful insert and a failure will
	 * return NULL. The only way you can tell the difference is to follow
	 * with a call to lh_error().
	 */
	if (lh_retrieve(lh, "a") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved a before insert\n");
		goto failure;
	}
	if (lh_insert(lh, (void *)a) != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: insert a\n");
		goto failure;
	}
	if (lh_retrieve(lh, "a") != a) {
		fprintf(stderr, "FAIL: failed to retrieve a\n");
		goto failure;
	}

	if (lh_retrieve(lh, "b") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved b before insert\n");
		goto failure;
	}
	if (lh_insert(lh, (void *)b) != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: insert b\n");
		goto failure;
	}
	if (lh_retrieve(lh, "b") != b) {
		fprintf(stderr, "FAIL: failed to retrieve b\n");
		goto failure;
	}

	if (lh_retrieve(lh, "c") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved c before insert\n");
		goto failure;
	}
	if (lh_insert(lh, (void *)c) != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: insert c\n");
		goto failure;
	}
	if (lh_retrieve(lh, "c") != c) {
		fprintf(stderr, "FAIL: failed to retrieve c\n");
		goto failure;
	}

	if (lh_retrieve(lh, "d") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved d before insert\n");
		goto failure;
	}
	if (lh_insert(lh, (void *)d) != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: insert d\n");
		goto failure;
	}
	if (lh_retrieve(lh, "d") != d) {
		fprintf(stderr, "FAIL: failed to retrieve d\n");
		goto failure;
	}

	if (lh_num_items(lh) != 4) {
		fprintf(stderr, "FAIL: lh_num_items() = %ld, want 4\n",
		    lh_num_items(lh));
		goto failure;
	}

	/* Insert should replace. */
	if (lh_insert(lh, (void *)a2) != a || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: replace a\n");
		goto failure;
	}
	if (lh_retrieve(lh, "a") != a2) {
		fprintf(stderr, "FAIL: failed to retrieve a2\n");
		goto failure;
	}
	if (lh_insert(lh, (void *)b2) != b || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: replace b\n");
		goto failure;
	}
	if (lh_retrieve(lh, "b") != b2) {
		fprintf(stderr, "FAIL: failed to retrieve b2\n");
		goto failure;
	}

	if (lh_num_items(lh) != 4) {
		fprintf(stderr, "FAIL: lh_num_items() = %ld, want 4\n",
		    lh_num_items(lh));
		goto failure;
	}

	/* Do all. */
	count = 0;
	lh_doall_arg(lh, test_doall_count, &count);
	if (count != 4) {
		fprintf(stderr, "FAIL: lh_doall_arg failed (count = %d)\n",
		    count);
		goto failure;
	}

	/* Delete. */
	if (lh_delete(lh, "z") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: delete succeeded for z\n");
		goto failure;
	}
	if (lh_delete(lh, "a") != a2 || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: delete failed for a\n");
		goto failure;
	}
	if (lh_retrieve(lh, "a") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved a after deletion\n");
		goto failure;
	}
	if (lh_delete(lh, "b") != b2 || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: delete failed for b\n");
		goto failure;
	}
	if (lh_retrieve(lh, "b") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved b after deletion\n");
		goto failure;
	}
	if (lh_delete(lh, "c") != c || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: delete failed for c\n");
		goto failure;
	}
	if (lh_retrieve(lh, "c") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved c after deletion\n");
		goto failure;
	}
	if (lh_delete(lh, "d") != d || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: delete failed for d\n");
		goto failure;
	}
	if (lh_retrieve(lh, "d") != NULL || lh_error(lh) != 0) {
		fprintf(stderr, "FAIL: retrieved d after deletion\n");
		goto failure;
	}

	if (lh_num_items(lh) != 0) {
		fprintf(stderr, "FAIL: lh_num_items() = %ld, want 0\n",
		    lh_num_items(lh));
		goto failure;
	}

	failed = 0;

 failure:
	lh_free(lh);

	return failed;
}

static void
test_doall_fn(void *arg1)
{
}

static int
test_lhash_doall(void)
{
	_LHASH *lh;
	int i;
	int failed = 1;

	if ((lh = lh_new(NULL, NULL)) == NULL)
		goto failure;

	/* Call doall multiple times while linked hash is empty. */
	for (i = 0; i < 100; i++)
		lh_doall(lh, test_doall_fn);

	failed = 0;

 failure:
	lh_free(lh);

	return failed;
}

static void
test_doall_delete_some(void *arg1, void *arg2)
{
	void *data;

	if (arc4random_uniform(32) != 0)
		return;

	data = lh_delete(arg2, arg1);
	free(data);
}

static void
test_doall_delete_all(void *arg1, void *arg2)
{
	void *data;

	data = lh_delete(arg2, arg1);
	free(data);
}

static int
test_lhash_load(void)
{
	uint8_t c3 = 1, c2 = 1, c1 = 1, c0 = 1;
	_LHASH *lh;
	char *data = NULL;
	int i, j;
	int failed = 1;

	if ((lh = lh_new(NULL, NULL)) == NULL)
		goto failure;

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			if ((data = calloc(1, 128)) == NULL)
				goto failure;

			data[0] = c0;
			data[1] = c1;
			data[2] = c2;
			data[3] = c3;

			if (++c0 == 0) {
				c0++;
				c1++;
			}
			if (c1 == 0) {
				c1++;
				c2++;
			}
			if (c2 == 0) {
				c2++;
				c3++;
			}

			if (lh_insert(lh, data) != NULL || lh_error(lh) != 0) {
				fprintf(stderr, "FAIL: lh_insert() failed\n");
				goto failure;
			}
			data = NULL;
		}
		lh_doall_arg(lh, test_doall_delete_some, lh);
	}

	/* We should have ~31,713 entries. */
	if (lh_num_items(lh) < 31000 || lh_num_items(lh) > 33000) {
		fprintf(stderr, "FAIL: unexpected number of entries (%ld)\n",
		    lh_num_items(lh));
		goto failure;
	}

	failed = 0;

 failure:
	if (lh != NULL)
		lh_doall_arg(lh, test_doall_delete_all, lh);

	lh_free(lh);
	free(data);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_lhash();
	failed |= test_lhash_doall();
	failed |= test_lhash_load();

	return failed;
}
