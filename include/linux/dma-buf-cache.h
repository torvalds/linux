/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */
#ifndef _LINUX_DMA_BUF_CACHE_H
#define _LINUX_DMA_BUF_CACHE_H

#include <linux/dma-buf.h>

extern void dma_buf_cache_detach(struct dma_buf *dmabuf,
				 struct dma_buf_attachment *attach);

extern void dma_buf_cache_unmap_attachment(struct dma_buf_attachment *attach,
					   struct sg_table *sg_table,
					   enum dma_data_direction direction);

extern struct dma_buf_attachment *
dma_buf_cache_attach(struct dma_buf *dmabuf, struct device *dev);

extern struct sg_table *
dma_buf_cache_map_attachment(struct dma_buf_attachment *attach,
			     enum dma_data_direction direction);

#ifdef CONFIG_DMABUF_CACHE
/* Replace dma-buf apis to cached apis */
#define dma_buf_attach dma_buf_cache_attach
#define dma_buf_detach dma_buf_cache_detach
#define dma_buf_map_attachment dma_buf_cache_map_attachment
#define dma_buf_unmap_attachment dma_buf_cache_unmap_attachment
#endif

#endif /* _LINUX_DMA_BUF_CACHE_H */
