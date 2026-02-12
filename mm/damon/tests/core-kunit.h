/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.  All rights reserved.
 *
 * Author: SeongJae Park <sj@kernel.org>
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
	if (!r)
		kunit_skip(test, "region alloc fail");
	KUNIT_EXPECT_EQ(test, 1ul, r->ar.start);
	KUNIT_EXPECT_EQ(test, 2ul, r->ar.end);
	KUNIT_EXPECT_EQ(test, 0u, r->nr_accesses);

	t = damon_new_target();
	if (!t) {
		damon_free_region(r);
		kunit_skip(test, "target alloc fail");
	}
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

	if (!c)
		kunit_skip(test, "ctx alloc fail");

	t = damon_new_target();
	if (!t) {
		damon_destroy_ctx(c);
		kunit_skip(test, "target alloc fail");
	}
	KUNIT_EXPECT_EQ(test, 0u, nr_damon_targets(c));

	damon_add_target(c, t);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_targets(c));

	damon_destroy_target(t, c);
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

	if (!ctx)
		kunit_skip(test, "ctx alloc fail");

	for (it = 0; it < 3; it++) {
		t = damon_new_target();
		if (!t) {
			damon_destroy_ctx(ctx);
			kunit_skip(test, "target alloc fail");
		}
		damon_add_target(ctx, t);
	}

	it = 0;
	damon_for_each_target(t, ctx) {
		for (ir = 0; ir < 3; ir++) {
			r = damon_new_region(saddr[it][ir], eaddr[it][ir]);
			if (!r) {
				damon_destroy_ctx(ctx);
				kunit_skip(test, "region alloc fail");
			}
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
	struct damon_target *t;
	struct damon_region *r, *r_new;

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "target alloc fail");
	r = damon_new_region(0, 100);
	if (!r) {
		damon_free_target(t);
		kunit_skip(test, "region alloc fail");
	}
	r->nr_accesses_bp = 420000;
	r->nr_accesses = 42;
	r->last_nr_accesses = 15;
	r->age = 10;
	damon_add_region(r, t);
	damon_split_region_at(t, r, 25);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 25ul);

	r_new = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r_new->ar.start, 25ul);
	KUNIT_EXPECT_EQ(test, r_new->ar.end, 100ul);

	KUNIT_EXPECT_EQ(test, r->nr_accesses_bp, r_new->nr_accesses_bp);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, r_new->nr_accesses);
	KUNIT_EXPECT_EQ(test, r->last_nr_accesses, r_new->last_nr_accesses);
	KUNIT_EXPECT_EQ(test, r->age, r_new->age);

	damon_free_target(t);
}

static void damon_test_merge_two(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2, *r3;
	int i;

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "target alloc fail");
	r = damon_new_region(0, 100);
	if (!r) {
		damon_free_target(t);
		kunit_skip(test, "region alloc fail");
	}
	r->nr_accesses = 10;
	r->nr_accesses_bp = 100000;
	r->age = 9;
	damon_add_region(r, t);
	r2 = damon_new_region(100, 300);
	if (!r2) {
		damon_free_target(t);
		kunit_skip(test, "second region alloc fail");
	}
	r2->nr_accesses = 20;
	r2->nr_accesses_bp = 200000;
	r2->age = 21;
	damon_add_region(r2, t);

	damon_merge_two_regions(t, r, r2);
	KUNIT_EXPECT_EQ(test, r->ar.start, 0ul);
	KUNIT_EXPECT_EQ(test, r->ar.end, 300ul);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 16u);
	KUNIT_EXPECT_EQ(test, r->nr_accesses_bp, 160000u);
	KUNIT_EXPECT_EQ(test, r->age, 17u);

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
	unsigned long sa[] = {0, 100, 114, 122, 130, 156, 170, 184, 230};
	unsigned long ea[] = {100, 112, 122, 130, 156, 170, 184, 230, 10170};
	unsigned int nrs[] = {0, 0, 10, 10, 20, 30, 1, 2, 5};

	unsigned long saddrs[] = {0, 114, 130, 156, 170, 230};
	unsigned long eaddrs[] = {112, 130, 156, 170, 230, 10170};
	int i;

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "target alloc fail");
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		if (!r) {
			damon_free_target(t);
			kunit_skip(test, "region alloc fail");
		}
		r->nr_accesses = nrs[i];
		r->nr_accesses_bp = nrs[i] * 10000;
		damon_add_region(r, t);
	}

	damon_merge_regions_of(t, 9, 9999);
	/* 0-112, 114-130, 130-156, 156-170, 170-230, 230-10170 */
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 6u);
	for (i = 0; i < 6; i++) {
		r = __nth_region_of(t, i);
		KUNIT_EXPECT_EQ(test, r->ar.start, saddrs[i]);
		KUNIT_EXPECT_EQ(test, r->ar.end, eaddrs[i]);
	}
	damon_free_target(t);
}

