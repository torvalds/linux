/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON api
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifndef _DAMON_H_
#define _DAMON_H_

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/random.h>

/* Minimal region size.  Every damon_region is aligned by this. */
#define DAMON_MIN_REGION	PAGE_SIZE
/* Max priority score for DAMON-based operation schemes */
#define DAMOS_MAX_SCORE		(99)

/* Get a random number in [l, r) */
static inline unsigned long damon_rand(unsigned long l, unsigned long r)
{
	return l + prandom_u32_max(r - l);
}

/**
 * struct damon_addr_range - Represents an address region of [@start, @end).
 * @start:	Start address of the region (inclusive).
 * @end:	End address of the region (exclusive).
 */
struct damon_addr_range {
	unsigned long start;
	unsigned long end;
};

/**
 * struct damon_region - Represents a monitoring target region.
 * @ar:			The address range of the region.
 * @sampling_addr:	Address of the sample for the next access check.
 * @nr_accesses:	Access frequency of this region.
 * @list:		List head for siblings.
 * @age:		Age of this region.
 *
 * @age is initially zero, increased for each aggregation interval, and reset
 * to zero again if the access frequency is significantly changed.  If two
 * regions are merged into a new region, both @nr_accesses and @age of the new
 * region are set as region size-weighted average of those of the two regions.
 */
struct damon_region {
	struct damon_addr_range ar;
	unsigned long sampling_addr;
	unsigned int nr_accesses;
	struct list_head list;

	unsigned int age;
/* private: Internal value for age calculation. */
	unsigned int last_nr_accesses;
};

/**
 * struct damon_target - Represents a monitoring target.
 * @pid:		The PID of the virtual address space to monitor.
 * @nr_regions:		Number of monitoring target regions of this target.
 * @regions_list:	Head of the monitoring target regions of this target.
 * @list:		List head for siblings.
 *
 * Each monitoring context could have multiple targets.  For example, a context
 * for virtual memory address spaces could have multiple target processes.  The
 * @pid should be set for appropriate &struct damon_operations including the
 * virtual address spaces monitoring operations.
 */
struct damon_target {
	struct pid *pid;
	unsigned int nr_regions;
	struct list_head regions_list;
	struct list_head list;
};

/**
 * enum damos_action - Represents an action of a Data Access Monitoring-based
 * Operation Scheme.
 *
 * @DAMOS_WILLNEED:	Call ``madvise()`` for the region with MADV_WILLNEED.
 * @DAMOS_COLD:		Call ``madvise()`` for the region with MADV_COLD.
 * @DAMOS_PAGEOUT:	Call ``madvise()`` for the region with MADV_PAGEOUT.
 * @DAMOS_HUGEPAGE:	Call ``madvise()`` for the region with MADV_HUGEPAGE.
 * @DAMOS_NOHUGEPAGE:	Call ``madvise()`` for the region with MADV_NOHUGEPAGE.
 * @DAMOS_LRU_PRIO:	Prioritize the region on its LRU lists.
 * @DAMOS_LRU_DEPRIO:	Deprioritize the region on its LRU lists.
 * @DAMOS_STAT:		Do nothing but count the stat.
 * @NR_DAMOS_ACTIONS:	Total number of DAMOS actions
 */
enum damos_action {
	DAMOS_WILLNEED,
	DAMOS_COLD,
	DAMOS_PAGEOUT,
	DAMOS_HUGEPAGE,
	DAMOS_NOHUGEPAGE,
	DAMOS_LRU_PRIO,
	DAMOS_LRU_DEPRIO,
	DAMOS_STAT,		/* Do nothing but only record the stat */
	NR_DAMOS_ACTIONS,
};

/**
 * struct damos_quota - Controls the aggressiveness of the given scheme.
 * @ms:			Maximum milliseconds that the scheme can use.
 * @sz:			Maximum bytes of memory that the action can be applied.
 * @reset_interval:	Charge reset interval in milliseconds.
 *
 * @weight_sz:		Weight of the region's size for prioritization.
 * @weight_nr_accesses:	Weight of the region's nr_accesses for prioritization.
 * @weight_age:		Weight of the region's age for prioritization.
 *
 * To avoid consuming too much CPU time or IO resources for applying the
 * &struct damos->action to large memory, DAMON allows users to set time and/or
 * size quotas.  The quotas can be set by writing non-zero values to &ms and
 * &sz, respectively.  If the time quota is set, DAMON tries to use only up to
 * &ms milliseconds within &reset_interval for applying the action.  If the
 * size quota is set, DAMON tries to apply the action only up to &sz bytes
 * within &reset_interval.
 *
 * Internally, the time quota is transformed to a size quota using estimated
 * throughput of the scheme's action.  DAMON then compares it against &sz and
 * uses smaller one as the effective quota.
 *
 * For selecting regions within the quota, DAMON prioritizes current scheme's
 * target memory regions using the &struct damon_operations->get_scheme_score.
 * You could customize the prioritization logic by setting &weight_sz,
 * &weight_nr_accesses, and &weight_age, because monitoring operations are
 * encouraged to respect those.
 */
