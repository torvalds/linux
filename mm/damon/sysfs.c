// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

static DEFINE_MUTEX(damon_sysfs_lock);

/*
 * unsigned long range directory
 */

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

static struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
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

static void damon_sysfs_ul_range_release(struct kobject *kobj)
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

static struct kobj_type damon_sysfs_ul_range_ktype = {
	.release = damon_sysfs_ul_range_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ul_range_groups,
};

/*
 * schemes/stats directory
 */

struct damon_sysfs_stats {
	struct kobject kobj;
	unsigned long nr_tried;
	unsigned long sz_tried;
	unsigned long nr_applied;
	unsigned long sz_applied;
	unsigned long qt_exceeds;
};

static struct damon_sysfs_stats *damon_sysfs_stats_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_stats), GFP_KERNEL);
}

static ssize_t nr_tried_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->nr_tried);
}

static ssize_t sz_tried_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->sz_tried);
}

static ssize_t nr_applied_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->nr_applied);
}

static ssize_t sz_applied_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->sz_applied);
}

static ssize_t qt_exceeds_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_stats *stats = container_of(kobj,
			struct damon_sysfs_stats, kobj);

	return sysfs_emit(buf, "%lu\n", stats->qt_exceeds);
}

static void damon_sysfs_stats_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_stats, kobj));
}

static struct kobj_attribute damon_sysfs_stats_nr_tried_attr =
		__ATTR_RO_MODE(nr_tried, 0400);

static struct kobj_attribute damon_sysfs_stats_sz_tried_attr =
		__ATTR_RO_MODE(sz_tried, 0400);

static struct kobj_attribute damon_sysfs_stats_nr_applied_attr =
		__ATTR_RO_MODE(nr_applied, 0400);

static struct kobj_attribute damon_sysfs_stats_sz_applied_attr =
		__ATTR_RO_MODE(sz_applied, 0400);

static struct kobj_attribute damon_sysfs_stats_qt_exceeds_attr =
		__ATTR_RO_MODE(qt_exceeds, 0400);

static struct attribute *damon_sysfs_stats_attrs[] = {
	&damon_sysfs_stats_nr_tried_attr.attr,
	&damon_sysfs_stats_sz_tried_attr.attr,
	&damon_sysfs_stats_nr_applied_attr.attr,
	&damon_sysfs_stats_sz_applied_attr.attr,
	&damon_sysfs_stats_qt_exceeds_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_stats);

static struct kobj_type damon_sysfs_stats_ktype = {
	.release = damon_sysfs_stats_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_stats_groups,
};

/*
 * watermarks directory
 */

struct damon_sysfs_watermarks {
	struct kobject kobj;
	enum damos_wmark_metric metric;
	unsigned long interval_us;
	unsigned long high;
	unsigned long mid;
	unsigned long low;
};

static struct damon_sysfs_watermarks *damon_sysfs_watermarks_alloc(
		enum damos_wmark_metric metric, unsigned long interval_us,
		unsigned long high, unsigned long mid, unsigned long low)
{
	struct damon_sysfs_watermarks *watermarks = kmalloc(
			sizeof(*watermarks), GFP_KERNEL);

	if (!watermarks)
		return NULL;
	watermarks->kobj = (struct kobject){};
	watermarks->metric = metric;
	watermarks->interval_us = interval_us;
	watermarks->high = high;
	watermarks->mid = mid;
	watermarks->low = low;
	return watermarks;
}

/* Should match with enum damos_wmark_metric */
static const char * const damon_sysfs_wmark_metric_strs[] = {
	"none",
	"free_mem_rate",
};

static ssize_t metric_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_sysfs_wmark_metric_strs[watermarks->metric]);
}

static ssize_t metric_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	enum damos_wmark_metric metric;

	for (metric = 0; metric < NR_DAMOS_WMARK_METRICS; metric++) {
		if (sysfs_streq(buf, damon_sysfs_wmark_metric_strs[metric])) {
			watermarks->metric = metric;
			return count;
		}
	}
	return -EINVAL;
}

static ssize_t interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->interval_us);
}

static ssize_t interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 0, &watermarks->interval_us);

	return err ? err : count;
}

static ssize_t high_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->high);
}

static ssize_t high_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 0, &watermarks->high);

	return err ? err : count;
}