static void damon_test_split_regions_of(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sa[] = {0, 300, 500};
	unsigned long ea[] = {220, 400, 700};
	int i;

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "target alloc fail");
	r = damon_new_region(0, 22);
	if (!r) {
		damon_free_target(t);
		kunit_skip(test, "region alloc fail");
	}
	damon_add_region(r, t);
	damon_split_regions_of(t, 2, 1);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 2u);
	damon_free_target(t);

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "second target alloc fail");
	r = damon_new_region(0, 220);
	if (!r) {
		damon_free_target(t);
		kunit_skip(test, "second region alloc fail");
	}
	damon_add_region(r, t);
	damon_split_regions_of(t, 4, 1);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 4u);
	damon_free_target(t);

	t = damon_new_target();
	if (!t)
		kunit_skip(test, "third target alloc fail");
	for (i = 0; i < ARRAY_SIZE(sa); i++) {
		r = damon_new_region(sa[i], ea[i]);
		if (!r) {
			damon_free_target(t);
			kunit_skip(test, "region alloc fail");
		}
		damon_add_region(r, t);
	}
	damon_split_regions_of(t, 4, 5);
	KUNIT_EXPECT_LE(test, damon_nr_regions(t), 12u);
	damon_for_each_region(r, t)
		KUNIT_EXPECT_GE(test, damon_sz_region(r) % 5ul, 0ul);
	damon_free_target(t);
}

static void damon_test_ops_registration(struct kunit *test)
{
	struct damon_ctx *c = damon_new_ctx();
	struct damon_operations ops = {.id = DAMON_OPS_VADDR}, bak;
	bool need_cleanup = false;

	if (!c)
		kunit_skip(test, "ctx alloc fail");

	/* DAMON_OPS_VADDR is registered only if CONFIG_DAMON_VADDR is set */
	if (!damon_is_registered_ops(DAMON_OPS_VADDR)) {
		bak.id = DAMON_OPS_VADDR;
		KUNIT_EXPECT_EQ(test, damon_register_ops(&bak), 0);
		need_cleanup = true;
	}

	/* DAMON_OPS_VADDR is ensured to be registered */
	KUNIT_EXPECT_EQ(test, damon_select_ops(c, DAMON_OPS_VADDR), 0);

	/* Double-registration is prohibited */
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

	if (need_cleanup) {
		mutex_lock(&damon_ops_lock);
		damon_registered_ops[DAMON_OPS_VADDR] =
			(struct damon_operations){};
		mutex_unlock(&damon_ops_lock);
	}
}

static void damon_test_set_regions(struct kunit *test)
{
	struct damon_target *t = damon_new_target();
	struct damon_region *r1, *r2;
	struct damon_addr_range range = {.start = 8, .end = 28};
	unsigned long expects[] = {8, 16, 16, 24, 24, 28};
	int expect_idx = 0;
	struct damon_region *r;

	if (!t)
		kunit_skip(test, "target alloc fail");
	r1 = damon_new_region(4, 16);
	if (!r1) {
		damon_free_target(t);
		kunit_skip(test, "region alloc fail");
	}
	r2 = damon_new_region(24, 32);
	if (!r2) {
		damon_free_target(t);
		damon_free_region(r1);
		kunit_skip(test, "second region alloc fail");
	}

	damon_add_region(r1, t);
	damon_add_region(r2, t);
	damon_set_regions(t, &range, 1, 1);

	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 3);
	damon_for_each_region(r, t) {
		KUNIT_EXPECT_EQ(test, r->ar.start, expects[expect_idx++]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expects[expect_idx++]);
	}
	damon_destroy_target(t, NULL);
}

static void damon_test_nr_accesses_to_accesses_bp(struct kunit *test)
{
	struct damon_attrs attrs = {
		.sample_interval = 10,
		.aggr_interval = ((unsigned long)UINT_MAX + 1) * 10
	};

	/*
	 * In some cases such as 32bit architectures where UINT_MAX is
	 * ULONG_MAX, attrs.aggr_interval becomes zero.  Calling
	 * damon_nr_accesses_to_accesses_bp() in the case will cause
	 * divide-by-zero.  Such case is prohibited in normal execution since
	 * the caution is documented on the comment for the function, and
	 * damon_update_monitoring_results() does the check.  Skip the test in
	 * the case.
	 */
	if (!attrs.aggr_interval)
		kunit_skip(test, "aggr_interval is zero.");

	KUNIT_EXPECT_EQ(test, damon_nr_accesses_to_accesses_bp(123, &attrs), 0);
}

