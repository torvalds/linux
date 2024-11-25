/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_LPM_LEVEL_H__
#define __SOC_QCOM_LPM_LEVEL_H__

#include <linux/errno.h>
#include <linux/types.h>

struct cpumask;

struct system_pm_ops {
	int (*enter)(struct cpumask *mask);
	void (*exit)(bool success);
	int (*update_wakeup)(bool b);
	bool (*sleep_allowed)(void);
};

#if defined(CONFIG_MSM_PM) || defined(CONFIG_MSM_PM_LEGACY)
uint32_t register_system_pm_ops(struct system_pm_ops *pm_ops);
void update_ipi_history(int cpu);
#else
static inline uint32_t register_system_pm_ops(struct system_pm_ops *pm_ops)
{ return -ENODEV; }
static inline void update_ipi_history(int cpu) {}
#endif

#endif
