/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <atf-c.h>
#define _SEARCH_PRIVATE
#include <search.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

static int n_nodes = 0;
static int n_seen = 0;

/* Validates the integrity of an AVL tree. */
static inline unsigned int
tnode_assert(const posix_tnode *n)
{
	unsigned int height_left, height_right;
	int balance;

	if (n == NULL)
		return 0;
	height_left = tnode_assert(n->llink);
	height_right = tnode_assert(n->rlink);
	balance = (int)height_left - (int)height_right;
	ATF_CHECK(balance >= -1);
	ATF_CHECK(balance <= 1);
	ATF_CHECK_EQ(balance, n->balance);
	return (height_left > height_right ? height_left : height_right) + 1;
}

static int
compar(const void *a, const void *b)
{

	return *(int *)a - *(int *)b;
}

static void
treewalk(const posix_tnode *node, VISIT v, int level)
{

	if (v == postorder || v == leaf)
		n_seen++;
}

ATF_TC_WITHOUT_HEAD(tsearch_test);
ATF_TC_BODY(tsearch_test, tc)
{
	/*
	 * Run the test below in a deterministic fashion to prevent this
	 * test from potentially flapping. We assume that this provides
	 * enough coverage.
	 */
#if 0
	unsigned short random_state[3];
	arc4random_buf(random_state, sizeof(random_state));
#else
	unsigned short random_state[3] = { 26554, 13330, 3246 };
#endif

#define NKEYS 1000
	/* Create 1000 possible keys. */
	int keys[NKEYS];
	for (int i = 0; i < NKEYS; ++i)
		keys[i] = i;

	/* Apply random operations on a binary tree and check the results. */
	posix_tnode *root = NULL;
	bool present[NKEYS] = {};
	for (int i = 0; i < NKEYS * 10; ++i) {
		int key = nrand48(random_state) % NKEYS;
		int sample = i;

		/*
		 * Ensure each case is tested at least 10 times, plus a
		 * random sampling.
		 */
		if ((sample % NKEYS) > 3)
			sample = nrand48(random_state) % 3;

		switch (sample) {
		case 0:  /* tdelete(). */
			if (present[key]) {
				ATF_CHECK(tdelete(&key, &root, compar) != NULL);
				present[key] = false;
				ATF_CHECK(n_nodes > 0);
				n_nodes--;
			} else {
				ATF_CHECK_EQ(NULL,
				    tdelete(&key, &root, compar));
			}
			break;
		case 1:  /* tfind(). */
			if (present[key]) {
				ATF_CHECK_EQ(&keys[key],
				    *(int **)tfind(&key, &root, compar));
			} else {
				ATF_CHECK_EQ(NULL, tfind(&key, &root, compar));
			}
			break;
		case 2:  /* tsearch(). */
			if (present[key]) {
				ATF_CHECK_EQ(&keys[key],
				    *(int **)tsearch(&key, &root, compar));
			} else {
				ATF_CHECK_EQ(&keys[key], *(int **)tsearch(
				    &keys[key], &root, compar));
				present[key] = true;
				n_nodes++;
			}
			break;
		}
		tnode_assert(root);
	}

	/* Walk the tree. */
	twalk(root, treewalk);
	ATF_CHECK_EQ(n_nodes, n_seen);

	/* Remove all entries from the tree. */
	for (int key = 0; key < NKEYS; ++key)
		if (present[key])
			ATF_CHECK(tdelete(&key, &root, compar) != NULL);
	ATF_CHECK_EQ(NULL, root);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, tsearch_test);

	return (atf_no_error());
}