static ssize_t mid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->mid);
}

static ssize_t mid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 0, &watermarks->mid);

	return err ? err : count;
}

static ssize_t low_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);

	return sysfs_emit(buf, "%lu\n", watermarks->low);
}

static ssize_t low_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_watermarks *watermarks = container_of(kobj,
			struct damon_sysfs_watermarks, kobj);
	int err = kstrtoul(buf, 0, &watermarks->low);

	return err ? err : count;
}

static void damon_sysfs_watermarks_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_watermarks, kobj));
}

static struct kobj_attribute damon_sysfs_watermarks_metric_attr =
		__ATTR_RW_MODE(metric, 0600);

static struct kobj_attribute damon_sysfs_watermarks_interval_us_attr =
		__ATTR_RW_MODE(interval_us, 0600);

static struct kobj_attribute damon_sysfs_watermarks_high_attr =
		__ATTR_RW_MODE(high, 0600);

static struct kobj_attribute damon_sysfs_watermarks_mid_attr =
		__ATTR_RW_MODE(mid, 0600);

static struct kobj_attribute damon_sysfs_watermarks_low_attr =
		__ATTR_RW_MODE(low, 0600);

static struct attribute *damon_sysfs_watermarks_attrs[] = {
	&damon_sysfs_watermarks_metric_attr.attr,
	&damon_sysfs_watermarks_interval_us_attr.attr,
	&damon_sysfs_watermarks_high_attr.attr,
	&damon_sysfs_watermarks_mid_attr.attr,
	&damon_sysfs_watermarks_low_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_watermarks);

static struct kobj_type damon_sysfs_watermarks_ktype = {
	.release = damon_sysfs_watermarks_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_watermarks_groups,
};

/*
 * scheme/weights directory
 */

struct damon_sysfs_weights {
	struct kobject kobj;
	unsigned int sz;
	unsigned int nr_accesses;
	unsigned int age;
};

static struct damon_sysfs_weights *damon_sysfs_weights_alloc(unsigned int sz,
		unsigned int nr_accesses, unsigned int age)
{
	struct damon_sysfs_weights *weights = kmalloc(sizeof(*weights),
			GFP_KERNEL);

	if (!weights)
		return NULL;
	weights->kobj = (struct kobject){};
	weights->sz = sz;
	weights->nr_accesses = nr_accesses;
	weights->age = age;
	return weights;
}

static ssize_t sz_permil_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->sz);
}

static ssize_t sz_permil_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 0, &weights->sz);

	return err ? err : count;
}

static ssize_t nr_accesses_permil_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->nr_accesses);
}

static ssize_t nr_accesses_permil_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 0, &weights->nr_accesses);

	return err ? err : count;
}

static ssize_t age_permil_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);

	return sysfs_emit(buf, "%u\n", weights->age);
}

static ssize_t age_permil_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_weights *weights = container_of(kobj,
			struct damon_sysfs_weights, kobj);
	int err = kstrtouint(buf, 0, &weights->age);

	return err ? err : count;
}

static void damon_sysfs_weights_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_weights, kobj));
}

static struct kobj_attribute damon_sysfs_weights_sz_attr =
		__ATTR_RW_MODE(sz_permil, 0600);

static struct kobj_attribute damon_sysfs_weights_nr_accesses_attr =
		__ATTR_RW_MODE(nr_accesses_permil, 0600);

static struct kobj_attribute damon_sysfs_weights_age_attr =
		__ATTR_RW_MODE(age_permil, 0600);

static struct attribute *damon_sysfs_weights_attrs[] = {
	&damon_sysfs_weights_sz_attr.attr,
	&damon_sysfs_weights_nr_accesses_attr.attr,
	&damon_sysfs_weights_age_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_weights);

static struct kobj_type damon_sysfs_weights_ktype = {
	.release = damon_sysfs_weights_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_weights_groups,
};

/*
 * quotas directory
 */

struct damon_sysfs_quotas {
	struct kobject kobj;
	struct damon_sysfs_weights *weights;
	unsigned long ms;
	unsigned long sz;
	unsigned long reset_interval_ms;
};

static struct damon_sysfs_quotas *damon_sysfs_quotas_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_quotas), GFP_KERNEL);
}

