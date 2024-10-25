/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Data Access Monitor Unit Tests
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#ifdef CONFIG_DAMON_SYSFS_KUNIT_TEST

#ifndef _DAMON_SYSFS_TEST_H
#define _DAMON_SYSFS_TEST_H

#include <kunit/test.h>

static unsigned int nr_damon_targets(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_targets = 0;

	damon_for_each_target(t, ctx)
		nr_targets++;

	return nr_targets;
}

static int __damon_sysfs_test_get_any_pid(int min, int max)
{
	struct pid *pid;
	int i;

	for (i = min; i <= max; i++) {
		pid = find_get_pid(i);
		if (pid) {
			put_pid(pid);
			return i;
		}
	}
	return -1;
}

static void damon_sysfs_test_add_targets(struct kunit *test)
{
	struct damon_sysfs_targets *sysfs_targets;
	struct damon_sysfs_target *sysfs_target;
	struct damon_ctx *ctx;

	sysfs_targets = damon_sysfs_targets_alloc();
	sysfs_targets->nr = 1;
	sysfs_targets->targets_arr = kmalloc_array(1,
			sizeof(*sysfs_targets->targets_arr), GFP_KERNEL);

	sysfs_target = damon_sysfs_target_alloc();
	sysfs_target->pid = __damon_sysfs_test_get_any_pid(12, 100);
	sysfs_target->regions = damon_sysfs_regions_alloc();
	sysfs_targets->targets_arr[0] = sysfs_target;

	ctx = damon_new_ctx();

	damon_sysfs_add_targets(ctx, sysfs_targets);
	KUNIT_EXPECT_EQ(test, 1u, nr_damon_targets(ctx));

	sysfs_target->pid = __damon_sysfs_test_get_any_pid(
			sysfs_target->pid + 1, 200);
	damon_sysfs_add_targets(ctx, sysfs_targets);
	KUNIT_EXPECT_EQ(test, 2u, nr_damon_targets(ctx));

	damon_destroy_ctx(ctx);
	kfree(sysfs_targets->targets_arr);
	kfree(sysfs_targets);
	kfree(sysfs_target->regions);
	kfree(sysfs_target);
}

static struct kunit_case damon_sysfs_test_cases[] = {
	KUNIT_CASE(damon_sysfs_test_add_targets),
	{},
};

static struct kunit_suite damon_sysfs_test_suite = {
	.name = "damon-sysfs",
	.test_cases = damon_sysfs_test_cases,
};
kunit_test_suite(damon_sysfs_test_suite);

#endif /* _DAMON_SYSFS_TEST_H */

#endif /* CONFIG_DAMON_SYSFS_KUNIT_TEST */
