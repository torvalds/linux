/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SRAM DMA-Heap exporter && support alloc page and dmabuf on kernel
 *
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */
#ifndef _LINUX_SRAM_HEAP_H
#define _LINUX_SRAM_HEAP_H

#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/mm.h>

#if IS_REACHABLE(CONFIG_DMABUF_HEAPS_SRAM)
struct dma_buf *sram_heap_alloc_dma_buf(size_t size);
struct page *sram_heap_alloc_pages(size_t size);
void sram_heap_free_pages(struct page *p);
void sram_heap_free_dma_buf(struct dma_buf *dmabuf);
void *sram_heap_get_vaddr(struct dma_buf *dmabuf);
phys_addr_t sram_heap_get_paddr(struct dma_buf *dmabuf);

#else
static inline struct dma_buf *sram_heap_alloc_dma_buf(size_t size)
{
	return NULL;
}

static inline struct page *sram_heap_alloc_pages(size_t size)
{
	return NULL;
}

static inline void sram_heap_free_pages(struct page *p) {}
static inline void sram_heap_free_dma_buf(struct dma_buf *dmabuf) {}

static inline void *sram_heap_get_vaddr(struct dma_buf *dmabuf)
{
	return NULL;
}

static inline phys_addr_t sram_heap_get_paddr(struct dma_buf *dmabuf)
{
	return 0;
}
#endif

#endif
