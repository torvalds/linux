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

	t = damon_new_target();
	KUNIT_EXPECT_EQ(test, 0u, damon_nr_regions(t));

	damon_add_region(r, t);
	KUNIT_EXPECT_EQ(test, 1u, damon_nr_regions(t));

	damon_destroy_region(r, t);
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

	t = damon_new_target();
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
	unsigned long saddr[][3] = {{10, 20, 30}, {5, 42, 49}, {13, 33, 55} };
	unsigned long eaddr[][3] = {{15, 27, 40}, {31, 45, 55}, {23, 44, 66} };
	unsigned long accesses[][3] = {{42, 95, 84}, {10, 20, 30}, {0, 1, 2} };
	struct damon_target *t;
	struct damon_region *r;
	int it, ir;

	for (it = 0; it < 3; it++) {
		t = damon_new_target();
		damon_add_target(ctx, t);
	}

	it = 0;
	damon_for_each_target(t, ctx) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			r->nr_accesses = accesses[it][ir];
			r->nr_accesses_bp = accesses[it][ir] * 10000;
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

	t = damon_new_target();
	r = damon_new_region(0, 100);
	damon_add_region(r, t);
	damon_split_region_at(t, r, 25);
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

	t = damon_new_target();
	r = damon_new_region(0, 100);
	r->nr_accesses = 10;
	r->nr_accesses_bp = 100000;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	r2->nr_accesses = 20;
	r2->nr_accesses_bp = 200000;
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

	t = damon_new_target();
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		r->nr_accesses = nrs[i];
		r->nr_accesses_bp = nrs[i] * 10000;
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

	t = damon_new_target();
	r = damon_new_region(0, 22);
	damon_add_region(r, t);
	damon_split_regions_of(t, 2);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 2u);
	damon_free_target(t);

	t = damon_new_target();
	r = damon_new_region(0, 220);
	damon_add_region(r, t);
	damon_split_regions_of(t, 4);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 4u);
	damon_free_target(t);
	damon_destroy_ctx(c);
}

static void damon_test_ops_registration(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_operations ops, bak;

	/* DAMON_OPS_{V,P}ADDR are registered on subsys_initcall */
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, DAMON_OPS_VADDR), 0);
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, DAMON_OPS_PADDR), 0);

	/* Double-registration is prohibited */
	ops.id = DAMON_OPS_VADDR;
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), -EINVAL);
	ops.id = DAMON_OPS_PADDR;
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), -EINVAL);

	/* Unknown ops id cannot be registered */
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, NR_DAMON_OPS), -EINVAL);

	/* Registration should success after unregistration */
	mutex_lock(&damon_ops_lock);
	bak = damon_registered_ops[DAMON_OPS_VADDR];
	damon_registered_ops[DAMON_OPS_VADDR] = (struct damon_operations){};
	mutex_unlock(&damon_ops_lock);

	ops.id = DAMON_OPS_VADDR;
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), 0);

	mutex_lock(&damon_ops_lock);
	damon_registered_ops[DAMON_OPS_VADDR] = bak;
	mutex_unlock(&damon_ops_lock);

	/* Check double-registration failure again */
	KUNIT_EXPECT_EQ(test, damon_register_ops(&ops), -EINVAL);

	damon_destroy_ctx(c);
}

static void damon_test_set_regions(struct kunit *test)
{
	struct damon_target *t = damon_new_target();
	struct damon_region *r1 = damon_new_region(4, 16);
	struct damon_region *r2 = damon_new_region(24, 32);
	struct damon_addr_range range = {.start = 8, .end = 28};
	unsigned long expects[] = {8, 16, 16, 24, 24, 28};
	int expect_idx = 0;
	struct damon_region *r;

	damon_add_region(r1, t);
	damon_add_region(r2, t);
	damon_set_regions(t, &range, 1);

	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 3);
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, expects[expect_idx++]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expects[expect_idx++]);
	}
	damon_destroy_target(t);
}

static void damon_test_update_monitoring_result(struct kunit *test)
{
	struct damon_attrs old_attrs = {
		.sample_interval = 10, .aggr_interval = 1000,};
	struct damon_attrs new_attrs;
	struct damon_region *r = damon_new_region(3, 7);

	r->nr_accesses = 15;
	r->nr_accesses_bp = 150000;
	r->age = 20;

	new_attrs = (struct damon_attrs){
		.sample_interval = 100, .aggr_interval = 10000,};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 15);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 1000};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 150);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 100};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 150);
	KUNIT_EXPECT_EQ(test, r->age, 20);

	damon_free_region(r);
}