static void damon_test_update_monitoring_result(struct kunit *test)
{
	struct damon_attrs old_attrs = {
		.sample_interval = 10, .aggr_interval = 1000,};
	struct damon_attrs new_attrs;
	struct damon_region *r = damon_new_region(3, 7);

	if (!r)
		kunit_skip(test, "region alloc fail");

	r->nr_accesses = 15;
	r->nr_accesses_bp = 150000;
	r->age = 20;

	new_attrs = (struct damon_attrs){
		.sample_interval = 100, .aggr_interval = 10000,};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 15);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 1000};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
	KUNIT_EXPECT_EQ(test, r->nr_accesses, 150);
	KUNIT_EXPECT_EQ(test, r->age, 2);

	new_attrs = (struct damon_attrs){
		.sample_interval = 1, .aggr_interval = 100};
	damon_update_monitoring_result(r, &old_attrs, &new_attrs, false);
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

	if (!c)
		kunit_skip(test, "ctx alloc fail");

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

	filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true, false);
	if (!filter)
		kunit_skip(test, "filter alloc fail");
	KUNIT_EXPECT_EQ(test, filter->type, DAMOS_FILTER_TYPE_ANON);
	KUNIT_EXPECT_EQ(test, filter->matching, true);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.prev, &filter->list);
	KUNIT_EXPECT_PTR_EQ(test, filter->list.next, &filter->list);
	damos_destroy_filter(filter);
}

static void damos_test_commit_quota_goal_for(struct kunit *test,
		struct damos_quota_goal *dst,
		struct damos_quota_goal *src)
{
	u64 dst_last_psi_total = 0;

	if (dst->metric == DAMOS_QUOTA_SOME_MEM_PSI_US)
		dst_last_psi_total = dst->last_psi_total;
	damos_commit_quota_goal(dst, src);

	KUNIT_EXPECT_EQ(test, dst->metric, src->metric);
	KUNIT_EXPECT_EQ(test, dst->target_value, src->target_value);
	if (src->metric == DAMOS_QUOTA_USER_INPUT)
		KUNIT_EXPECT_EQ(test, dst->current_value, src->current_value);
	if (dst_last_psi_total && src->metric == DAMOS_QUOTA_SOME_MEM_PSI_US)
		KUNIT_EXPECT_EQ(test, dst->last_psi_total, dst_last_psi_total);
	switch (dst->metric) {
	case DAMOS_QUOTA_NODE_MEM_USED_BP:
	case DAMOS_QUOTA_NODE_MEM_FREE_BP:
		KUNIT_EXPECT_EQ(test, dst->nid, src->nid);
		break;
	case DAMOS_QUOTA_NODE_MEMCG_USED_BP:
	case DAMOS_QUOTA_NODE_MEMCG_FREE_BP:
		KUNIT_EXPECT_EQ(test, dst->nid, src->nid);
		KUNIT_EXPECT_EQ(test, dst->memcg_id, src->memcg_id);
		break;
	default:
		break;
	}
}

static void damos_test_commit_quota_goal(struct kunit *test)
{
	struct damos_quota_goal dst = {
		.metric = DAMOS_QUOTA_SOME_MEM_PSI_US,
		.target_value = 1000,
		.current_value = 123,
		.last_psi_total = 456,
	};

	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal){
			.metric = DAMOS_QUOTA_USER_INPUT,
			.target_value = 789,
			.current_value = 12});
	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal){
			.metric = DAMOS_QUOTA_NODE_MEM_FREE_BP,
			.target_value = 345,
			.current_value = 678,
			.nid = 9,
			});
	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal){
			.metric = DAMOS_QUOTA_NODE_MEM_USED_BP,
			.target_value = 12,
			.current_value = 345,
			.nid = 6,
			});
	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal){
			.metric = DAMOS_QUOTA_NODE_MEMCG_USED_BP,
			.target_value = 456,
			.current_value = 567,
			.nid = 6,
			.memcg_id = 7,
			});
	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal){
			.metric = DAMOS_QUOTA_NODE_MEMCG_FREE_BP,
			.target_value = 890,
			.current_value = 901,
			.nid = 10,
			.memcg_id = 1,
			});
	damos_test_commit_quota_goal_for(test, &dst,
			&(struct damos_quota_goal) {
			.metric = DAMOS_QUOTA_SOME_MEM_PSI_US,
			.target_value = 234,
			.current_value = 345,
			.last_psi_total = 567,
			});
}

