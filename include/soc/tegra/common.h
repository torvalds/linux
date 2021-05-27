/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 NVIDIA Corporation
 */

#ifndef __SOC_TEGRA_COMMON_H__
#define __SOC_TEGRA_COMMON_H__

#include <linux/types.h>

#ifdef CONFIG_ARCH_TEGRA
bool soc_is_tegra(void);
#else
static inline bool soc_is_tegra(void)
{
	return false;
}
#endif

#endif /* __SOC_TEGRA_COMMON_H__ */
