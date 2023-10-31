// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "sysfs-common.h"

/*
 * init region directory
 */

struct damon_sysfs_region {
	struct kobject kobj;
	struct damon_addr_range ar;
};

static struct damon_sysfs_region *damon_sysfs_region_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_region), GFP_KERNEL);
}

static ssize_t start_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->ar.start);
}

static ssize_t start_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 0, &region->ar.start);

	return err ? err : count;
}

static ssize_t end_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->ar.end);
}

static ssize_t end_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 0, &region->ar.end);

	return err ? err : count;
}

static void damon_sysfs_region_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_region, kobj));
}

static struct kobj_attribute damon_sysfs_region_start_attr =
		__ATTR_RW_MODE(start, 0600);

static struct kobj_attribute damon_sysfs_region_end_attr =
		__ATTR_RW_MODE(end, 0600);

static struct attribute *damon_sysfs_region_attrs[] = {
	&damon_sysfs_region_start_attr.attr,
	&damon_sysfs_region_end_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_region);

static const struct kobj_type damon_sysfs_region_ktype = {
	.release = damon_sysfs_region_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_region_groups,
};

/*
 * init_regions directory
 */

struct damon_sysfs_regions {
	struct kobject kobj;
	struct damon_sysfs_region **regions_arr;
	int nr;
};

static struct damon_sysfs_regions *damon_sysfs_regions_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_regions), GFP_KERNEL);
}

static void damon_sysfs_regions_rm_dirs(struct damon_sysfs_regions *regions)
{
	struct damon_sysfs_region **regions_arr = regions->regions_arr;
	int i;

	for (i = 0; i < regions->nr; i++)
		kobject_put(&regions_arr[i]->kobj);
	regions->nr = 0;
	kfree(regions_arr);
	regions->regions_arr = NULL;
}

static int damon_sysfs_regions_add_dirs(struct damon_sysfs_regions *regions,
		int nr_regions)
{
	struct damon_sysfs_region **regions_arr, *region;
	int err, i;

	damon_sysfs_regions_rm_dirs(regions);
	if (!nr_regions)
		return 0;

	regions_arr = kmalloc_array(nr_regions, sizeof(*regions_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!regions_arr)
		return -ENOMEM;
	regions->regions_arr = regions_arr;

	for (i = 0; i < nr_regions; i++) {
		region = damon_sysfs_region_alloc();
		if (!region) {
			damon_sysfs_regions_rm_dirs(regions);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&region->kobj,
				&damon_sysfs_region_ktype, &regions->kobj,
				"%d", i);
		if (err) {
			kobject_put(&region->kobj);
			damon_sysfs_regions_rm_dirs(regions);
			return err;
		}

		regions_arr[i] = region;
		regions->nr++;
	}
	return 0;
}

static ssize_t nr_regions_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_regions *regions = container_of(kobj,
			struct damon_sysfs_regions, kobj);

	return sysfs_emit(buf, "%d\n", regions->nr);
}

static ssize_t nr_regions_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_regions *regions;
	int nr, err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	regions = container_of(kobj, struct damon_sysfs_regions, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_regions_add_dirs(regions, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_regions_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_regions, kobj));
}

static struct kobj_attribute damon_sysfs_regions_nr_attr =
		__ATTR_RW_MODE(nr_regions, 0600);

static struct attribute *damon_sysfs_regions_attrs[] = {
	&damon_sysfs_regions_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_regions);

static const struct kobj_type damon_sysfs_regions_ktype = {
	.release = damon_sysfs_regions_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_regions_groups,
};

/*
 * target directory
 */

struct damon_sysfs_target {
	struct kobject kobj;
	struct damon_sysfs_regions *regions;
	int pid;
};

static struct damon_sysfs_target *damon_sysfs_target_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_target), GFP_KERNEL);
}

static int damon_sysfs_target_add_dirs(struct damon_sysfs_target *target)
{
	struct damon_sysfs_regions *regions = damon_sysfs_regions_alloc();
	int err;

	if (!regions)
		return -ENOMEM;

	err = kobject_init_and_add(&regions->kobj, &damon_sysfs_regions_ktype,
			&target->kobj, "regions");
	if (err)
		kobject_put(&regions->kobj);
	else
		target->regions = regions;
	return err;
}

static void damon_sysfs_target_rm_dirs(struct damon_sysfs_target *target)
{
	damon_sysfs_regions_rm_dirs(target->regions);
	kobject_put(&target->regions->kobj);
}

static ssize_t pid_target_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);

	return sysfs_emit(buf, "%d\n", target->pid);
}

static ssize_t pid_target_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);
	int err = kstrtoint(buf, 0, &target->pid);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_target_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_target, kobj));
}

static struct kobj_attribute damon_sysfs_target_pid_attr =
		__ATTR_RW_MODE(pid_target, 0600);

static struct attribute *damon_sysfs_target_attrs[] = {
	&damon_sysfs_target_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_target);

static const struct kobj_type damon_sysfs_target_ktype = {
	.release = damon_sysfs_target_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_target_groups,
};

/*
 * targets directory
 */

struct damon_sysfs_targets {
	struct kobject kobj;
	struct damon_sysfs_target **targets_arr;
	int nr;
};

static struct damon_sysfs_targets *damon_sysfs_targets_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_targets), GFP_KERNEL);
}

static void damon_sysfs_targets_rm_dirs(struct damon_sysfs_targets *targets)
{
	struct damon_sysfs_target **targets_arr = targets->targets_arr;
	int i;

	for (i = 0; i < targets->nr; i++) {
		damon_sysfs_target_rm_dirs(targets_arr[i]);
		kobject_put(&targets_arr[i]->kobj);
	}
	targets->nr = 0;
	kfree(targets_arr);
	targets->targets_arr = NULL;
}

