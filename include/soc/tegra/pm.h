/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#ifndef __SOC_TEGRA_PM_H__
#define __SOC_TEGRA_PM_H__

#include <linux/errno.h>

enum tegra_suspend_mode {
	TEGRA_SUSPEND_NONE = 0,
	TEGRA_SUSPEND_LP2, /* CPU voltage off */
	TEGRA_SUSPEND_LP1, /* CPU voltage off, DRAM self-refresh */
	TEGRA_SUSPEND_LP0, /* CPU + core voltage off, DRAM self-refresh */
	TEGRA_MAX_SUSPEND_MODE,
	TEGRA_SUSPEND_NOT_READY,
};

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM) && defined(CONFIG_ARCH_TEGRA)
enum tegra_suspend_mode
tegra_pm_validate_suspend_mode(enum tegra_suspend_mode mode);

/* low-level resume entry point */
void tegra_resume(void);

int tegra30_pm_secondary_cpu_suspend(unsigned long arg);
void tegra_pm_clear_cpu_in_lp2(void);
void tegra_pm_set_cpu_in_lp2(void);
int tegra_pm_enter_lp2(void);
int tegra_pm_park_secondary_cpu(unsigned long cpu);
void tegra_pm_init_suspend(void);
#else
static inline enum tegra_suspend_mode
tegra_pm_validate_suspend_mode(enum tegra_suspend_mode mode)
{
	return TEGRA_SUSPEND_NONE;
}

static inline void tegra_resume(void)
{
}

static inline int tegra30_pm_secondary_cpu_suspend(unsigned long arg)
{
	return -ENOTSUPP;
}

static inline void tegra_pm_clear_cpu_in_lp2(void)
{
}

static inline void tegra_pm_set_cpu_in_lp2(void)
{
}

static inline int tegra_pm_enter_lp2(void)
{
	return -ENOTSUPP;
}

static inline int tegra_pm_park_secondary_cpu(unsigned long cpu)
{
	return -ENOTSUPP;
}

static inline void tegra_pm_init_suspend(void)
{
}
#endif /* CONFIG_PM_SLEEP */

#endif /* __SOC_TEGRA_PM_H__ */
