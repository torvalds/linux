// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/psi.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/string_choices.h>

#define CREATE_TRACE_POINTS
#include <trace/events/damon.h>

#ifdef CONFIG_DAMON_KUNIT_TEST
#undef DAMON_MIN_REGION
#define DAMON_MIN_REGION 1
#endif

static DEFINE_MUTEX(damon_lock);
static int nr_running_ctxs;
static bool running_exclusive_ctxs;

static DEFINE_MUTEX(damon_ops_lock);
static struct damon_operations damon_registered_ops[NR_DAMON_OPS];

static struct kmem_cache *damon_region_cache __ro_after_init;

/* Should be called under damon_ops_lock with id smaller than NR_DAMON_OPS */
static bool __damon_is_registered_ops(enum damon_ops_id id)
{
	struct damon_operations empty_ops = {};

	if (!memcmp(&empty_ops, &damon_registered_ops[id], sizeof(empty_ops)))
		return false;
	return true;
}

/**
 * damon_is_registered_ops() - Check if a given damon_operations is registered.
 * @id:	Id of the damon_operations to check if registered.
 *
 * Return: true if the ops is set, false otherwise.
 */
bool damon_is_registered_ops(enum damon_ops_id id)
{
	bool registered;

	if (id >= NR_DAMON_OPS)
		return false;
	mutex_lock(&damon_ops_lock);
	registered = __damon_is_registered_ops(id);
	mutex_unlock(&damon_ops_lock);
	return registered;
}

/**
 * damon_register_ops() - Register a monitoring operations set to DAMON.
 * @ops:	monitoring operations set to register.
 *
 * This function registers a monitoring operations set of valid &struct
 * damon_operations->id so that others can find and use them later.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_register_ops(struct damon_operations *ops)
{
	int err = 0;

	if (ops->id >= NR_DAMON_OPS)
		return -EINVAL;
	mutex_lock(&damon_ops_lock);
	/* Fail for already registered ops */
	if (__damon_is_registered_ops(ops->id)) {
		err = -EINVAL;
		goto out;
	}
	damon_registered_ops[ops->id] = *ops;
out:
	mutex_unlock(&damon_ops_lock);
	return err;
}

/**
 * damon_select_ops() - Select a monitoring operations to use with the context.
 * @ctx:	monitoring context to use the operations.
 * @id:		id of the registered monitoring operations to select.
 *
 * This function finds registered monitoring operations set of @id and make
 * @ctx to use it.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_select_ops(struct damon_ctx *ctx, enum damon_ops_id id)
{
	int err = 0;

	if (id >= NR_DAMON_OPS)
		return -EINVAL;

	mutex_lock(&damon_ops_lock);
	if (!__damon_is_registered_ops(id))
		err = -EINVAL;
	else
		ctx->ops = damon_registered_ops[id];
	mutex_unlock(&damon_ops_lock);
	return err;
}

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_region *damon_new_region(unsigned long start, unsigned long end)
{
	struct damon_region *region;

	region = kmem_cache_alloc(damon_region_cache, GFP_KERNEL);
	if (!region)
		return NULL;

	region->ar.start = start;
	region->ar.end = end;
	region->nr_accesses = 0;
	region->nr_accesses_bp = 0;
	INIT_LIST_HEAD(&region->list);

	region->age = 0;
	region->last_nr_accesses = 0;

	return region;
}

void damon_add_region(struct damon_region *r, struct damon_target *t)
{
	list_add_tail(&r->list, &t->regions_list);
	t->nr_regions++;
}

static void damon_del_region(struct damon_region *r, struct damon_target *t)
{
	list_del(&r->list);
	t->nr_regions--;
}

static void damon_free_region(struct damon_region *r)
{
	kmem_cache_free(damon_region_cache, r);
}

void damon_destroy_region(struct damon_region *r, struct damon_target *t)
{
	damon_del_region(r, t);
	damon_free_region(r);
}

/*
 * Check whether a region is intersecting an address range
 *
 * Returns true if it is.
 */
static bool damon_intersect(struct damon_region *r,
		struct damon_addr_range *re)
{
	return !(r->ar.end <= re->start || re->end <= r->ar.start);
}

/*
 * Fill holes in regions with new regions.
 */
static int damon_fill_regions_holes(struct damon_region *first,
		struct damon_region *last, struct damon_target *t)
{
	struct damon_region *r = first;

	damon_for_each_region_from(r, t) {
		struct damon_region *next, *newr;

		if (r == last)
			break;
		next = damon_next_region(r);
		if (r->ar.end != next->ar.start) {
			newr = damon_new_region(r->ar.end, next->ar.start);
			if (!newr)
				return -ENOMEM;
			damon_insert_region(newr, r, next, t);
		}
	}
	return 0;
}

/*
 * damon_set_regions() - Set regions of a target for given address ranges.
 * @t:		the given target.
 * @ranges:	array of new monitoring target ranges.
 * @nr_ranges:	length of @ranges.
 *
 * This function adds new regions to, or modify existing regions of a
 * monitoring target to fit in specific ranges.
 *
 * Return: 0 if success, or negative error code otherwise.
 */
int damon_set_regions(struct damon_target *t, struct damon_addr_range *ranges,
		unsigned int nr_ranges)
{
	struct damon_region *r, *next;
	unsigned int i;
	int err;

	/* Remove regions which are not in the new ranges */
	damon_for_each_region_safe(r, next, t) {
		for (i = 0; i < nr_ranges; i++) {
			if (damon_intersect(r, &ranges[i]))
				break;
		}
		if (i == nr_ranges)
			damon_destroy_region(r, t);
	}

	r = damon_first_region(t);
	/* Add new regions or resize existing regions to fit in the ranges */
	for (i = 0; i < nr_ranges; i++) {
		struct damon_region *first = NULL, *last, *newr;
		struct damon_addr_range *range;

		range = &ranges[i];
		/* Get the first/last regions intersecting with the range */
		damon_for_each_region_from(r, t) {
			if (damon_intersect(r, range)) {
				if (!first)
					first = r;
				last = r;
			}
			if (r->ar.start >= range->end)
				break;
		}
		if (!first) {
			/* no region intersects with this range */
			newr = damon_new_region(
					ALIGN_DOWN(range->start,
						DAMON_MIN_REGION),
					ALIGN(range->end, DAMON_MIN_REGION));
			if (!newr)
				return -ENOMEM;
			damon_insert_region(newr, damon_prev_region(r), r, t);
		} else {
			/* resize intersecting regions to fit in this range */
			first->ar.start = ALIGN_DOWN(range->start,
					DAMON_MIN_REGION);
			last->ar.end = ALIGN(range->end, DAMON_MIN_REGION);

			/* fill possible holes in the range */
			err = damon_fill_regions_holes(first, last, t);
			if (err)
				return err;
		}
	}
	return 0;
}

struct damos_filter *damos_new_filter(enum damos_filter_type type,
		bool matching, bool allow)
{
	struct damos_filter *filter;

	filter = kmalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return NULL;
	filter->type = type;
	filter->matching = matching;
	filter->allow = allow;
	INIT_LIST_HEAD(&filter->list);
	return filter;
}

void damos_add_filter(struct damos *s, struct damos_filter *f)
{
	list_add_tail(&f->list, &s->filters);
}

static void damos_del_filter(struct damos_filter *f)
{
	list_del(&f->list);
}

static void damos_free_filter(struct damos_filter *f)
{
	kfree(f);
}

void damos_destroy_filter(struct damos_filter *f)
{
	damos_del_filter(f);
	damos_free_filter(f);
}

struct damos_quota_goal *damos_new_quota_goal(
		enum damos_quota_goal_metric metric,
		unsigned long target_value)
{
	struct damos_quota_goal *goal;

	goal = kmalloc(sizeof(*goal), GFP_KERNEL);
	if (!goal)
		return NULL;
	goal->metric = metric;
	goal->target_value = target_value;
	INIT_LIST_HEAD(&goal->list);
	return goal;
}

void damos_add_quota_goal(struct damos_quota *q, struct damos_quota_goal *g)
{
	list_add_tail(&g->list, &q->goals);
}

static void damos_del_quota_goal(struct damos_quota_goal *g)
{
	list_del(&g->list);
}

static void damos_free_quota_goal(struct damos_quota_goal *g)
{
	kfree(g);
}

void damos_destroy_quota_goal(struct damos_quota_goal *g)
{
	damos_del_quota_goal(g);
	damos_free_quota_goal(g);
}

/* initialize fields of @quota that normally API users wouldn't set */
static struct damos_quota *damos_quota_init(struct damos_quota *quota)
{
	quota->esz = 0;
	quota->total_charged_sz = 0;
	quota->total_charged_ns = 0;
	quota->charged_sz = 0;
	quota->charged_from = 0;
	quota->charge_target_from = NULL;
	quota->charge_addr_from = 0;
	quota->esz_bp = 0;
	return quota;
}