static void damos_test_commit_quota_goals_for(struct kunit *test,
		struct damos_quota_goal *dst_goals, int nr_dst_goals,
		struct damos_quota_goal *src_goals, int nr_src_goals)
{
	struct damos_quota dst, src;
	struct damos_quota_goal *goal, *next;
	bool skip = true;
	int i;

	INIT_LIST_HEAD(&dst.goals);
	INIT_LIST_HEAD(&src.goals);

	for (i = 0; i < nr_dst_goals; i++) {
		/*
		 * When nr_src_goals is smaller than dst_goals,
		 * damos_commit_quota_goals() will kfree() the dst goals.
		 * Make it kfree()-able.
		 */
		goal = damos_new_quota_goal(dst_goals[i].metric,
				dst_goals[i].target_value);
		if (!goal)
			goto out;
		damos_add_quota_goal(&dst, goal);
	}
	skip = false;
	for (i = 0; i < nr_src_goals; i++)
		damos_add_quota_goal(&src, &src_goals[i]);

	damos_commit_quota_goals(&dst, &src);

	i = 0;
	damos_for_each_quota_goal(goal, (&dst)) {
		KUNIT_EXPECT_EQ(test, goal->metric, src_goals[i].metric);
		KUNIT_EXPECT_EQ(test, goal->target_value,
				src_goals[i++].target_value);
	}
	KUNIT_EXPECT_EQ(test, i, nr_src_goals);

out:
	damos_for_each_quota_goal_safe(goal, next, (&dst))
		damos_destroy_quota_goal(goal);
	if (skip)
		kunit_skip(test, "goal alloc fail");
}

static void damos_test_commit_quota_goals(struct kunit *test)
{
	damos_test_commit_quota_goals_for(test,
			(struct damos_quota_goal[]){}, 0,
			(struct damos_quota_goal[]){
				{
				.metric = DAMOS_QUOTA_USER_INPUT,
				.target_value = 123,
				},
			}, 1);
	damos_test_commit_quota_goals_for(test,
			(struct damos_quota_goal[]){
				{
				.metric = DAMOS_QUOTA_USER_INPUT,
				.target_value = 234,
				},

			}, 1,
			(struct damos_quota_goal[]){
				{
				.metric = DAMOS_QUOTA_USER_INPUT,
				.target_value = 345,
				},
			}, 1);
	damos_test_commit_quota_goals_for(test,
			(struct damos_quota_goal[]){
				{
				.metric = DAMOS_QUOTA_USER_INPUT,
				.target_value = 456,
				},

			}, 1,
			(struct damos_quota_goal[]){}, 0);
}

static void damos_test_commit_quota(struct kunit *test)
{
	struct damos_quota dst = {
		.reset_interval = 1,
		.ms = 2,
		.sz = 3,
		.weight_sz = 4,
		.weight_nr_accesses = 5,
		.weight_age = 6,
	};
	struct damos_quota src = {
		.reset_interval = 7,
		.ms = 8,
		.sz = 9,
		.weight_sz = 10,
		.weight_nr_accesses = 11,
		.weight_age = 12,
	};

	INIT_LIST_HEAD(&dst.goals);
	INIT_LIST_HEAD(&src.goals);

	damos_commit_quota(&dst, &src);

	KUNIT_EXPECT_EQ(test, dst.reset_interval, src.reset_interval);
	KUNIT_EXPECT_EQ(test, dst.ms, src.ms);
	KUNIT_EXPECT_EQ(test, dst.sz, src.sz);
	KUNIT_EXPECT_EQ(test, dst.weight_sz, src.weight_sz);
	KUNIT_EXPECT_EQ(test, dst.weight_nr_accesses, src.weight_nr_accesses);
	KUNIT_EXPECT_EQ(test, dst.weight_age, src.weight_age);
}

static int damos_test_help_dests_setup(struct damos_migrate_dests *dests,
		unsigned int *node_id_arr, unsigned int *weight_arr,
		size_t nr_dests)
{
	size_t i;

	dests->node_id_arr = kmalloc_array(nr_dests,
			sizeof(*dests->node_id_arr), GFP_KERNEL);
	if (!dests->node_id_arr)
		return -ENOMEM;
	dests->weight_arr = kmalloc_array(nr_dests,
			sizeof(*dests->weight_arr), GFP_KERNEL);
	if (!dests->weight_arr) {
		kfree(dests->node_id_arr);
		dests->node_id_arr = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < nr_dests; i++) {
		dests->node_id_arr[i] = node_id_arr[i];
		dests->weight_arr[i] = weight_arr[i];
	}
	dests->nr_dests = nr_dests;
	return 0;
}

