/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freescale eDMA platform data, ColdFire SoC's family.
 *
 * Copyright (c) 2017 Angelo Dureghello <angelo@sysam.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_PLATFORM_DATA_MCF_EDMA_H__
#define __LINUX_PLATFORM_DATA_MCF_EDMA_H__

struct dma_slave_map;

bool mcf_edma_filter_fn(struct dma_chan *chan, void *param);

#define MCF_EDMA_FILTER_PARAM(ch)	((void *)ch)

/**
 * struct mcf_edma_platform_data - platform specific data for eDMA engine
 *
 * @ver			The eDMA module version.
 * @dma_channels	The number of eDMA channels.
 */
struct mcf_edma_platform_data {
	int dma_channels;
	const struct dma_slave_map *slave_map;
	int slavecnt;
};

#endif /* __LINUX_PLATFORM_DATA_MCF_EDMA_H__ */
