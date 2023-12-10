// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/slab.h>

#include "sysfs-common.h"

/*
 * scheme region directory
 */

struct damon_sysfs_scheme_region {
	struct kobject kobj;
	struct damon_addr_range ar;
	unsigned int nr_accesses;
	unsigned int age;
	struct list_head list;
};

static struct damon_sysfs_scheme_region *damon_sysfs_scheme_region_alloc(
		struct damon_region *region)
{
	struct damon_sysfs_scheme_region *sysfs_region = kmalloc(
			sizeof(*sysfs_region), GFP_KERNEL);

	if (!sysfs_region)
		return NULL;
	sysfs_region->kobj = (struct kobject){};
	sysfs_region->ar = region->ar;
	sysfs_region->nr_accesses = region->nr_accesses_bp / 10000;
	sysfs_region->age = region->age;
	INIT_LIST_HEAD(&sysfs_region->list);
	return sysfs_region;
}

static ssize_t start_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_scheme_region *region = container_of(kobj,
			struct damon_sysfs_scheme_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->ar.start);
}

static ssize_t end_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_scheme_region *region = container_of(kobj,
			struct damon_sysfs_scheme_region, kobj);

	return sysfs_emit(buf, "%lu\n", region->ar.end);
}

static ssize_t nr_accesses_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_region *region = container_of(kobj,
			struct damon_sysfs_scheme_region, kobj);

	return sysfs_emit(buf, "%u\n", region->nr_accesses);
}

static ssize_t age_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	struct damon_sysfs_scheme_region *region = container_of(kobj,
			struct damon_sysfs_scheme_region, kobj);

	return sysfs_emit(buf, "%u\n", region->age);
}

static void damon_sysfs_scheme_region_release(struct kobject *kobj)
{
	struct damon_sysfs_scheme_region *region = container_of(kobj,
			struct damon_sysfs_scheme_region, kobj);

	list_del(&region->list);
	kfree(region);
}

static struct kobj_attribute damon_sysfs_scheme_region_start_attr =
		__ATTR_RO_MODE(start, 0400);

static struct kobj_attribute damon_sysfs_scheme_region_end_attr =
		__ATTR_RO_MODE(end, 0400);

static struct kobj_attribute damon_sysfs_scheme_region_nr_accesses_attr =
		__ATTR_RO_MODE(nr_accesses, 0400);

static struct kobj_attribute damon_sysfs_scheme_region_age_attr =
		__ATTR_RO_MODE(age, 0400);

static struct attribute *damon_sysfs_scheme_region_attrs[] = {
	&damon_sysfs_scheme_region_start_attr.attr,
	&damon_sysfs_scheme_region_end_attr.attr,
	&damon_sysfs_scheme_region_nr_accesses_attr.attr,
	&damon_sysfs_scheme_region_age_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme_region);

static const struct kobj_type damon_sysfs_scheme_region_ktype = {
	.release = damon_sysfs_scheme_region_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_region_groups,
};

/*
 * scheme regions directory
 */

/*
 * enum damos_sysfs_regions_upd_status - Represent DAMOS tried regions update
 *					 status
 * @DAMOS_TRIED_REGIONS_UPD_IDLE:		Waiting for next request.
 * @DAMOS_TRIED_REGIONS_UPD_STARTED:		Update started.
 * @DAMOS_TRIED_REGIONS_UPD_FINISHED:	Update finished.
 *
 * Each DAMON-based operation scheme (&struct damos) has its own apply
 * interval, and we need to expose the scheme tried regions based on only
 * single snapshot.  For this, we keep the tried regions update status for each
 * scheme.  The status becomes 'idle' at the beginning.
 *
 * Once the tried regions update request is received, the request handling
 * start function (damon_sysfs_scheme_update_regions_start()) sets the status
 * of all schemes as 'idle' again, and register ->before_damos_apply() and
 * ->after_sampling() callbacks.
 *
 * Then, the first followup ->before_damos_apply() callback
 * (damon_sysfs_before_damos_apply()) sets the status 'started'.  The first
 * ->after_sampling() callback (damon_sysfs_after_sampling()) after the call
 * is called only after the scheme is completely applied
 * to the given snapshot.  Hence the callback knows the situation by showing
 * 'started' status, and sets the status as 'finished'.  Then,
 * damon_sysfs_before_damos_apply() understands the situation by showing the
 * 'finished' status and do nothing.
 *
 *  Finally, the tried regions request handling finisher function
 *  (damon_sysfs_schemes_update_regions_stop()) unregisters the callbacks.
 */
