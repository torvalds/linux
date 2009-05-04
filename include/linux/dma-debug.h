/*
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __DMA_DEBUG_H
#define __DMA_DEBUG_H

#include <linux/types.h>

struct device;
struct scatterlist;
struct bus_type;

#ifdef CONFIG_DMA_API_DEBUG

extern void dma_debug_add_bus(struct bus_type *bus);

extern void dma_debug_init(u32 num_entries);

extern void debug_dma_map_page(struct device *dev, struct page *page,
			       size_t offset, size_t size,
			       int direction, dma_addr_t dma_addr,
			       bool map_single);

extern void debug_dma_unmap_page(struct device *dev, dma_addr_t addr,
				 size_t size, int direction, bool map_single);

extern void debug_dma_map_sg(struct device *dev, struct scatterlist *sg,
			     int nents, int mapped_ents, int direction);

extern void debug_dma_unmap_sg(struct device *dev, struct scatterlist *sglist,
			       int nelems, int dir);

extern void debug_dma_alloc_coherent(struct device *dev, size_t size,
				     dma_addr_t dma_addr, void *virt);

extern void debug_dma_free_coherent(struct device *dev, size_t size,
				    void *virt, dma_addr_t addr);

extern void debug_dma_sync_single_for_cpu(struct device *dev,
					  dma_addr_t dma_handle, size_t size,
					  int direction);

extern void debug_dma_sync_single_for_device(struct device *dev,
					     dma_addr_t dma_handle,
					     size_t size, int direction);

extern void debug_dma_sync_single_range_for_cpu(struct device *dev,
						dma_addr_t dma_handle,
						unsigned long offset,
						size_t size,
						int direction);

extern void debug_dma_sync_single_range_for_device(struct device *dev,
						   dma_addr_t dma_handle,
						   unsigned long offset,
						   size_t size, int direction);

extern void debug_dma_sync_sg_for_cpu(struct device *dev,
				      struct scatterlist *sg,
				      int nelems, int direction);

extern void debug_dma_sync_sg_for_device(struct device *dev,
					 struct scatterlist *sg,
					 int nelems, int direction);

extern void debug_dma_dump_mappings(struct device *dev);

#else /* CONFIG_DMA_API_DEBUG */

static inline void dma_debug_add_bus(struct bus_type *bus)
{
}

static inline void dma_debug_init(u32 num_entries)
{
}

static inline void debug_dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      int direction, dma_addr_t dma_addr,
				      bool map_single)
{
}

static inline void debug_dma_unmap_page(struct device *dev, dma_addr_t addr,
					size_t size, int direction,
					bool map_single)
{
}

static inline void debug_dma_map_sg(struct device *dev, struct scatterlist *sg,
				    int nents, int mapped_ents, int direction)
{
}

static inline void debug_dma_unmap_sg(struct device *dev,
				      struct scatterlist *sglist,
				      int nelems, int dir)
{
}

static inline void debug_dma_alloc_coherent(struct device *dev, size_t size,
					    dma_addr_t dma_addr, void *virt)
{
}

static inline void debug_dma_free_coherent(struct device *dev, size_t size,
					   void *virt, dma_addr_t addr)
{
}

static inline void debug_dma_sync_single_for_cpu(struct device *dev,
						 dma_addr_t dma_handle,
						 size_t size, int direction)
{
}

static inline void debug_dma_sync_single_for_device(struct device *dev,
						    dma_addr_t dma_handle,
						    size_t size, int direction)
{
}

static inline void debug_dma_sync_single_range_for_cpu(struct device *dev,
						       dma_addr_t dma_handle,
						       unsigned long offset,
						       size_t size,
						       int direction)
{
}

static inline void debug_dma_sync_single_range_for_device(struct device *dev,
							  dma_addr_t dma_handle,
							  unsigned long offset,
							  size_t size,
							  int direction)
{
}

static inline void debug_dma_sync_sg_for_cpu(struct device *dev,
					     struct scatterlist *sg,
					     int nelems, int direction)
{
}

static inline void debug_dma_sync_sg_for_device(struct device *dev,
						struct scatterlist *sg,
						int nelems, int direction)
{
}

static inline void debug_dma_dump_mappings(struct device *dev)
{
}

#endif /* CONFIG_DMA_API_DEBUG */

#endif /* __DMA_DEBUG_H */
