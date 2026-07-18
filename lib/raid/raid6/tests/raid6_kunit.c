// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2002-2007 H. Peter Anvin - All Rights Reserved
 *
 * Test RAID-6 recovery algorithms.
 */

#include <kunit/test.h>
#include <linux/prandom.h>
#include <linux/vmalloc.h>
#include <linux/raid/pq.h>
#include "../algos.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

#define RAID6_KUNIT_SEED		42
#define RAID6_KUNIT_NUM_TEST_ITERS	10
#define RAID6_KUNIT_MAX_BUFFERS		64 /* Including P and Q */
#define RAID6_KUNIT_MAX_FAILURES	2
#define RAID6_KUNIT_MAX_BYTES		PAGE_SIZE

static struct rnd_state rng;
static void *test_buffers[RAID6_KUNIT_MAX_BUFFERS];
static void *aligned_buffers[RAID6_KUNIT_MAX_BUFFERS];
static void *test_recov_buffers[RAID6_KUNIT_MAX_FAILURES];
static size_t test_buflen;

struct test_args {
	unsigned int recov_idx;
	const struct raid6_recov_calls *recov;
	unsigned int gen_idx;
	const struct raid6_calls *gen;
};

static struct test_args args;

static u32 rand32(void)
{
	return prandom_u32_state(&rng);
}

/* Generate a random length that is a multiple of 512. */
static unsigned int random_length(unsigned int max_length)
{
	return round_up((rand32() % max_length) + 1, 512);
}

static unsigned int random_nr_buffers(void)
{
	return (rand32() % (RAID6_KUNIT_MAX_BUFFERS - (RAID6_MIN_DISKS - 1))) +
			RAID6_MIN_DISKS;
}

/* Generate a random alignment that is a multiple of 64. */
static unsigned int random_alignment(unsigned int max_alignment)
{
	if (max_alignment == 0)
		return 0;
	return (rand32() % (max_alignment + 1)) & ~63;
}

static void makedata(int start, int stop)
{
	int i;

	for (i = start; i <= stop; i++)
		prandom_bytes_state(&rng, test_buffers[i], test_buflen);
}

static char member_type(unsigned int nr_buffers, int d)
{
	if (d == nr_buffers - 2)
		return 'P';
	if (d == nr_buffers - 1)
		return 'Q';
	return 'D';
}

static void test_recover_one(struct kunit *test, unsigned int nr_buffers,
		unsigned int len, int faila, int failb)
{
	const struct test_args *ta = test->param_value;
	void *dataptrs[RAID6_KUNIT_MAX_BUFFERS];
	int i;

	if (faila > failb)
		swap(faila, failb);

	for (i = 0; i < RAID6_KUNIT_MAX_FAILURES; i++)
		memset(test_recov_buffers[i], 0xf0, test_buflen);

	memcpy(dataptrs, aligned_buffers, sizeof(dataptrs));
	dataptrs[faila] = test_recov_buffers[0];
	dataptrs[failb] = test_recov_buffers[1];

	if (failb == nr_buffers - 1) {
		/*
		 * We don't implement the data+Q failure scenario, since it
		 * is equivalent to a RAID-5 failure (XOR, then recompute Q).
		 */
		if (WARN_ON_ONCE(faila != nr_buffers - 2))
			return;

		/* P+Q failure.  Just rebuild the syndrome. */
		ta->gen->gen_syndrome(nr_buffers, len, dataptrs);
	} else if (failb == nr_buffers - 2) {
		/* data+P failure. */
		ta->recov->datap(nr_buffers, len, faila, dataptrs);
	} else {
		/* data+data failure. */
		ta->recov->data2(nr_buffers, len, faila, failb, dataptrs);
	}

	KUNIT_EXPECT_MEMEQ_MSG(test, aligned_buffers[faila], dataptrs[faila],
			len,
			"faila miscompared: %3d[%c] buffers %u len %u (failb=%3d[%c])\n",
			faila, member_type(nr_buffers, faila),
			nr_buffers, len,
			failb, member_type(nr_buffers, failb));
	KUNIT_EXPECT_MEMEQ_MSG(test, aligned_buffers[failb], dataptrs[failb],
			len,
			"failb miscompared: %3d[%c] buffers %u len %u (faila=%3d[%c])\n",
			failb, member_type(nr_buffers, failb),
			nr_buffers, len,
			faila, member_type(nr_buffers, faila));
}

static void test_recover(struct kunit *test, unsigned int nr_buffers,
		unsigned int len)
{
	unsigned int nr_data = nr_buffers - 2;
	int iterations, i;

	/* Test P+Q recovery */
	test_recover_one(test, nr_buffers, len, nr_data, nr_buffers - 1);

	/* Test data+P recovery */
	for (i = 0; i < nr_buffers - 2; i++)
		test_recover_one(test, nr_buffers, len, i, nr_data);

	/* Double data failure is impossible with a single data disk */
	if (nr_data == 1)
		return;

	/* Test data+data recovery using random sampling */
	iterations = nr_buffers * 2; /* should provide good enough coverage */
	for (i = 0; i < iterations; i++) {
		int faila = rand32() % nr_data, failb;

		do {
			failb = rand32() % nr_data;
		} while (failb == faila);

		test_recover_one(test, nr_buffers, len, faila, failb);
	}
}

