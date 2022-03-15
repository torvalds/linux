/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#ifndef _DMA_HEAPS_H
#define _DMA_HEAPS_H

#if defined(CONFIG_DMABUF_HEAPS_ROCKCHIP)
int rk_dma_heap_cma_setup(void);
#else
static inline int rk_dma_heap_cma_setup(void)
{
	return -ENODEV;
}
#endif
#endif /* _DMA_HEAPS_H */
