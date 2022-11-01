/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for DAMON Sysfs Interface
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>

extern struct mutex damon_sysfs_lock;

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
		unsigned long min,
		unsigned long max);
void damon_sysfs_ul_range_release(struct kobject *kobj);

extern struct kobj_type damon_sysfs_ul_range_ktype;

/*
 * schemes directory
 */

struct damon_sysfs_schemes {
	struct kobject kobj;
	struct damon_sysfs_scheme **schemes_arr;
	int nr;
};

struct damon_sysfs_schemes *damon_sysfs_schemes_alloc(void);
void damon_sysfs_schemes_rm_dirs(struct damon_sysfs_schemes *schemes);

extern struct kobj_type damon_sysfs_schemes_ktype;

int damon_sysfs_set_schemes(struct damon_ctx *ctx,
		struct damon_sysfs_schemes *sysfs_schemes);

void damon_sysfs_schemes_update_stats(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx);

int damon_sysfs_schemes_update_regions_start(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx);

int damon_sysfs_schemes_update_regions_stop(struct damon_ctx *ctx);

int damon_sysfs_schemes_clear_regions(
		struct damon_sysfs_schemes *sysfs_schemes,
		struct damon_ctx *ctx);
