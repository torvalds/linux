// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for DAMON Sysfs Interface
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/slab.h>

#include "sysfs-common.h"

DEFINE_MUTEX(damon_sysfs_lock);

/*
 * unsigned long range directory
 */

struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
		unsigned long min,
		unsigned long max)
{
	struct damon_sysfs_ul_range *range = kmalloc(sizeof(*range),
			GFP_KERNEL);

	if (!range)
		return NULL;
	range->kobj = (struct kobject){};
	range->min = min;
	range->max = max;

	return range;
}

static ssize_t min_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->min);
}

static ssize_t min_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long min;
	int err;

	err = kstrtoul(buf, 0, &min);
	if (err)
		return err;

	range->min = min;
	return count;
}

static ssize_t max_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->max);
}

static ssize_t max_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long max;
	int err;

	err = kstrtoul(buf, 0, &max);
	if (err)
		return err;

	range->max = max;
	return count;
}

void damon_sysfs_ul_range_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ul_range, kobj));
}

static struct kobj_attribute damon_sysfs_ul_range_min_attr =
		__ATTR_RW_MODE(min, 0600);

static struct kobj_attribute damon_sysfs_ul_range_max_attr =
		__ATTR_RW_MODE(max, 0600);

static struct attribute *damon_sysfs_ul_range_attrs[] = {
	&damon_sysfs_ul_range_min_attr.attr,
	&damon_sysfs_ul_range_max_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_ul_range);

struct kobj_type damon_sysfs_ul_range_ktype = {
	.release = damon_sysfs_ul_range_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ul_range_groups,
};