static int damon_sysfs_quotas_add_dirs(struct damon_sysfs_quotas *quotas)
{
	struct damon_sysfs_weights *weights;
	int err;

	weights = damon_sysfs_weights_alloc(0, 0, 0);
	if (!weights)
		return -ENOMEM;

	err = kobject_init_and_add(&weights->kobj, &damon_sysfs_weights_ktype,
			&quotas->kobj, "weights");
	if (err)
		kobject_put(&weights->kobj);
	else
		quotas->weights = weights;
	return err;
}

static void damon_sysfs_quotas_rm_dirs(struct damon_sysfs_quotas *quotas)
{
	kobject_put(&quotas->weights->kobj);
}

static ssize_t ms_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->ms);
}

static ssize_t ms_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 0, &quotas->ms);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t bytes_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->sz);
}

static ssize_t bytes_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 0, &quotas->sz);

	if (err)
		return -EINVAL;
	return count;
}

static ssize_t reset_interval_ms_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);

	return sysfs_emit(buf, "%lu\n", quotas->reset_interval_ms);
}

static ssize_t reset_interval_ms_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_quotas *quotas = container_of(kobj,
			struct damon_sysfs_quotas, kobj);
	int err = kstrtoul(buf, 0, &quotas->reset_interval_ms);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_quotas_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_quotas, kobj));
}

static struct kobj_attribute damon_sysfs_quotas_ms_attr =
		__ATTR_RW_MODE(ms, 0600);

static struct kobj_attribute damon_sysfs_quotas_sz_attr =
		__ATTR_RW_MODE(bytes, 0600);

static struct kobj_attribute damon_sysfs_quotas_reset_interval_ms_attr =
		__ATTR_RW_MODE(reset_interval_ms, 0600);

static struct attribute *damon_sysfs_quotas_attrs[] = {
	&damon_sysfs_quotas_ms_attr.attr,
	&damon_sysfs_quotas_sz_attr.attr,
	&damon_sysfs_quotas_reset_interval_ms_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_quotas);

static struct kobj_type damon_sysfs_quotas_ktype = {
	.release = damon_sysfs_quotas_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_quotas_groups,
};

/*
 * access_pattern directory
 */

struct damon_sysfs_access_pattern {
	struct kobject kobj;
	struct damon_sysfs_ul_range *sz;
	struct damon_sysfs_ul_range *nr_accesses;
	struct damon_sysfs_ul_range *age;
};

static
struct damon_sysfs_access_pattern *damon_sysfs_access_pattern_alloc(void)
{
	struct damon_sysfs_access_pattern *access_pattern =
		kmalloc(sizeof(*access_pattern), GFP_KERNEL);

	if (!access_pattern)
		return NULL;
	access_pattern->kobj = (struct kobject){};
	return access_pattern;
}

static int damon_sysfs_access_pattern_add_range_dir(
		struct damon_sysfs_access_pattern *access_pattern,
		struct damon_sysfs_ul_range **range_dir_ptr,
		char *name)
{
	struct damon_sysfs_ul_range *range = damon_sysfs_ul_range_alloc(0, 0);
	int err;

	if (!range)
		return -ENOMEM;
	err = kobject_init_and_add(&range->kobj, &damon_sysfs_ul_range_ktype,
			&access_pattern->kobj, name);
	if (err)
		kobject_put(&range->kobj);
	else
		*range_dir_ptr = range;
	return err;
}

static int damon_sysfs_access_pattern_add_dirs(
		struct damon_sysfs_access_pattern *access_pattern)
{
	int err;

	err = damon_sysfs_access_pattern_add_range_dir(access_pattern,
			&access_pattern->sz, "sz");
	if (err)
		goto put_sz_out;

	err = damon_sysfs_access_pattern_add_range_dir(access_pattern,
			&access_pattern->nr_accesses, "nr_accesses");
	if (err)
		goto put_nr_accesses_sz_out;

	err = damon_sysfs_access_pattern_add_range_dir(access_pattern,
			&access_pattern->age, "age");
	if (err)
		goto put_age_nr_accesses_sz_out;
	return 0;

put_age_nr_accesses_sz_out:
	kobject_put(&access_pattern->age->kobj);
	access_pattern->age = NULL;
put_nr_accesses_sz_out:
	kobject_put(&access_pattern->nr_accesses->kobj);
	access_pattern->nr_accesses = NULL;
put_sz_out:
	kobject_put(&access_pattern->sz->kobj);
	access_pattern->sz = NULL;
	return err;
}