enum damos_sysfs_regions_upd_status {
	DAMOS_TRIED_REGIONS_UPD_IDLE,
	DAMOS_TRIED_REGIONS_UPD_STARTED,
	DAMOS_TRIED_REGIONS_UPD_FINISHED,
};

struct damon_sysfs_scheme_regions {
	struct kobject kobj;
	struct list_head regions_list;
	int nr_regions;
	unsigned long total_bytes;
	enum damos_sysfs_regions_upd_status upd_status;
};

static struct damon_sysfs_scheme_regions *
damon_sysfs_scheme_regions_alloc(void)
{
	struct damon_sysfs_scheme_regions *regions = kmalloc(sizeof(*regions),
			GFP_KERNEL);

	if (!regions)
		return NULL;

	regions->kobj = (struct kobject){};
	INIT_LIST_HEAD(&regions->regions_list);
	regions->nr_regions = 0;
	regions->total_bytes = 0;
	regions->upd_status = DAMOS_TRIED_REGIONS_UPD_IDLE;
	return regions;
}

static ssize_t total_bytes_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_regions *regions = container_of(kobj,
			struct damon_sysfs_scheme_regions, kobj);

	return sysfs_emit(buf, "%lu\n", regions->total_bytes);
}

static void damon_sysfs_scheme_regions_rm_dirs(
		struct damon_sysfs_scheme_regions *regions)
{
	struct damon_sysfs_scheme_region *r, *next;

	list_for_each_entry_safe(r, next, &regions->regions_list, list) {
		/* release function deletes it from the list */
		kobject_put(&r->kobj);
		regions->nr_regions--;
	}
}

static void damon_sysfs_scheme_regions_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_scheme_regions, kobj));
}

static struct kobj_attribute damon_sysfs_scheme_regions_total_bytes_attr =
		__ATTR_RO_MODE(total_bytes, 0400);

static struct attribute *damon_sysfs_scheme_regions_attrs[] = {
	&damon_sysfs_scheme_regions_total_bytes_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme_regions);

static const struct kobj_type damon_sysfs_scheme_regions_ktype = {
	.release = damon_sysfs_scheme_regions_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_regions_groups,
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

static const struct kobj_type damon_sysfs_stats_ktype = {
	.release = damon_sysfs_stats_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_stats_groups,
};

/*
 * filter directory
 */

struct damon_sysfs_scheme_filter {
	struct kobject kobj;
	enum damos_filter_type type;
	bool matching;
	char *memcg_path;
	struct damon_addr_range addr_range;
	int target_idx;
};

static struct damon_sysfs_scheme_filter *damon_sysfs_scheme_filter_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_scheme_filter), GFP_KERNEL);
}

/* Should match with enum damos_filter_type */
static const char * const damon_sysfs_scheme_filter_type_strs[] = {
	"anon",
	"memcg",
	"addr",
	"target",
};

static ssize_t type_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%s\n",
			damon_sysfs_scheme_filter_type_strs[filter->type]);
}

static ssize_t type_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	enum damos_filter_type type;
	ssize_t ret = -EINVAL;

	for (type = 0; type < NR_DAMOS_FILTER_TYPES; type++) {
		if (sysfs_streq(buf, damon_sysfs_scheme_filter_type_strs[
					type])) {
			filter->type = type;
			ret = count;
			break;
		}
	}
	return ret;
}

static ssize_t matching_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%c\n", filter->matching ? 'Y' : 'N');
}

static ssize_t matching_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	bool matching;
	int err = kstrtobool(buf, &matching);

	if (err)
		return err;

	filter->matching = matching;
	return count;
}

static ssize_t memcg_path_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%s\n",
			filter->memcg_path ? filter->memcg_path : "");
}

