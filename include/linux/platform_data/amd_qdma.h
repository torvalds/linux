/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#ifndef _PLATDATA_AMD_QDMA_H
#define _PLATDATA_AMD_QDMA_H

#include <linux/dmaengine.h>

/**
 * struct qdma_queue_info - DMA queue information. This information is used to
 *			    match queue when DMA channel is requested
 * @dir: Channel transfer direction
 */
struct qdma_queue_info {
	enum dma_transfer_direction dir;
};

#define QDMA_FILTER_PARAM(qinfo)	((void *)(qinfo))

struct dma_slave_map;

/**
 * struct qdma_platdata - Platform specific data for QDMA engine
 * @max_mm_channels: Maximum number of MM DMA channels in each direction
 * @device_map: DMA slave map
 * @irq_index: The index of first IRQ
 */
struct qdma_platdata {
	u32			max_mm_channels;
	u32			irq_index;
	struct dma_slave_map	*device_map;
};

#endif /* _PLATDATA_AMD_QDMA_H */