static void damon_sysfs_access_pattern_rm_dirs(
		struct damon_sysfs_access_pattern *access_pattern)
{
	kobject_put(&access_pattern->sz->kobj);
	kobject_put(&access_pattern->nr_accesses->kobj);
	kobject_put(&access_pattern->age->kobj);
}

static void damon_sysfs_access_pattern_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_access_pattern, kobj));
}

static struct attribute *damon_sysfs_access_pattern_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_access_pattern);

static struct kobj_type damon_sysfs_access_pattern_ktype = {
	.release = damon_sysfs_access_pattern_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_access_pattern_groups,
};

/*
 * scheme directory
 */

struct damon_sysfs_scheme {
	struct kobject kobj;
	enum damos_action action;
	struct damon_sysfs_access_pattern *access_pattern;
	struct damon_sysfs_quotas *quotas;
	struct damon_sysfs_watermarks *watermarks;
	struct damon_sysfs_stats *stats;
};

/* This should match with enum damos_action */
static const char * const damon_sysfs_damos_action_strs[] = {
	"willneed",
	"cold",
	"pageout",
	"hugepage",
	"nohugepage",
	"lru_prio",
	"lru_deprio",
	"stat",
};

static struct damon_sysfs_scheme *damon_sysfs_scheme_alloc(
		enum damos_action action)
{
	struct damon_sysfs_scheme *scheme = kmalloc(sizeof(*scheme),
				GFP_KERNEL);

	if (!scheme)
		return NULL;
	scheme->kobj = (struct kobject){};
	scheme->action = action;
	return scheme;
}

static int damon_sysfs_scheme_set_access_pattern(
		struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_access_pattern *access_pattern;
	int err;

	access_pattern = damon_sysfs_access_pattern_alloc();
	if (!access_pattern)
		return -ENOMEM;
	err = kobject_init_and_add(&access_pattern->kobj,
			&damon_sysfs_access_pattern_ktype, &scheme->kobj,
			"access_pattern");
	if (err)
		goto out;
	err = damon_sysfs_access_pattern_add_dirs(access_pattern);
	if (err)
		goto out;
	scheme->access_pattern = access_pattern;
	return 0;

out:
	kobject_put(&access_pattern->kobj);
	return err;
}

static int damon_sysfs_scheme_set_quotas(struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_quotas *quotas = damon_sysfs_quotas_alloc();
	int err;

	if (!quotas)
		return -ENOMEM;
	err = kobject_init_and_add(&quotas->kobj, &damon_sysfs_quotas_ktype,
			&scheme->kobj, "quotas");
	if (err)
		goto out;
	err = damon_sysfs_quotas_add_dirs(quotas);
	if (err)
		goto out;
	scheme->quotas = quotas;
	return 0;

out:
	kobject_put(&quotas->kobj);
	return err;
}

static int damon_sysfs_scheme_set_watermarks(struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_watermarks *watermarks =
		damon_sysfs_watermarks_alloc(DAMOS_WMARK_NONE, 0, 0, 0, 0);
	int err;

	if (!watermarks)
		return -ENOMEM;
	err = kobject_init_and_add(&watermarks->kobj,
			&damon_sysfs_watermarks_ktype, &scheme->kobj,
			"watermarks");
	if (err)
		kobject_put(&watermarks->kobj);
	else
		scheme->watermarks = watermarks;
	return err;
}

static int damon_sysfs_scheme_set_stats(struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_stats *stats = damon_sysfs_stats_alloc();
	int err;

	if (!stats)
		return -ENOMEM;
	err = kobject_init_and_add(&stats->kobj, &damon_sysfs_stats_ktype,
			&scheme->kobj, "stats");
	if (err)
		kobject_put(&stats->kobj);
	else
		scheme->stats = stats;
	return err;
}