static ssize_t memcg_path_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	char *path = kmalloc(sizeof(*path) * (count + 1), GFP_KERNEL);

	if (!path)
		return -ENOMEM;

	strscpy(path, buf, count + 1);
	filter->memcg_path = path;
	return count;
}

static ssize_t addr_start_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%lu\n", filter->addr_range.start);
}

static ssize_t addr_start_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	int err = kstrtoul(buf, 0, &filter->addr_range.start);

	return err ? err : count;
}

static ssize_t addr_end_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%lu\n", filter->addr_range.end);
}

static ssize_t addr_end_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	int err = kstrtoul(buf, 0, &filter->addr_range.end);

	return err ? err : count;
}

static ssize_t damon_target_idx_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	return sysfs_emit(buf, "%d\n", filter->target_idx);
}

static ssize_t damon_target_idx_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);
	int err = kstrtoint(buf, 0, &filter->target_idx);

	return err ? err : count;
}

static void damon_sysfs_scheme_filter_release(struct kobject *kobj)
{
	struct damon_sysfs_scheme_filter *filter = container_of(kobj,
			struct damon_sysfs_scheme_filter, kobj);

	kfree(filter->memcg_path);
	kfree(filter);
}

static struct kobj_attribute damon_sysfs_scheme_filter_type_attr =
		__ATTR_RW_MODE(type, 0600);

static struct kobj_attribute damon_sysfs_scheme_filter_matching_attr =
		__ATTR_RW_MODE(matching, 0600);

static struct kobj_attribute damon_sysfs_scheme_filter_memcg_path_attr =
		__ATTR_RW_MODE(memcg_path, 0600);

static struct kobj_attribute damon_sysfs_scheme_filter_addr_start_attr =
		__ATTR_RW_MODE(addr_start, 0600);

static struct kobj_attribute damon_sysfs_scheme_filter_addr_end_attr =
		__ATTR_RW_MODE(addr_end, 0600);

static struct kobj_attribute damon_sysfs_scheme_filter_damon_target_idx_attr =
		__ATTR_RW_MODE(damon_target_idx, 0600);

static struct attribute *damon_sysfs_scheme_filter_attrs[] = {
	&damon_sysfs_scheme_filter_type_attr.attr,
	&damon_sysfs_scheme_filter_matching_attr.attr,
	&damon_sysfs_scheme_filter_memcg_path_attr.attr,
	&damon_sysfs_scheme_filter_addr_start_attr.attr,
	&damon_sysfs_scheme_filter_addr_end_attr.attr,
	&damon_sysfs_scheme_filter_damon_target_idx_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme_filter);

static const struct kobj_type damon_sysfs_scheme_filter_ktype = {
	.release = damon_sysfs_scheme_filter_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_filter_groups,
};

/*
 * filters directory
 */

struct damon_sysfs_scheme_filters {
	struct kobject kobj;
	struct damon_sysfs_scheme_filter **filters_arr;
	int nr;
};

static struct damon_sysfs_scheme_filters *
damon_sysfs_scheme_filters_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_scheme_filters), GFP_KERNEL);
}

static void damon_sysfs_scheme_filters_rm_dirs(
		struct damon_sysfs_scheme_filters *filters)
{
	struct damon_sysfs_scheme_filter **filters_arr = filters->filters_arr;
	int i;

	for (i = 0; i < filters->nr; i++)
		kobject_put(&filters_arr[i]->kobj);
	filters->nr = 0;
	kfree(filters_arr);
	filters->filters_arr = NULL;
}

static int damon_sysfs_scheme_filters_add_dirs(
		struct damon_sysfs_scheme_filters *filters, int nr_filters)
{
	struct damon_sysfs_scheme_filter **filters_arr, *filter;
	int err, i;

	damon_sysfs_scheme_filters_rm_dirs(filters);
	if (!nr_filters)
		return 0;

	filters_arr = kmalloc_array(nr_filters, sizeof(*filters_arr),
			GFP_KERNEL | __GFP_NOWARN);
	if (!filters_arr)
		return -ENOMEM;
	filters->filters_arr = filters_arr;

