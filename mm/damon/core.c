// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>

#define CREATE_TRACE_POINTS
#include <trace/events/damon.h>

#ifdef CONFIG_DAMON_KUNIT_TEST
#undef DAMON_MIN_REGION
#define DAMON_MIN_REGION 1
#endif

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32_max(r - l))

static DEFINE_MUTEX(damon_lock);
static int nr_running_ctxs;

/*
 * Construct a damon_region struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_region *damon_new_region(unsigned long start, unsigned long end)
{
	struct damon_region *region;

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return NULL;

	region->ar.start = start;
	region->ar.end = end;
	region->nr_accesses = 0;
	INIT_LIST_HEAD(&region->list);

	region->age = 0;
	region->last_nr_accesses = 0;

	return region;
}

/*
 * Add a region between two other regions
 */
inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next,
		struct damon_target *t)
{
	__list_add(&r->list, &prev->list, &next->list);
	t->nr_regions++;
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
	kfree(r);
}

void damon_destroy_region(struct damon_region *r, struct damon_target *t)
{
	damon_del_region(r, t);
	damon_free_region(r);
}

struct damos *damon_new_scheme(
		unsigned long min_sz_region, unsigned long max_sz_region,
		unsigned int min_nr_accesses, unsigned int max_nr_accesses,
		unsigned int min_age_region, unsigned int max_age_region,
		enum damos_action action, struct damos_quota *quota,
		struct damos_watermarks *wmarks)
{
	struct damos *scheme;

	scheme = kmalloc(sizeof(*scheme), GFP_KERNEL);
	if (!scheme)
		return NULL;
	scheme->min_sz_region = min_sz_region;
	scheme->max_sz_region = max_sz_region;
	scheme->min_nr_accesses = min_nr_accesses;
	scheme->max_nr_accesses = max_nr_accesses;
	scheme->min_age_region = min_age_region;
	scheme->max_age_region = max_age_region;
	scheme->action = action;
	scheme->stat_count = 0;
	scheme->stat_sz = 0;
	INIT_LIST_HEAD(&scheme->list);

	scheme->quota.ms = quota->ms;
	scheme->quota.sz = quota->sz;
	scheme->quota.reset_interval = quota->reset_interval;
	scheme->quota.weight_sz = quota->weight_sz;
	scheme->quota.weight_nr_accesses = quota->weight_nr_accesses;
	scheme->quota.weight_age = quota->weight_age;
	scheme->quota.total_charged_sz = 0;
	scheme->quota.total_charged_ns = 0;
	scheme->quota.esz = 0;
	scheme->quota.charged_sz = 0;
	scheme->quota.charged_from = 0;
	scheme->quota.charge_target_from = NULL;
	scheme->quota.charge_addr_from = 0;

	scheme->wmarks.metric = wmarks->metric;
	scheme->wmarks.interval = wmarks->interval;
	scheme->wmarks.high = wmarks->high;
	scheme->wmarks.mid = wmarks->mid;
	scheme->wmarks.low = wmarks->low;
	scheme->wmarks.activated = true;

	return scheme;
}

void damon_add_scheme(struct damon_ctx *ctx, struct damos *s)
{
	list_add_tail(&s->list, &ctx->schemes);
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
	damon_del_scheme(s);
	damon_free_scheme(s);
}

/*
 * Construct a damon_target struct
 *
 * Returns the pointer to the new struct if success, or NULL otherwise
 */
struct damon_target *damon_new_target(unsigned long id)
{
	struct damon_target *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	t->id = id;
	t->nr_regions = 0;
	INIT_LIST_HEAD(&t->regions_list);

	return t;
}

void damon_add_target(struct damon_ctx *ctx, struct damon_target *t)
{
	list_add_tail(&t->list, &ctx->adaptive_targets);
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

	ctx->sample_interval = 5 * 1000;
	ctx->aggr_interval = 100 * 1000;
	ctx->primitive_update_interval = 60 * 1000 * 1000;

	ktime_get_coarse_ts64(&ctx->last_aggregation);
	ctx->last_primitive_update = ctx->last_aggregation;

	mutex_init(&ctx->kdamond_lock);

	ctx->min_nr_regions = 10;
	ctx->max_nr_regions = 1000;

	INIT_LIST_HEAD(&ctx->adaptive_targets);
	INIT_LIST_HEAD(&ctx->schemes);

	return ctx;
}

