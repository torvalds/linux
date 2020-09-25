/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pointer to dma-buf-mapped memory, plus helpers.
 */

#ifndef __DMA_BUF_MAP_H__
#define __DMA_BUF_MAP_H__

#include <linux/io.h>

/**
 * struct dma_buf_map - Pointer to vmap'ed dma-buf memory.
 * @vaddr_iomem:	The buffer's address if in I/O memory
 * @vaddr:		The buffer's address if in system memory
 * @is_iomem:		True if the dma-buf memory is located in I/O
 *			memory, or false otherwise.
 */
struct dma_buf_map {
	union {
		void __iomem *vaddr_iomem;
		void *vaddr;
	};
	bool is_iomem;
};

/* API transition helper */
static inline bool dma_buf_map_is_vaddr(const struct dma_buf_map *map, const void *vaddr)
{
	return !map->is_iomem && (map->vaddr == vaddr);
}

/**
 * dma_buf_map_is_null - Tests for a dma-buf mapping to be NULL
 * @map:	The dma-buf mapping structure
 *
 * Depending on the state of struct dma_buf_map.is_iomem, tests if the
 * mapping is NULL.
 *
 * Returns:
 * True if the mapping is NULL, or false otherwise.
 */
static inline bool dma_buf_map_is_null(const struct dma_buf_map *map)
{
	if (map->is_iomem)
		return !map->vaddr_iomem;
	return !map->vaddr;
}

/**
 * dma_buf_map_is_set - Tests is the dma-buf mapping has been set
 * @map:	The dma-buf mapping structure
 *
 * Depending on the state of struct dma_buf_map.is_iomem, tests if the
 * mapping has been set.
 *
 * Returns:
 * True if the mapping is been set, or false otherwise.
 */
static inline bool dma_buf_map_is_set(const struct dma_buf_map *map)
{
	return !dma_buf_map_is_null(map);
}

/**
 * dma_buf_map_clear - Clears a dma-buf mapping structure
 * @map:	The dma-buf mapping structure
 *
 * Clears all fields to zero; including struct dma_buf_map.is_iomem. So
 * mapping structures that were set to point to I/O memory are reset for
 * system memory. Pointers are cleared to NULL. This is the default.
 */
static inline void dma_buf_map_clear(struct dma_buf_map *map)
{
	if (map->is_iomem) {
		map->vaddr_iomem = NULL;
		map->is_iomem = false;
	} else {
		map->vaddr = NULL;
	}
}

#endif /* __DMA_BUF_MAP_H__ */
