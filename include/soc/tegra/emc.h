/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 NVIDIA Corporation. All rights reserved.
 */

#ifndef __SOC_TEGRA_EMC_H__
#define __SOC_TEGRA_EMC_H__

struct tegra_emc;

int tegra_emc_prepare_timing_change(struct tegra_emc *emc,
				    unsigned long rate);
void tegra_emc_complete_timing_change(struct tegra_emc *emc,
				      unsigned long rate);

#endif /* __SOC_TEGRA_EMC_H__ */