struct damos_quota {
	unsigned long ms;
	unsigned long sz;
	unsigned long reset_interval;

	unsigned int weight_sz;
	unsigned int weight_nr_accesses;
	unsigned int weight_age;

/* private: */
	/* For throughput estimation */
	unsigned long total_charged_sz;
	unsigned long total_charged_ns;

	unsigned long esz;	/* Effective size quota in bytes */

	/* For charging the quota */
	unsigned long charged_sz;
	unsigned long charged_from;
	struct damon_target *charge_target_from;
	unsigned long charge_addr_from;

	/* For prioritization */
	unsigned long histogram[DAMOS_MAX_SCORE + 1];
	unsigned int min_score;
};

/**
 * enum damos_wmark_metric - Represents the watermark metric.
 *
 * @DAMOS_WMARK_NONE:		Ignore the watermarks of the given scheme.
 * @DAMOS_WMARK_FREE_MEM_RATE:	Free memory rate of the system in [0,1000].
 * @NR_DAMOS_WMARK_METRICS:	Total number of DAMOS watermark metrics
 */
enum damos_wmark_metric {
	DAMOS_WMARK_NONE,
	DAMOS_WMARK_FREE_MEM_RATE,
	NR_DAMOS_WMARK_METRICS,
};

/**
 * struct damos_watermarks - Controls when a given scheme should be activated.
 * @metric:	Metric for the watermarks.
 * @interval:	Watermarks check time interval in microseconds.
 * @high:	High watermark.
 * @mid:	Middle watermark.
 * @low:	Low watermark.
 *
 * If &metric is &DAMOS_WMARK_NONE, the scheme is always active.  Being active
 * means DAMON does monitoring and applying the action of the scheme to
 * appropriate memory regions.  Else, DAMON checks &metric of the system for at
 * least every &interval microseconds and works as below.
 *
 * If &metric is higher than &high, the scheme is inactivated.  If &metric is
 * between &mid and &low, the scheme is activated.  If &metric is lower than
 * &low, the scheme is inactivated.
 */
struct damos_watermarks {
	enum damos_wmark_metric metric;
	unsigned long interval;
	unsigned long high;
	unsigned long mid;
	unsigned long low;

/* private: */
	bool activated;
};

/**
 * struct damos_stat - Statistics on a given scheme.
 * @nr_tried:	Total number of regions that the scheme is tried to be applied.
 * @sz_tried:	Total size of regions that the scheme is tried to be applied.
 * @nr_applied:	Total number of regions that the scheme is applied.
 * @sz_applied:	Total size of regions that the scheme is applied.
 * @qt_exceeds: Total number of times the quota of the scheme has exceeded.
 */
struct damos_stat {
	unsigned long nr_tried;
	unsigned long sz_tried;
	unsigned long nr_applied;
	unsigned long sz_applied;
	unsigned long qt_exceeds;
};

/**
 * struct damos_access_pattern - Target access pattern of the given scheme.
 * @min_sz_region:	Minimum size of target regions.
 * @max_sz_region:	Maximum size of target regions.
 * @min_nr_accesses:	Minimum ``->nr_accesses`` of target regions.
 * @max_nr_accesses:	Maximum ``->nr_accesses`` of target regions.
 * @min_age_region:	Minimum age of target regions.
 * @max_age_region:	Maximum age of target regions.
 */
struct damos_access_pattern {
	unsigned long min_sz_region;
	unsigned long max_sz_region;
	unsigned int min_nr_accesses;
	unsigned int max_nr_accesses;
	unsigned int min_age_region;
	unsigned int max_age_region;
};