static void damos_test_help_dests_free(struct damos_migrate_dests *dests)
{
	kfree(dests->node_id_arr);
	kfree(dests->weight_arr);
}

static void damos_test_commit_dests_for(struct kunit *test,
		unsigned int *dst_node_id_arr, unsigned int *dst_weight_arr,
		size_t dst_nr_dests,
		unsigned int *src_node_id_arr, unsigned int *src_weight_arr,
		size_t src_nr_dests)
{
	struct damos_migrate_dests dst = {}, src = {};
	int i, err;
	bool skip = true;

	err = damos_test_help_dests_setup(&dst, dst_node_id_arr,
			dst_weight_arr, dst_nr_dests);
	if (err)
		kunit_skip(test, "dests setup fail");
	err = damos_test_help_dests_setup(&src, src_node_id_arr,
			src_weight_arr, src_nr_dests);
	if (err) {
		damos_test_help_dests_free(&dst);
		kunit_skip(test, "src setup fail");
	}
	err = damos_commit_dests(&dst, &src);
	if (err)
		goto out;
	skip = false;

	KUNIT_EXPECT_EQ(test, dst.nr_dests, src_nr_dests);
	for (i = 0; i < dst.nr_dests; i++) {
		KUNIT_EXPECT_EQ(test, dst.node_id_arr[i], src_node_id_arr[i]);
		KUNIT_EXPECT_EQ(test, dst.weight_arr[i], src_weight_arr[i]);
	}

out:
	damos_test_help_dests_free(&dst);
	damos_test_help_dests_free(&src);
	if (skip)
		kunit_skip(test, "skip");
}

static void damos_test_commit_dests(struct kunit *test)
{
	damos_test_commit_dests_for(test,
			(unsigned int[]){1, 2, 3}, (unsigned int[]){2, 3, 4},
			3,
			(unsigned int[]){4, 5, 6}, (unsigned int[]){5, 6, 7},
			3);
	damos_test_commit_dests_for(test,
			(unsigned int[]){1, 2}, (unsigned int[]){2, 3},
			2,
			(unsigned int[]){4, 5, 6}, (unsigned int[]){5, 6, 7},
			3);
	damos_test_commit_dests_for(test,
			NULL, NULL, 0,
			(unsigned int[]){4, 5, 6}, (unsigned int[]){5, 6, 7},
			3);
	damos_test_commit_dests_for(test,
			(unsigned int[]){1, 2, 3}, (unsigned int[]){2, 3, 4},
			3,
			(unsigned int[]){4, 5}, (unsigned int[]){5, 6}, 2);
	damos_test_commit_dests_for(test,
			(unsigned int[]){1, 2, 3}, (unsigned int[]){2, 3, 4},
			3,
			NULL, NULL, 0);
}

static void damos_test_commit_filter_for(struct kunit *test,
		struct damos_filter *dst, struct damos_filter *src)
{
	damos_commit_filter(dst, src);
	KUNIT_EXPECT_EQ(test, dst->type, src->type);
	KUNIT_EXPECT_EQ(test, dst->matching, src->matching);
	KUNIT_EXPECT_EQ(test, dst->allow, src->allow);
	switch (src->type) {
	case DAMOS_FILTER_TYPE_MEMCG:
		KUNIT_EXPECT_EQ(test, dst->memcg_id, src->memcg_id);
		break;
	case DAMOS_FILTER_TYPE_ADDR:
		KUNIT_EXPECT_EQ(test, dst->addr_range.start,
				src->addr_range.start);
		KUNIT_EXPECT_EQ(test, dst->addr_range.end,
				src->addr_range.end);
		break;
	case DAMOS_FILTER_TYPE_TARGET:
		KUNIT_EXPECT_EQ(test, dst->target_idx, src->target_idx);
		break;
	case DAMOS_FILTER_TYPE_HUGEPAGE_SIZE:
		KUNIT_EXPECT_EQ(test, dst->sz_range.min, src->sz_range.min);
		KUNIT_EXPECT_EQ(test, dst->sz_range.max, src->sz_range.max);
		break;
	default:
		break;
	}
}