struct damos *damon_new_scheme(struct damos_access_pattern *pattern,
			enum damos_action action,
			unsigned long apply_interval_us,
			struct damos_quota *quota,
			struct damos_watermarks *wmarks,
			int target_nid)
{
	struct damos *scheme;

	scheme = kmalloc(sizeof(*scheme), GFP_KERNEL);
	if (!scheme)
		return NULL;
	scheme->pattern = *pattern;
	scheme->action = action;
	scheme->apply_interval_us = apply_interval_us;
	/*
	 * next_apply_sis will be set when kdamond starts.  While kdamond is
	 * running, it will also updated when it is added to the DAMON context,
	 * or damon_attrs are updated.
	 */
	scheme->next_apply_sis = 0;
	INIT_LIST_HEAD(&scheme->filters);
	scheme->stat = (struct damos_stat){};
	INIT_LIST_HEAD(&scheme->list);

	scheme->quota = *(damos_quota_init(quota));
	/* quota.goals should be separately set by caller */
	INIT_LIST_HEAD(&scheme->quota.goals);

	scheme->wmarks = *wmarks;
	scheme->wmarks.activated = true;

	scheme->target_nid = target_nid;

	return scheme;
}

static void damos_set_next_apply_sis(struct damos *s, struct damon_ctx *ctx)
{
	unsigned long sample_interval = ctx->attrs.sample_interval ?
		ctx->attrs.sample_interval : 1;
	unsigned long apply_interval = s->apply_interval_us ?
		s->apply_interval_us : ctx->attrs.aggr_interval;

	s->next_apply_sis = ctx->passed_sample_intervals +
		apply_interval / sample_interval;
}

void damon_add_scheme(struct damon_ctx *ctx, struct damos *s)
{
	list_add_tail(&s->list, &ctx->schemes);
	damos_set_next_apply_sis(s, ctx);
}

static void damon_del_scheme(struct damos *s)
{
	list_del(&s->list);
}

static void damon_free_scheme(struct damos *s)
{
	kfree(s);
}

void damon_destroy_scheme(struct damos *s)
{
	struct damos_quota_goal *g, *g_next;
	struct damos_filter *f, *next;

	damos_for_each_quota_goal_safe(g, g_next, &s->quota)
		damos_destroy_quota_goal(g);

	damos_for_each_filter_safe(f, next, s)
		damos_destroy_filter(f);
	damon_del_scheme(s);
	damon_free_scheme(s);
}

/*
 * Construct a damon_target struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_target *damon_new_target(void)
{
	struct damon_target *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	t->pid = NULL;
	t->nr_regions = 0;
	INIT_LIST_HEAD(&t->regions_list);
	INIT_LIST_HEAD(&t->list);

	return t;
}

void damon_add_target(struct damon_ctx *ctx, struct damon_target *t)
{
	list_add_tail(&t->list, &ctx->adaptive_targets);
}

bool damon_targets_empty(struct damon_ctx *ctx)
{
	return list_empty(&ctx->adaptive_targets);
}

static void damon_del_target(struct damon_target *t)
{
	list_del(&t->list);
}

void damon_free_target(struct damon_target *t)
{
	struct damon_region *r, *next;

	damon_for_each_region_safe(r, next, t)
		damon_free_region(r);
	kfree(t);
}

void damon_destroy_target(struct damon_target *t)
{
	damon_del_target(t);
	damon_free_target(t);
}

unsigned int damon_nr_regions(struct damon_target *t)
{
	return t->nr_regions;
}

struct damon_ctx *damon_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	init_completion(&ctx->kdamond_started);

	ctx->attrs.sample_interval = 5 * 1000;
	ctx->attrs.aggr_interval = 100 * 1000;
	ctx->attrs.ops_update_interval = 60 * 1000 * 1000;

	ctx->passed_sample_intervals = 0;
	/* These will be set from kdamond_init_intervals_sis() */
	ctx->next_aggregation_sis = 0;
	ctx->next_ops_update_sis = 0;

	mutex_init(&ctx->kdamond_lock);
	mutex_init(&ctx->call_control_lock);
	mutex_init(&ctx->walk_control_lock);

	ctx->attrs.min_nr_regions = 10;
	ctx->attrs.max_nr_regions = 1000;

	INIT_LIST_HEAD(&ctx->adaptive_targets);
	INIT_LIST_HEAD(&ctx->schemes);

	return ctx;
}

static void damon_destroy_targets(struct damon_ctx *ctx)
{
	struct damon_target *t, *next_t;

	if (ctx->ops.cleanup) {
		ctx->ops.cleanup(ctx);
		return;
	}

	damon_for_each_target_safe(t, next_t, ctx)
		damon_destroy_target(t);
}

void damon_destroy_ctx(struct damon_ctx *ctx)
{
	struct damos *s, *next_s;

	damon_destroy_targets(ctx);

	damon_for_each_scheme_safe(s, next_s, ctx)
		damon_destroy_scheme(s);

	kfree(ctx);
}

static unsigned int damon_age_for_new_attrs(unsigned int age,
		struct damon_attrs *old_attrs, struct damon_attrs *new_attrs)
{
	return age * old_attrs->aggr_interval / new_attrs->aggr_interval;
}

/* convert access ratio in bp (per 10,000) to nr_accesses */
static unsigned int damon_accesses_bp_to_nr_accesses(
		unsigned int accesses_bp, struct damon_attrs *attrs)
{
	return accesses_bp * damon_max_nr_accesses(attrs) / 10000;
}

/*
 * Convert nr_accesses to access ratio in bp (per 10,000).
 *
 * Callers should ensure attrs.aggr_interval is not zero, like
 * damon_update_monitoring_results() does .  Otherwise, divide-by-zero would
 * happen.
 */
static unsigned int damon_nr_accesses_to_accesses_bp(
		unsigned int nr_accesses, struct damon_attrs *attrs)
{
	return nr_accesses * 10000 / damon_max_nr_accesses(attrs);
}

static unsigned int damon_nr_accesses_for_new_attrs(unsigned int nr_accesses,
		struct damon_attrs *old_attrs, struct damon_attrs *new_attrs)
{
	return damon_accesses_bp_to_nr_accesses(
			damon_nr_accesses_to_accesses_bp(
				nr_accesses, old_attrs),
			new_attrs);
}

static void damon_update_monitoring_result(struct damon_region *r,
		struct damon_attrs *old_attrs, struct damon_attrs *new_attrs)
{
	r->nr_accesses = damon_nr_accesses_for_new_attrs(r->nr_accesses,
			old_attrs, new_attrs);
	r->nr_accesses_bp = r->nr_accesses * 10000;
	r->age = damon_age_for_new_attrs(r->age, old_attrs, new_attrs);
}

/*
 * region->nr_accesses is the number of sampling intervals in the last
 * aggregation interval that access to the region has found, and region->age is
 * the number of aggregation intervals that its access pattern has maintained.
 * For the reason, the real meaning of the two fields depend on current
 * sampling interval and aggregation interval.  This function updates
 * ->nr_accesses and ->age of given damon_ctx's regions for new damon_attrs.
 */
static void damon_update_monitoring_results(struct damon_ctx *ctx,
		struct damon_attrs *new_attrs)
{
	struct damon_attrs *old_attrs = &ctx->attrs;
	struct damon_target *t;
	struct damon_region *r;

	/* if any interval is zero, simply forgive conversion */
	if (!old_attrs->sample_interval || !old_attrs->aggr_interval ||
			!new_attrs->sample_interval ||
			!new_attrs->aggr_interval)
		return;

	damon_for_each_target(t, ctx)
		damon_for_each_region(r, t)
			damon_update_monitoring_result(
					r, old_attrs, new_attrs);
}

/**
 * damon_set_attrs() - Set attributes for the monitoring.
 * @ctx:		monitoring context
 * @attrs:		monitoring attributes
 *
 * This function should be called while the kdamond is not running, or an
 * access check results aggregation is not ongoing (e.g., from
 * &struct damon_callback->after_aggregation or
 * &struct damon_callback->after_wmarks_check callbacks).
 *
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, struct damon_attrs *attrs)
{
	unsigned long sample_interval = attrs->sample_interval ?
		attrs->sample_interval : 1;
	struct damos *s;

	if (attrs->min_nr_regions < 3)
		return -EINVAL;
	if (attrs->min_nr_regions > attrs->max_nr_regions)
		return -EINVAL;
	if (attrs->sample_interval > attrs->aggr_interval)
		return -EINVAL;

	ctx->next_aggregation_sis = ctx->passed_sample_intervals +
		attrs->aggr_interval / sample_interval;
	ctx->next_ops_update_sis = ctx->passed_sample_intervals +
		attrs->ops_update_interval / sample_interval;

	damon_update_monitoring_results(ctx, attrs);
	ctx->attrs = *attrs;

	damon_for_each_scheme(s, ctx)
		damos_set_next_apply_sis(s, ctx);

	return 0;
}

/**
 * damon_set_schemes() - Set data access monitoring based operation schemes.
 * @ctx:	monitoring context
 * @schemes:	array of the schemes
 * @nr_schemes:	number of entries in @schemes
 *
 * This function should not be called while the kdamond of the context is
 * running.
 */
void damon_set_schemes(struct damon_ctx *ctx, struct damos **schemes,
			ssize_t nr_schemes)
{
	struct damos *s, *next;
	ssize_t i;

	damon_for_each_scheme_safe(s, next, ctx)
		damon_destroy_scheme(s);
	for (i = 0; i < nr_schemes; i++)
		damon_add_scheme(ctx, schemes[i]);
}