/**
 * struct damos - Represents a Data Access Monitoring-based Operation Scheme.
 * @pattern:		Access pattern of target regions.
 * @action:		&damo_action to be applied to the target regions.
 * @quota:		Control the aggressiveness of this scheme.
 * @wmarks:		Watermarks for automated (in)activation of this scheme.
 * @stat:		Statistics of this scheme.
 * @list:		List head for siblings.
 *
 * For each aggregation interval, DAMON finds regions which fit in the
 * &pattern and applies &action to those. To avoid consuming too much
 * CPU time or IO resources for the &action, &quota is used.
 *
 * To do the work only when needed, schemes can be activated for specific
 * system situations using &wmarks.  If all schemes that registered to the
 * monitoring context are inactive, DAMON stops monitoring either, and just
 * repeatedly checks the watermarks.
 *
 * If all schemes that registered to a &struct damon_ctx are inactive, DAMON
 * stops monitoring and just repeatedly checks the watermarks.
 *
 * After applying the &action to each region, &stat_count and &stat_sz is
 * updated to reflect the number of regions and total size of regions that the
 * &action is applied.
 */
struct damos {
	struct damos_access_pattern pattern;
	enum damos_action action;
	struct damos_quota quota;
	struct damos_watermarks wmarks;
	struct damos_stat stat;
	struct list_head list;
};

/**
 * enum damon_ops_id - Identifier for each monitoring operations implementation
 *
 * @DAMON_OPS_VADDR:	Monitoring operations for virtual address spaces
 * @DAMON_OPS_FVADDR:	Monitoring operations for only fixed ranges of virtual
 *			address spaces
 * @DAMON_OPS_PADDR:	Monitoring operations for the physical address space
 * @NR_DAMON_OPS:	Number of monitoring operations implementations
 */
enum damon_ops_id {
	DAMON_OPS_VADDR,
	DAMON_OPS_FVADDR,
	DAMON_OPS_PADDR,
	NR_DAMON_OPS,
};

struct damon_ctx;

/**
 * struct damon_operations - Monitoring operations for given use cases.
 *
 * @id:				Identifier of this operations set.
 * @init:			Initialize operations-related data structures.
 * @update:			Update operations-related data structures.
 * @prepare_access_checks:	Prepare next access check of target regions.
 * @check_accesses:		Check the accesses to target regions.
 * @reset_aggregated:		Reset aggregated accesses monitoring results.
 * @get_scheme_score:		Get the score of a region for a scheme.
 * @apply_scheme:		Apply a DAMON-based operation scheme.
 * @target_valid:		Determine if the target is valid.
 * @cleanup:			Clean up the context.
 *
 * DAMON can be extended for various address spaces and usages.  For this,
 * users should register the low level operations for their target address
 * space and usecase via the &damon_ctx.ops.  Then, the monitoring thread
 * (&damon_ctx.kdamond) calls @init and @prepare_access_checks before starting
 * the monitoring, @update after each &damon_ctx.ops_update_interval, and
 * @check_accesses, @target_valid and @prepare_access_checks after each
 * &damon_ctx.sample_interval.  Finally, @reset_aggregated is called after each
 * &damon_ctx.aggr_interval.
 *
 * Each &struct damon_operations instance having valid @id can be registered
 * via damon_register_ops() and selected by damon_select_ops() later.
 * @init should initialize operations-related data structures.  For example,
 * this could be used to construct proper monitoring target regions and link
 * those to @damon_ctx.adaptive_targets.
 * @update should update the operations-related data structures.  For example,
 * this could be used to update monitoring target regions for current status.
 * @prepare_access_checks should manipulate the monitoring regions to be
 * prepared for the next access check.
 * @check_accesses should check the accesses to each region that made after the
 * last preparation and update the number of observed accesses of each region.
 * It should also return max number of observed accesses that made as a result
 * of its update.  The value will be used for regions adjustment threshold.
 * @reset_aggregated should reset the access monitoring results that aggregated
 * by @check_accesses.
 * @get_scheme_score should return the priority score of a region for a scheme
 * as an integer in [0, &DAMOS_MAX_SCORE].
 * @apply_scheme is called from @kdamond when a region for user provided
 * DAMON-based operation scheme is found.  It should apply the scheme's action
 * to the region and return bytes of the region that the action is successfully
 * applied.
 * @target_valid should check whether the target is still valid for the
 * monitoring.
 * @cleanup is called from @kdamond just before its termination.
 */
