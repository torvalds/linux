/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_THERMAL_PAUSE_H
#define _LINUX_THERMAL_PAUSE_H

#include <linux/notifier.h>
#include <linux/cpumask.h>

#if IS_ENABLED(CONFIG_QTI_CPU_PAUSE_COOLING_DEVICE)
extern void thermal_pause_notifier_register(struct notifier_block *n);
extern void thermal_pause_notifier_unregister(struct notifier_block *n);
extern const struct cpumask *thermal_paused_cpumask(void);
#else
static inline
void thermal_pause_notifier_register(struct notifier_block *n)
{
}

static inline
void thermal_pause_notifier_unregister(struct notifier_block *n)
{
}

static inline const struct cpumask *thermal_paused_cpumask(void)
{
	return cpu_none_mask;
}
#endif /* CONFIG_QTI_CPU_PAUSE_COOLING_DEVICE */

#endif /* _LINUX_THERMAL_PAUSE_H */