static struct damos_quota_goal *damos_nth_quota_goal(
		int n, struct damos_quota *q)
{
	struct damos_quota_goal *goal;
	int i = 0;

	damos_for_each_quota_goal(goal, q) {
		if (i++ == n)
			return goal;
	}
	return NULL;
}

static void damos_commit_quota_goal(
		struct damos_quota_goal *dst, struct damos_quota_goal *src)
{
	dst->metric = src->metric;
	dst->target_value = src->target_value;
	if (dst->metric == DAMOS_QUOTA_USER_INPUT)
		dst->current_value = src->current_value;
	/* keep last_psi_total as is, since it will be updated in next cycle */
}

/**
 * damos_commit_quota_goals() - Commit DAMOS quota goals to another quota.
 * @dst:	The commit destination DAMOS quota.
 * @src:	The commit source DAMOS quota.
 *
 * Copies user-specified parameters for quota goals from @src to @dst.  Users
 * should use this function for quota goals-level parameters update of running
 * DAMON contexts, instead of manual in-place updates.
 *
 * This function should be called from parameters-update safe context, like
 * DAMON callbacks.
 */
int damos_commit_quota_goals(struct damos_quota *dst, struct damos_quota *src)
{
	struct damos_quota_goal *dst_goal, *next, *src_goal, *new_goal;
	int i = 0, j = 0;

	damos_for_each_quota_goal_safe(dst_goal, next, dst) {
		src_goal = damos_nth_quota_goal(i++, src);
		if (src_goal)
			damos_commit_quota_goal(dst_goal, src_goal);
		else
			damos_destroy_quota_goal(dst_goal);
	}
	damos_for_each_quota_goal_safe(src_goal, next, src) {
		if (j++ < i)
			continue;
		new_goal = damos_new_quota_goal(
				src_goal->metric, src_goal->target_value);
		if (!new_goal)
			return -ENOMEM;
		damos_add_quota_goal(dst, new_goal);
	}
	return 0;
}

static int damos_commit_quota(struct damos_quota *dst, struct damos_quota *src)
{
	int err;

	dst->reset_interval = src->reset_interval;
	dst->ms = src->ms;
	dst->sz = src->sz;
	err = damos_commit_quota_goals(dst, src);
	if (err)
		return err;
	dst->weight_sz = src->weight_sz;
	dst->weight_nr_accesses = src->weight_nr_accesses;
	dst->weight_age = src->weight_age;
	return 0;
}

static struct damos_filter *damos_nth_filter(int n, struct damos *s)
{
	struct damos_filter *filter;
	int i = 0;

	damos_for_each_filter(filter, s) {
		if (i++ == n)
			return filter;
	}
	return NULL;
}

static void damos_commit_filter_arg(
		struct damos_filter *dst, struct damos_filter *src)
{
	switch (dst->type) {
	case DAMOS_FILTER_TYPE_MEMCG:
		dst->memcg_id = src->memcg_id;
		break;
	case DAMOS_FILTER_TYPE_ADDR:
		dst->addr_range = src->addr_range;
		break;
	case DAMOS_FILTER_TYPE_TARGET:
		dst->target_idx = src->target_idx;
		break;
	default:
		break;
	}
}

static void damos_commit_filter(
		struct damos_filter *dst, struct damos_filter *src)
{
	dst->type = src->type;
	dst->matching = src->matching;
	damos_commit_filter_arg(dst, src);
}

static int damos_commit_filters(struct damos *dst, struct damos *src)
{
	struct damos_filter *dst_filter, *next, *src_filter, *new_filter;
	int i = 0, j = 0;

	damos_for_each_filter_safe(dst_filter, next, dst) {
		src_filter = damos_nth_filter(i++, src);
		if (src_filter)
			damos_commit_filter(dst_filter, src_filter);
		else
			damos_destroy_filter(dst_filter);
	}

	damos_for_each_filter_safe(src_filter, next, src) {
		if (j++ < i)
			continue;

		new_filter = damos_new_filter(
				src_filter->type, src_filter->matching,
				src_filter->allow);
		if (!new_filter)
			return -ENOMEM;
		damos_commit_filter_arg(new_filter, src_filter);
		damos_add_filter(dst, new_filter);
	}
	return 0;
}

static struct damos *damon_nth_scheme(int n, struct damon_ctx *ctx)
{
	struct damos *s;
	int i = 0;

	damon_for_each_scheme(s, ctx) {
		if (i++ == n)
			return s;
	}
	return NULL;
}

static int damos_commit(struct damos *dst, struct damos *src)
{
	int err;

	dst->pattern = src->pattern;
	dst->action = src->action;
	dst->apply_interval_us = src->apply_interval_us;

	err = damos_commit_quota(&dst->quota, &src->quota);
	if (err)
		return err;

	dst->wmarks = src->wmarks;

	err = damos_commit_filters(dst, src);
	return err;
}

static int damon_commit_schemes(struct damon_ctx *dst, struct damon_ctx *src)
{
	struct damos *dst_scheme, *next, *src_scheme, *new_scheme;
	int i = 0, j = 0, err;

	damon_for_each_scheme_safe(dst_scheme, next, dst) {
		src_scheme = damon_nth_scheme(i++, src);
		if (src_scheme) {
			err = damos_commit(dst_scheme, src_scheme);
			if (err)
				return err;
		} else {
			damon_destroy_scheme(dst_scheme);
		}
	}

	damon_for_each_scheme_safe(src_scheme, next, src) {
		if (j++ < i)
			continue;
		new_scheme = damon_new_scheme(&src_scheme->pattern,
				src_scheme->action,
				src_scheme->apply_interval_us,
				&src_scheme->quota, &src_scheme->wmarks,
				NUMA_NO_NODE);
		if (!new_scheme)
			return -ENOMEM;
		err = damos_commit(new_scheme, src_scheme);
		if (err) {
			damon_destroy_scheme(new_scheme);
			return err;
		}
		damon_add_scheme(dst, new_scheme);
	}
	return 0;
}

static struct damon_target *damon_nth_target(int n, struct damon_ctx *ctx)
{
	struct damon_target *t;
	int i = 0;

	damon_for_each_target(t, ctx) {
		if (i++ == n)
			return t;
	}
	return NULL;
}

/*
 * The caller should ensure the regions of @src are
 * 1. valid (end >= src) and
 * 2. sorted by starting address.
 *
 * If @src has no region, @dst keeps current regions.
 */
static int damon_commit_target_regions(
		struct damon_target *dst, struct damon_target *src)
{
	struct damon_region *src_region;
	struct damon_addr_range *ranges;
	int i = 0, err;

	damon_for_each_region(src_region, src)
		i++;
	if (!i)
		return 0;

	ranges = kmalloc_array(i, sizeof(*ranges), GFP_KERNEL | __GFP_NOWARN);
	if (!ranges)
		return -ENOMEM;
	i = 0;
	damon_for_each_region(src_region, src)
		ranges[i++] = src_region->ar;
	err = damon_set_regions(dst, ranges, i);
	kfree(ranges);
	return err;
}

static int damon_commit_target(
		struct damon_target *dst, bool dst_has_pid,
		struct damon_target *src, bool src_has_pid)
{
	int err;

	err = damon_commit_target_regions(dst, src);
	if (err)
		return err;
	if (dst_has_pid)
		put_pid(dst->pid);
	if (src_has_pid)
		get_pid(src->pid);
	dst->pid = src->pid;
	return 0;
}

static int damon_commit_targets(
		struct damon_ctx *dst, struct damon_ctx *src)
{
	struct damon_target *dst_target, *next, *src_target, *new_target;
	int i = 0, j = 0, err;

	damon_for_each_target_safe(dst_target, next, dst) {
		src_target = damon_nth_target(i++, src);
		if (src_target) {
			err = damon_commit_target(
					dst_target, damon_target_has_pid(dst),
					src_target, damon_target_has_pid(src));
			if (err)
				return err;
		} else {
			if (damon_target_has_pid(dst))
				put_pid(dst_target->pid);
			damon_destroy_target(dst_target);
		}
	}

	damon_for_each_target_safe(src_target, next, src) {
		if (j++ < i)
			continue;
		new_target = damon_new_target();
		if (!new_target)
			return -ENOMEM;
		err = damon_commit_target(new_target, false,
				src_target, damon_target_has_pid(src));
		if (err) {
			damon_destroy_target(new_target);
			return err;
		}
		damon_add_target(dst, new_target);
	}
	return 0;
}

/**
 * damon_commit_ctx() - Commit parameters of a DAMON context to another.
 * @dst:	The commit destination DAMON context.
 * @src:	The commit source DAMON context.
 *
 * This function copies user-specified parameters from @src to @dst and update
 * the internal status and results accordingly.  Users should use this function
 * for context-level parameters update of running context, instead of manual
 * in-place updates.
 *
 * This function should be called from parameters-update safe context, like
 * DAMON callbacks.
 */