static int damon_sysfs_targets_add_dirs(struct damon_sysfs_targets *targets,
		int nr_targets)
{
	struct damon_sysfs_target **targets_arr, *target;
	int err, i;

	damon_sysfs_targets_rm_dirs(targets);
	if (!nr_targets)
		return 0;

	targets_arr = kmalloc_array(nr_targets, sizeof(*targets_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!targets_arr)
		return -ENOMEM;
	targets->targets_arr = targets_arr;

	for (i = 0; i < nr_targets; i++) {
		target = damon_sysfs_target_alloc();
		if (!target) {
			damon_sysfs_targets_rm_dirs(targets);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&target->kobj,
				&damon_sysfs_target_ktype, &targets->kobj,
				"%d", i);
		if (err)
			goto out;

		err = damon_sysfs_target_add_dirs(target);
		if (err)
			goto out;

		targets_arr[i] = target;
		targets->nr++;
	}
	return 0;

out:
	damon_sysfs_targets_rm_dirs(targets);
	kobject_put(&target->kobj);
	return err;
}

static ssize_t nr_targets_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_targets *targets = container_of(kobj,
			struct damon_sysfs_targets, kobj);

	return sysfs_emit(buf, "%d\n", targets->nr);
}

static ssize_t nr_targets_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_targets *targets;
	int nr, err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	targets = container_of(kobj, struct damon_sysfs_targets, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_targets_add_dirs(targets, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_targets_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_targets, kobj));
}

static struct kobj_attribute damon_sysfs_targets_nr_attr =
		__ATTR_RW_MODE(nr_targets, 0600);

static struct attribute *damon_sysfs_targets_attrs[] = {
	&damon_sysfs_targets_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_targets);

static const struct kobj_type damon_sysfs_targets_ktype = {
	.release = damon_sysfs_targets_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_targets_groups,
};

/*
 * intervals directory
 */

struct damon_sysfs_intervals {
	struct kobject kobj;
	unsigned long sample_us;
	unsigned long aggr_us;
	unsigned long update_us;
};

static struct damon_sysfs_intervals *damon_sysfs_intervals_alloc(
		unsigned long sample_us, unsigned long aggr_us,
		unsigned long update_us)
{
	struct damon_sysfs_intervals *intervals = kmalloc(sizeof(*intervals),
			GFP_KERNEL);

	if (!intervals)
		return NULL;

	intervals->kobj = (struct kobject){};
	intervals->sample_us = sample_us;
	intervals->aggr_us = aggr_us;
	intervals->update_us = update_us;
	return intervals;
}

static ssize_t sample_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->sample_us);
}

static ssize_t sample_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return err;

	intervals->sample_us = us;
	return count;
}

static ssize_t aggr_us_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->aggr_us);
}

static ssize_t aggr_us_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return err;

	intervals->aggr_us = us;
	return count;
}

static ssize_t update_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->update_us);
}

static ssize_t update_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return err;

	intervals->update_us = us;
	return count;
}

static void damon_sysfs_intervals_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_intervals, kobj));
}

static struct kobj_attribute damon_sysfs_intervals_sample_us_attr =
		__ATTR_RW_MODE(sample_us, 0600);

static struct kobj_attribute damon_sysfs_intervals_aggr_us_attr =
		__ATTR_RW_MODE(aggr_us, 0600);

static struct kobj_attribute damon_sysfs_intervals_update_us_attr =
		__ATTR_RW_MODE(update_us, 0600);

static struct attribute *damon_sysfs_intervals_attrs[] = {
	&damon_sysfs_intervals_sample_us_attr.attr,
	&damon_sysfs_intervals_aggr_us_attr.attr,
	&damon_sysfs_intervals_update_us_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_intervals);

static const struct kobj_type damon_sysfs_intervals_ktype = {
	.release = damon_sysfs_intervals_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_intervals_groups,
};

/*
 * monitoring_attrs directory
 */

struct damon_sysfs_attrs {
	struct kobject kobj;
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions_range;
};

static struct damon_sysfs_attrs *damon_sysfs_attrs_alloc(void)
{
	struct damon_sysfs_attrs *attrs = kmalloc(sizeof(*attrs), GFP_KERNEL);

	if (!attrs)
		return NULL;
	attrs->kobj = (struct kobject){};
	return attrs;
}

static int damon_sysfs_attrs_add_dirs(struct damon_sysfs_attrs *attrs)
{
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions_range;
	int err;

	intervals = damon_sysfs_intervals_alloc(5000, 100000, 60000000);
	if (!intervals)
		return -ENOMEM;

	err = kobject_init_and_add(&intervals->kobj,
			&damon_sysfs_intervals_ktype, &attrs->kobj,
			"intervals");
	if (err)
		goto put_intervals_out;
	attrs->intervals = intervals;

	nr_regions_range = damon_sysfs_ul_range_alloc(10, 1000);
	if (!nr_regions_range) {
		err = -ENOMEM;
		goto put_intervals_out;
	}

	err = kobject_init_and_add(&nr_regions_range->kobj,
			&damon_sysfs_ul_range_ktype, &attrs->kobj,
			"nr_regions");
	if (err)
		goto put_nr_regions_intervals_out;
	attrs->nr_regions_range = nr_regions_range;
	return 0;

put_nr_regions_intervals_out:
	kobject_put(&nr_regions_range->kobj);
	attrs->nr_regions_range = NULL;
put_intervals_out:
	kobject_put(&intervals->kobj);
	attrs->intervals = NULL;
	return err;
}

static void damon_sysfs_attrs_rm_dirs(struct damon_sysfs_attrs *attrs)
{
	kobject_put(&attrs->nr_regions_range->kobj);
	kobject_put(&attrs->intervals->kobj);
}

static void damon_sysfs_attrs_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_attrs, kobj));
}

static struct attribute *damon_sysfs_attrs_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_attrs);

static const struct kobj_type damon_sysfs_attrs_ktype = {
	.release = damon_sysfs_attrs_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_attrs_groups,
};

/*
 * context directory
 */

