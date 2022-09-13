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