int damon_commit_ctx(struct damon_ctx *dst, struct damon_ctx *src)
{
	int err;

	err = damon_commit_schemes(dst, src);
	if (err)
		return err;
	err = damon_commit_targets(dst, src);
	if (err)
		return err;
	/*
	 * schemes and targets should be updated first, since
	 * 1. damon_set_attrs() updates monitoring results of targets and
	 * next_apply_sis of schemes, and
	 * 2. ops update should be done after pid handling is done (target
	 *    committing require putting pids).
	 */
	err = damon_set_attrs(dst, &src->attrs);
	if (err)
		return err;
	dst->ops = src->ops;

	return 0;
}

/**
 * damon_nr_running_ctxs() - Return number of currently running contexts.
 */
int damon_nr_running_ctxs(void)
{
	int nr_ctxs;

	mutex_lock(&damon_lock);
	nr_ctxs = nr_running_ctxs;
	mutex_unlock(&damon_lock);

	return nr_ctxs;
}

/* Returns the size upper limit for each monitoring region */
static unsigned long damon_region_sz_limit(struct damon_ctx *ctx)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long sz = 0;

	damon_for_each_target(t, ctx) {
		damon_for_each_region(r, t)
			sz += damon_sz_region(r);
	}

	if (ctx->attrs.min_nr_regions)
		sz /= ctx->attrs.min_nr_regions;
	if (sz < DAMON_MIN_REGION)
		sz = DAMON_MIN_REGION;

	return sz;
}

static int kdamond_fn(void *data);

/*
 * __damon_start() - Starts monitoring with given context.
 * @ctx:	monitoring context
 *
 * This function should be called while damon_lock is hold.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_start(struct damon_ctx *ctx)
{
	int err = -EBUSY;

	mutex_lock(&ctx->kdamond_lock);
	if (!ctx->kdamond) {
		err = 0;
		reinit_completion(&ctx->kdamond_started);
		ctx->kdamond = kthread_run(kdamond_fn, ctx, "kdamond.%d",
				nr_running_ctxs);
		if (IS_ERR(ctx->kdamond)) {
			err = PTR_ERR(ctx->kdamond);
			ctx->kdamond = NULL;
		} else {
			wait_for_completion(&ctx->kdamond_started);
		}
	}
	mutex_unlock(&ctx->kdamond_lock);

	return err;
}

/**
 * damon_start() - Starts the monitorings for a given group of contexts.
 * @ctxs:	an array of the pointers for contexts to start monitoring
 * @nr_ctxs:	size of @ctxs
 * @exclusive:	exclusiveness of this contexts group
 *
 * This function starts a group of monitoring threads for a group of monitoring
 * contexts.  One thread per each context is created and run in parallel.  The
 * caller should handle synchronization between the threads by itself.  If
 * @exclusive is true and a group of threads that created by other
 * 'damon_start()' call is currently running, this function does nothing but
 * returns -EBUSY.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_start(struct damon_ctx **ctxs, int nr_ctxs, bool exclusive)
{
	int i;
	int err = 0;

	mutex_lock(&damon_lock);
	if ((exclusive && nr_running_ctxs) ||
			(!exclusive && running_exclusive_ctxs)) {
		mutex_unlock(&damon_lock);
		return -EBUSY;
	}

	for (i = 0; i < nr_ctxs; i++) {
		err = __damon_start(ctxs[i]);
		if (err)
			break;
		nr_running_ctxs++;
	}
	if (exclusive && nr_running_ctxs)
		running_exclusive_ctxs = true;
	mutex_unlock(&damon_lock);

	return err;
}

/*
 * __damon_stop() - Stops monitoring of a given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_stop(struct damon_ctx *ctx)
{
	struct task_struct *tsk;

	mutex_lock(&ctx->kdamond_lock);
	tsk = ctx->kdamond;
	if (tsk) {
		get_task_struct(tsk);
		mutex_unlock(&ctx->kdamond_lock);
		kthread_stop_put(tsk);
		return 0;
	}
	mutex_unlock(&ctx->kdamond_lock);

	return -EPERM;
}

/**
 * damon_stop() - Stops the monitorings for a given group of contexts.
 * @ctxs:	an array of the pointers for contexts to stop monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_stop(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i, err = 0;

	for (i = 0; i < nr_ctxs; i++) {
		/* nr_running_ctxs is decremented in kdamond_fn */
		err = __damon_stop(ctxs[i]);
		if (err)
			break;
	}
	return err;
}

static bool damon_is_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);
	return running;
}

/**
 * damon_call() - Invoke a given function on DAMON worker thread (kdamond).
 * @ctx:	DAMON context to call the function for.
 * @control:	Control variable of the call request.
 *
 * Ask DAMON worker thread (kdamond) of @ctx to call a function with an
 * argument data that respectively passed via &damon_call_control->fn and
 * &damon_call_control->data of @control, and wait until the kdamond finishes
 * handling of the request.
 *
 * The kdamond executes the function with the argument in the main loop, just
 * after a sampling of the iteration is finished.  The function can hence
 * safely access the internal data of the &struct damon_ctx without additional
 * synchronization.  The return value of the function will be saved in
 * &damon_call_control->return_code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_call(struct damon_ctx *ctx, struct damon_call_control *control)
{
	init_completion(&control->completion);
	control->canceled = false;

	mutex_lock(&ctx->call_control_lock);
	if (ctx->call_control) {
		mutex_unlock(&ctx->call_control_lock);
		return -EBUSY;
	}
	ctx->call_control = control;
	mutex_unlock(&ctx->call_control_lock);
	if (!damon_is_running(ctx))
		return -EINVAL;
	wait_for_completion(&control->completion);
	if (control->canceled)
		return -ECANCELED;
	return 0;
}

/**
 * damos_walk() - Invoke a given functions while DAMOS walk regions.
 * @ctx:	DAMON context to call the functions for.
 * @control:	Control variable of the walk request.
 *
 * Ask DAMON worker thread (kdamond) of @ctx to call a function for each region
 * that the kdamond will apply DAMOS action to, and wait until the kdamond
 * finishes handling of the request.
 *
 * The kdamond executes the given function in the main loop, for each region
 * just after it applied any DAMOS actions of @ctx to it.  The invocation is
 * made only within one &damos->apply_interval_us since damos_walk()
 * invocation, for each scheme.  The given callback function can hence safely
 * access the internal data of &struct damon_ctx and &struct damon_region that
 * each of the scheme will apply the action for next interval, without
 * additional synchronizations against the kdamond.  If every scheme of @ctx
 * passed at least one &damos->apply_interval_us, kdamond marks the request as
 * completed so that damos_walk() can wakeup and return.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damos_walk(struct damon_ctx *ctx, struct damos_walk_control *control)
{
	init_completion(&control->completion);
	control->canceled = false;
	mutex_lock(&ctx->walk_control_lock);
	if (ctx->walk_control) {
		mutex_unlock(&ctx->walk_control_lock);
		return -EBUSY;
	}
	ctx->walk_control = control;
	mutex_unlock(&ctx->walk_control_lock);
	if (!damon_is_running(ctx))
		return -EINVAL;
	wait_for_completion(&control->completion);
	if (control->canceled)
		return -ECANCELED;
	return 0;
}

/*
 * Reset the aggregated monitoring results ('nr_accesses' of each region).
 */
static void kdamond_reset_aggregated(struct damon_ctx *c)
{
	struct damon_target *t;
	unsigned int ti = 0;	/* target's index */

	damon_for_each_target(t, c) {
		struct damon_region *r;

		damon_for_each_region(r, t) {
			trace_damon_aggregated(ti, r, damon_nr_regions(t));
			r->last_nr_accesses = r->nr_accesses;
			r->nr_accesses = 0;
		}
		ti++;
	}
}

static void damon_split_region_at(struct damon_target *t,
				  struct damon_region *r, unsigned long sz_r);

static bool __damos_valid_target(struct damon_region *r, struct damos *s)
{
	unsigned long sz;
	unsigned int nr_accesses = r->nr_accesses_bp / 10000;

	sz = damon_sz_region(r);
	return s->pattern.min_sz_region <= sz &&
		sz <= s->pattern.max_sz_region &&
		s->pattern.min_nr_accesses <= nr_accesses &&
		nr_accesses <= s->pattern.max_nr_accesses &&
		s->pattern.min_age_region <= r->age &&
		r->age <= s->pattern.max_age_region;
}

static bool damos_valid_target(struct damon_ctx *c, struct damon_target *t,
		struct damon_region *r, struct damos *s)
{
	bool ret = __damos_valid_target(r, s);

	if (!ret || !s->quota.esz || !c->ops.get_scheme_score)
		return ret;

	return c->ops.get_scheme_score(c, t, r, s) >= s->quota.min_score;
}

/*
 * damos_skip_charged_region() - Check if the given region or starting part of
 * it is already charged for the DAMOS quota.
 * @t:	The target of the region.
 * @rp:	The pointer to the region.
 * @s:	The scheme to be applied.
 *
 * If a quota of a scheme has exceeded in a quota charge window, the scheme's
 * action would applied to only a part of the target access pattern fulfilling
 * regions.  To avoid applying the scheme action to only already applied
 * regions, DAMON skips applying the scheme action to the regions that charged
 * in the previous charge window.
 *
 * This function checks if a given region should be skipped or not for the
 * reason.  If only the starting part of the region has previously charged,
 * this function splits the region into two so that the second one covers the
 * area that not charged in the previous charge widnow and saves the second
 * region in *rp and returns false, so that the caller can apply DAMON action
 * to the second one.
 *
 * Return: true if the region should be entirely skipped, false otherwise.
 */