/* This should match with enum damon_ops_id */
static const char * const damon_sysfs_ops_strs[] = {
	"vaddr",
	"fvaddr",
	"paddr",
};

struct damon_sysfs_context {
	struct kobject kobj;
	enum damon_ops_id ops_id;
	struct damon_sysfs_attrs *attrs;
	struct damon_sysfs_targets *targets;
	struct damon_sysfs_schemes *schemes;
};

static struct damon_sysfs_context *damon_sysfs_context_alloc(
		enum damon_ops_id ops_id)
{
	struct damon_sysfs_context *context = kmalloc(sizeof(*context),
				GFP_KERNEL);

	if (!context)
		return NULL;
	context->kobj = (struct kobject){};
	context->ops_id = ops_id;
	return context;
}

static int damon_sysfs_context_set_attrs(struct damon_sysfs_context *context)
{
	struct damon_sysfs_attrs *attrs = damon_sysfs_attrs_alloc();
	int err;

	if (!attrs)
		return -ENOMEM;
	err = kobject_init_and_add(&attrs->kobj, &damon_sysfs_attrs_ktype,
			&context->kobj, "monitoring_attrs");
	if (err)
		goto out;
	err = damon_sysfs_attrs_add_dirs(attrs);
	if (err)
		goto out;
	context->attrs = attrs;
	return 0;

out:
	kobject_put(&attrs->kobj);
	return err;
}

static int damon_sysfs_context_set_targets(struct damon_sysfs_context *context)
{
	struct damon_sysfs_targets *targets = damon_sysfs_targets_alloc();
	int err;

	if (!targets)
		return -ENOMEM;
	err = kobject_init_and_add(&targets->kobj, &damon_sysfs_targets_ktype,
			&context->kobj, "targets");
	if (err) {
		kobject_put(&targets->kobj);
		return err;
	}
	context->targets = targets;
	return 0;
}

static int damon_sysfs_context_set_schemes(struct damon_sysfs_context *context)
{
	struct damon_sysfs_schemes *schemes = damon_sysfs_schemes_alloc();
	int err;

	if (!schemes)
		return -ENOMEM;
	err = kobject_init_and_add(&schemes->kobj, &damon_sysfs_schemes_ktype,
			&context->kobj, "schemes");
	if (err) {
		kobject_put(&schemes->kobj);
		return err;
	}
	context->schemes = schemes;
	return 0;
}

static int damon_sysfs_context_add_dirs(struct damon_sysfs_context *context)
{
	int err;

	err = damon_sysfs_context_set_attrs(context);
	if (err)
		return err;

	err = damon_sysfs_context_set_targets(context);
	if (err)
		goto put_attrs_out;

	err = damon_sysfs_context_set_schemes(context);
	if (err)
		goto put_targets_attrs_out;
	return 0;

put_targets_attrs_out:
	kobject_put(&context->targets->kobj);
	context->targets = NULL;
put_attrs_out:
	kobject_put(&context->attrs->kobj);
	context->attrs = NULL;
	return err;
}

static void damon_sysfs_context_rm_dirs(struct damon_sysfs_context *context)
{
	damon_sysfs_attrs_rm_dirs(context->attrs);
	kobject_put(&context->attrs->kobj);
	damon_sysfs_targets_rm_dirs(context->targets);
	kobject_put(&context->targets->kobj);
	damon_sysfs_schemes_rm_dirs(context->schemes);
	kobject_put(&context->schemes->kobj);
}

static ssize_t avail_operations_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	enum damon_ops_id id;
	int len = 0;

	for (id = 0; id < NR_DAMON_OPS; id++) {
		if (!damon_is_registered_ops(id))
			continue;
		len += sysfs_emit_at(buf, len, "%s\n",
				damon_sysfs_ops_strs[id]);
	}
	return len;
}

static ssize_t operations_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);

	return sysfs_emit(buf, "%s\n", damon_sysfs_ops_strs[context->ops_id]);
}

static ssize_t operations_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);
	enum damon_ops_id id;

	for (id = 0; id < NR_DAMON_OPS; id++) {
		if (sysfs_streq(buf, damon_sysfs_ops_strs[id])) {
			context->ops_id = id;
			return count;
		}
	}
	return -EINVAL;
}

static void damon_sysfs_context_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_context, kobj));
}

static struct kobj_attribute damon_sysfs_context_avail_operations_attr =
		__ATTR_RO_MODE(avail_operations, 0400);

static struct kobj_attribute damon_sysfs_context_operations_attr =
		__ATTR_RW_MODE(operations, 0600);

static struct attribute *damon_sysfs_context_attrs[] = {
	&damon_sysfs_context_avail_operations_attr.attr,
	&damon_sysfs_context_operations_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_context);

static const struct kobj_type damon_sysfs_context_ktype = {
	.release = damon_sysfs_context_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_context_groups,
};

/*
 * contexts directory
 */

struct damon_sysfs_contexts {
	struct kobject kobj;
	struct damon_sysfs_context **contexts_arr;
	int nr;
};

static struct damon_sysfs_contexts *damon_sysfs_contexts_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_contexts), GFP_KERNEL);
}

static void damon_sysfs_contexts_rm_dirs(struct damon_sysfs_contexts *contexts)
{
	struct damon_sysfs_context **contexts_arr = contexts->contexts_arr;
	int i;

	for (i = 0; i < contexts->nr; i++) {
		damon_sysfs_context_rm_dirs(contexts_arr[i]);
		kobject_put(&contexts_arr[i]->kobj);
	}
	contexts->nr = 0;
	kfree(contexts_arr);
	contexts->contexts_arr = NULL;
}