struct damon_operations {
	enum damon_ops_id id;
	void (*init)(struct damon_ctx *context);
	void (*update)(struct damon_ctx *context);
	void (*prepare_access_checks)(struct damon_ctx *context);
	unsigned int (*check_accesses)(struct damon_ctx *context);
	void (*reset_aggregated)(struct damon_ctx *context);
	int (*get_scheme_score)(struct damon_ctx *context,
			struct damon_target *t, struct damon_region *r,
			struct damos *scheme);
	unsigned long (*apply_scheme)(struct damon_ctx *context,
			struct damon_target *t, struct damon_region *r,
			struct damos *scheme);
	bool (*target_valid)(struct damon_target *t);
	void (*cleanup)(struct damon_ctx *context);
};

/**
 * struct damon_callback - Monitoring events notification callbacks.
 *
 * @before_start:	Called before starting the monitoring.
 * @after_wmarks_check:	Called after each schemes' watermarks check.
 * @after_sampling:	Called after each sampling.
 * @after_aggregation:	Called after each aggregation.
 * @before_terminate:	Called before terminating the monitoring.
 * @private:		User private data.
 *
 * The monitoring thread (&damon_ctx.kdamond) calls @before_start and
 * @before_terminate just before starting and finishing the monitoring,
 * respectively.  Therefore, those are good places for installing and cleaning
 * @private.
 *
 * The monitoring thread calls @after_wmarks_check after each DAMON-based
 * operation schemes' watermarks check.  If users need to make changes to the
 * attributes of the monitoring context while it's deactivated due to the
 * watermarks, this is the good place to do.
 *
 * The monitoring thread calls @after_sampling and @after_aggregation for each
 * of the sampling intervals and aggregation intervals, respectively.
 * Therefore, users can safely access the monitoring results without additional
 * protection.  For the reason, users are recommended to use these callback for
 * the accesses to the results.
 *
 * If any callback returns non-zero, monitoring stops.
 */
struct damon_callback {
	void *private;

	int (*before_start)(struct damon_ctx *context);
	int (*after_wmarks_check)(struct damon_ctx *context);
	int (*after_sampling)(struct damon_ctx *context);
	int (*after_aggregation)(struct damon_ctx *context);
	void (*before_terminate)(struct damon_ctx *context);
};

/**
 * struct damon_attrs - Monitoring attributes for accuracy/overhead control.
 *
 * @sample_interval:		The time between access samplings.
 * @aggr_interval:		The time between monitor results aggregations.
 * @ops_update_interval:	The time between monitoring operations updates.
 * @min_nr_regions:		The minimum number of adaptive monitoring
 *				regions.
 * @max_nr_regions:		The maximum number of adaptive monitoring
 *				regions.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  DAMON also checks whether the target
 * memory regions need update (e.g., by ``mmap()`` calls from the application,
 * in case of virtual memory monitoring) and applies the changes for each
 * @ops_update_interval.  All time intervals are in micro-seconds.
 * Please refer to &struct damon_operations and &struct damon_callback for more
 * detail.
 */
struct damon_attrs {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long ops_update_interval;
	unsigned long min_nr_regions;
	unsigned long max_nr_regions;
};

/**
 * struct damon_ctx - Represents a context for each monitoring.  This is the
 * main interface that allows users to set the attributes and get the results
 * of the monitoring.
 *
 * @attrs:		Monitoring attributes for accuracy/overhead control.
 * @kdamond:		Kernel thread who does the monitoring.
 * @kdamond_lock:	Mutex for the synchronizations with @kdamond.
 *
 * For each monitoring context, one kernel thread for the monitoring is
 * created.  The pointer to the thread is stored in @kdamond.
 *
 * Once started, the monitoring thread runs until explicitly required to be
 * terminated or every monitoring target is invalid.  The validity of the
 * targets is checked via the &damon_operations.target_valid of @ops.  The
 * termination can also be explicitly requested by calling damon_stop().
 * The thread sets @kdamond to NULL when it terminates. Therefore, users can
 * know whether the monitoring is ongoing or terminated by reading @kdamond.
 * Reads and writes to @kdamond from outside of the monitoring thread must
 * be protected by @kdamond_lock.
 *
 * Note that the monitoring thread protects only @kdamond via @kdamond_lock.
 * Accesses to other fields must be protected by themselves.
 *
 * @ops:	Set of monitoring operations for given use cases.
 * @callback:	Set of callbacks for monitoring events notifications.
 *
 * @adaptive_targets:	Head of monitoring targets (&damon_target) list.
 * @schemes:		Head of schemes (&damos) list.
 */
struct damon_ctx {
	struct damon_attrs attrs;

/* private: internal use only */
	struct timespec64 last_aggregation;
	struct timespec64 last_ops_update;
	/* for waiting until the execution of the kdamond_fn is started */
	struct completion kdamond_started;

/* public: */
	struct task_struct *kdamond;
	struct mutex kdamond_lock;