static bool damos_skip_charged_region(struct damon_target *t,
		struct damon_region **rp, struct damos *s)
{
	struct damon_region *r = *rp;
	struct damos_quota *quota = &s->quota;
	unsigned long sz_to_skip;

	/* Skip previously charged regions */
	if (quota->charge_target_from) {
		if (t != quota->charge_target_from)
			return true;
		if (r == damon_last_region(t)) {
			quota->charge_target_from = NULL;
			quota->charge_addr_from = 0;
			return true;
		}
		if (quota->charge_addr_from &&
				r->ar.end <= quota->charge_addr_from)
			return true;

		if (quota->charge_addr_from && r->ar.start <
				quota->charge_addr_from) {
			sz_to_skip = ALIGN_DOWN(quota->charge_addr_from -
					r->ar.start, DAMON_MIN_REGION);
			if (!sz_to_skip) {
				if (damon_sz_region(r) <= DAMON_MIN_REGION)
					return true;
				sz_to_skip = DAMON_MIN_REGION;
			}
			damon_split_region_at(t, r, sz_to_skip);
			r = damon_next_region(r);
			*rp = r;
		}
		quota->charge_target_from = NULL;
		quota->charge_addr_from = 0;
	}
	return false;
}

static void damos_update_stat(struct damos *s,
		unsigned long sz_tried, unsigned long sz_applied,
		unsigned long sz_ops_filter_passed)
{
	s->stat.nr_tried++;
	s->stat.sz_tried += sz_tried;
	if (sz_applied)
		s->stat.nr_applied++;
	s->stat.sz_applied += sz_applied;
	s->stat.sz_ops_filter_passed += sz_ops_filter_passed;
}

static bool damos_filter_match(struct damon_ctx *ctx, struct damon_target *t,
		struct damon_region *r, struct damos_filter *filter)
{
	bool matched = false;
	struct damon_target *ti;
	int target_idx = 0;
	unsigned long start, end;

	switch (filter->type) {
	case DAMOS_FILTER_TYPE_TARGET:
		damon_for_each_target(ti, ctx) {
			if (ti == t)
				break;
			target_idx++;
		}
		matched = target_idx == filter->target_idx;
		break;
	case DAMOS_FILTER_TYPE_ADDR:
		start = ALIGN_DOWN(filter->addr_range.start, DAMON_MIN_REGION);
		end = ALIGN_DOWN(filter->addr_range.end, DAMON_MIN_REGION);

		/* inside the range */
		if (start <= r->ar.start && r->ar.end <= end) {
			matched = true;
			break;
		}
		/* outside of the range */
		if (r->ar.end <= start || end <= r->ar.start) {
			matched = false;
			break;
		}
		/* start before the range and overlap */
		if (r->ar.start < start) {
			damon_split_region_at(t, r, start - r->ar.start);
			matched = false;
			break;
		}
		/* start inside the range */
		damon_split_region_at(t, r, end - r->ar.start);
		matched = true;
		break;
	default:
		return false;
	}

	return matched == filter->matching;
}

static bool damos_filter_out(struct damon_ctx *ctx, struct damon_target *t,
		struct damon_region *r, struct damos *s)
{
	struct damos_filter *filter;

	s->core_filters_allowed = false;
	damos_for_each_filter(filter, s) {
		if (damos_filter_match(ctx, t, r, filter)) {
			if (filter->allow)
				s->core_filters_allowed = true;
			return !filter->allow;
		}
	}
	return false;
}

/*
 * damos_walk_call_walk() - Call &damos_walk_control->walk_fn.
 * @ctx:	The context of &damon_ctx->walk_control.
 * @t:		The monitoring target of @r that @s will be applied.
 * @r:		The region of @t that @s will be applied.
 * @s:		The scheme of @ctx that will be applied to @r.
 *
 * This function is called from kdamond whenever it asked the operation set to
 * apply a DAMOS scheme action to a region.  If a DAMOS walk request is
 * installed by damos_walk() and not yet uninstalled, invoke it.
 */
static void damos_walk_call_walk(struct damon_ctx *ctx, struct damon_target *t,
		struct damon_region *r, struct damos *s,
		unsigned long sz_filter_passed)
{
	struct damos_walk_control *control;

	mutex_lock(&ctx->walk_control_lock);
	control = ctx->walk_control;
	mutex_unlock(&ctx->walk_control_lock);
	if (!control)
		return;
	control->walk_fn(control->data, ctx, t, r, s, sz_filter_passed);
}

/*
 * damos_walk_complete() - Complete DAMOS walk request if all walks are done.
 * @ctx:	The context of &damon_ctx->walk_control.
 * @s:		A scheme of @ctx that all walks are now done.
 *
 * This function is called when kdamond finished applying the action of a DAMOS
 * scheme to all regions that eligible for the given &damos->apply_interval_us.
 * If every scheme of @ctx including @s now finished walking for at least one
 * &damos->apply_interval_us, this function makrs the handling of the given
 * DAMOS walk request is done, so that damos_walk() can wake up and return.
 */
static void damos_walk_complete(struct damon_ctx *ctx, struct damos *s)
{
	struct damos *siter;
	struct damos_walk_control *control;

	mutex_lock(&ctx->walk_control_lock);
	control = ctx->walk_control;
	mutex_unlock(&ctx->walk_control_lock);
	if (!control)
		return;

	s->walk_completed = true;
	/* if all schemes completed, signal completion to walker */
	damon_for_each_scheme(siter, ctx) {
		if (!siter->walk_completed)
			return;
	}
	complete(&control->completion);
	mutex_lock(&ctx->walk_control_lock);
	ctx->walk_control = NULL;
	mutex_unlock(&ctx->walk_control_lock);
}

/*
 * damos_walk_cancel() - Cancel the current DAMOS walk request.
 * @ctx:	The context of &damon_ctx->walk_control.
 *
 * This function is called when @ctx is deactivated by DAMOS watermarks, DAMOS
 * walk is requested but there is no DAMOS scheme to walk for, or the kdamond
 * is already out of the main loop and therefore gonna be terminated, and hence
 * cannot continue the walks.  This function therefore marks the walk request
 * as canceled, so that damos_walk() can wake up and return.
 */
static void damos_walk_cancel(struct damon_ctx *ctx)
{
	struct damos_walk_control *control;

	mutex_lock(&ctx->walk_control_lock);
	control = ctx->walk_control;
	mutex_unlock(&ctx->walk_control_lock);

	if (!control)
		return;
	control->canceled = true;
	complete(&control->completion);
	mutex_lock(&ctx->walk_control_lock);
	ctx->walk_control = NULL;
	mutex_unlock(&ctx->walk_control_lock);
}

static void damos_apply_scheme(struct damon_ctx *c, struct damon_target *t,
		struct damon_region *r, struct damos *s)
{
	struct damos_quota *quota = &s->quota;
	unsigned long sz = damon_sz_region(r);
	struct timespec64 begin, end;
	unsigned long sz_applied = 0;
	unsigned long sz_ops_filter_passed = 0;
	int err = 0;
	/*
	 * We plan to support multiple context per kdamond, as DAMON sysfs
	 * implies with 'nr_contexts' file.  Nevertheless, only single context
	 * per kdamond is supported for now.  So, we can simply use '0' context
	 * index here.
	 */
	unsigned int cidx = 0;
	struct damos *siter;		/* schemes iterator */
	unsigned int sidx = 0;
	struct damon_target *titer;	/* targets iterator */
	unsigned int tidx = 0;
	bool do_trace = false;

	/* get indices for trace_damos_before_apply() */
	if (trace_damos_before_apply_enabled()) {
		damon_for_each_scheme(siter, c) {
			if (siter == s)
				break;
			sidx++;
		}
		damon_for_each_target(titer, c) {
			if (titer == t)
				break;
			tidx++;
		}
		do_trace = true;
	}

	if (c->ops.apply_scheme) {
		if (quota->esz && quota->charged_sz + sz > quota->esz) {
			sz = ALIGN_DOWN(quota->esz - quota->charged_sz,
					DAMON_MIN_REGION);
			if (!sz)
				goto update_stat;
			damon_split_region_at(t, r, sz);
		}
		if (damos_filter_out(c, t, r, s))
			return;
		ktime_get_coarse_ts64(&begin);
		if (c->callback.before_damos_apply)
			err = c->callback.before_damos_apply(c, t, r, s);
		if (!err) {
			trace_damos_before_apply(cidx, sidx, tidx, r,
					damon_nr_regions(t), do_trace);
			sz_applied = c->ops.apply_scheme(c, t, r, s,
					&sz_ops_filter_passed);
		}
		damos_walk_call_walk(c, t, r, s, sz_ops_filter_passed);
		ktime_get_coarse_ts64(&end);
		quota->total_charged_ns += timespec64_to_ns(&end) -
			timespec64_to_ns(&begin);
		quota->charged_sz += sz;
		if (quota->esz && quota->charged_sz >= quota->esz) {
			quota->charge_target_from = t;
			quota->charge_addr_from = r->ar.end + 1;
		}
	}
	if (s->action != DAMOS_STAT)
		r->age = 0;

update_stat:
	damos_update_stat(s, sz, sz_applied, sz_ops_filter_passed);
}