static int damon_sysfs_contexts_add_dirs(struct damon_sysfs_contexts *contexts,
		int nr_contexts)
{
	struct damon_sysfs_context **contexts_arr, *context;
	int err, i;

	damon_sysfs_contexts_rm_dirs(contexts);
	if (!nr_contexts)
		return 0;

	contexts_arr = kmalloc_array(nr_contexts, sizeof(*contexts_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!contexts_arr)
		return -ENOMEM;
	contexts->contexts_arr = contexts_arr;

	for (i = 0; i < nr_contexts; i++) {
		context = damon_sysfs_context_alloc(DAMON_OPS_VADDR);
		if (!context) {
			damon_sysfs_contexts_rm_dirs(contexts);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&context->kobj,
				&damon_sysfs_context_ktype, &contexts->kobj,
				"%d", i);
		if (err)
			goto out;

		err = damon_sysfs_context_add_dirs(context);
		if (err)
			goto out;

		contexts_arr[i] = context;
		contexts->nr++;
	}
	return 0;

out:
	damon_sysfs_contexts_rm_dirs(contexts);
	kobject_put(&context->kobj);
	return err;
}

static ssize_t nr_contexts_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);

	return sysfs_emit(buf, "%d\n", contexts->nr);
}

static ssize_t nr_contexts_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_contexts *contexts;
	int nr, err;

	err = kstrtoint(buf, 0, &nr);
	if (err)
		return err;
	/* TODO: support multiple contexts per kdamond */
	if (nr < 0 || 1 < nr)
		return -EINVAL;

	contexts = container_of(kobj, struct damon_sysfs_contexts, kobj);
	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_contexts_add_dirs(contexts, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_contexts_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_contexts, kobj));
}

static struct kobj_attribute damon_sysfs_contexts_nr_attr
		= __ATTR_RW_MODE(nr_contexts, 0600);

static struct attribute *damon_sysfs_contexts_attrs[] = {
	&damon_sysfs_contexts_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_contexts);

static const struct kobj_type damon_sysfs_contexts_ktype = {
	.release = damon_sysfs_contexts_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_contexts_groups,
};

/*
 * kdamond directory
 */

struct damon_sysfs_kdamond {
	struct kobject kobj;
	struct damon_sysfs_contexts *contexts;
	struct damon_ctx *damon_ctx;
};

static struct damon_sysfs_kdamond *damon_sysfs_kdamond_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamond), GFP_KERNEL);
}

static int damon_sysfs_kdamond_add_dirs(struct damon_sysfs_kdamond *kdamond)
{
	struct damon_sysfs_contexts *contexts;
	int err;

	contexts = damon_sysfs_contexts_alloc();
	if (!contexts)
		return -ENOMEM;

	err = kobject_init_and_add(&contexts->kobj,
			&damon_sysfs_contexts_ktype, &kdamond->kobj,
			"contexts");
	if (err) {
		kobject_put(&contexts->kobj);
		return err;
	}
	kdamond->contexts = contexts;

	return err;
}

static void damon_sysfs_kdamond_rm_dirs(struct damon_sysfs_kdamond *kdamond)
{
	damon_sysfs_contexts_rm_dirs(kdamond->contexts);
	kobject_put(&kdamond->contexts->kobj);
}

static bool damon_sysfs_ctx_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);
	return running;
}

/*
 * enum damon_sysfs_cmd - Commands for a specific kdamond.
 */
enum damon_sysfs_cmd {
	/* @DAMON_SYSFS_CMD_ON: Turn the kdamond on. */
	DAMON_SYSFS_CMD_ON,
	/* @DAMON_SYSFS_CMD_OFF: Turn the kdamond off. */
	DAMON_SYSFS_CMD_OFF,
	/* @DAMON_SYSFS_CMD_COMMIT: Update kdamond inputs. */
	DAMON_SYSFS_CMD_COMMIT,
	/*
	 * @DAMON_SYSFS_CMD_UPDATE_SCHEMES_STATS: Update scheme stats sysfs
	 * files.
	 */
	DAMON_SYSFS_CMD_UPDATE_SCHEMES_STATS,
	/*
	 * @DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_BYTES: Update
	 * tried_regions/total_bytes sysfs files for each scheme.
	 */
	DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_BYTES,
	/*
	 * @DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_REGIONS: Update schemes tried
	 * regions
	 */
	DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_REGIONS,
	/*
	 * @DAMON_SYSFS_CMD_CLEAR_SCHEMES_TRIED_REGIONS: Clear schemes tried
	 * regions
	 */
	DAMON_SYSFS_CMD_CLEAR_SCHEMES_TRIED_REGIONS,
	/*
	 * @NR_DAMON_SYSFS_CMDS: Total number of DAMON sysfs commands.
	 */
	NR_DAMON_SYSFS_CMDS,
};

/* Should match with enum damon_sysfs_cmd */
static const char * const damon_sysfs_cmd_strs[] = {
	"on",
	"off",
	"commit",
	"update_schemes_stats",
	"update_schemes_tried_bytes",
	"update_schemes_tried_regions",
	"clear_schemes_tried_regions",
};

/*
 * struct damon_sysfs_cmd_request - A request to the DAMON callback.
 * @cmd:	The command that needs to be handled by the callback.
 * @kdamond:	The kobject wrapper that associated to the kdamond thread.
 *
 * This structure represents a sysfs command request that need to access some
 * DAMON context-internal data.  Because DAMON context-internal data can be
 * safely accessed from DAMON callbacks without additional synchronization, the
 * request will be handled by the DAMON callback.  None-``NULL`` @kdamond means
 * the request is valid.
 */
struct damon_sysfs_cmd_request {
	enum damon_sysfs_cmd cmd;
	struct damon_sysfs_kdamond *kdamond;
};

/* Current DAMON callback request.  Protected by damon_sysfs_lock. */
static struct damon_sysfs_cmd_request damon_sysfs_cmd_request;

static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	struct damon_ctx *ctx = kdamond->damon_ctx;
	bool running;

	if (!ctx)
		running = false;
	else
		running = damon_sysfs_ctx_running(ctx);

	return sysfs_emit(buf, "%s\n", running ?
			damon_sysfs_cmd_strs[DAMON_SYSFS_CMD_ON] :
			damon_sysfs_cmd_strs[DAMON_SYSFS_CMD_OFF]);
}

