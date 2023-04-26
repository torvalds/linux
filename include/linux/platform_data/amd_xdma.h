/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022, Advanced Micro Devices, Inc.
 */

#ifndef _PLATDATA_AMD_XDMA_H
#define _PLATDATA_AMD_XDMA_H

#include <linux/dmaengine.h>

/**
 * struct xdma_chan_info - DMA channel information
 *	This information is used to match channel when request dma channel
 * @dir: Channel transfer direction
 */
struct xdma_chan_info {
	enum dma_transfer_direction dir;
};

#define XDMA_FILTER_PARAM(chan_info)	((void *)(chan_info))

struct dma_slave_map;

/**
 * struct xdma_platdata - platform specific data for XDMA engine
 * @max_dma_channels: Maximum dma channels in each direction
 */
struct xdma_platdata {
	u32 max_dma_channels;
	u32 device_map_cnt;
	struct dma_slave_map *device_map;
};

#endif /* _PLATDATA_AMD_XDMA_H */
