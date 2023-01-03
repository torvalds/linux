/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for DAMON Modules
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/moduleparam.h>

#define DEFINE_DAMON_MODULES_MON_ATTRS_PARAMS(attrs)			\
	module_param_named(sample_interval, attrs.sample_interval,	\
			ulong, 0600);					\
	module_param_named(aggr_interval, attrs.aggr_interval, ulong,	\
			0600);						\
	module_param_named(min_nr_regions, attrs.min_nr_regions, ulong,	\
			0600);						\
	module_param_named(max_nr_regions, attrs.max_nr_regions, ulong,	\
			0600);

#define DEFINE_DAMON_MODULES_DAMOS_TIME_QUOTA(quota)			\
	module_param_named(quota_ms, quota.ms, ulong, 0600);		\
	module_param_named(quota_reset_interval_ms,			\
			quota.reset_interval, ulong, 0600);

#define DEFINE_DAMON_MODULES_DAMOS_QUOTAS(quota)			\
	DEFINE_DAMON_MODULES_DAMOS_TIME_QUOTA(quota)			\
	module_param_named(quota_sz, quota.sz, ulong, 0600);

#define DEFINE_DAMON_MODULES_WMARKS_PARAMS(wmarks)			\
	module_param_named(wmarks_interval, wmarks.interval, ulong,	\
			0600);						\
	module_param_named(wmarks_high, wmarks.high, ulong, 0600);	\
	module_param_named(wmarks_mid, wmarks.mid, ulong, 0600);	\
	module_param_named(wmarks_low, wmarks.low, ulong, 0600);

#define DEFINE_DAMON_MODULES_DAMOS_STATS_PARAMS(stat, try_name,		\
		succ_name, qt_exceed_name)				\
	module_param_named(nr_##try_name, stat.nr_tried, ulong, 0400);	\
	module_param_named(bytes_##try_name, stat.sz_tried, ulong,	\
			0400);						\
	module_param_named(nr_##succ_name, stat.nr_applied, ulong,	\
			0400);						\
	module_param_named(bytes_##succ_name, stat.sz_applied, ulong,	\
			0400);						\
	module_param_named(nr_##qt_exceed_name, stat.qt_exceeds, ulong,	\
			0400);

int damon_modules_new_paddr_ctx_target(struct damon_ctx **ctxp,
		struct damon_target **targetp);
