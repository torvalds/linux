/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#ifndef __SOC_TEGRA_PM_H__
#define __SOC_TEGRA_PM_H__

enum tegra_suspend_mode {
	TEGRA_SUSPEND_NONE = 0,
	TEGRA_SUSPEND_LP2, /* CPU voltage off */
	TEGRA_SUSPEND_LP1, /* CPU voltage off, DRAM self-refresh */
	TEGRA_SUSPEND_LP0, /* CPU + core voltage off, DRAM self-refresh */
	TEGRA_MAX_SUSPEND_MODE,
};

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM)
enum tegra_suspend_mode
tegra_pm_validate_suspend_mode(enum tegra_suspend_mode mode);

/* low-level resume entry point */
void tegra_resume(void);
#else
static inline enum tegra_suspend_mode
tegra_pm_validate_suspend_mode(enum tegra_suspend_mode mode)
{
	return TEGRA_SUSPEND_NONE;
}

static inline void tegra_resume(void)
{
}
#endif /* CONFIG_PM_SLEEP */

#endif /* __SOC_TEGRA_PM_H__ */