	for (i = 0; i < nr_filters; i++) {
		filter = damon_sysfs_scheme_filter_alloc();
		if (!filter) {
			damon_sysfs_scheme_filters_rm_dirs(filters);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&filter->kobj,
				&damon_sysfs_scheme_filter_ktype,
				&filters->kobj, "%d", i);
		if (err) {
			kobject_put(&filter->kobj);
			damon_sysfs_scheme_filters_rm_dirs(filters);
			return err;
		}

		filters_arr[i] = filter;
		filters->nr++;
	}
	return 0;
}

static ssize_t nr_filters_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme_filters *filters = container_of(kobj,
			struct damon_sysfs_scheme_filters, kobj);

	return sysfs_emit(buf, "%d\n", filters->nr);
}

static ssize_t nr_filters_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme_filters *filters;
	int nr, err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	filters = container_of(kobj, struct damon_sysfs_scheme_filters, kobj);

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_scheme_filters_add_dirs(filters, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_scheme_filters_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_scheme_filters, kobj));
}

static struct kobj_attribute damon_sysfs_scheme_filters_nr_attr =
		__ATTR_RW_MODE(nr_filters, 0600);

static struct attribute *damon_sysfs_scheme_filters_attrs[] = {
	&damon_sysfs_scheme_filters_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme_filters);

static const struct kobj_type damon_sysfs_scheme_filters_ktype = {
	.release = damon_sysfs_scheme_filters_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_filters_groups,
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

static const struct kobj_type damon_sysfs_watermarks_ktype = {
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

static const struct kobj_type damon_sysfs_weights_ktype = {
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

static const struct kobj_type damon_sysfs_quotas_ktype = {
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

static const struct kobj_type damon_sysfs_access_pattern_ktype = {
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
	unsigned long apply_interval_us;
	struct damon_sysfs_quotas *quotas;
	struct damon_sysfs_watermarks *watermarks;
	struct damon_sysfs_scheme_filters *filters;
	struct damon_sysfs_stats *stats;
	struct damon_sysfs_scheme_regions *tried_regions;
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
		enum damos_action action, unsigned long apply_interval_us)
{
	struct damon_sysfs_scheme *scheme = kmalloc(sizeof(*scheme),
				GFP_KERNEL);

	if (!scheme)
		return NULL;
	scheme->kobj = (struct kobject){};
	scheme->action = action;
	scheme->apply_interval_us = apply_interval_us;
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

static int damon_sysfs_scheme_set_filters(struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_scheme_filters *filters =
		damon_sysfs_scheme_filters_alloc();
	int err;

	if (!filters)
		return -ENOMEM;
	err = kobject_init_and_add(&filters->kobj,
			&damon_sysfs_scheme_filters_ktype, &scheme->kobj,
			"filters");
	if (err)
		kobject_put(&filters->kobj);
	else
		scheme->filters = filters;
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

static int damon_sysfs_scheme_set_tried_regions(
		struct damon_sysfs_scheme *scheme)
{
	struct damon_sysfs_scheme_regions *tried_regions =
		damon_sysfs_scheme_regions_alloc();
	int err;

	if (!tried_regions)
		return -ENOMEM;
	err = kobject_init_and_add(&tried_regions->kobj,
			&damon_sysfs_scheme_regions_ktype, &scheme->kobj,
			"tried_regions");
	if (err)
		kobject_put(&tried_regions->kobj);
	else
		scheme->tried_regions = tried_regions;
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
	err = damon_sysfs_scheme_set_filters(scheme);
	if (err)
		goto put_watermarks_quotas_access_pattern_out;
	err = damon_sysfs_scheme_set_stats(scheme);
	if (err)
		goto put_filters_watermarks_quotas_access_pattern_out;
	err = damon_sysfs_scheme_set_tried_regions(scheme);
	if (err)
		goto put_tried_regions_out;
	return 0;

put_tried_regions_out:
	kobject_put(&scheme->tried_regions->kobj);
	scheme->tried_regions = NULL;
put_filters_watermarks_quotas_access_pattern_out:
	kobject_put(&scheme->filters->kobj);
	scheme->filters = NULL;
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
	damon_sysfs_scheme_filters_rm_dirs(scheme->filters);
	kobject_put(&scheme->filters->kobj);
	kobject_put(&scheme->stats->kobj);
	damon_sysfs_scheme_regions_rm_dirs(scheme->tried_regions);
	kobject_put(&scheme->tried_regions->kobj);
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

static ssize_t apply_interval_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);

	return sysfs_emit(buf, "%lu\n", scheme->apply_interval_us);
}

static ssize_t apply_interval_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_scheme *scheme = container_of(kobj,
			struct damon_sysfs_scheme, kobj);
	int err = kstrtoul(buf, 0, &scheme->apply_interval_us);

	return err ? err : count;
}

static void damon_sysfs_scheme_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_scheme, kobj));
}

static struct kobj_attribute damon_sysfs_scheme_action_attr =
		__ATTR_RW_MODE(action, 0600);

static struct kobj_attribute damon_sysfs_scheme_apply_interval_us_attr =
		__ATTR_RW_MODE(apply_interval_us, 0600);

static struct attribute *damon_sysfs_scheme_attrs[] = {
	&damon_sysfs_scheme_action_attr.attr,
	&damon_sysfs_scheme_apply_interval_us_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_scheme);

static const struct kobj_type damon_sysfs_scheme_ktype = {
	.release = damon_sysfs_scheme_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_scheme_groups,
};

/*
 * schemes directory
 */

struct damon_sysfs_schemes *damon_sysfs_schemes_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_schemes), GFP_KERNEL);
}