static int damon_sysfs_set_attrs(struct damon_ctx *ctx,
		struct damon_sysfs_attrs *sys_attrs)
{
	struct damon_sysfs_intervals *sys_intervals = sys_attrs->intervals;
	struct damon_sysfs_ul_range *sys_nr_regions =
		sys_attrs->nr_regions_range;
	struct damon_attrs attrs = {
		.sample_interval = sys_intervals->sample_us,
		.aggr_interval = sys_intervals->aggr_us,
		.ops_update_interval = sys_intervals->update_us,
		.min_nr_regions = sys_nr_regions->min,
		.max_nr_regions = sys_nr_regions->max,
	};
	return damon_set_attrs(ctx, &attrs);
}

static void damon_sysfs_destroy_targets(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;
	bool has_pid = damon_target_has_pid(ctx);

	damon_for_each_target_safe(t, next, ctx) {
		if (has_pid)
			put_pid(t->pid);
		damon_destroy_target(t);
	}
}

static int damon_sysfs_set_regions(struct damon_target *t,
		struct damon_sysfs_regions *sysfs_regions)
{
	struct damon_addr_range *ranges = kmalloc_array(sysfs_regions->nr,
			sizeof(*ranges), GFP_KERNEL | __GFP_NOWARN);
	int i, err = -EINVAL;

	if (!ranges)
		return -ENOMEM;
	for (i = 0; i < sysfs_regions->nr; i++) {
		struct damon_sysfs_region *sys_region =
			sysfs_regions->regions_arr[i];

		if (sys_region->ar.start > sys_region->ar.end)
			goto out;

		ranges[i].start = sys_region->ar.start;
		ranges[i].end = sys_region->ar.end;
		if (i == 0)
			continue;
		if (ranges[i - 1].end > ranges[i].start)
			goto out;
	}
	err = damon_set_regions(t, ranges, sysfs_regions->nr);
out:
	kfree(ranges);
	return err;

}

static int damon_sysfs_add_target(struct damon_sysfs_target *sys_target,
		struct damon_ctx *ctx)
{
	struct damon_target *t = damon_new_target();
	int err = -EINVAL;

	if (!t)
		return -ENOMEM;
	damon_add_target(ctx, t);
	if (damon_target_has_pid(ctx)) {
		t->pid = find_get_pid(sys_target->pid);
		if (!t->pid)
			goto destroy_targets_out;
	}
	err = damon_sysfs_set_regions(t, sys_target->regions);
	if (err)
		goto destroy_targets_out;
	return 0;

destroy_targets_out:
	damon_sysfs_destroy_targets(ctx);
	return err;
}

static int damon_sysfs_update_target_pid(struct damon_target *target, int pid)
{
	struct pid *pid_new;

	pid_new = find_get_pid(pid);
	if (!pid_new)
		return -EINVAL;

	if (pid_new == target->pid) {
		put_pid(pid_new);
		return 0;
	}

	put_pid(target->pid);
	target->pid = pid_new;
	return 0;
}

static int damon_sysfs_update_target(struct damon_target *target,
		struct damon_ctx *ctx,
		struct damon_sysfs_target *sys_target)
{
	int err;

	if (damon_target_has_pid(ctx)) {
		err = damon_sysfs_update_target_pid(target, sys_target->pid);
		if (err)
			return err;
	}

	/*
	 * Do monitoring target region boundary update only if one or more
	 * regions are set by the user.  This is for keeping current monitoring
	 * target results and range easier, especially for dynamic monitoring
	 * target regions update ops like 'vaddr'.
	 */
	if (sys_target->regions->nr)
		err = damon_sysfs_set_regions(target, sys_target->regions);
	return err;
}

static int damon_sysfs_set_targets(struct damon_ctx *ctx,
		struct damon_sysfs_targets *sysfs_targets)
{
	struct damon_target *t, *next;
	int i = 0, err;

	/* Multiple physical address space monitoring targets makes no sense */
	if (ctx->ops.id == DAMON_OPS_PADDR && sysfs_targets->nr > 1)
		return -EINVAL;

	damon_for_each_target_safe(t, next, ctx) {
		if (i < sysfs_targets->nr) {
			damon_sysfs_update_target(t, ctx,
					sysfs_targets->targets_arr[i]);
		} else {
			if (damon_target_has_pid(ctx))
				put_pid(t->pid);
			damon_destroy_target(t);
		}
		i++;
	}

	for (; i < sysfs_targets->nr; i++) {
		struct damon_sysfs_target *st = sysfs_targets->targets_arr[i];

		err = damon_sysfs_add_target(st, ctx);
		if (err)
			return err;
	}
	return 0;
}

static bool damon_sysfs_schemes_regions_updating;

static void damon_sysfs_before_terminate(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;
	struct damon_sysfs_kdamond *kdamond;
	enum damon_sysfs_cmd cmd;

	/* damon_sysfs_schemes_update_regions_stop() might not yet called */
	kdamond = damon_sysfs_cmd_request.kdamond;
	cmd = damon_sysfs_cmd_request.cmd;
	if (kdamond && ctx == kdamond->damon_ctx &&
			(cmd == DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_REGIONS ||
			 cmd == DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_BYTES) &&
			damon_sysfs_schemes_regions_updating) {
		damon_sysfs_schemes_update_regions_stop(ctx);
		damon_sysfs_schemes_regions_updating = false;
		mutex_unlock(&damon_sysfs_lock);
	}

	if (!damon_target_has_pid(ctx))
		return;

	mutex_lock(&ctx->kdamond_lock);
	damon_for_each_target_safe(t, next, ctx) {
		put_pid(t->pid);
		damon_destroy_target(t);
	}
	mutex_unlock(&ctx->kdamond_lock);
}