static void damon_do_apply_schemes(struct damon_ctx *c,
				   struct damon_target *t,
				   struct damon_region *r)
{
	struct damos *s;

	damon_for_each_scheme(s, c) {
		struct damos_quota *quota = &s->quota;

		if (c->passed_sample_intervals < s->next_apply_sis)
			continue;

		if (!s->wmarks.activated)
			continue;

		/* Check the quota */
		if (quota->esz && quota->charged_sz >= quota->esz)
			continue;

		if (damos_skip_charged_region(t, &r, s))
			continue;

		if (!damos_valid_target(c, t, r, s))
			continue;

		damos_apply_scheme(c, t, r, s);
	}
}

/*
 * damon_feed_loop_next_input() - get next input to achieve a target score.
 * @last_input	The last input.
 * @score	Current score that made with @last_input.
 *
 * Calculate next input to achieve the target score, based on the last input
 * and current score.  Assuming the input and the score are positively
 * proportional, calculate how much compensation should be added to or
 * subtracted from the last input as a proportion of the last input.  Avoid
 * next input always being zero by setting it non-zero always.  In short form
 * (assuming support of float and signed calculations), the algorithm is as
 * below.
 *
 * next_input = max(last_input * ((goal - current) / goal + 1), 1)
 *
 * For simple implementation, we assume the target score is always 10,000.  The
 * caller should adjust @score for this.
 *
 * Returns next input that assumed to achieve the target score.
 */
static unsigned long damon_feed_loop_next_input(unsigned long last_input,
		unsigned long score)
{
	const unsigned long goal = 10000;
	/* Set minimum input as 10000 to avoid compensation be zero */
	const unsigned long min_input = 10000;
	unsigned long score_goal_diff, compensation;
	bool over_achieving = score > goal;

	if (score == goal)
		return last_input;
	if (score >= goal * 2)
		return min_input;

	if (over_achieving)
		score_goal_diff = score - goal;
	else
		score_goal_diff = goal - score;

	if (last_input < ULONG_MAX / score_goal_diff)
		compensation = last_input * score_goal_diff / goal;
	else
		compensation = last_input / goal * score_goal_diff;

	if (over_achieving)
		return max(last_input - compensation, min_input);
	if (last_input < ULONG_MAX - compensation)
		return last_input + compensation;
	return ULONG_MAX;
}

#ifdef CONFIG_PSI

static u64 damos_get_some_mem_psi_total(void)
{
	if (static_branch_likely(&psi_disabled))
		return 0;
	return div_u64(psi_system.total[PSI_AVGS][PSI_MEM * 2],
			NSEC_PER_USEC);
}

#else	/* CONFIG_PSI */

static inline u64 damos_get_some_mem_psi_total(void)
{
	return 0;
};

#endif	/* CONFIG_PSI */

static void damos_set_quota_goal_current_value(struct damos_quota_goal *goal)
{
	u64 now_psi_total;

	switch (goal->metric) {
	case DAMOS_QUOTA_USER_INPUT:
		/* User should already set goal->current_value */
		break;
	case DAMOS_QUOTA_SOME_MEM_PSI_US:
		now_psi_total = damos_get_some_mem_psi_total();
		goal->current_value = now_psi_total - goal->last_psi_total;
		goal->last_psi_total = now_psi_total;
		break;
	default:
		break;
	}
}

/* Return the highest score since it makes schemes least aggressive */
static unsigned long damos_quota_score(struct damos_quota *quota)
{
	struct damos_quota_goal *goal;
	unsigned long highest_score = 0;

	damos_for_each_quota_goal(goal, quota) {
		damos_set_quota_goal_current_value(goal);
		highest_score = max(highest_score,
				goal->current_value * 10000 /
				goal->target_value);
	}

	return highest_score;
}

/*
 * Called only if quota->ms, or quota->sz are set, or quota->goals is not empty
 */
static void damos_set_effective_quota(struct damos_quota *quota)
{
	unsigned long throughput;
	unsigned long esz = ULONG_MAX;

	if (!quota->ms && list_empty(&quota->goals)) {
		quota->esz = quota->sz;
		return;
	}

	if (!list_empty(&quota->goals)) {
		unsigned long score = damos_quota_score(quota);

		quota->esz_bp = damon_feed_loop_next_input(
				max(quota->esz_bp, 10000UL),
				score);
		esz = quota->esz_bp / 10000;
	}

	if (quota->ms) {
		if (quota->total_charged_ns)
			throughput = quota->total_charged_sz * 1000000 /
				quota->total_charged_ns;
		else
			throughput = PAGE_SIZE * 1024;
		esz = min(throughput * quota->ms, esz);
	}

	if (quota->sz && quota->sz < esz)
		esz = quota->sz;

	quota->esz = esz;
}

static void damos_adjust_quota(struct damon_ctx *c, struct damos *s)
{
	struct damos_quota *quota = &s->quota;
	struct damon_target *t;
	struct damon_region *r;
	unsigned long cumulated_sz;
	unsigned int score, max_score = 0;

	if (!quota->ms && !quota->sz && list_empty(&quota->goals))
		return;

	/* New charge window starts */
	if (time_after_eq(jiffies, quota->charged_from +
				msecs_to_jiffies(quota->reset_interval))) {
		if (quota->esz && quota->charged_sz >= quota->esz)
			s->stat.qt_exceeds++;
		quota->total_charged_sz += quota->charged_sz;
		quota->charged_from = jiffies;
		quota->charged_sz = 0;
		damos_set_effective_quota(quota);
	}

	if (!c->ops.get_scheme_score)
		return;

	/* Fill up the score histogram */
	memset(c->regions_score_histogram, 0,
			sizeof(*c->regions_score_histogram) *
			(DAMOS_MAX_SCORE + 1));
	damon_for_each_target(t, c) {
		damon_for_each_region(r, t) {
			if (!__damos_valid_target(r, s))
				continue;
			score = c->ops.get_scheme_score(c, t, r, s);
			c->regions_score_histogram[score] +=
				damon_sz_region(r);
			if (score > max_score)
				max_score = score;
		}
	}

	/* Set the min score limit */
	for (cumulated_sz = 0, score = max_score; ; score--) {
		cumulated_sz += c->regions_score_histogram[score];
		if (cumulated_sz >= quota->esz || !score)
			break;
	}
	quota->min_score = score;
}

static void kdamond_apply_schemes(struct damon_ctx *c)
{
	struct damon_target *t;
	struct damon_region *r, *next_r;
	struct damos *s;
	unsigned long sample_interval = c->attrs.sample_interval ?
		c->attrs.sample_interval : 1;
	bool has_schemes_to_apply = false;

	damon_for_each_scheme(s, c) {
		if (c->passed_sample_intervals < s->next_apply_sis)
			continue;

		if (!s->wmarks.activated)
			continue;

		has_schemes_to_apply = true;

		damos_adjust_quota(c, s);
	}

	if (!has_schemes_to_apply)
		return;

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next_r, t)
			damon_do_apply_schemes(c, t, r);
	}

	damon_for_each_scheme(s, c) {
		if (c->passed_sample_intervals < s->next_apply_sis)
			continue;
		damos_walk_complete(c, s);
		s->next_apply_sis = c->passed_sample_intervals +
			(s->apply_interval_us ? s->apply_interval_us :
			 c->attrs.aggr_interval) / sample_interval;
	}
}

/*
 * Merge two adjacent regions into one region
 */
static void damon_merge_two_regions(struct damon_target *t,
		struct damon_region *l, struct damon_region *r)
{
	unsigned long sz_l = damon_sz_region(l), sz_r = damon_sz_region(r);

	l->nr_accesses = (l->nr_accesses * sz_l + r->nr_accesses * sz_r) /
			(sz_l + sz_r);
	l->nr_accesses_bp = l->nr_accesses * 10000;
	l->age = (l->age * sz_l + r->age * sz_r) / (sz_l + sz_r);
	l->ar.end = r->ar.end;
	damon_destroy_region(r, t);
}

/*
 * Merge adjacent regions having similar access frequencies
 *
 * t		target affected by this merge operation
 * thres	'->nr_accesses' diff threshold for the merge
 * sz_limit	size upper limit of each region
 */
static void damon_merge_regions_of(struct damon_target *t, unsigned int thres,
				   unsigned long sz_limit)
{
	struct damon_region *r, *prev = NULL, *next;

	damon_for_each_region_safe(r, next, t) {
		if (abs(r->nr_accesses - r->last_nr_accesses) > thres)
			r->age = 0;
		else
			r->age++;

		if (prev && prev->ar.end == r->ar.start &&
		    abs(prev->nr_accesses - r->nr_accesses) <= thres &&
		    damon_sz_region(prev) + damon_sz_region(r) <= sz_limit)
			damon_merge_two_regions(t, prev, r);
		else
			prev = r;
	}
}

