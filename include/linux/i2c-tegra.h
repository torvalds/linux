/*
 * drivers/i2c/busses/i2c-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_I2C_TEGRA_H
#define _LINUX_I2C_TEGRA_H

struct tegra_i2c_platform_data {
	unsigned long bus_clk_rate;
};

#endif /* _LINUX_I2C_TEGRA_H */