/*
 * damon_sysfs_upd_schemes_stats() - Update schemes stats sysfs files.
 * @kdamond:	The kobject wrapper that associated to the kdamond thread.
 *
 * This function reads the schemes stats of specific kdamond and update the
 * related values for sysfs files.  This function should be called from DAMON
 * callbacks while holding ``damon_syfs_lock``, to safely access the DAMON
 * contexts-internal data and DAMON sysfs variables.
 */
static int damon_sysfs_upd_schemes_stats(struct damon_sysfs_kdamond *kdamond)
{
	struct damon_ctx *ctx = kdamond->damon_ctx;

	if (!ctx)
		return -EINVAL;
	damon_sysfs_schemes_update_stats(
			kdamond->contexts->contexts_arr[0]->schemes, ctx);
	return 0;
}

static int damon_sysfs_upd_schemes_regions_start(
		struct damon_sysfs_kdamond *kdamond, bool total_bytes_only)
{
	struct damon_ctx *ctx = kdamond->damon_ctx;

	if (!ctx)
		return -EINVAL;
	return damon_sysfs_schemes_update_regions_start(
			kdamond->contexts->contexts_arr[0]->schemes, ctx,
			total_bytes_only);
}

static int damon_sysfs_upd_schemes_regions_stop(
		struct damon_sysfs_kdamond *kdamond)
{
	struct damon_ctx *ctx = kdamond->damon_ctx;

	if (!ctx)
		return -EINVAL;
	return damon_sysfs_schemes_update_regions_stop(ctx);
}

static int damon_sysfs_clear_schemes_regions(
		struct damon_sysfs_kdamond *kdamond)
{
	struct damon_ctx *ctx = kdamond->damon_ctx;

	if (!ctx)
		return -EINVAL;
	return damon_sysfs_schemes_clear_regions(
			kdamond->contexts->contexts_arr[0]->schemes, ctx);
}

static inline bool damon_sysfs_kdamond_running(
		struct damon_sysfs_kdamond *kdamond)
{
	return kdamond->damon_ctx &&
		damon_sysfs_ctx_running(kdamond->damon_ctx);
}

static int damon_sysfs_apply_inputs(struct damon_ctx *ctx,
		struct damon_sysfs_context *sys_ctx)
{
	int err;

	err = damon_select_ops(ctx, sys_ctx->ops_id);
	if (err)
		return err;
	err = damon_sysfs_set_attrs(ctx, sys_ctx->attrs);
	if (err)
		return err;
	err = damon_sysfs_set_targets(ctx, sys_ctx->targets);
	if (err)
		return err;
	return damon_sysfs_set_schemes(ctx, sys_ctx->schemes);
}

/*
 * damon_sysfs_commit_input() - Commit user inputs to a running kdamond.
 * @kdamond:	The kobject wrapper for the associated kdamond.
 *
 * If the sysfs input is wrong, the kdamond will be terminated.
 */
static int damon_sysfs_commit_input(struct damon_sysfs_kdamond *kdamond)
{
	if (!damon_sysfs_kdamond_running(kdamond))
		return -EINVAL;
	/* TODO: Support multiple contexts per kdamond */
	if (kdamond->contexts->nr != 1)
		return -EINVAL;

	return damon_sysfs_apply_inputs(kdamond->damon_ctx,
			kdamond->contexts->contexts_arr[0]);
}

/*
 * damon_sysfs_cmd_request_callback() - DAMON callback for handling requests.
 * @c:		The DAMON context of the callback.
 * @active:	Whether @c is not deactivated due to watermarks.
 *
 * This function is periodically called back from the kdamond thread for @c.
 * Then, it checks if there is a waiting DAMON sysfs request and handles it.
 */
static int damon_sysfs_cmd_request_callback(struct damon_ctx *c, bool active)
{
	struct damon_sysfs_kdamond *kdamond;
	bool total_bytes_only = false;
	int err = 0;

	/* avoid deadlock due to concurrent state_store('off') */
	if (!damon_sysfs_schemes_regions_updating &&
			!mutex_trylock(&damon_sysfs_lock))
		return 0;
	kdamond = damon_sysfs_cmd_request.kdamond;
	if (!kdamond || kdamond->damon_ctx != c)
		goto out;
	switch (damon_sysfs_cmd_request.cmd) {
	case DAMON_SYSFS_CMD_UPDATE_SCHEMES_STATS:
		err = damon_sysfs_upd_schemes_stats(kdamond);
		break;
	case DAMON_SYSFS_CMD_COMMIT:
		err = damon_sysfs_commit_input(kdamond);
		break;
	case DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_BYTES:
		total_bytes_only = true;
		fallthrough;
	case DAMON_SYSFS_CMD_UPDATE_SCHEMES_TRIED_REGIONS:
		if (!damon_sysfs_schemes_regions_updating) {
			err = damon_sysfs_upd_schemes_regions_start(kdamond,
					total_bytes_only);
			if (!err) {
				damon_sysfs_schemes_regions_updating = true;
				goto keep_lock_out;
			}
		} else {
			/*
			 * Continue regions updating if DAMON is till
			 * active and the update for all schemes is not
			 * finished.
			 */
			if (active && !damos_sysfs_regions_upd_done())
				goto keep_lock_out;
			err = damon_sysfs_upd_schemes_regions_stop(kdamond);
			damon_sysfs_schemes_regions_updating = false;
		}
		break;
	case DAMON_SYSFS_CMD_CLEAR_SCHEMES_TRIED_REGIONS:
		err = damon_sysfs_clear_schemes_regions(kdamond);
		break;
	default:
		break;
	}
	/* Mark the request as invalid now. */
	damon_sysfs_cmd_request.kdamond = NULL;
out:
	if (!damon_sysfs_schemes_regions_updating)
		mutex_unlock(&damon_sysfs_lock);
keep_lock_out:
	return err;
}

static int damon_sysfs_after_wmarks_check(struct damon_ctx *c)
{
	/*
	 * after_wmarks_check() is called back while the context is deactivated
	 * by watermarks.
	 */
	return damon_sysfs_cmd_request_callback(c, false);
}