/*
 * Merge adjacent regions having similar access frequencies
 *
 * threshold	'->nr_accesses' diff threshold for the merge
 * sz_limit	size upper limit of each region
 *
 * This function merges monitoring target regions which are adjacent and their
 * access frequencies are similar.  This is for minimizing the monitoring
 * overhead under the dynamically changeable access pattern.  If a merge was
 * unnecessarily made, later 'kdamond_split_regions()' will revert it.
 *
 * The total number of regions could be higher than the user-defined limit,
 * max_nr_regions for some cases.  For example, the user can update
 * max_nr_regions to a number that lower than the current number of regions
 * while DAMON is running.  For such a case, repeat merging until the limit is
 * met while increasing @threshold up to possible maximum level.
 */
static void kdamond_merge_regions(struct damon_ctx *c, unsigned int threshold,
				  unsigned long sz_limit)
{
	struct damon_target *t;
	unsigned int nr_regions;
	unsigned int max_thres;

	max_thres = c->attrs.aggr_interval /
		(c->attrs.sample_interval ?  c->attrs.sample_interval : 1);
	do {
		nr_regions = 0;
		damon_for_each_target(t, c) {
			damon_merge_regions_of(t, threshold, sz_limit);
			nr_regions += damon_nr_regions(t);
		}
		threshold = max(1, threshold * 2);
	} while (nr_regions > c->attrs.max_nr_regions &&
			threshold / 2 < max_thres);
}

/*
 * Split a region in two
 *
 * r		the region to be split
 * sz_r		size of the first sub-region that will be made
 */
static void damon_split_region_at(struct damon_target *t,
				  struct damon_region *r, unsigned long sz_r)
{
	struct damon_region *new;

	new = damon_new_region(r->ar.start + sz_r, r->ar.end);
	if (!new)
		return;

	r->ar.end = new->ar.start;

	new->age = r->age;
	new->last_nr_accesses = r->last_nr_accesses;
	new->nr_accesses_bp = r->nr_accesses_bp;
	new->nr_accesses = r->nr_accesses;

	damon_insert_region(new, r, damon_next_region(r), t);
}

/* Split every region in the given target into 'nr_subs' regions */
static void damon_split_regions_of(struct damon_target *t, int nr_subs)
{
	struct damon_region *r, *next;
	unsigned long sz_region, sz_sub = 0;
	int i;

	damon_for_each_region_safe(r, next, t) {
		sz_region = damon_sz_region(r);

		for (i = 0; i < nr_subs - 1 &&
				sz_region > 2 * DAMON_MIN_REGION; i++) {
			/*
			 * Randomly select size of left sub-region to be at
			 * least 10 percent and at most 90% of original region
			 */
			sz_sub = ALIGN_DOWN(damon_rand(1, 10) *
					sz_region / 10, DAMON_MIN_REGION);
			/* Do not allow blank region */
			if (sz_sub == 0 || sz_sub >= sz_region)
				continue;

			damon_split_region_at(t, r, sz_sub);
			sz_region = sz_sub;
		}
	}
}

/*
 * Split every target region into randomly-sized small regions
 *
 * This function splits every target region into random-sized small regions if
 * current total number of the regions is equal or smaller than half of the
 * user-specified maximum number of regions.  This is for maximizing the
 * monitoring accuracy under the dynamically changeable access patterns.  If a
 * split was unnecessarily made, later 'kdamond_merge_regions()' will revert
 * it.
 */
static void kdamond_split_regions(struct damon_ctx *ctx)
{
	struct damon_target *t;
	unsigned int nr_regions = 0;
	static unsigned int last_nr_regions;
	int nr_subregions = 2;

	damon_for_each_target(t, ctx)
		nr_regions += damon_nr_regions(t);

	if (nr_regions > ctx->attrs.max_nr_regions / 2)
		return;

	/* Maybe the middle of the region has different access frequency */
	if (last_nr_regions == nr_regions &&
			nr_regions < ctx->attrs.max_nr_regions / 3)
		nr_subregions = 3;

	damon_for_each_target(t, ctx)
		damon_split_regions_of(t, nr_subregions);

	last_nr_regions = nr_regions;
}

/*
 * Check whether current monitoring should be stopped
 *
 * The monitoring is stopped when either the user requested to stop, or all
 * monitoring targets are invalid.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(struct damon_ctx *ctx)
{
	struct damon_target *t;

	if (kthread_should_stop())
		return true;

	if (!ctx->ops.target_valid)
		return false;

	damon_for_each_target(t, ctx) {
		if (ctx->ops.target_valid(t))
			return false;
	}

	return true;
}

static int damos_get_wmark_metric_value(enum damos_wmark_metric metric,
					unsigned long *metric_value)
{
	switch (metric) {
	case DAMOS_WMARK_FREE_MEM_RATE:
		*metric_value = global_zone_page_state(NR_FREE_PAGES) * 1000 /
		       totalram_pages();
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

/*
 * Returns zero if the scheme is active.  Else, returns time to wait for next
 * watermark check in micro-seconds.
 */
static unsigned long damos_wmark_wait_us(struct damos *scheme)
{
	unsigned long metric;

	if (damos_get_wmark_metric_value(scheme->wmarks.metric, &metric))
		return 0;

	/* higher than high watermark or lower than low watermark */
	if (metric > scheme->wmarks.high || scheme->wmarks.low > metric) {
		if (scheme->wmarks.activated)
			pr_debug("deactivate a scheme (%d) for %s wmark\n",
				 scheme->action,
				 str_high_low(metric > scheme->wmarks.high));
		scheme->wmarks.activated = false;
		return scheme->wmarks.interval;
	}

	/* inactive and higher than middle watermark */
	if ((scheme->wmarks.high >= metric && metric >= scheme->wmarks.mid) &&
			!scheme->wmarks.activated)
		return scheme->wmarks.interval;

	if (!scheme->wmarks.activated)
		pr_debug("activate a scheme (%d)\n", scheme->action);
	scheme->wmarks.activated = true;
	return 0;
}

static void kdamond_usleep(unsigned long usecs)
{
	if (usecs >= USLEEP_RANGE_UPPER_BOUND)
		schedule_timeout_idle(usecs_to_jiffies(usecs));
	else
		usleep_range_idle(usecs, usecs + 1);
}

/*
 * kdamond_call() - handle damon_call_control.
 * @ctx:	The &struct damon_ctx of the kdamond.
 * @cancel:	Whether to cancel the invocation of the function.
 *
 * If there is a &struct damon_call_control request that registered via
 * &damon_call() on @ctx, do or cancel the invocation of the function depending
 * on @cancel.  @cancel is set when the kdamond is deactivated by DAMOS
 * watermarks, or the kdamond is already out of the main loop and therefore
 * will be terminated.
 */
static void kdamond_call(struct damon_ctx *ctx, bool cancel)
{
	struct damon_call_control *control;
	int ret = 0;

	mutex_lock(&ctx->call_control_lock);
	control = ctx->call_control;
	mutex_unlock(&ctx->call_control_lock);
	if (!control)
		return;
	if (cancel) {
		control->canceled = true;
	} else {
		ret = control->fn(control->data);
		control->return_code = ret;
	}
	complete(&control->completion);
	mutex_lock(&ctx->call_control_lock);
	ctx->call_control = NULL;
	mutex_unlock(&ctx->call_control_lock);
}

/* Returns negative error code if it's not activated but should return */
static int kdamond_wait_activation(struct damon_ctx *ctx)
{
	struct damos *s;
	unsigned long wait_time;
	unsigned long min_wait_time = 0;
	bool init_wait_time = false;

	while (!kdamond_need_stop(ctx)) {
		damon_for_each_scheme(s, ctx) {
			wait_time = damos_wmark_wait_us(s);
			if (!init_wait_time || wait_time < min_wait_time) {
				init_wait_time = true;
				min_wait_time = wait_time;
			}
		}
		if (!min_wait_time)
			return 0;

		kdamond_usleep(min_wait_time);

		if (ctx->callback.after_wmarks_check &&
				ctx->callback.after_wmarks_check(ctx))
			break;
		kdamond_call(ctx, true);
		damos_walk_cancel(ctx);
	}
	return -EBUSY;
}