	struct damon_operations ops;
	struct damon_callback callback;

	struct list_head adaptive_targets;
	struct list_head schemes;
};

static inline struct damon_region *damon_next_region(struct damon_region *r)
{
	return container_of(r->list.next, struct damon_region, list);
}

static inline struct damon_region *damon_prev_region(struct damon_region *r)
{
	return container_of(r->list.prev, struct damon_region, list);
}

static inline struct damon_region *damon_last_region(struct damon_target *t)
{
	return list_last_entry(&t->regions_list, struct damon_region, list);
}

static inline struct damon_region *damon_first_region(struct damon_target *t)
{
	return list_first_entry(&t->regions_list, struct damon_region, list);
}

static inline unsigned long damon_sz_region(struct damon_region *r)
{
	return r->ar.end - r->ar.start;
}


#define damon_for_each_region(r, t) \
	list_for_each_entry(r, &t->regions_list, list)

#define damon_for_each_region_from(r, t) \
	list_for_each_entry_from(r, &t->regions_list, list)

#define damon_for_each_region_safe(r, next, t) \
	list_for_each_entry_safe(r, next, &t->regions_list, list)

#define damon_for_each_target(t, ctx) \
	list_for_each_entry(t, &(ctx)->adaptive_targets, list)

#define damon_for_each_target_safe(t, next, ctx)	\
	list_for_each_entry_safe(t, next, &(ctx)->adaptive_targets, list)

#define damon_for_each_scheme(s, ctx) \
	list_for_each_entry(s, &(ctx)->schemes, list)

#define damon_for_each_scheme_safe(s, next, ctx) \
	list_for_each_entry_safe(s, next, &(ctx)->schemes, list)

#ifdef CONFIG_DAMON

struct damon_region *damon_new_region(unsigned long start, unsigned long end);

/*
 * Add a region between two other regions
 */
static inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next,
		struct damon_target *t)
{
	__list_add(&r->list, &prev->list, &next->list);
	t->nr_regions++;
}

void damon_add_region(struct damon_region *r, struct damon_target *t);
void damon_destroy_region(struct damon_region *r, struct damon_target *t);
int damon_set_regions(struct damon_target *t, struct damon_addr_range *ranges,
		unsigned int nr_ranges);

struct damos *damon_new_scheme(struct damos_access_pattern *pattern,
			enum damos_action action, struct damos_quota *quota,
			struct damos_watermarks *wmarks);
void damon_add_scheme(struct damon_ctx *ctx, struct damos *s);
void damon_destroy_scheme(struct damos *s);

struct damon_target *damon_new_target(void);
void damon_add_target(struct damon_ctx *ctx, struct damon_target *t);
bool damon_targets_empty(struct damon_ctx *ctx);
void damon_free_target(struct damon_target *t);
void damon_destroy_target(struct damon_target *t);
unsigned int damon_nr_regions(struct damon_target *t);

struct damon_ctx *damon_new_ctx(void);
void damon_destroy_ctx(struct damon_ctx *ctx);
int damon_set_attrs(struct damon_ctx *ctx, struct damon_attrs *attrs);
void damon_set_schemes(struct damon_ctx *ctx,
			struct damos **schemes, ssize_t nr_schemes);
int damon_nr_running_ctxs(void);
bool damon_is_registered_ops(enum damon_ops_id id);
int damon_register_ops(struct damon_operations *ops);
int damon_select_ops(struct damon_ctx *ctx, enum damon_ops_id id);

static inline bool damon_target_has_pid(const struct damon_ctx *ctx)
{
	return ctx->ops.id == DAMON_OPS_VADDR || ctx->ops.id == DAMON_OPS_FVADDR;
}

static inline unsigned int damon_max_nr_accesses(const struct damon_attrs *attrs)
{
	/* {aggr,sample}_interval are unsigned long, hence could overflow */
	return min(attrs->aggr_interval / attrs->sample_interval,
			(unsigned long)UINT_MAX);
}


int damon_start(struct damon_ctx **ctxs, int nr_ctxs, bool exclusive);
int damon_stop(struct damon_ctx **ctxs, int nr_ctxs);

int damon_set_region_biggest_system_ram_default(struct damon_target *t,
				unsigned long *start, unsigned long *end);

#endif	/* CONFIG_DAMON */

#endif	/* _DAMON_H */
