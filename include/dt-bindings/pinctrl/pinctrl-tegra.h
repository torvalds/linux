/*
 * This header provides constants for Tegra pinctrl bindings.
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _DT_BINDINGS_PINCTRL_TEGRA_H
#define _DT_BINDINGS_PINCTRL_TEGRA_H

/*
 * Enable/disable for diffeent dt properties. This is applicable for
 * properties nvidia,enable-input, nvidia,tristate, nvidia,open-drain,
 * nvidia,lock, nvidia,rcv-sel, nvidia,high-speed-mode, nvidia,schmitt.
 */
#define TEGRA_PIN_DISABLE				0
#define TEGRA_PIN_ENABLE				1

#define TEGRA_PIN_PULL_NONE				0
#define TEGRA_PIN_PULL_DOWN				1
#define TEGRA_PIN_PULL_UP				2

/* Low power mode driver */
#define TEGRA_PIN_LP_DRIVE_DIV_8			0
#define TEGRA_PIN_LP_DRIVE_DIV_4			1
#define TEGRA_PIN_LP_DRIVE_DIV_2			2
#define TEGRA_PIN_LP_DRIVE_DIV_1			3

/* Rising/Falling slew rate */
#define TEGRA_PIN_SLEW_RATE_FASTEST			0
#define TEGRA_PIN_SLEW_RATE_FAST			1
#define TEGRA_PIN_SLEW_RATE_SLOW			2
#define TEGRA_PIN_SLEW_RATE_SLOWEST			3

#endif
