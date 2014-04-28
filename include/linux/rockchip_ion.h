/*
 *
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_ROCKCHIP_ION_H
#define _LINUX_ROCKCHIP_ION_H

#ifdef __KERNEL__
#include "../../drivers/staging/android/ion/ion.h"
#else
#include <linux/ion.h>
#endif

#define ROCKCHIP_ION_VERSION	"v1.0"

enum ion_heap_ids {
	INVALID_HEAP_ID = -1,
	ION_CMA_HEAP_ID = 1,
	ION_IOMMU_HEAP_ID,
	ION_VMALLOC_HEAP_ID,
	ION_DRM_HEAP_ID,
	ION_CARVEOUT_HEAP_ID,

	ION_HEAP_ID_RESERVED = 31
};

#define ION_HEAP(bit) (1 << (bit))

#define ION_CMA_HEAP_NAME		"cma"
#define ION_IOMMU_HEAP_NAME		"iommu"
#define ION_VMALLOC_HEAP_NAME	"vmalloc"
#define ION_DRM_HEAP_NAME		"drm"
#define ION_CARVEOUT_HEAP_NAME	"carveout"

#define ION_SET_CACHED(__cache)		(__cache | ION_FLAG_CACHED)
#define ION_SET_UNCACHED(__cache)	(__cache & ~ION_FLAG_CACHED)

#define ION_IS_CACHED(__flags)	((__flags) & ION_FLAG_CACHED)

/* struct ion_flush_data - data passed to ion for flushing caches
 *
 * @handle:	handle with data to flush
 * @fd:		fd to flush
 * @vaddr:	userspace virtual address mapped with mmap
 * @offset:	offset into the handle to flush
 * @length:	length of handle to flush
 *
 * Performs cache operations on the handle. If p is the start address
 * of the handle, p + offset through p + offset + length will have
 * the cache operations performed
 */
struct ion_flush_data {
	ion_user_handle_t handle;
	int fd;
	void *vaddr;
	unsigned int offset;
	unsigned int length;
};

struct ion_phys_data {
	ion_user_handle_t handle;
	unsigned long phys;
	unsigned long size;
};

struct ion_share_id_data {
	int fd;
	unsigned int id;
};

#define ION_IOC_ROCKCHIP_MAGIC 'R'

/**
 * Clean the caches of the handle specified.
 */
#define ION_IOC_CLEAN_CACHES	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 0, \
						struct ion_flush_data)
/**
 * Invalidate the caches of the handle specified.
 */
#define ION_IOC_INV_CACHES	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 1, \
						struct ion_flush_data)
/**
 * Clean and invalidate the caches of the handle specified.
 */
#define ION_IOC_CLEAN_INV_CACHES	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 2, \
						struct ion_flush_data)

/**
 * Get phys addr of the handle specified.
 */
#define ION_IOC_GET_PHYS	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 3, \
						struct ion_phys_data)

/**
 * Get share object of the fd specified.
 */
#define ION_IOC_GET_SHARE_ID	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 4, \
						struct ion_share_id_data)

/**
 * Set share object and associate new fd.
 */
#define ION_IOC_SHARE_BY_ID	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 5, \
						struct ion_share_id_data)

#endif