static void damon_destroy_targets(struct damon_ctx *ctx)
{
	struct damon_target *t, *next_t;

	if (ctx->primitive.cleanup) {
		ctx->primitive.cleanup(ctx);
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

/**
 * damon_set_targets() - Set monitoring targets.
 * @ctx:	monitoring context
 * @ids:	array of target ids
 * @nr_ids:	number of entries in @ids
 *
 * This function should not be called while the kdamond is running.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_targets(struct damon_ctx *ctx,
		      unsigned long *ids, ssize_t nr_ids)
{
	ssize_t i;
	struct damon_target *t, *next;

	damon_destroy_targets(ctx);

	for (i = 0; i < nr_ids; i++) {
		t = damon_new_target(ids[i]);
		if (!t) {
			pr_err("Failed to alloc damon_target\n");
			/* The caller should do cleanup of the ids itself */
			damon_for_each_target_safe(t, next, ctx)
				damon_destroy_target(t);
			return -ENOMEM;
		}
		damon_add_target(ctx, t);
	}

	return 0;
}

/**
 * damon_set_attrs() - Set attributes for the monitoring.
 * @ctx:		monitoring context
 * @sample_int:		time interval between samplings
 * @aggr_int:		time interval between aggregations
 * @primitive_upd_int:	time interval between monitoring primitive updates
 * @min_nr_reg:		minimal number of regions
 * @max_nr_reg:		maximum number of regions
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		    unsigned long aggr_int, unsigned long primitive_upd_int,
		    unsigned long min_nr_reg, unsigned long max_nr_reg)
{
	if (min_nr_reg < 3) {
		pr_err("min_nr_regions (%lu) must be at least 3\n",
				min_nr_reg);
		return -EINVAL;
	}
	if (min_nr_reg > max_nr_reg) {
		pr_err("invalid nr_regions.  min (%lu) > max (%lu)\n",
				min_nr_reg, max_nr_reg);
		return -EINVAL;
	}

	ctx->sample_interval = sample_int;
	ctx->aggr_interval = aggr_int;
	ctx->primitive_update_interval = primitive_upd_int;
	ctx->min_nr_regions = min_nr_reg;
	ctx->max_nr_regions = max_nr_reg;

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
 *
 * Return: 0 if success, or negative error code otherwise.
 */
int damon_set_schemes(struct damon_ctx *ctx, struct damos **schemes,
			ssize_t nr_schemes)
{
	struct damos *s, *next;
	ssize_t i;

	damon_for_each_scheme_safe(s, next, ctx)
		damon_destroy_scheme(s);
	for (i = 0; i < nr_schemes; i++)
		damon_add_scheme(ctx, schemes[i]);
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
			sz += r->ar.end - r->ar.start;
	}

	if (ctx->min_nr_regions)
		sz /= ctx->min_nr_regions;
	if (sz < DAMON_MIN_REGION)
		sz = DAMON_MIN_REGION;

	return sz;
}

static bool damon_kdamond_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);

	return running;
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
		ctx->kdamond_stop = false;
		ctx->kdamond = kthread_run(kdamond_fn, ctx, "kdamond.%d",
				nr_running_ctxs);
		if (IS_ERR(ctx->kdamond)) {
			err = PTR_ERR(ctx->kdamond);
			ctx->kdamond = NULL;
		}
	}
	mutex_unlock(&ctx->kdamond_lock);

	return err;
}

/**
 * damon_start() - Starts the monitorings for a given group of contexts.
 * @ctxs:	an array of the pointers for contexts to start monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * This function starts a group of monitoring threads for a group of monitoring
 * contexts.  One thread per each context is created and run in parallel.  The
 * caller should handle synchronization between the threads by itself.  If a
 * group of threads that created by other 'damon_start()' call is currently
 * running, this function does nothing but returns -EBUSY.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_start(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i;
	int err = 0;

	mutex_lock(&damon_lock);
	if (nr_running_ctxs) {
		mutex_unlock(&damon_lock);
		return -EBUSY;
	}

	for (i = 0; i < nr_ctxs; i++) {
		err = __damon_start(ctxs[i]);
		if (err)
			break;
		nr_running_ctxs++;
	}
	mutex_unlock(&damon_lock);

	return err;
}

/*
 * __damon_stop() - Stops monitoring of given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_stop(struct damon_ctx *ctx)
{
	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ctx->kdamond_stop = true;
		mutex_unlock(&ctx->kdamond_lock);
		while (damon_kdamond_running(ctx))
			usleep_range(ctx->sample_interval,
					ctx->sample_interval * 2);
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
			return err;
	}

	return err;
}

/*
 * damon_check_reset_time_interval() - Check if a time interval is elapsed.
 * @baseline:	the time to check whether the interval has elapsed since
 * @interval:	the time interval (microseconds)
 *
 * See whether the given time interval has passed since the given baseline
 * time.  If so, it also updates the baseline to current time for next check.
 *
 * Return:	true if the time interval has passed, or false otherwise.
 */