static int damon_sysfs_scheme_add_dirs(struct damon_sysfs_scheme *scheme)
{
	int err;

	err = damon_sysfs_scheme_set_access_pattern(scheme);
	if (err)
		return err;
	err = damon_sysfs_scheme_set_quotas(scheme);
	if (err)
		goto put_access_pattern_out;
	err = damon_sysfs_scheme_set_watermarks(scheme);
	if (err)
		goto put_quotas_access_pattern_out;
	err = damon_sysfs_scheme_set_stats(scheme);
	if (err)
		goto put_watermarks_quotas_access_pattern_out;
	return 0;

put_watermarks_quotas_access_pattern_out:
	kobject_put(&scheme->watermarks->kobj);
	scheme->watermarks = NULL;
put_quotas_access_pattern_out:
	kobject_put(&scheme->quotas->kobj);
	scheme->quotas = NULL;
put_access_pattern_out:
	kobject_put(&scheme->access_pattern->kobj);
	scheme->access_pattern = NULL;
	return err;
}

static void damon_sysfs_scheme_rm_dirs(struct damon_sysfs_scheme *scheme)
{
	damon_sysfs_access_pattern_rm_dirs(scheme->access_pattern);
	kobject_put(&scheme->access_pattern->kobj);
	damon_sysfs_quotas_rm_dirs(scheme->quotas);
	kobject_put(&scheme->quotas->kobj);
	kobject_put(&scheme->watermarks->kobj);
	kobject_put(&scheme->stats->kobj);
}

static ssize_t action_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_sysfs_damos_action_strs[scheme->action]);
}

static ssize_t action_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);
	enum damos_action action;

	for (action = 0; action < NR_DAMOS_ACTIONS; action++) {
		if (sysfs_streq(buf, damon_sysfs_damos_action_strs[action])) {
			scheme->action = action;
			return count;
		}
	}
	return -EINVAL;
}

static void damon_sysfs_scheme_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_scheme, kobj));
}

static struct kobj_attribute damon_sysfs_scheme_action_attr =
		__ATTR_RW_MODE(action, 0600);

static struct attribute *damon_sysfs_scheme_attrs[] = {
	&damon_sysfs_scheme_action_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme);

static struct kobj_type damon_sysfs_scheme_ktype = {
	.release = damon_sysfs_scheme_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_groups,
};

/*
 * schemes directory
 */

struct damon_sysfs_schemes {
	struct kobject kobj;
	struct damon_sysfs_scheme **schemes_arr;
	int nr;
};

static struct damon_sysfs_schemes *damon_sysfs_schemes_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_schemes), GFP_KERNEL);
}

static void damon_sysfs_schemes_rm_dirs(struct damon_sysfs_schemes *schemes)
{
	struct damon_sysfs_scheme **schemes_arr = schemes->schemes_arr;
	int i;

	for (i = 0; i < schemes->nr; i++) {
		damon_sysfs_scheme_rm_dirs(schemes_arr[i]);
		kobject_put(&schemes_arr[i]->kobj);
	}
	schemes->nr = 0;
	kfree(schemes_arr);
	schemes->schemes_arr = NULL;
}

static int damon_sysfs_schemes_add_dirs(struct damon_sysfs_schemes *schemes,
		int nr_schemes)
{
	struct damon_sysfs_scheme **schemes_arr, *scheme;
	int err, i;

	damon_sysfs_schemes_rm_dirs(schemes);
	if (!nr_schemes)
		return 0;

	schemes_arr = kmalloc_array(nr_schemes, sizeof(*schemes_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!schemes_arr)
		return -ENOMEM;
	schemes->schemes_arr = schemes_arr;

	for (i = 0; i < nr_schemes; i++) {
		scheme = damon_sysfs_scheme_alloc(DAMOS_STAT);
		if (!scheme) {
			damon_sysfs_schemes_rm_dirs(schemes);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&scheme->kobj,
				&damon_sysfs_scheme_ktype, &schemes->kobj,
				"%d", i);
		if (err)
			goto out;
		err = damon_sysfs_scheme_add_dirs(scheme);
		if (err)
			goto out;

		schemes_arr[i] = scheme;
		schemes->nr++;
	}
	return 0;

out:
	damon_sysfs_schemes_rm_dirs(schemes);
	kobject_put(&scheme->kobj);
	return err;
}

static ssize_t nr_schemes_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_schemes *schemes = container_of(kobj,
			struct damon_sysfs_schemes, kobj);

	return sysfs_emit(buf, "%d\n", schemes->nr);
}

static ssize_t nr_schemes_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_schemes *schemes;
	int nr, err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	schemes = container_of(kobj, struct damon_sysfs_schemes, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_schemes_add_dirs(schemes, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;
	return count;
}

static void damon_sysfs_schemes_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_schemes, kobj));
}

