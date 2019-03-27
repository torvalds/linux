/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _LINUX_DMAPOOL_H_
#define	_LINUX_DMAPOOL_H_

#include <linux/types.h>
#include <linux/io.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/slab.h>

struct dma_pool {
	uma_zone_t	pool_zone;
};

static inline struct dma_pool *
dma_pool_create(char *name, struct device *dev, size_t size,
    size_t align, size_t boundary)
{
	struct dma_pool *pool;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	align--;
	/*
	 * XXX Eventually this could use a separate allocf to honor boundary
	 * and physical address requirements of the device.
	 */
	pool->pool_zone = uma_zcreate(name, size, NULL, NULL, NULL, NULL,
	    align, UMA_ZONE_OFFPAGE|UMA_ZONE_HASH);

	return (pool);
}

static inline void
dma_pool_destroy(struct dma_pool *pool)
{
	uma_zdestroy(pool->pool_zone);
	kfree(pool);
}

static inline void *
dma_pool_alloc(struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{
	void *vaddr;

	vaddr = uma_zalloc(pool->pool_zone, mem_flags);
	if (vaddr)
		*handle = vtophys(vaddr);
	return (vaddr);
}

static inline void *
dma_pool_zalloc(struct dma_pool *pool, gfp_t mem_flags, dma_addr_t *handle)
{

	return (dma_pool_alloc(pool, mem_flags | __GFP_ZERO, handle));
}

static inline void
dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr)
{
	uma_zfree(pool->pool_zone, vaddr);
}


#endif /* _LINUX_DMAPOOL_H_ */
