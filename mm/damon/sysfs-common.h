/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for DAMON Sysfs Interface
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>

extern struct mutex damon_sysfs_lock;

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
		unsigned long min,
		unsigned long max);
void damon_sysfs_ul_range_release(struct kobject *kobj);

extern struct kobj_type damon_sysfs_ul_range_ktype;