static struct kobj_attribute damon_sysfs_schemes_nr_attr =
		__ATTR_RW_MODE(nr_schemes, 0600);

static struct attribute *damon_sysfs_schemes_attrs[] = {
	&damon_sysfs_schemes_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_schemes);

static struct kobj_type damon_sysfs_schemes_ktype = {
	.release = damon_sysfs_schemes_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_schemes_groups,
};

/*
 * init region directory
 */

struct damon_sysfs_region {
	struct kobject kobj;
	unsigned long start;
	unsigned long end;
};

static struct damon_sysfs_region *damon_sysfs_region_alloc(
		unsigned long start,
		unsigned long end)
{
	struct damon_sysfs_region *region = kmalloc(sizeof(*region),
			GFP_KERNEL);

	if (!region)
		return NULL;
	region->kobj = (struct kobject){};
	region->start = start;
	region->end = end;
	return region;
}

static ssize_t start_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->start);
}

static ssize_t start_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 0, &region->start);

	return err ? err : count;
}

static ssize_t end_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->end);
}

static ssize_t end_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct damon_sysfs_region *region = container_of(kobj,
			struct damon_sysfs_region, kobj);
	int err = kstrtoul(buf, 0, &region->end);

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

static struct kobj_type damon_sysfs_region_ktype = {
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
		region = damon_sysfs_region_alloc(0, 0);
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

static struct kobj_type damon_sysfs_regions_ktype = {
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

static struct kobj_type damon_sysfs_target_ktype = {
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

static struct kobj_type damon_sysfs_targets_ktype = {
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

static struct kobj_type damon_sysfs_intervals_ktype = {
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

static struct kobj_type damon_sysfs_attrs_ktype = {
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

static struct kobj_type damon_sysfs_context_ktype = {
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

static struct kobj_type damon_sysfs_contexts_ktype = {
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

		if (sys_region->start > sys_region->end)
			goto out;

		ranges[i].start = sys_region->start;
		ranges[i].end = sys_region->end;
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

static struct damos *damon_sysfs_mk_scheme(
		struct damon_sysfs_scheme *sysfs_scheme)
{
	struct damon_sysfs_access_pattern *access_pattern =
		sysfs_scheme->access_pattern;
	struct damon_sysfs_quotas *sysfs_quotas = sysfs_scheme->quotas;
	struct damon_sysfs_weights *sysfs_weights = sysfs_quotas->weights;
	struct damon_sysfs_watermarks *sysfs_wmarks = sysfs_scheme->watermarks;

	struct damos_access_pattern pattern = {
		.min_sz_region = access_pattern->sz->min,
		.max_sz_region = access_pattern->sz->max,
		.min_nr_accesses = access_pattern->nr_accesses->min,
		.max_nr_accesses = access_pattern->nr_accesses->max,
		.min_age_region = access_pattern->age->min,
		.max_age_region = access_pattern->age->max,
	};
	struct damos_quota quota = {
		.ms = sysfs_quotas->ms,
		.sz = sysfs_quotas->sz,
		.reset_interval = sysfs_quotas->reset_interval_ms,
		.weight_sz = sysfs_weights->sz,
		.weight_nr_accesses = sysfs_weights->nr_accesses,
		.weight_age = sysfs_weights->age,
	};
	struct damos_watermarks wmarks = {
		.metric = sysfs_wmarks->metric,
		.interval = sysfs_wmarks->interval_us,
		.high = sysfs_wmarks->high,
		.mid = sysfs_wmarks->mid,
		.low = sysfs_wmarks->low,
	};

	return damon_new_scheme(&pattern, sysfs_scheme->action, &quota,
			&wmarks);
}

static void damon_sysfs_update_scheme(struct damos *scheme,
		struct damon_sysfs_scheme *sysfs_scheme)
{
	struct damon_sysfs_access_pattern *access_pattern =
		sysfs_scheme->access_pattern;
	struct damon_sysfs_quotas *sysfs_quotas = sysfs_scheme->quotas;
	struct damon_sysfs_weights *sysfs_weights = sysfs_quotas->weights;
	struct damon_sysfs_watermarks *sysfs_wmarks = sysfs_scheme->watermarks;