static void damos_test_commit_filter(struct kunit *test)
{
	struct damos_filter dst = {
		.type = DAMOS_FILTER_TYPE_ACTIVE,
		.matching = false,
		.allow = false,
	};

	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_ANON,
			.matching = true,
			.allow = true,
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_MEMCG,
			.matching = false,
			.allow = false,
			.memcg_id = 123,
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_YOUNG,
			.matching = true,
			.allow = true,
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_HUGEPAGE_SIZE,
			.matching = false,
			.allow = false,
			.sz_range = {.min = 234, .max = 345},
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_UNMAPPED,
			.matching = true,
			.allow = true,
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_ADDR,
			.matching = false,
			.allow = false,
			.addr_range = {.start = 456, .end = 567},
			});
	damos_test_commit_filter_for(test, &dst,
			&(struct damos_filter){
			.type = DAMOS_FILTER_TYPE_TARGET,
			.matching = true,
			.allow = true,
			.target_idx = 6,
			});
}

static void damos_test_help_initailize_scheme(struct damos *scheme)
{
	INIT_LIST_HEAD(&scheme->quota.goals);
	INIT_LIST_HEAD(&scheme->core_filters);
	INIT_LIST_HEAD(&scheme->ops_filters);
}

static void damos_test_commit_for(struct kunit *test, struct damos *dst,
		struct damos *src)
{
	int err;

	damos_test_help_initailize_scheme(dst);
	damos_test_help_initailize_scheme(src);

	err = damos_commit(dst, src);
	if (err)
		kunit_skip(test, "damos_commit fail");

	KUNIT_EXPECT_EQ(test, dst->pattern.min_sz_region,
			src->pattern.min_sz_region);
	KUNIT_EXPECT_EQ(test, dst->pattern.max_sz_region,
			src->pattern.max_sz_region);
	KUNIT_EXPECT_EQ(test, dst->pattern.min_nr_accesses,
			src->pattern.min_nr_accesses);
	KUNIT_EXPECT_EQ(test, dst->pattern.max_nr_accesses,
			src->pattern.max_nr_accesses);
	KUNIT_EXPECT_EQ(test, dst->pattern.min_age_region,
			src->pattern.min_age_region);
	KUNIT_EXPECT_EQ(test, dst->pattern.max_age_region,
			src->pattern.max_age_region);

	KUNIT_EXPECT_EQ(test, dst->action, src->action);
	KUNIT_EXPECT_EQ(test, dst->apply_interval_us, src->apply_interval_us);

	KUNIT_EXPECT_EQ(test, dst->wmarks.metric, src->wmarks.metric);
	KUNIT_EXPECT_EQ(test, dst->wmarks.interval, src->wmarks.interval);
	KUNIT_EXPECT_EQ(test, dst->wmarks.high, src->wmarks.high);
	KUNIT_EXPECT_EQ(test, dst->wmarks.mid, src->wmarks.mid);
	KUNIT_EXPECT_EQ(test, dst->wmarks.low, src->wmarks.low);

	switch (src->action) {
	case DAMOS_MIGRATE_COLD:
	case DAMOS_MIGRATE_HOT:
		KUNIT_EXPECT_EQ(test, dst->target_nid, src->target_nid);
		break;
	default:
		break;
	}
}

static void damos_test_commit_pageout(struct kunit *test)
{
	damos_test_commit_for(test,
			&(struct damos){
				.pattern = (struct damos_access_pattern){
					1, 2, 3, 4, 5, 6},
				.action = DAMOS_PAGEOUT,
				.apply_interval_us = 1000000,
				.wmarks = (struct damos_watermarks){
					DAMOS_WMARK_FREE_MEM_RATE,
					900, 100, 50},
			},
			&(struct damos){
				.pattern = (struct damos_access_pattern){
					2, 3, 4, 5, 6, 7},
				.action = DAMOS_PAGEOUT,
				.apply_interval_us = 2000000,
				.wmarks = (struct damos_watermarks){
					DAMOS_WMARK_FREE_MEM_RATE,
					800, 50, 30},
			});
}

static void damos_test_commit_migrate_hot(struct kunit *test)
{
	damos_test_commit_for(test,
			&(struct damos){
				.pattern = (struct damos_access_pattern){
					1, 2, 3, 4, 5, 6},
				.action = DAMOS_PAGEOUT,
				.apply_interval_us = 1000000,
				.wmarks = (struct damos_watermarks){
					DAMOS_WMARK_FREE_MEM_RATE,
					900, 100, 50},
			},
			&(struct damos){
				.pattern = (struct damos_access_pattern){
					2, 3, 4, 5, 6, 7},
				.action = DAMOS_MIGRATE_HOT,
				.apply_interval_us = 2000000,
				.target_nid = 5,
			});
}