static bool damon_check_reset_time_interval(struct timespec64 *baseline,
		unsigned long interval)
{
	struct timespec64 now;

	ktime_get_coarse_ts64(&now);
	if ((timespec64_to_ns(&now) - timespec64_to_ns(baseline)) <
			interval * 1000)
		return false;
	*baseline = now;
	return true;
}

/*
 * Check whether it is time to flush the aggregated information
 */
static bool kdamond_aggregate_interval_passed(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_aggregation,
			ctx->aggr_interval);
}

/*
 * Reset the aggregated monitoring results ('nr_accesses' of each region).
 */
static void kdamond_reset_aggregated(struct damon_ctx *c)
{
	struct damon_target *t;

	damon_for_each_target(t, c) {
		struct damon_region *r;

		damon_for_each_region(r, t) {
			trace_damon_aggregated(t, r, damon_nr_regions(t));
			r->last_nr_accesses = r->nr_accesses;
			r->nr_accesses = 0;
		}
	}
}

static void damon_split_region_at(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_region *r,
		unsigned long sz_r);

static bool __damos_valid_target(struct damon_region *r, struct damos *s)
{
	unsigned long sz;

	sz = r->ar.end - r->ar.start;
	return s->min_sz_region <= sz && sz <= s->max_sz_region &&
		s->min_nr_accesses <= r->nr_accesses &&
		r->nr_accesses <= s->max_nr_accesses &&
		s->min_age_region <= r->age && r->age <= s->max_age_region;
}

static bool damos_valid_target(struct damon_ctx *c, struct damon_target *t,
		struct damon_region *r, struct damos *s)
{
	bool ret = __damos_valid_target(r, s);

	if (!ret || !s->quota.esz || !c->primitive.get_scheme_score)
		return ret;

	return c->primitive.get_scheme_score(c, t, r, s) >= s->quota.min_score;
}

static void damon_do_apply_schemes(struct damon_ctx *c,
				   struct damon_target *t,
				   struct damon_region *r)
{
	struct damos *s;

	damon_for_each_scheme(s, c) {
		struct damos_quota *quota = &s->quota;
		unsigned long sz = r->ar.end - r->ar.start;
		struct timespec64 begin, end;

		if (!s->wmarks.activated)
			continue;

		/* Check the quota */
		if (quota->esz && quota->charged_sz >= quota->esz)
			continue;

		/* Skip previously charged regions */
		if (quota->charge_target_from) {
			if (t != quota->charge_target_from)
				continue;
			if (r == damon_last_region(t)) {
				quota->charge_target_from = NULL;
				quota->charge_addr_from = 0;
				continue;
			}
			if (quota->charge_addr_from &&
					r->ar.end <= quota->charge_addr_from)
				continue;

			if (quota->charge_addr_from && r->ar.start <
					quota->charge_addr_from) {
				sz = ALIGN_DOWN(quota->charge_addr_from -
						r->ar.start, DAMON_MIN_REGION);
				if (!sz) {
					if (r->ar.end - r->ar.start <=
							DAMON_MIN_REGION)
						continue;
					sz = DAMON_MIN_REGION;
				}
				damon_split_region_at(c, t, r, sz);
				r = damon_next_region(r);
				sz = r->ar.end - r->ar.start;
			}
			quota->charge_target_from = NULL;
			quota->charge_addr_from = 0;
		}

		if (!damos_valid_target(c, t, r, s))
			continue;

		/* Apply the scheme */
		if (c->primitive.apply_scheme) {
			if (quota->esz &&
					quota->charged_sz + sz > quota->esz) {
				sz = ALIGN_DOWN(quota->esz - quota->charged_sz,
						DAMON_MIN_REGION);
				if (!sz)
					goto update_stat;
				damon_split_region_at(c, t, r, sz);
			}
			ktime_get_coarse_ts64(&begin);
			c->primitive.apply_scheme(c, t, r, s);
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
		s->stat_count++;
		s->stat_sz += sz;
	}
}

/* Shouldn't be called if quota->ms and quota->sz are zero */
static void damos_set_effective_quota(struct damos_quota *quota)
{
	unsigned long throughput;
	unsigned long esz;

	if (!quota->ms) {
		quota->esz = quota->sz;
		return;
	}

	if (quota->total_charged_ns)
		throughput = quota->total_charged_sz * 1000000 /
			quota->total_charged_ns;
	else
		throughput = PAGE_SIZE * 1024;
	esz = throughput * quota->ms;

	if (quota->sz && quota->sz < esz)
		esz = quota->sz;
	quota->esz = esz;
}

static void kdamond_apply_schemes(struct damon_ctx *c)
{
	struct damon_target *t;
	struct damon_region *r, *next_r;
	struct damos *s;

	damon_for_each_scheme(s, c) {
		struct damos_quota *quota = &s->quota;
		unsigned long cumulated_sz;
		unsigned int score, max_score = 0;

		if (!s->wmarks.activated)
			continue;

		if (!quota->ms && !quota->sz)
			continue;

		/* New charge window starts */
		if (time_after_eq(jiffies, quota->charged_from +
					msecs_to_jiffies(
						quota->reset_interval))) {
			quota->total_charged_sz += quota->charged_sz;
			quota->charged_from = jiffies;
			quota->charged_sz = 0;
			damos_set_effective_quota(quota);
		}

		if (!c->primitive.get_scheme_score)
			continue;

		/* Fill up the score histogram */
		memset(quota->histogram, 0, sizeof(quota->histogram));
		damon_for_each_target(t, c) {
			damon_for_each_region(r, t) {
				if (!__damos_valid_target(r, s))
					continue;
				score = c->primitive.get_scheme_score(
						c, t, r, s);
				quota->histogram[score] +=
					r->ar.end - r->ar.start;
				if (score > max_score)
					max_score = score;
			}
		}

		/* Set the min score limit */
		for (cumulated_sz = 0, score = max_score; ; score--) {
			cumulated_sz += quota->histogram[score];
			if (cumulated_sz >= quota->esz || !score)
				break;
		}
		quota->min_score = score;
	}

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next_r, t)
			damon_do_apply_schemes(c, t, r);
	}
}

