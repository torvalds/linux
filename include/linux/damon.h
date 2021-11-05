/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DAMON api
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#ifndef _DAMON_H_
#define _DAMON_H_

#include <linux/mutex.h>
#include <linux/time64.h>
#include <linux/types.h>

/* Minimal region size.  Every damon_region is aligned by this. */
#define DAMON_MIN_REGION	PAGE_SIZE

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
 * @id:			Unique identifier for this target.
 * @nr_regions:		Number of monitoring target regions of this target.
 * @regions_list:	Head of the monitoring target regions of this target.
 * @list:		List head for siblings.
 *
 * Each monitoring context could have multiple targets.  For example, a context
 * for virtual memory address spaces could have multiple target processes.  The
 * @id of each target should be unique among the targets of the context.  For
 * example, in the virtual address monitoring context, it could be a pidfd or
 * an address of an mm_struct.
 */
struct damon_target {
	unsigned long id;
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
 * @DAMOS_STAT:		Do nothing but count the stat.
 */
enum damos_action {
	DAMOS_WILLNEED,
	DAMOS_COLD,
	DAMOS_PAGEOUT,
	DAMOS_HUGEPAGE,
	DAMOS_NOHUGEPAGE,
	DAMOS_STAT,		/* Do nothing but only record the stat */
};

/**
 * struct damos - Represents a Data Access Monitoring-based Operation Scheme.
 * @min_sz_region:	Minimum size of target regions.
 * @max_sz_region:	Maximum size of target regions.
 * @min_nr_accesses:	Minimum ``->nr_accesses`` of target regions.
 * @max_nr_accesses:	Maximum ``->nr_accesses`` of target regions.
 * @min_age_region:	Minimum age of target regions.
 * @max_age_region:	Maximum age of target regions.
 * @action:		&damo_action to be applied to the target regions.
 * @stat_count:		Total number of regions that this scheme is applied.
 * @stat_sz:		Total size of regions that this scheme is applied.
 * @list:		List head for siblings.
 *
 * For each aggregation interval, DAMON applies @action to monitoring target
 * regions fit in the condition and updates the statistics.  Note that both
 * the minimums and the maximums are inclusive.
 */
struct damos {
	unsigned long min_sz_region;
	unsigned long max_sz_region;
	unsigned int min_nr_accesses;
	unsigned int max_nr_accesses;
	unsigned int min_age_region;
	unsigned int max_age_region;
	enum damos_action action;
	unsigned long stat_count;
	unsigned long stat_sz;
	struct list_head list;
};

struct damon_ctx;

/**
 * struct damon_primitive - Monitoring primitives for given use cases.
 *
 * @init:			Initialize primitive-internal data structures.
 * @update:			Update primitive-internal data structures.
 * @prepare_access_checks:	Prepare next access check of target regions.
 * @check_accesses:		Check the accesses to target regions.
 * @reset_aggregated:		Reset aggregated accesses monitoring results.
 * @apply_scheme:		Apply a DAMON-based operation scheme.
 * @target_valid:		Determine if the target is valid.
 * @cleanup:			Clean up the context.
 *
 * DAMON can be extended for various address spaces and usages.  For this,
 * users should register the low level primitives for their target address
 * space and usecase via the &damon_ctx.primitive.  Then, the monitoring thread
 * (&damon_ctx.kdamond) calls @init and @prepare_access_checks before starting
 * the monitoring, @update after each &damon_ctx.primitive_update_interval, and
 * @check_accesses, @target_valid and @prepare_access_checks after each
 * &damon_ctx.sample_interval.  Finally, @reset_aggregated is called after each
 * &damon_ctx.aggr_interval.
 *
 * @init should initialize primitive-internal data structures.  For example,
 * this could be used to construct proper monitoring target regions and link
 * those to @damon_ctx.adaptive_targets.
 * @update should update the primitive-internal data structures.  For example,
 * this could be used to update monitoring target regions for current status.
 * @prepare_access_checks should manipulate the monitoring regions to be
 * prepared for the next access check.
 * @check_accesses should check the accesses to each region that made after the
 * last preparation and update the number of observed accesses of each region.
 * It should also return max number of observed accesses that made as a result
 * of its update.  The value will be used for regions adjustment threshold.
 * @reset_aggregated should reset the access monitoring results that aggregated
 * by @check_accesses.
 * @apply_scheme is called from @kdamond when a region for user provided
 * DAMON-based operation scheme is found.  It should apply the scheme's action
 * to the region.  This is not used for &DAMON_ARBITRARY_TARGET case.
 * @target_valid should check whether the target is still valid for the
 * monitoring.
 * @cleanup is called from @kdamond just before its termination.
 */
struct damon_primitive {
	void (*init)(struct damon_ctx *context);
	void (*update)(struct damon_ctx *context);
	void (*prepare_access_checks)(struct damon_ctx *context);
	unsigned int (*check_accesses)(struct damon_ctx *context);
	void (*reset_aggregated)(struct damon_ctx *context);
	int (*apply_scheme)(struct damon_ctx *context, struct damon_target *t,
			struct damon_region *r, struct damos *scheme);
	bool (*target_valid)(void *target);
	void (*cleanup)(struct damon_ctx *context);
};

/**
 * struct damon_callback - Monitoring events notification callbacks.
 *
 * @before_start:	Called before starting the monitoring.
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
	int (*after_sampling)(struct damon_ctx *context);
	int (*after_aggregation)(struct damon_ctx *context);
	int (*before_terminate)(struct damon_ctx *context);
};

/**
 * struct damon_ctx - Represents a context for each monitoring.  This is the
 * main interface that allows users to set the attributes and get the results
 * of the monitoring.
 *
 * @sample_interval:		The time between access samplings.
 * @aggr_interval:		The time between monitor results aggregations.
 * @primitive_update_interval:	The time between monitoring primitive updates.
 *
 * For each @sample_interval, DAMON checks whether each region is accessed or
 * not.  It aggregates and keeps the access information (number of accesses to
 * each region) for @aggr_interval time.  DAMON also checks whether the target
 * memory regions need update (e.g., by ``mmap()`` calls from the application,
 * in case of virtual memory monitoring) and applies the changes for each
 * @primitive_update_interval.  All time intervals are in micro-seconds.
 * Please refer to &struct damon_primitive and &struct damon_callback for more
 * detail.
 *
 * @kdamond:		Kernel thread who does the monitoring.
 * @kdamond_stop:	Notifies whether kdamond should stop.
 * @kdamond_lock:	Mutex for the synchronizations with @kdamond.
 *
 * For each monitoring context, one kernel thread for the monitoring is
 * created.  The pointer to the thread is stored in @kdamond.
 *
 * Once started, the monitoring thread runs until explicitly required to be
 * terminated or every monitoring target is invalid.  The validity of the
 * targets is checked via the &damon_primitive.target_valid of @primitive.  The
 * termination can also be explicitly requested by writing non-zero to
 * @kdamond_stop.  The thread sets @kdamond to NULL when it terminates.
 * Therefore, users can know whether the monitoring is ongoing or terminated by
 * reading @kdamond.  Reads and writes to @kdamond and @kdamond_stop from
 * outside of the monitoring thread must be protected by @kdamond_lock.
 *
 * Note that the monitoring thread protects only @kdamond and @kdamond_stop via
 * @kdamond_lock.  Accesses to other fields must be protected by themselves.
 *
 * @primitive:	Set of monitoring primitives for given use cases.
 * @callback:	Set of callbacks for monitoring events notifications.
 *
 * @min_nr_regions:	The minimum number of adaptive monitoring regions.
 * @max_nr_regions:	The maximum number of adaptive monitoring regions.
 * @adaptive_targets:	Head of monitoring targets (&damon_target) list.
 * @schemes:		Head of schemes (&damos) list.
 */
struct damon_ctx {
	unsigned long sample_interval;
	unsigned long aggr_interval;
	unsigned long primitive_update_interval;

/* private: internal use only */
	struct timespec64 last_aggregation;
	struct timespec64 last_primitive_update;

/* public: */
	struct task_struct *kdamond;
	bool kdamond_stop;
	struct mutex kdamond_lock;