static struct damon_target *damon_test_help_setup_target(
		unsigned long region_start_end[][2], int nr_regions)
{
	struct damon_target *t;
	struct damon_region *r;
	int i;

	t = damon_new_target();
	if (!t)
		return NULL;
	for (i = 0; i < nr_regions; i++) {
		r = damon_new_region(region_start_end[i][0],
				region_start_end[i][1]);
		if (!r) {
			damon_free_target(t);
			return NULL;
		}
		damon_add_region(r, t);
	}
	return t;
}

static void damon_test_commit_target_regions_for(struct kunit *test,
		unsigned long dst_start_end[][2], int nr_dst_regions,
		unsigned long src_start_end[][2], int nr_src_regions,
		unsigned long expect_start_end[][2], int nr_expect_regions)
{
	struct damon_target *dst_target, *src_target;
	struct damon_region *r;
	int i;

	dst_target = damon_test_help_setup_target(dst_start_end, nr_dst_regions);
	if (!dst_target)
		kunit_skip(test, "dst target setup fail");
	src_target = damon_test_help_setup_target(src_start_end, nr_src_regions);
	if (!src_target) {
		damon_free_target(dst_target);
		kunit_skip(test, "src target setup fail");
	}
	damon_commit_target_regions(dst_target, src_target, 1);
	i = 0;
	damon_for_each_region(r, dst_target) {
		KUNIT_EXPECT_EQ(test, r->ar.start, expect_start_end[i][0]);
		KUNIT_EXPECT_EQ(test, r->ar.end, expect_start_end[i][1]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, damon_nr_regions(dst_target), nr_expect_regions);
	KUNIT_EXPECT_EQ(test, i, nr_expect_regions);
	damon_free_target(dst_target);
	damon_free_target(src_target);
}

static void damon_test_commit_target_regions(struct kunit *test)
{
	damon_test_commit_target_regions_for(test,
			(unsigned long[][2]) {{3, 8}, {8, 10}}, 2,
			(unsigned long[][2]) {{4, 6}}, 1,
			(unsigned long[][2]) {{4, 6}}, 1);
	damon_test_commit_target_regions_for(test,
			(unsigned long[][2]) {{3, 8}, {8, 10}}, 2,
			(unsigned long[][2]) {}, 0,
			(unsigned long[][2]) {{3, 8}, {8, 10}}, 2);
}

static void damos_test_filter_out(struct kunit *test)
{
	struct damon_target *t;
	struct damon_region *r, *r2;
	struct damos_filter *f;

	f = damos_new_filter(DAMOS_FILTER_TYPE_ADDR, true, false);
	if (!f)
		kunit_skip(test, "filter alloc fail");
	f->addr_range = (struct damon_addr_range){.start = 2, .end = 6};

	t = damon_new_target();
	if (!t) {
		damos_destroy_filter(f);
		kunit_skip(test, "target alloc fail");
	}
	r = damon_new_region(3, 5);
	if (!r) {
		damos_destroy_filter(f);
		damon_free_target(t);
		kunit_skip(test, "region alloc fail");
	}
	damon_add_region(r, t);

	/* region in the range */
	KUNIT_EXPECT_TRUE(test, damos_filter_match(NULL, t, r, f, 1));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region before the range */
	r->ar.start = 1;
	r->ar.end = 2;
	KUNIT_EXPECT_FALSE(test,
			damos_filter_match(NULL, t, r, f, 1));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region after the range */
	r->ar.start = 6;
	r->ar.end = 8;
	KUNIT_EXPECT_FALSE(test,
			damos_filter_match(NULL, t, r, f, 1));
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 1);

	/* region started before the range */
	r->ar.start = 1;
	r->ar.end = 4;
	KUNIT_EXPECT_FALSE(test, damos_filter_match(NULL, t, r, f, 1));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, 1);
	KUNIT_EXPECT_EQ(test, r->ar.end, 2);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, 2);
	KUNIT_EXPECT_EQ(test, r2->ar.end, 4);
	damon_destroy_region(r2, t);

	/* region started in the range */
	r->ar.start = 2;
	r->ar.end = 8;
	KUNIT_EXPECT_TRUE(test,
			damos_filter_match(NULL, t, r, f, 1));
	/* filter should have split the region */
	KUNIT_EXPECT_EQ(test, r->ar.start, 2);
	KUNIT_EXPECT_EQ(test, r->ar.end, 6);
	KUNIT_EXPECT_EQ(test, damon_nr_regions(t), 2);
	r2 = damon_next_region(r);
	KUNIT_EXPECT_EQ(test, r2->ar.start, 6);
	KUNIT_EXPECT_EQ(test, r2->ar.end, 8);
	damon_destroy_region(r2, t);

	damon_free_target(t);
	damos_free_filter(f);
}