static int damon_sysfs_after_aggregation(struct damon_ctx *c)
{
	/*
	 * after_aggregation() is called back only while the context is not
	 * deactivated by watermarks.
	 */
	return damon_sysfs_cmd_request_callback(c, true);
}

static struct damon_ctx *damon_sysfs_build_ctx(
		struct damon_sysfs_context *sys_ctx)
{
	struct damon_ctx *ctx = damon_new_ctx();
	int err;

	if (!ctx)
		return ERR_PTR(-ENOMEM);

	err = damon_sysfs_apply_inputs(ctx, sys_ctx);
	if (err) {
		damon_destroy_ctx(ctx);
		return ERR_PTR(err);
	}

	ctx->callback.after_wmarks_check = damon_sysfs_after_wmarks_check;
	ctx->callback.after_aggregation = damon_sysfs_after_aggregation;
	ctx->callback.before_terminate = damon_sysfs_before_terminate;
	return ctx;
}

static int damon_sysfs_turn_damon_on(struct damon_sysfs_kdamond *kdamond)
{
	struct damon_ctx *ctx;
	int err;

	if (damon_sysfs_kdamond_running(kdamond))
		return -EBUSY;
	if (damon_sysfs_cmd_request.kdamond == kdamond)
		return -EBUSY;
	/* TODO: support multiple contexts per kdamond */
	if (kdamond->contexts->nr != 1)
		return -EINVAL;

	if (kdamond->damon_ctx)
		damon_destroy_ctx(kdamond->damon_ctx);
	kdamond->damon_ctx = NULL;

	ctx = damon_sysfs_build_ctx(kdamond->contexts->contexts_arr[0]);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	err = damon_start(&ctx, 1, false);
	if (err) {
		damon_destroy_ctx(ctx);
		return err;
	}
	kdamond->damon_ctx = ctx;
	return err;
}

static int damon_sysfs_turn_damon_off(struct damon_sysfs_kdamond *kdamond)
{
	if (!kdamond->damon_ctx)
		return -EINVAL;
	return damon_stop(&kdamond->damon_ctx, 1);
	/*
	 * To allow users show final monitoring results of already turned-off
	 * DAMON, we free kdamond->damon_ctx in next
	 * damon_sysfs_turn_damon_on(), or kdamonds_nr_store()
	 */
}

/*
 * damon_sysfs_handle_cmd() - Handle a command for a specific kdamond.
 * @cmd:	The command to handle.
 * @kdamond:	The kobject wrapper for the associated kdamond.
 *
 * This function handles a DAMON sysfs command for a kdamond.  For commands
 * that need to access running DAMON context-internal data, it requests
 * handling of the command to the DAMON callback
 * (@damon_sysfs_cmd_request_callback()) and wait until it is properly handled,
 * or the context is completed.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int damon_sysfs_handle_cmd(enum damon_sysfs_cmd cmd,
		struct damon_sysfs_kdamond *kdamond)
{
	bool need_wait = true;

	/* Handle commands that doesn't access DAMON context-internal data */
	switch (cmd) {
	case DAMON_SYSFS_CMD_ON:
		return damon_sysfs_turn_damon_on(kdamond);
	case DAMON_SYSFS_CMD_OFF:
		return damon_sysfs_turn_damon_off(kdamond);
	default:
		break;
	}

	/* Pass the command to DAMON callback for safe DAMON context access */
	if (damon_sysfs_cmd_request.kdamond)
		return -EBUSY;
	if (!damon_sysfs_kdamond_running(kdamond))
		return -EINVAL;
	damon_sysfs_cmd_request.cmd = cmd;
	damon_sysfs_cmd_request.kdamond = kdamond;

	/*
	 * wait until damon_sysfs_cmd_request_callback() handles the request
	 * from kdamond context
	 */
	mutex_unlock(&damon_sysfs_lock);
	while (need_wait) {
		schedule_timeout_idle(msecs_to_jiffies(100));
		if (!mutex_trylock(&damon_sysfs_lock))
			continue;
		if (!damon_sysfs_cmd_request.kdamond) {
			/* damon_sysfs_cmd_request_callback() handled */
			need_wait = false;
		} else if (!damon_sysfs_kdamond_running(kdamond)) {
			/* kdamond has already finished */
			need_wait = false;
			damon_sysfs_cmd_request.kdamond = NULL;
		}
		mutex_unlock(&damon_sysfs_lock);
	}
	mutex_lock(&damon_sysfs_lock);
	return 0;
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	enum damon_sysfs_cmd cmd;
	ssize_t ret = -EINVAL;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	for (cmd = 0; cmd < NR_DAMON_SYSFS_CMDS; cmd++) {
		if (sysfs_streq(buf, damon_sysfs_cmd_strs[cmd])) {
			ret = damon_sysfs_handle_cmd(cmd, kdamond);
			break;
		}
	}
	mutex_unlock(&damon_sysfs_lock);
	if (!ret)
		ret = count;
	return ret;
}

static ssize_t pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	struct damon_ctx *ctx;
	int pid = -1;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	ctx = kdamond->damon_ctx;
	if (!ctx)
		goto out;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		pid = ctx->kdamond->pid;
	mutex_unlock(&ctx->kdamond_lock);
out:
	mutex_unlock(&damon_sysfs_lock);
	return sysfs_emit(buf, "%d\n", pid);
}

static void damon_sysfs_kdamond_release(struct kobject *kobj)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);

	if (kdamond->damon_ctx)
		damon_destroy_ctx(kdamond->damon_ctx);
	kfree(kdamond);
}

static struct kobj_attribute damon_sysfs_kdamond_state_attr =
		__ATTR_RW_MODE(state, 0600);

static struct kobj_attribute damon_sysfs_kdamond_pid_attr =
		__ATTR_RO_MODE(pid, 0400);