static void damon_test_set_attrs(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_attrs valid_attrs = {
		.min_nr_regions = 10, .max_nr_regions = 1000,
		.sample_interval = 5000, .aggr_interval = 100000,};
	struct damon_attrs invalid_attrs;

	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &valid_attrs), 0);

	invalid_attrs = valid_attrs;
	invalid_attrs.min_nr_regions = 1;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	invalid_attrs = valid_attrs;
	invalid_attrs.max_nr_regions = 9;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	invalid_attrs = valid_attrs;
	invalid_attrs.aggr_interval = 4999;
	KUNIT_EXPECT_EQ(test, damon_set_attrs(c, &invalid_attrs), -EINVAL);

	damon_destroy_ctx(c);
}

static void damon_test_moving_sum(struct kunit *test)
{
	unsigned int mvsum = 50000, nomvsum = 50000, len_window = 10;
	unsigned int new_values[] = {10000, 0, 10000, 0, 0, 0, 10000, 0, 0, 0};
	unsigned int expects[] = {55000, 50000, 55000, 50000, 45000, 40000,
		45000, 40000, 35000, 30000};
	int i;

	for (i = 0; i < ARRAY_SIZE(new_values); i++) {
		mvsum = damon_moving_sum(mvsum, nomvsum, len_window,
				new_values[i]);
		KUNIT_EXPECT_EQ(test, mvsum, expects[i]);
	}
}

static void damos_test_new_filter(struct kunit *test)
{
	struct damos_filter *filter;

	filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true);
	KUNIT_EXPECT_EQ(test, filter->type, DAMOS_FILTER_TYPE_ANON);
	KUNIT_EXPECT_EQ(test, filter->matching, true);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.prev, &filter->list);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.next, &filter->list);
	damos_destroy_filter(filter);
}

static void damos_test_filter_out(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2;
	struct damos_filter *f;

	f = damos_new_filter(DAMOS_FILTER_TYPE_ADDR, true);
	f->addr_range = (struct damon_addr_range){
		.start = DAMON_MIN_REGION * 2, .end = DAMON_MIN_REGION * 6};

	t = damon_new_target();
	r = damon_new_region(DAMON_MIN_REGION * 3, DAMON_MIN_REGION * 5);
	damon_add_region(r, t);

	/* region in the range */
	KUNIT_EXPECT_TRUE(test, __damos_filter_out(NULL, t, r, f));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region before the range */
	r->ar.start = DAMON_MIN_REGION * 1;
	r->ar.end = DAMON_MIN_REGION * 2;
	KUNIT_EXPECT_FALSE(test, __damos_filter_out(NULL, t, r, f));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region after the range */
	r->ar.start = DAMON_MIN_REGION * 6;
	r->ar.end = DAMON_MIN_REGION * 8;
	KUNIT_EXPECT_FALSE(test, __damos_filter_out(NULL, t, r, f));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region started before the range */
	r->ar.start = DAMON_MIN_REGION * 1;
	r->ar.end = DAMON_MIN_REGION * 4;
	KUNIT_EXPECT_FALSE(test, __damos_filter_out(NULL, t, r, f));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, DAMON_MIN_REGION * 1);
	KUNIT_EXPECT_EQ(test, r->ar.end, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, r2->ar.end, DAMON_MIN_REGION * 4);
	damon_destroy_region(r2, t);

	/* region started in the range */
	r->ar.start = DAMON_MIN_REGION * 2;
	r->ar.end = DAMON_MIN_REGION * 8;
	KUNIT_EXPECT_TRUE(test, __damos_filter_out(NULL, t, r, f));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, DAMON_MIN_REGION * 2);
	KUNIT_EXPECT_EQ(test, r->ar.end, DAMON_MIN_REGION * 6);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, DAMON_MIN_REGION * 6);
	KUNIT_EXPECT_EQ(test, r2->ar.end, DAMON_MIN_REGION * 8);
	damon_destroy_region(r2, t);

	damon_free_target(t);
	damos_free_filter(f);
}

static struct kunit_case damon_test_cases[] = {
	KUNIT_CASE(damon_test_target),
	KUNIT_CASE(damon_test_regions),
	KUNIT_CASE(damon_test_aggregate),
	KUNIT_CASE(damon_test_split_at),
	KUNIT_CASE(damon_test_merge_two),
	KUNIT_CASE(damon_test_merge_regions_of),
	KUNIT_CASE(damon_test_split_regions_of),
	KUNIT_CASE(damon_test_ops_registration),
	KUNIT_CASE(damon_test_set_regions),
	KUNIT_CASE(damon_test_update_monitoring_result),
	KUNIT_CASE(damon_test_set_attrs),
	KUNIT_CASE(damon_test_moving_sum),
	KUNIT_CASE(damos_test_new_filter),
	KUNIT_CASE(damos_test_filter_out),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_CORE_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