void damon_sysfs_schemes_rm_dirs(struct damon_sysfs_schemes *schemes)
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
		/*
		 * apply_interval_us as 0 means same to aggregation interval
		 * (same to before-apply_interval behavior)
		 */
		scheme = damon_sysfs_scheme_alloc(DAMOS_STAT, 0);
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

const struct kobj_type damon_sysfs_schemes_ktype = {
	.release = damon_sysfs_schemes_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_schemes_groups,
};

static bool damon_sysfs_memcg_path_eq(struct mem_cgroup *memcg,
		char *memcg_path_buf, char *path)
{
#ifdef CONFIG_MEMCG
	cgroup_path(memcg->css.cgroup, memcg_path_buf, PATH_MAX);
	if (sysfs_streq(memcg_path_buf, path))
		return true;
#endif /* CONFIG_MEMCG */
	return false;
}

static int damon_sysfs_memcg_path_to_id(char *memcg_path, unsigned short *id)
{
	struct mem_cgroup *memcg;
	char *path;
	bool found = false;

	if (!memcg_path)
		return -EINVAL;

	path = kmalloc(sizeof(*path) * PATH_MAX, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	for (memcg = mem_cgroup_iter(NULL, NULL, NULL); memcg;
			memcg = mem_cgroup_iter(NULL, memcg, NULL)) {
		/* skip removed memcg */
		if (!mem_cgroup_id(memcg))
			continue;
		if (damon_sysfs_memcg_path_eq(memcg, path, memcg_path)) {
			*id = mem_cgroup_id(memcg);
			found = true;
			break;
		}
	}

	kfree(path);
	return found ? 0 : -EINVAL;
}

static int damon_sysfs_set_scheme_filters(struct damos *scheme,
		struct damon_sysfs_scheme_filters *sysfs_filters)
{
	int i;
	struct damos_filter *filter, *next;

	damos_for_each_filter_safe(filter, next, scheme)
		damos_destroy_filter(filter);

	for (i = 0; i < sysfs_filters->nr; i++) {
		struct damon_sysfs_scheme_filter *sysfs_filter =
			sysfs_filters->filters_arr[i];
		struct damos_filter *filter =
			damos_new_filter(sysfs_filter->type,
					sysfs_filter->matching);
		int err;

		if (!filter)
			return -ENOMEM;
		if (filter->type == DAMOS_FILTER_TYPE_MEMCG) {
			err = damon_sysfs_memcg_path_to_id(
					sysfs_filter->memcg_path,
					&filter->memcg_id);
			if (err) {
				damos_destroy_filter(filter);
				return err;
			}
		} else if (filter->type == DAMOS_FILTER_TYPE_ADDR) {
			if (sysfs_filter->addr_range.end <
					sysfs_filter->addr_range.start) {
				damos_destroy_filter(filter);
				return -EINVAL;
			}
			filter->addr_range = sysfs_filter->addr_range;
		} else if (filter->type == DAMOS_FILTER_TYPE_TARGET) {
			filter->target_idx = sysfs_filter->target_idx;
		}

		damos_add_filter(scheme, filter);
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
	struct damon_sysfs_scheme_filters *sysfs_filters =
		sysfs_scheme->filters;
	struct damos *scheme;
	int err;

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

	scheme = damon_new_scheme(&pattern, sysfs_scheme->action,
			sysfs_scheme->apply_interval_us, &quota, &wmarks);
	if (!scheme)
		return NULL;

	err = damon_sysfs_set_scheme_filters(scheme, sysfs_filters);
	if (err) {
		damon_destroy_scheme(scheme);
		return NULL;
	}
	return scheme;
}

static void damon_sysfs_update_scheme(struct damos *scheme,
		struct damon_sysfs_scheme *sysfs_scheme)
{
	struct damon_sysfs_access_pattern *access_pattern =
		sysfs_scheme->access_pattern;
	struct damon_sysfs_quotas *sysfs_quotas = sysfs_scheme->quotas;
	struct damon_sysfs_weights *sysfs_weights = sysfs_quotas->weights;
	struct damon_sysfs_watermarks *sysfs_wmarks = sysfs_scheme->watermarks;
	int err;

	scheme->pattern.min_sz_region = access_pattern->sz->min;
	scheme->pattern.max_sz_region = access_pattern->sz->max;
	scheme->pattern.min_nr_accesses = access_pattern->nr_accesses->min;
	scheme->pattern.max_nr_accesses = access_pattern->nr_accesses->max;
	scheme->pattern.min_age_region = access_pattern->age->min;
	scheme->pattern.max_age_region = access_pattern->age->max;

	scheme->action = sysfs_scheme->action;
	scheme->apply_interval_us = sysfs_scheme->apply_interval_us;

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

	err = damon_sysfs_set_scheme_filters(scheme, sysfs_scheme->filters);
	if (err)
		damon_destroy_scheme(scheme);
}

int damon_sysfs_set_schemes(struct damon_ctx *ctx,
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

void damon_sysfs_schemes_update_stats(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx)
{
	struct damos *scheme;
	int schemes_idx = 0;

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
}

/*
 * damon_sysfs_schemes that need to update its schemes regions dir.  Protected
 * by damon_sysfs_lock
 */
static struct damon_sysfs_schemes *damon_sysfs_schemes_for_damos_callback;
static int damon_sysfs_schemes_region_idx;
static bool damos_regions_upd_total_bytes_only;

/*
 * DAMON callback that called before damos apply.  While this callback is
 * registered, damon_sysfs_lock should be held to ensure the regions
 * directories exist.
 */
static int damon_sysfs_before_damos_apply(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_region *r,
		struct damos *s)
{
	struct damos *scheme;
	struct damon_sysfs_scheme_regions *sysfs_regions;
	struct damon_sysfs_scheme_region *region;
	struct damon_sysfs_schemes *sysfs_schemes =
		damon_sysfs_schemes_for_damos_callback;
	int schemes_idx = 0;

	damon_for_each_scheme(scheme, ctx) {
		if (scheme == s)
			break;
		schemes_idx++;
	}

	/* user could have removed the scheme sysfs dir */
	if (schemes_idx >= sysfs_schemes->nr)
		return 0;

	sysfs_regions = sysfs_schemes->schemes_arr[schemes_idx]->tried_regions;
	if (sysfs_regions->upd_status == DAMOS_TRIED_REGIONS_UPD_FINISHED)
		return 0;
	if (sysfs_regions->upd_status == DAMOS_TRIED_REGIONS_UPD_IDLE)
		sysfs_regions->upd_status = DAMOS_TRIED_REGIONS_UPD_STARTED;
	sysfs_regions->total_bytes += r->ar.end - r->ar.start;
	if (damos_regions_upd_total_bytes_only)
		return 0;

	region = damon_sysfs_scheme_region_alloc(r);
	if (!region)
		return 0;
	list_add_tail(&region->list, &sysfs_regions->regions_list);
	sysfs_regions->nr_regions++;
	if (kobject_init_and_add(&region->kobj,
				&damon_sysfs_scheme_region_ktype,
				&sysfs_regions->kobj, "%d",
				damon_sysfs_schemes_region_idx++)) {
		kobject_put(&region->kobj);
	}
	return 0;
}

/*
 * DAMON callback that called after each accesses sampling.  While this
 * callback is registered, damon_sysfs_lock should be held to ensure the
 * regions directories exist.
 */
static int damon_sysfs_after_sampling(struct damon_ctx *ctx)
{
	struct damon_sysfs_schemes *sysfs_schemes =
		damon_sysfs_schemes_for_damos_callback;
	struct damon_sysfs_scheme_regions *sysfs_regions;
	int i;

	for (i = 0; i < sysfs_schemes->nr; i++) {
		sysfs_regions = sysfs_schemes->schemes_arr[i]->tried_regions;
		if (sysfs_regions->upd_status ==
				DAMOS_TRIED_REGIONS_UPD_STARTED)
			sysfs_regions->upd_status =
				DAMOS_TRIED_REGIONS_UPD_FINISHED;
	}

	return 0;
}

/* Called from damon_sysfs_cmd_request_callback under damon_sysfs_lock */
int damon_sysfs_schemes_clear_regions(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx)
{
	struct damos *scheme;
	int schemes_idx = 0;

	damon_for_each_scheme(scheme, ctx) {
		struct damon_sysfs_scheme *sysfs_scheme;

		/* user could have removed the scheme sysfs dir */
		if (schemes_idx >= sysfs_schemes->nr)
			break;

		sysfs_scheme = sysfs_schemes->schemes_arr[schemes_idx++];
		damon_sysfs_scheme_regions_rm_dirs(
				sysfs_scheme->tried_regions);
		sysfs_scheme->tried_regions->total_bytes = 0;
	}
	return 0;
}

static void damos_tried_regions_init_upd_status(
		struct damon_sysfs_schemes *sysfs_schemes)
{
	int i;

	for (i = 0; i < sysfs_schemes->nr; i++)
		sysfs_schemes->schemes_arr[i]->tried_regions->upd_status =
			DAMOS_TRIED_REGIONS_UPD_IDLE;
}

/* Called from damon_sysfs_cmd_request_callback under damon_sysfs_lock */
int damon_sysfs_schemes_update_regions_start(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx, bool total_bytes_only)
{
	damon_sysfs_schemes_clear_regions(sysfs_schemes, ctx);
	damon_sysfs_schemes_for_damos_callback = sysfs_schemes;
	damos_tried_regions_init_upd_status(sysfs_schemes);
	damos_regions_upd_total_bytes_only = total_bytes_only;
	ctx->callback.before_damos_apply = damon_sysfs_before_damos_apply;
	ctx->callback.after_sampling = damon_sysfs_after_sampling;
	return 0;
}

bool damos_sysfs_regions_upd_done(void)
{
	struct damon_sysfs_schemes *sysfs_schemes =
		damon_sysfs_schemes_for_damos_callback;
	struct damon_sysfs_scheme_regions *sysfs_regions;
	int i;

	for (i = 0; i < sysfs_schemes->nr; i++) {
		sysfs_regions = sysfs_schemes->schemes_arr[i]->tried_regions;
		if (sysfs_regions->upd_status !=
				DAMOS_TRIED_REGIONS_UPD_FINISHED)
			return false;
	}
	return true;
}

/*
 * Called from damon_sysfs_cmd_request_callback under damon_sysfs_lock.  Caller
 * should unlock damon_sysfs_lock which held before
 * damon_sysfs_schemes_update_regions_start()
 */
int damon_sysfs_schemes_update_regions_stop(struct damon_ctx *ctx)
{
	damon_sysfs_schemes_for_damos_callback = NULL;
	ctx->callback.before_damos_apply = NULL;
	ctx->callback.after_sampling = NULL;
	damon_sysfs_schemes_region_idx = 0;
	return 0;
}