	struct damon_primitive primitive;
	struct damon_callback callback;

	unsigned long min_nr_regions;
	unsigned long max_nr_regions;
	struct list_head adaptive_targets;
	struct list_head schemes;
};

#define damon_next_region(r) \
	(container_of(r->list.next, struct damon_region, list))

#define damon_prev_region(r) \
	(container_of(r->list.prev, struct damon_region, list))

#define damon_for_each_region(r, t) \
	list_for_each_entry(r, &t->regions_list, list)

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
inline void damon_insert_region(struct damon_region *r,
		struct damon_region *prev, struct damon_region *next,
		struct damon_target *t);
void damon_add_region(struct damon_region *r, struct damon_target *t);
void damon_destroy_region(struct damon_region *r, struct damon_target *t);

struct damos *damon_new_scheme(
		unsigned long min_sz_region, unsigned long max_sz_region,
		unsigned int min_nr_accesses, unsigned int max_nr_accesses,
		unsigned int min_age_region, unsigned int max_age_region,
		enum damos_action action);
void damon_add_scheme(struct damon_ctx *ctx, struct damos *s);
void damon_destroy_scheme(struct damos *s);

struct damon_target *damon_new_target(unsigned long id);
void damon_add_target(struct damon_ctx *ctx, struct damon_target *t);
void damon_free_target(struct damon_target *t);
void damon_destroy_target(struct damon_target *t);
unsigned int damon_nr_regions(struct damon_target *t);

struct damon_ctx *damon_new_ctx(void);
void damon_destroy_ctx(struct damon_ctx *ctx);
int damon_set_targets(struct damon_ctx *ctx,
		unsigned long *ids, ssize_t nr_ids);
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		unsigned long aggr_int, unsigned long primitive_upd_int,
		unsigned long min_nr_reg, unsigned long max_nr_reg);
int damon_set_schemes(struct damon_ctx *ctx,
			struct damos **schemes, ssize_t nr_schemes);
int damon_nr_running_ctxs(void);

int damon_start(struct damon_ctx **ctxs, int nr_ctxs);
int damon_stop(struct damon_ctx **ctxs, int nr_ctxs);

#endif	/* CONFIG_DAMON */

#ifdef CONFIG_DAMON_VADDR

/* Monitoring primitives for virtual memory address spaces */
void damon_va_init(struct damon_ctx *ctx);
void damon_va_update(struct damon_ctx *ctx);
void damon_va_prepare_access_checks(struct damon_ctx *ctx);
unsigned int damon_va_check_accesses(struct damon_ctx *ctx);
bool damon_va_target_valid(void *t);
void damon_va_cleanup(struct damon_ctx *ctx);
int damon_va_apply_scheme(struct damon_ctx *context, struct damon_target *t,
		struct damon_region *r, struct damos *scheme);
void damon_va_set_primitives(struct damon_ctx *ctx);

#endif	/* CONFIG_DAMON_VADDR */

#endif	/* _DAMON_H */
