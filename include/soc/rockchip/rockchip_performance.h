/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_PERFORMANCE_H
#define __SOC_ROCKCHIP_PERFORMANCE_H

enum {
	ROCKCHIP_PERFORMANCE_LOW = 0,
	ROCKCHIP_PERFORMANCE_NORMAL,
	ROCKCHIP_PERFORMANCE_HIGH
};

#ifdef CONFIG_ROCKCHIP_PERFORMANCE
extern int rockchip_perf_get_level(void);
extern struct cpumask *rockchip_perf_get_cpul_mask(void);
extern struct cpumask *rockchip_perf_get_cpub_mask(void);
extern int rockchip_perf_select_rt_cpu(int prev_cpu, struct cpumask *lowest_mask);
extern bool rockchip_perf_misfit_rt(int cpu);
extern void rockchip_perf_uclamp_sync_util_min_rt_default(void);
#else
static inline int rockchip_perf_get_level(void) { return ROCKCHIP_PERFORMANCE_NORMAL; }
static inline struct cpumask *rockchip_perf_get_cpul_mask(void) { return NULL; };
static inline struct cpumask *rockchip_perf_get_cpub_mask(void) { return NULL; };
static inline int rockchip_perf_select_rt_cpu(int prev_cpu, struct cpumask *lowest_mask)
{
	return prev_cpu;
}
static inline bool rockchip_perf_misfit_rt(int cpu) { return false; }
static inline void rockchip_perf_uclamp_sync_util_min_rt_default(void) {}
#endif

#endif
