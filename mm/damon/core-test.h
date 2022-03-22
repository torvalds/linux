/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifdef CONFIG_DAMON_KUNIT_TEST

#ifndef _DAMON_CORE_TEST_H
#define _DAMON_CORE_TEST_H

#include <kunit/test.h>

static void damon_test_regions(struct kunit *test)
{
	struct damon_region *r;
	struct damon_target *t;

	r = damon_new_region(1, 2);
	KUNIT_EXPECT_EQ(test, 1ul, r->ar.start);
	KUNIT_EXPECT_EQ(test, 2ul, r->ar.end);
	KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);

	t = damon_new_target(42);
	KUNIT_EXPECT_EQ(test, 0u, damon_nr_regions(t));

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, 1u, damon_nr_regions(t));

	damon_del_region(r, t);
	KUNIT_EXPECT_EQ(test, 0u, damon_nr_regions(t));

	damon_free_target(t);
}

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

static void damon_test_target(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;

	t = damon_new_target(42);
	KUNIT_EXPECT_EQ(test, 42ul, t->id);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_add_target(c, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_targets(c));

	damon_destroy_target(t);
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_destroy_ctx(c);
}

/*
 * Test kdamond_reset_aggregated()
 *
 * DAMON checks access to each region and aggregates this information as the
 * access frequency of each region.  In detail, it increases '->nr_accesses' of
 * regions that an access has confirmed.  'kdamond_reset_aggregated()' flushes
 * the aggregated information ('->nr_accesses' of each regions) to the result
 * buffer.  As a result of the flushing, the '->nr_accesses' of regions are
 * initialized to zero.
 */
static void damon_test_aggregate(struct kunit *test)
{
	struct damon_ctx *ctx = damon_new_ctx();
	unsigned long target_ids[] = {1, 2, 3};
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_target *t;
	struct damon_region *r;
	int it, ir;

	for (it = 0; it < 3; it++) {
		t = damon_new_target(target_ids[it]);
		damon_add_target(ctx, t);
	}

	it = 0;
	damon_for_each_target(t, ctx) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			r->nr_accesses = accesses[it][ir];
			damon_add_region(r, t);
		}
		it++;
	}
	kdamond_reset_aggregated(ctx);
	it = 0;
	damon_for_each_target(t, ctx) {
		ir = 0;
		/* '->nr_accesses' should be zeroed */
		damon_for_each_region(r, t) {
			KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);
			ir++;
		}
		/* regions should be preserved */
		KUNIT_EXPECT_EQ(test, 3, ir);
		it++;
	}
	/* targets also should be preserved */
	KUNIT_EXPECT_EQ(test, 3, it);

	damon_destroy_ctx(ctx);
}

static void damon_test_split_at(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;
	struct damon_region *r;

	t = damon_new_target(42);
	r = damon_new_region(0, 100);
	damon_add_region(r, t);
	damon_split_region_at(c, t, r, 25);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 25ul);

	r = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r->ar.start, 25ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 100ul);

	damon_free_target(t);
	damon_destroy_ctx(c);
}

static void damon_test_merge_two(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2, *r3;
	int i;

	t = damon_new_target(42);
	r = damon_new_region(0, 100);
	r->nr_accesses = 10;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 20;
	damon_add_region(r2, t);

	damon_merge_two_regions(t, r, r2);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 300ul);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 16u);

	i = 0;
	damon_for_each_region(r3, t) {
		KUNIT_EXPECT_PTR_EQ(test, r, r3);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 1);

	damon_free_target(t);
}

static struct damon_region *__nth_region_of(struct damon_target *t, int idx)
{
	struct damon_region *r;
	unsigned int i = 0;

	damon_for_each_region(r, t) {
		if (i++ == idx)
			return r;
	}

	return NULL;
}

static void damon_test_merge_regions_of(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sa[] = {0, 100, 114, 122, 130, 156, 170, 184};
	unsigned long ea[] = {100, 112, 122, 130, 156, 170, 184, 230};
	unsigned int nrs[] = {0, 0, 10, 10, 20, 30, 1, 2};

	unsigned long saddrs[] = {0, 114, 130, 156, 170};
	unsigned long eaddrs[] = {112, 130, 156, 170, 230};
	int i;

	t = damon_new_target(42);
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		r->nr_accesses = nrs[i];
		damon_add_region(r, t);
	}

	damon_merge_regions_of(t, 9, 9999);
	/* 0-112, 114-130, 130-156, 156-170 */
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 5u);
	for (i = 0; i < 5; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, saddrs[i]);
		KUNIT_EXPECT_EQ(test, r->ar.end, eaddrs[i]);
	}
	damon_free_target(t);
}

static void damon_test_split_regions_of(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_target *t;
	struct damon_region *r;

	t = damon_new_target(42);
	r = damon_new_region(0, 22);
	damon_add_region(r, t);
	damon_split_regions_of(c, t, 2);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 2u);
	damon_free_target(t);

	t = damon_new_target(42);
	r = damon_new_region(0, 220);
	damon_add_region(r, t);
	damon_split_regions_of(c, t, 4);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 4u);
	damon_free_target(t);
	damon_destroy_ctx(c);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_target),
	KUNIT_CASE(damon_test_regions),
	KUNIT_CASE(damon_test_aggregate),
	KUNIT_CASE(damon_test_split_at),
	KUNIT_CASE(damon_test_merge_two),
	KUNIT_CASE(damon_test_merge_regions_of),
	KUNIT_CASE(damon_test_split_regions_of),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_CORE_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