static struct attribute *damon_sysfs_kdamond_attrs[] = {
	&damon_sysfs_kdamond_state_attr.attr,
	&damon_sysfs_kdamond_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamond);

static const struct kobj_type damon_sysfs_kdamond_ktype = {
	.release = damon_sysfs_kdamond_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamond_groups,
};

/*
 * kdamonds directory
 */

struct damon_sysfs_kdamonds {
	struct kobject kobj;
	struct damon_sysfs_kdamond **kdamonds_arr;
	int nr;
};

static struct damon_sysfs_kdamonds *damon_sysfs_kdamonds_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamonds), GFP_KERNEL);
}

static void damon_sysfs_kdamonds_rm_dirs(struct damon_sysfs_kdamonds *kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr = kdamonds->kdamonds_arr;
	int i;

	for (i = 0; i < kdamonds->nr; i++) {
		damon_sysfs_kdamond_rm_dirs(kdamonds_arr[i]);
		kobject_put(&kdamonds_arr[i]->kobj);
	}
	kdamonds->nr = 0;
	kfree(kdamonds_arr);
	kdamonds->kdamonds_arr = NULL;
}

static bool damon_sysfs_kdamonds_busy(struct damon_sysfs_kdamond **kdamonds,
		int nr_kdamonds)
{
	int i;

	for (i = 0; i < nr_kdamonds; i++) {
		if (damon_sysfs_kdamond_running(kdamonds[i]) ||
		    damon_sysfs_cmd_request.kdamond == kdamonds[i])
			return true;
	}

	return false;
}

static int damon_sysfs_kdamonds_add_dirs(struct damon_sysfs_kdamonds *kdamonds,
		int nr_kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr, *kdamond;
	int err, i;

	if (damon_sysfs_kdamonds_busy(kdamonds->kdamonds_arr, kdamonds->nr))
		return -EBUSY;

	damon_sysfs_kdamonds_rm_dirs(kdamonds);
	if (!nr_kdamonds)
		return 0;

	kdamonds_arr = kmalloc_array(nr_kdamonds, sizeof(*kdamonds_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!kdamonds_arr)
		return -ENOMEM;
	kdamonds->kdamonds_arr = kdamonds_arr;

	for (i = 0; i < nr_kdamonds; i++) {
		kdamond = damon_sysfs_kdamond_alloc();
		if (!kdamond) {
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&kdamond->kobj,
				&damon_sysfs_kdamond_ktype, &kdamonds->kobj,
				"%d", i);
		if (err)
			goto out;

		err = damon_sysfs_kdamond_add_dirs(kdamond);
		if (err)
			goto out;

		kdamonds_arr[i] = kdamond;
		kdamonds->nr++;
	}
	return 0;

out:
	damon_sysfs_kdamonds_rm_dirs(kdamonds);
	kobject_put(&kdamond->kobj);
	return err;
}

static ssize_t nr_kdamonds_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamonds *kdamonds = container_of(kobj,
			struct damon_sysfs_kdamonds, kobj);

	return sysfs_emit(buf, "%d\n", kdamonds->nr);
}

static ssize_t nr_kdamonds_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_kdamonds *kdamonds;
	int nr, err;

	err = kstrtoint(buf, 0, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	kdamonds = container_of(kobj, struct damon_sysfs_kdamonds, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_kdamonds_add_dirs(kdamonds, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_kdamonds_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_kdamonds, kobj));
}

static struct kobj_attribute damon_sysfs_kdamonds_nr_attr =
		__ATTR_RW_MODE(nr_kdamonds, 0600);

static struct attribute *damon_sysfs_kdamonds_attrs[] = {
	&damon_sysfs_kdamonds_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamonds);

static const struct kobj_type damon_sysfs_kdamonds_ktype = {
	.release = damon_sysfs_kdamonds_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamonds_groups,
};

/*
 * damon user interface directory
 */

struct damon_sysfs_ui_dir {
	struct kobject kobj;
	struct damon_sysfs_kdamonds *kdamonds;
};

static struct damon_sysfs_ui_dir *damon_sysfs_ui_dir_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_ui_dir), GFP_KERNEL);
}

static int damon_sysfs_ui_dir_add_dirs(struct damon_sysfs_ui_dir *ui_dir)
{
	struct damon_sysfs_kdamonds *kdamonds;
	int err;

	kdamonds = damon_sysfs_kdamonds_alloc();
	if (!kdamonds)
		return -ENOMEM;

	err = kobject_init_and_add(&kdamonds->kobj,
			&damon_sysfs_kdamonds_ktype, &ui_dir->kobj,
			"kdamonds");
	if (err) {
		kobject_put(&kdamonds->kobj);
		return err;
	}
	ui_dir->kdamonds = kdamonds;
	return err;
}

static void damon_sysfs_ui_dir_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ui_dir, kobj));
}

static struct attribute *damon_sysfs_ui_dir_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_ui_dir);

static const struct kobj_type damon_sysfs_ui_dir_ktype = {
	.release = damon_sysfs_ui_dir_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ui_dir_groups,
};

static int __init damon_sysfs_init(void)
{
	struct kobject *damon_sysfs_root;
	struct damon_sysfs_ui_dir *admin;
	int err;

	damon_sysfs_root = kobject_create_and_add("damon", mm_kobj);
	if (!damon_sysfs_root)
		return -ENOMEM;

	admin = damon_sysfs_ui_dir_alloc();
	if (!admin) {
		kobject_put(damon_sysfs_root);
		return -ENOMEM;
	}
	err = kobject_init_and_add(&admin->kobj, &damon_sysfs_ui_dir_ktype,
			damon_sysfs_root, "admin");
	if (err)
		goto out;
	err = damon_sysfs_ui_dir_add_dirs(admin);
	if (err)
		goto out;
	return 0;

out:
	kobject_put(&admin->kobj);
	kobject_put(damon_sysfs_root);
	return err;
}
subsys_initcall(damon_sysfs_init);

#include "sysfs-test.h"