/* Simulate rmw run */
static void test_rmw_one(struct kunit *test, unsigned int nr_buffers,
		unsigned int len, int p1, int p2)
{
	const struct test_args *ta = test->param_value;

	ta->gen->xor_syndrome(nr_buffers, p1, p2, len, aligned_buffers);
	makedata(p1, p2);
	ta->gen->xor_syndrome(nr_buffers, p1, p2, len, aligned_buffers);
	test_recover(test, nr_buffers, len);
}

static void test_rmw(struct kunit *test, unsigned int nr_buffers,
		unsigned int len)
{
	int iterations = nr_buffers / 2, i;

	for (i = 0; i < iterations; i++) {
		int p1 = rand32() % (nr_buffers - 2);
		int p2 = rand32() % (nr_buffers - 2);

		if (p2 < p1)
			swap(p1, p2);
		test_rmw_one(test, nr_buffers, len, p1, p2);
	}
}

static void raid6_test_one(struct kunit *test)
{
	const struct test_args *ta = test->param_value;
	unsigned int nr_buffers = random_nr_buffers();
	unsigned int len = random_length(RAID6_KUNIT_MAX_BYTES);
	unsigned int max_alignment;
	int i;

	/* Nuke syndromes */
	memset(test_buffers[nr_buffers - 2], 0xee, test_buflen);
	memset(test_buffers[nr_buffers - 1], 0xee, test_buflen);

	/*
	 * If we're not using the entire buffer size, inject randomize alignment
	 * into the buffer.
	 */
	max_alignment = RAID6_KUNIT_MAX_BYTES - len;
	if (rand32() % 2 == 0) {
		/* Use random alignments mod 64 */
		for (i = 0; i < nr_buffers; i++)
			aligned_buffers[i] = test_buffers[i] +
				random_alignment(max_alignment);
	} else {
		/* Go up to the guard page, to catch buffer overreads */
		unsigned int align = test_buflen - len;

		for (i = 0; i < nr_buffers; i++)
			aligned_buffers[i] = test_buffers[i] + align;
	}

	/* Generate assumed good syndrome */
	ta->gen->gen_syndrome(nr_buffers, len, aligned_buffers);

	test_recover(test, nr_buffers, len);

	if (ta->gen->xor_syndrome)
		test_rmw(test, nr_buffers, len);
}

static void raid6_test(struct kunit *test)
{
	int i;

	for (i = 0; i < RAID6_KUNIT_NUM_TEST_ITERS; i++)
		raid6_test_one(test);
}

static const void *raid6_gen_params(struct kunit *test, const void *prev,
		char *desc)
{
	if (!prev) {
		memset(&args, 0, sizeof(args));
next_algo:
		args.recov_idx = 0;
		args.gen = raid6_algo_find(args.gen_idx);
		if (!args.gen)
			return NULL;
	}

	if (args.recov)
		args.recov_idx++;
	args.recov = raid6_recov_algo_find(args.recov_idx);
	if (!args.recov) {
		args.gen_idx++;
		goto next_algo;
	}

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "gen=%s recov=%s",
			args.gen->name, args.recov->name);
	return &args;
}

static struct kunit_case raid6_test_cases[] = {
	KUNIT_CASE_PARAM(raid6_test, raid6_gen_params),
	{},
};

static int raid6_suite_init(struct kunit_suite *suite)
{
	int i;

	prandom_seed_state(&rng, RAID6_KUNIT_SEED);

	/*
	 * Allocate the test buffer using vmalloc() with a page-aligned length
	 * so that it is immediately followed by a guard page.  This allows
	 * buffer overreads to be detected, even in assembly code.
	 */
	test_buflen = round_up(RAID6_KUNIT_MAX_BYTES, PAGE_SIZE);
	for (i = 0; i < RAID6_KUNIT_MAX_FAILURES; i++) {
		test_recov_buffers[i] = vmalloc(test_buflen);
		if (!test_recov_buffers[i])
			goto out_free_recov_buffers;
	}
	for (i = 0; i < RAID6_KUNIT_MAX_BUFFERS; i++) {
		test_buffers[i] = vmalloc(test_buflen);
		if (!test_buffers[i])
			goto out_free_buffers;
	}

	makedata(0, RAID6_KUNIT_MAX_BUFFERS - 1);

	return 0;

out_free_buffers:
	for (i = 0; i < RAID6_KUNIT_MAX_BUFFERS; i++)
		vfree(test_buffers[i]);
	memset(test_buffers, 0, sizeof(test_buffers));
out_free_recov_buffers:
	for (i = 0; i < RAID6_KUNIT_MAX_FAILURES; i++)
		vfree(test_recov_buffers[i]);
	memset(test_recov_buffers, 0, sizeof(test_recov_buffers));
	return -ENOMEM;
}

static void raid6_suite_exit(struct kunit_suite *suite)
{
	int i;

	for (i = 0; i < RAID6_KUNIT_MAX_BUFFERS; i++)
		vfree(test_buffers[i]);
	memset(test_buffers, 0, sizeof(test_buffers));
	for (i = 0; i < RAID6_KUNIT_MAX_FAILURES; i++)
		vfree(test_recov_buffers[i]);
	memset(test_recov_buffers, 0, sizeof(test_recov_buffers));
}

static struct kunit_suite raid6_test_suite = {
	.name		= "raid6",
	.test_cases	= raid6_test_cases,
	.suite_init	= raid6_suite_init,
	.suite_exit	= raid6_suite_exit,
};
kunit_test_suite(raid6_test_suite);

MODULE_DESCRIPTION("Unit test for the RAID P/Q library functions");
MODULE_LICENSE("GPL");