#define sz_damon_region(r) (r->ar.end - r->ar.start)

/*
 * Merge two adjacent regions into one region
 */
static void damon_merge_two_regions(struct damon_target *t,
		struct damon_region *l, struct damon_region *r)
{
	unsigned long sz_l = sz_damon_region(l), sz_r = sz_damon_region(r);

	l->nr_accesses = (l->nr_accesses * sz_l + r->nr_accesses * sz_r) /
			(sz_l + sz_r);
	l->age = (l->age * sz_l + r->age * sz_r) / (sz_l + sz_r);
	l->ar.end = r->ar.end;
	damon_destroy_region(r, t);
}

#define diff_of(a, b) (a > b ? a - b : b - a)

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
		if (diff_of(r->nr_accesses, r->last_nr_accesses) > thres)
			r->age = 0;
		else
			r->age++;

		if (prev && prev->ar.end == r->ar.start &&
		    diff_of(prev->nr_accesses, r->nr_accesses) <= thres &&
		    sz_damon_region(prev) + sz_damon_region(r) <= sz_limit)
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
 */
static void kdamond_merge_regions(struct damon_ctx *c, unsigned int threshold,
				  unsigned long sz_limit)
{
	struct damon_target *t;

	damon_for_each_target(t, c)
		damon_merge_regions_of(t, threshold, sz_limit);
}

/*
 * Split a region in two
 *
 * r		the region to be split
 * sz_r		size of the first sub-region that will be made
 */
static void damon_split_region_at(struct damon_ctx *ctx,
		struct damon_target *t, struct damon_region *r,
		unsigned long sz_r)
{
	struct damon_region *new;

	new = damon_new_region(r->ar.start + sz_r, r->ar.end);
	if (!new)
		return;

	r->ar.end = new->ar.start;

	new->age = r->age;
	new->last_nr_accesses = r->last_nr_accesses;

	damon_insert_region(new, r, damon_next_region(r), t);
}

/* Split every region in the given target into 'nr_subs' regions */
static void damon_split_regions_of(struct damon_ctx *ctx,
				     struct damon_target *t, int nr_subs)
{
	struct damon_region *r, *next;
	unsigned long sz_region, sz_sub = 0;
	int i;

	damon_for_each_region_safe(r, next, t) {
		sz_region = r->ar.end - r->ar.start;

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

			damon_split_region_at(ctx, t, r, sz_sub);
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

	if (nr_regions > ctx->max_nr_regions / 2)
		return;

	/* Maybe the middle of the region has different access frequency */
	if (last_nr_regions == nr_regions &&
			nr_regions < ctx->max_nr_regions / 3)
		nr_subregions = 3;

	damon_for_each_target(t, ctx)
		damon_split_regions_of(ctx, t, nr_subregions);

	last_nr_regions = nr_regions;
}

/*
 * Check whether it is time to check and apply the target monitoring regions
 *
 * Returns true if it is.
 */
static bool kdamond_need_update_primitive(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_primitive_update,
			ctx->primitive_update_interval);
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
	bool stop;