	scheme->pattern.min_sz_region = access_pattern->sz->min;
	scheme->pattern.max_sz_region = access_pattern->sz->max;
	scheme->pattern.min_nr_accesses = access_pattern->nr_accesses->min;
	scheme->pattern.max_nr_accesses = access_pattern->nr_accesses->max;
	scheme->pattern.min_age_region = access_pattern->age->min;
	scheme->pattern.max_age_region = access_pattern->age->max;

	scheme->action = sysfs_scheme->action;

	scheme->quota.ms = sysfs_quotas->ms;
	scheme->quota.sz = sysfs_quotas->sz;
	scheme->quota.reset_interval = sysfs_quotas->reset_interval_ms;
	scheme->quota.weight_sz = sysfs_weights->sz;
	scheme->quota.weight_nr_accesses = sysfs_weights->nr_accesses;
	scheme->quota.weight_age = sysfs_weights->age;

	scheme->wmarks.metric = sysfs_wmarks->metric;
	scheme->wmarks.interval = sysfs_wmarks->interval_us;
	scheme->wmarks.high = sysfs_wmarks->high;
	scheme->wmarks.mid = sysfs_wmarks->mid;
	scheme->wmarks.low = sysfs_wmarks->low;
}

static int damon_sysfs_set_schemes(struct damon_ctx *ctx,
		struct damon_sysfs_schemes *sysfs_schemes)
{
	struct damos *scheme, *next;
	int i = 0;

	damon_for_each_scheme_safe(scheme, next, ctx) {
		if (i < sysfs_schemes->nr)
			damon_sysfs_update_scheme(scheme,
					sysfs_schemes->schemes_arr[i]);
		else
			damon_destroy_scheme(scheme);
		i++;
	}

	for (; i < sysfs_schemes->nr; i++) {
		struct damos *scheme, *next;

		scheme = damon_sysfs_mk_scheme(sysfs_schemes->schemes_arr[i]);
		if (!scheme) {
			damon_for_each_scheme_safe(scheme, next, ctx)
				damon_destroy_scheme(scheme);
			return -ENOMEM;
		}
		damon_add_scheme(ctx, scheme);
	}
	return 0;
}

static void damon_sysfs_before_terminate(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;

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
	struct damon_sysfs_schemes *sysfs_schemes;
	struct damos *scheme;
	int schemes_idx = 0;

	if (!ctx)
		return -EINVAL;
	sysfs_schemes = kdamond->contexts->contexts_arr[0]->schemes;
	damon_for_each_scheme(scheme, ctx) {
		struct damon_sysfs_stats *sysfs_stats;

		/* user could have removed the scheme sysfs dir */
		if (schemes_idx >= sysfs_schemes->nr)
			break;

		sysfs_stats = sysfs_schemes->schemes_arr[schemes_idx++]->stats;
		sysfs_stats->nr_tried = scheme->stat.nr_tried;
		sysfs_stats->sz_tried = scheme->stat.sz_tried;
		sysfs_stats->nr_applied = scheme->stat.nr_applied;
		sysfs_stats->sz_applied = scheme->stat.sz_applied;
		sysfs_stats->qt_exceeds = scheme->stat.qt_exceeds;
	}
	return 0;
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
 * @c:	The DAMON context of the callback.
 *
 * This function is periodically called back from the kdamond thread for @c.
 * Then, it checks if there is a waiting DAMON sysfs request and handles it.
 */
static int damon_sysfs_cmd_request_callback(struct damon_ctx *c)
{
	struct damon_sysfs_kdamond *kdamond;
	int err = 0;

	/* avoid deadlock due to concurrent state_store('off') */
	if (!mutex_trylock(&damon_sysfs_lock))
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
	default:
		break;
	}
	/* Mark the request as invalid now. */
	damon_sysfs_cmd_request.kdamond = NULL;
out:
	mutex_unlock(&damon_sysfs_lock);
	return err;
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

	ctx->callback.after_wmarks_check = damon_sysfs_cmd_request_callback;
	ctx->callback.after_aggregation = damon_sysfs_cmd_request_callback;
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

static struct kobj_type damon_sysfs_kdamond_ktype = {
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

static struct kobj_type damon_sysfs_kdamonds_ktype = {
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

static struct kobj_type damon_sysfs_ui_dir_ktype = {
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
