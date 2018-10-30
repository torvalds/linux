/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides Tegra210-specific constants for binding
 * nvidia,tegra210-car.
 */

#ifndef _DT_BINDINGS_RESET_TEGRA210_CAR_H
#define _DT_BINDINGS_RESET_TEGRA210_CAR_H

#define TEGRA210_RESET(x)		(7 * 32 + (x))
#define TEGRA210_RST_DFLL_DVCO		TEGRA210_RESET(0)
#define TEGRA210_RST_ADSP		TEGRA210_RESET(1)

#endif	/* _DT_BINDINGS_RESET_TEGRA210_CAR_H */
