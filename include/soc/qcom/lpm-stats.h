/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2014-2015,2017,2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __ARCH_ARM_MACH_MSM_LPM_STATS_H
#define __ARCH_ARM_MACH_MSM_LPM_STATS_H

#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/types.h>

struct lpm_stats;

#define MAX_STR_LEN 256

struct lifo_stats {
	uint32_t last_in;
	uint32_t first_out;
};

struct lpm_stats {
	char name[MAX_STR_LEN];
	struct level_stats *time_stats;
	uint32_t num_levels;
	struct lifo_stats lifo;
	struct lpm_stats *parent;
	struct list_head sibling;
	struct list_head child;
	struct cpumask mask;
	struct dentry *directory;
	int64_t sleep_time;
	bool is_cpu;
};

#ifdef CONFIG_MSM_IDLE_STATS
struct lpm_stats *lpm_stats_config_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask);
void lpm_stats_cluster_enter(struct lpm_stats *stats, uint32_t index);
void lpm_stats_cluster_exit(struct lpm_stats *stats, uint32_t index,
				bool success);
void lpm_stats_cpu_enter(uint32_t index, uint64_t time);
void lpm_stats_cpu_exit(uint32_t index, uint64_t time, bool success);
void lpm_stats_suspend_enter(void);
void lpm_stats_suspend_exit(void);
#else
static inline struct lpm_stats *lpm_stats_config_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask)
{
	return ERR_PTR(-ENODEV);
}

static inline void lpm_stats_cluster_enter(struct lpm_stats *stats,
						uint32_t index)
{ }

static inline void lpm_stats_cluster_exit(struct lpm_stats *stats,
					uint32_t index, bool success)
{ }

static inline void lpm_stats_cpu_enter(uint32_t index, uint64_t time)
{ }

static inline void lpm_stats_cpu_exit(uint32_t index, bool success,
							uint64_t time)
{ }

static inline void lpm_stats_suspend_enter(void)
{ }

static inline void lpm_stats_suspend_exit(void)
{ }
#endif
#endif  /* __ARCH_ARM_MACH_MSM_LPM_STATS_H */