	mutex_lock(&ctx->kdamond_lock);
	stop = ctx->kdamond_stop;
	mutex_unlock(&ctx->kdamond_lock);
	if (stop)
		return true;

	if (!ctx->primitive.target_valid)
		return false;

	damon_for_each_target(t, ctx) {
		if (ctx->primitive.target_valid(t))
			return false;
	}

	return true;
}

static unsigned long damos_wmark_metric_value(enum damos_wmark_metric metric)
{
	struct sysinfo i;

	switch (metric) {
	case DAMOS_WMARK_FREE_MEM_RATE:
		si_meminfo(&i);
		return i.freeram * 1000 / i.totalram;
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

	if (scheme->wmarks.metric == DAMOS_WMARK_NONE)
		return 0;

	metric = damos_wmark_metric_value(scheme->wmarks.metric);
	/* higher than high watermark or lower than low watermark */
	if (metric > scheme->wmarks.high || scheme->wmarks.low > metric) {
		if (scheme->wmarks.activated)
			pr_debug("inactivate a scheme (%d) for %s wmark\n",
					scheme->action,
					metric > scheme->wmarks.high ?
					"high" : "low");
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
	if (usecs > 100 * 1000)
		schedule_timeout_interruptible(usecs_to_jiffies(usecs));
	else
		usleep_range(usecs, usecs + 1);
}

/* Returns negative error code if it's not activated but should return */
static int kdamond_wait_activation(struct damon_ctx *ctx)
{
	struct damos *s;
	unsigned long wait_time;
	unsigned long min_wait_time = 0;

	while (!kdamond_need_stop(ctx)) {
		damon_for_each_scheme(s, ctx) {
			wait_time = damos_wmark_wait_us(s);
			if (!min_wait_time || wait_time < min_wait_time)
				min_wait_time = wait_time;
		}
		if (!min_wait_time)
			return 0;

		kdamond_usleep(min_wait_time);
	}
	return -EBUSY;
}

static void set_kdamond_stop(struct damon_ctx *ctx)
{
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond_stop = true;
	mutex_unlock(&ctx->kdamond_lock);
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kdamond_fn(void *data)
{
	struct damon_ctx *ctx = (struct damon_ctx *)data;
	struct damon_target *t;
	struct damon_region *r, *next;
	unsigned int max_nr_accesses = 0;
	unsigned long sz_limit = 0;

	pr_debug("kdamond (%d) starts\n", current->pid);

	if (ctx->primitive.init)
		ctx->primitive.init(ctx);
	if (ctx->callback.before_start && ctx->callback.before_start(ctx))
		set_kdamond_stop(ctx);

	sz_limit = damon_region_sz_limit(ctx);

	while (!kdamond_need_stop(ctx)) {
		if (kdamond_wait_activation(ctx))
			continue;

		if (ctx->primitive.prepare_access_checks)
			ctx->primitive.prepare_access_checks(ctx);
		if (ctx->callback.after_sampling &&
				ctx->callback.after_sampling(ctx))
			set_kdamond_stop(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);

		if (ctx->primitive.check_accesses)
			max_nr_accesses = ctx->primitive.check_accesses(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			kdamond_merge_regions(ctx,
					max_nr_accesses / 10,
					sz_limit);
			if (ctx->callback.after_aggregation &&
					ctx->callback.after_aggregation(ctx))
				set_kdamond_stop(ctx);
			kdamond_apply_schemes(ctx);
			kdamond_reset_aggregated(ctx);
			kdamond_split_regions(ctx);
			if (ctx->primitive.reset_aggregated)
				ctx->primitive.reset_aggregated(ctx);
		}

		if (kdamond_need_update_primitive(ctx)) {
			if (ctx->primitive.update)
				ctx->primitive.update(ctx);
			sz_limit = damon_region_sz_limit(ctx);
		}
	}
	damon_for_each_target(t, ctx) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r, t);
	}

	if (ctx->callback.before_terminate &&
			ctx->callback.before_terminate(ctx))
		set_kdamond_stop(ctx);
	if (ctx->primitive.cleanup)
		ctx->primitive.cleanup(ctx);

	pr_debug("kdamond (%d) finishes\n", current->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	mutex_lock(&damon_lock);
	nr_running_ctxs--;
	mutex_unlock(&damon_lock);

	return 0;
}

#include "core-test.h"
