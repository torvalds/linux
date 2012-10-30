/*
 * spi-tegra.h: SPI interface for Nvidia Tegra20 SLINK controller.
 *
 * Copyright (C) 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _LINUX_SPI_TEGRA_H
#define _LINUX_SPI_TEGRA_H

struct tegra_spi_platform_data {
	int dma_req_sel;
	unsigned int spi_max_frequency;
};

/*
 * Controller data from device to pass some info like
 * hw based chip select can be used or not and if yes
 * then CS hold and setup time.
 */
struct tegra_spi_device_controller_data {
	bool is_hw_based_cs;
	int cs_setup_clk_count;
	int cs_hold_clk_count;
};

#endif /* _LINUX_SPI_TEGRA_H */