static void damon_test_feed_loop_next_input(struct kunit *test)
{
	unsigned long last_input = 900000, current_score = 200;

	/*
	 * If current score is lower than the goal, which is always 10,000
	 * (read the comment on damon_feed_loop_next_input()'s comment), next
	 * input should be higher than the last input.
	 */
	KUNIT_EXPECT_GT(test,
			damon_feed_loop_next_input(last_input, current_score),
			last_input);

	/*
	 * If current score is higher than the goal, next input should be lower
	 * than the last input.
	 */
	current_score = 250000000;
	KUNIT_EXPECT_LT(test,
			damon_feed_loop_next_input(last_input, current_score),
			last_input);

	/*
	 * The next input depends on the distance between the current score and
	 * the goal
	 */
	KUNIT_EXPECT_GT(test,
			damon_feed_loop_next_input(last_input, 200),
			damon_feed_loop_next_input(last_input, 2000));
}

static void damon_test_set_filters_default_reject(struct kunit *test)
{
	struct damos scheme;
	struct damos_filter *target_filter, *anon_filter;

	INIT_LIST_HEAD(&scheme.core_filters);
	INIT_LIST_HEAD(&scheme.ops_filters);

	damos_set_filters_default_reject(&scheme);
	/*
	 * No filter is installed.  Allow by default on both core and ops layer
	 * filtering stages, since there are no filters at all.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	target_filter = damos_new_filter(DAMOS_FILTER_TYPE_TARGET, true, true);
	if (!target_filter)
		kunit_skip(test, "filter alloc fail");
	damos_add_filter(&scheme, target_filter);
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled allow-filter is installed.
	 * Reject by default on core layer filtering stage due to the last
	 * core-layer-filter's behavior.
	 * Allow by default on ops layer filtering stage due to the absence of
	 * ops layer filters.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, true);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	target_filter->allow = false;
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled reject-filter is installed.
	 * Allow by default on core layer filtering stage due to the last
	 * core-layer-filter's behavior.
	 * Allow by default on ops layer filtering stage due to the absence of
	 * ops layer filters.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, false);

	anon_filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true, true);
	if (!anon_filter) {
		damos_free_filter(target_filter);
		kunit_skip(test, "anon_filter alloc fail");
	}
	damos_add_filter(&scheme, anon_filter);

	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled reject-filter and ops-handled allow-filter are installed.
	 * Allow by default on core layer filtering stage due to the existence
	 * of the ops-handled filter.
	 * Reject by default on ops layer filtering stage due to the last
	 * ops-layer-filter's behavior.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, true);

	target_filter->allow = true;
	damos_set_filters_default_reject(&scheme);
	/*
	 * A core-handled allow-filter and ops-handled allow-filter are
	 * installed.
	 * Allow by default on core layer filtering stage due to the existence
	 * of the ops-handled filter.
	 * Reject by default on ops layer filtering stage due to the last
	 * ops-layer-filter's behavior.
	 */
	KUNIT_EXPECT_EQ(test, scheme.core_filters_default_reject, false);
	KUNIT_EXPECT_EQ(test, scheme.ops_filters_default_reject, true);

	damos_free_filter(anon_filter);
	damos_free_filter(target_filter);
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
	KUNIT_CASE(damon_test_nr_accesses_to_accesses_bp),
	KUNIT_CASE(damon_test_update_monitoring_result),
	KUNIT_CASE(damon_test_set_attrs),
	KUNIT_CASE(damon_test_moving_sum),
	KUNIT_CASE(damos_test_new_filter),
	KUNIT_CASE(damos_test_commit_quota_goal),
	KUNIT_CASE(damos_test_commit_quota_goals),
	KUNIT_CASE(damos_test_commit_quota),
	KUNIT_CASE(damos_test_commit_dests),
	KUNIT_CASE(damos_test_commit_filter),
	KUNIT_CASE(damos_test_commit_pageout),
	KUNIT_CASE(damos_test_commit_migrate_hot),
	KUNIT_CASE(damon_test_commit_target_regions),
	KUNIT_CASE(damos_test_filter_out),
	KUNIT_CASE(damon_test_feed_loop_next_input),
	KUNIT_CASE(damon_test_set_filters_default_reject),
	{},
};

static struct kunit_suite damon_test_suite = {
	.name = "damon",
	.test_cases = damon_test_cases,
};
kunit_test_suite(damon_test_suite);

#endif /* _DAMON_CORE_TEST_H */

#endif	/* CONFIG_DAMON_KUNIT_TEST */