static void kdamond_init_intervals_sis(struct damon_ctx *ctx)
{
	unsigned long sample_interval = ctx->attrs.sample_interval ?
		ctx->attrs.sample_interval : 1;
	unsigned long apply_interval;
	struct damos *scheme;

	ctx->passed_sample_intervals = 0;
	ctx->next_aggregation_sis = ctx->attrs.aggr_interval / sample_interval;
	ctx->next_ops_update_sis = ctx->attrs.ops_update_interval /
		sample_interval;

	damon_for_each_scheme(scheme, ctx) {
		apply_interval = scheme->apply_interval_us ?
			scheme->apply_interval_us : ctx->attrs.aggr_interval;
		scheme->next_apply_sis = apply_interval / sample_interval;
	}
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kdamond_fn(void *data)
{
	struct damon_ctx *ctx = data;
	struct damon_target *t;
	struct damon_region *r, *next;
	unsigned int max_nr_accesses = 0;
	unsigned long sz_limit = 0;

	pr_debug("kdamond (%d) starts\n", current->pid);

	complete(&ctx->kdamond_started);
	kdamond_init_intervals_sis(ctx);

	if (ctx->ops.init)
		ctx->ops.init(ctx);
	if (ctx->callback.before_start && ctx->callback.before_start(ctx))
		goto done;
	ctx->regions_score_histogram = kmalloc_array(DAMOS_MAX_SCORE + 1,
			sizeof(*ctx->regions_score_histogram), GFP_KERNEL);
	if (!ctx->regions_score_histogram)
		goto done;

	sz_limit = damon_region_sz_limit(ctx);

	while (!kdamond_need_stop(ctx)) {
		/*
		 * ctx->attrs and ctx->next_{aggregation,ops_update}_sis could
		 * be changed from after_wmarks_check() or after_aggregation()
		 * callbacks.  Read the values here, and use those for this
		 * iteration.  That is, damon_set_attrs() updated new values
		 * are respected from next iteration.
		 */
		unsigned long next_aggregation_sis = ctx->next_aggregation_sis;
		unsigned long next_ops_update_sis = ctx->next_ops_update_sis;
		unsigned long sample_interval = ctx->attrs.sample_interval;

		if (kdamond_wait_activation(ctx))
			break;

		if (ctx->ops.prepare_access_checks)
			ctx->ops.prepare_access_checks(ctx);
		if (ctx->callback.after_sampling &&
				ctx->callback.after_sampling(ctx))
			break;
		kdamond_call(ctx, false);

		kdamond_usleep(sample_interval);
		ctx->passed_sample_intervals++;

		if (ctx->ops.check_accesses)
			max_nr_accesses = ctx->ops.check_accesses(ctx);

		if (ctx->passed_sample_intervals >= next_aggregation_sis) {
			kdamond_merge_regions(ctx,
					max_nr_accesses / 10,
					sz_limit);
			if (ctx->callback.after_aggregation &&
					ctx->callback.after_aggregation(ctx))
				break;
		}

		/*
		 * do kdamond_apply_schemes() after kdamond_merge_regions() if
		 * possible, to reduce overhead
		 */
		if (!list_empty(&ctx->schemes))
			kdamond_apply_schemes(ctx);
		else
			damos_walk_cancel(ctx);

		sample_interval = ctx->attrs.sample_interval ?
			ctx->attrs.sample_interval : 1;
		if (ctx->passed_sample_intervals >= next_aggregation_sis) {
			ctx->next_aggregation_sis = next_aggregation_sis +
				ctx->attrs.aggr_interval / sample_interval;

			kdamond_reset_aggregated(ctx);
			kdamond_split_regions(ctx);
			if (ctx->ops.reset_aggregated)
				ctx->ops.reset_aggregated(ctx);
		}

		if (ctx->passed_sample_intervals >= next_ops_update_sis) {
			ctx->next_ops_update_sis = next_ops_update_sis +
				ctx->attrs.ops_update_interval /
				sample_interval;
			if (ctx->ops.update)
				ctx->ops.update(ctx);
			sz_limit = damon_region_sz_limit(ctx);
		}
	}
done:
	damon_for_each_target(t, ctx) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r, t);
	}

	if (ctx->callback.before_terminate)
		ctx->callback.before_terminate(ctx);
	if (ctx->ops.cleanup)
		ctx->ops.cleanup(ctx);
	kfree(ctx->regions_score_histogram);

	pr_debug("kdamond (%d) finishes\n", current->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	kdamond_call(ctx, true);
	damos_walk_cancel(ctx);

	mutex_lock(&damon_lock);
	nr_running_ctxs--;
	if (!nr_running_ctxs && running_exclusive_ctxs)
		running_exclusive_ctxs = false;
	mutex_unlock(&damon_lock);

	return 0;
}

/*
 * struct damon_system_ram_region - System RAM resource address region of
 *				    [@start, @end).
 * @start:	Start address of the region (inclusive).
 * @end:	End address of the region (exclusive).
 */
struct damon_system_ram_region {
	unsigned long start;
	unsigned long end;
};

static int walk_system_ram(struct resource *res, void *arg)
{
	struct damon_system_ram_region *a = arg;

	if (a->end - a->start < resource_size(res)) {
		a->start = res->start;
		a->end = res->end;
	}
	return 0;
}

/*
 * Find biggest 'System RAM' resource and store its start and end address in
 * @start and @end, respectively.  If no System RAM is found, returns false.
 */
static bool damon_find_biggest_system_ram(unsigned long *start,
						unsigned long *end)

{
	struct damon_system_ram_region arg = {};

	walk_system_ram_res(0, ULONG_MAX, &arg, walk_system_ram);
	if (arg.end <= arg.start)
		return false;

	*start = arg.start;
	*end = arg.end;
	return true;
}

/**
 * damon_set_region_biggest_system_ram_default() - Set the region of the given
 * monitoring target as requested, or biggest 'System RAM'.
 * @t:		The monitoring target to set the region.
 * @start:	The pointer to the start address of the region.
 * @end:	The pointer to the end address of the region.
 *
 * This function sets the region of @t as requested by @start and @end.  If the
 * values of @start and @end are zero, however, this function finds the biggest
 * 'System RAM' resource and sets the region to cover the resource.  In the
 * latter case, this function saves the start and end addresses of the resource
 * in @start and @end, respectively.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_region_biggest_system_ram_default(struct damon_target *t,
			unsigned long *start, unsigned long *end)
{
	struct damon_addr_range addr_range;

	if (*start > *end)
		return -EINVAL;

	if (!*start && !*end &&
		!damon_find_biggest_system_ram(start, end))
		return -EINVAL;

	addr_range.start = *start;
	addr_range.end = *end;
	return damon_set_regions(t, &addr_range, 1);
}

/*
 * damon_moving_sum() - Calculate an inferred moving sum value.
 * @mvsum:	Inferred sum of the last @len_window values.
 * @nomvsum:	Non-moving sum of the last discrete @len_window window values.
 * @len_window:	The number of last values to take care of.
 * @new_value:	New value that will be added to the pseudo moving sum.
 *
 * Moving sum (moving average * window size) is good for handling noise, but
 * the cost of keeping past values can be high for arbitrary window size.  This
 * function implements a lightweight pseudo moving sum function that doesn't
 * keep the past window values.
 *
 * It simply assumes there was no noise in the past, and get the no-noise
 * assumed past value to drop from @nomvsum and @len_window.  @nomvsum is a
 * non-moving sum of the last window.  For example, if @len_window is 10 and we
 * have 25 values, @nomvsum is the sum of the 11th to 20th values of the 25
 * values.  Hence, this function simply drops @nomvsum / @len_window from
 * given @mvsum and add @new_value.
 *
 * For example, if @len_window is 10 and @nomvsum is 50, the last 10 values for
 * the last window could be vary, e.g., 0, 10, 0, 10, 0, 10, 0, 0, 0, 20.  For
 * calculating next moving sum with a new value, we should drop 0 from 50 and
 * add the new value.  However, this function assumes it got value 5 for each
 * of the last ten times.  Based on the assumption, when the next value is
 * measured, it drops the assumed past value, 5 from the current sum, and add
 * the new value to get the updated pseduo-moving average.
 *
 * This means the value could have errors, but the errors will be disappeared
 * for every @len_window aligned calls.  For example, if @len_window is 10, the
 * pseudo moving sum with 11th value to 19th value would have an error.  But
 * the sum with 20th value will not have the error.
 *
 * Return: Pseudo-moving average after getting the @new_value.
 */
static unsigned int damon_moving_sum(unsigned int mvsum, unsigned int nomvsum,
		unsigned int len_window, unsigned int new_value)
{
	return mvsum - nomvsum / len_window + new_value;
}

/**
 * damon_update_region_access_rate() - Update the access rate of a region.
 * @r:		The DAMON region to update for its access check result.
 * @accessed:	Whether the region has accessed during last sampling interval.
 * @attrs:	The damon_attrs of the DAMON context.
 *
 * Update the access rate of a region with the region's last sampling interval
 * access check result.
 *
 * Usually this will be called by &damon_operations->check_accesses callback.
 */
void damon_update_region_access_rate(struct damon_region *r, bool accessed,
		struct damon_attrs *attrs)
{
	unsigned int len_window = 1;

	/*
	 * sample_interval can be zero, but cannot be larger than
	 * aggr_interval, owing to validation of damon_set_attrs().
	 */
	if (attrs->sample_interval)
		len_window = damon_max_nr_accesses(attrs);
	r->nr_accesses_bp = damon_moving_sum(r->nr_accesses_bp,
			r->last_nr_accesses * 10000, len_window,
			accessed ? 10000 : 0);

	if (accessed)
		r->nr_accesses++;
}

static int __init damon_init(void)
{
	damon_region_cache = KMEM_CACHE(damon_region, 0);
	if (unlikely(!damon_region_cache)) {
		pr_err("creating damon_region_cache fails\n");
		return -ENOMEM;
	}

	return 0;
}

subsys_initcall(damon_init);

#include "tests/core-kunit.h"
