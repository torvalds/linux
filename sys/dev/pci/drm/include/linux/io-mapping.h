/* Public domain. */

#ifndef _LINUX_IO_MAPPING_H
#define _LINUX_IO_MAPPING_H

#include <linux/types.h>

struct io_mapping {
	resource_size_t base;
	unsigned long size;
	void *iomem;
};

static inline void *
io_mapping_map_wc(struct io_mapping *map, unsigned long off, unsigned long size)
{
	return ((uint8_t *)map->iomem + off);
}

static inline void
io_mapping_unmap(void *va)
{
}

static inline void *
io_mapping_map_local_wc(struct io_mapping *map, unsigned long off)
{
	return ((uint8_t *)map->iomem + off);
}

static inline void
io_mapping_unmap_local(void *va)
{
}

static inline void *
io_mapping_map_atomic_wc(struct io_mapping *map, unsigned long off)
{
	return ((uint8_t *)map->iomem + off);
}

static inline void
io_mapping_unmap_atomic(void *va)
{
}

#endif
