/*
 * include/linux/dmapool.h
 *
 * Allocation pools for DMAable (coherent) memory.
 *
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#ifndef LINUX_DMAPOOL_H
#define	LINUX_DMAPOOL_H

#include <asm/io.h>
#include <asm/scatterlist.h>

struct dma_pool *dma_pool_create(const char *name, struct device *dev, 
			size_t size, size_t align, size_t allocation);

void dma_pool_destroy(struct dma_pool *pool);

void *dma_pool_alloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle);

void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);

#endif

