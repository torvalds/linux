/* SPDX-License-Identifier: MIT */
#ifndef _DRM_PAGEMAP_H_
#define _DRM_PAGEMAP_H_

#include <linux/dma-direction.h>
#include <linux/hmm.h>
#include <linux/types.h>

struct drm_pagemap;
struct device;

/**
 * enum drm_interconnect_protocol - Used to identify an interconnect protocol.
 *
 * @DRM_INTERCONNECT_SYSTEM: DMA map is system pages
 * @DRM_INTERCONNECT_DRIVER: DMA map is driver defined
 */
enum drm_interconnect_protocol {
	DRM_INTERCONNECT_SYSTEM,
	DRM_INTERCONNECT_DRIVER,
	/* A driver can add private values beyond DRM_INTERCONNECT_DRIVER */
};

/**
 * struct drm_pagemap_device_addr - Device address representation.
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Note: There is room for improvement here. We should be able to pack into
 * 64 bits.
 */
struct drm_pagemap_device_addr {
	dma_addr_t addr;
	u64 proto : 54;
	u64 order : 8;
	u64 dir : 2;
};

/**
 * drm_pagemap_device_addr_encode() - Encode a dma address with metadata
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the dma mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Return: A struct drm_pagemap_device_addr encoding the above information.
 */
static inline struct drm_pagemap_device_addr
drm_pagemap_device_addr_encode(dma_addr_t addr,
			       enum drm_interconnect_protocol proto,
			       unsigned int order,
			       enum dma_data_direction dir)
{
	return (struct drm_pagemap_device_addr) {
		.addr = addr,
		.proto = proto,
		.order = order,
		.dir = dir,
	};
}

/**
 * struct drm_pagemap_ops: Ops for a drm-pagemap.
 */
struct drm_pagemap_ops {
	/**
	 * @device_map: Map for device access or provide a virtual address suitable for
	 *
	 * @dpagemap: The struct drm_pagemap for the page.
	 * @dev: The device mapper.
	 * @page: The page to map.
	 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
	 * @dir: The transfer direction.
	 */
	struct drm_pagemap_device_addr (*device_map)(struct drm_pagemap *dpagemap,
						     struct device *dev,
						     struct page *page,
						     unsigned int order,
						     enum dma_data_direction dir);

	/**
	 * @device_unmap: Unmap a device address previously obtained using @device_map.
	 *
	 * @dpagemap: The struct drm_pagemap for the mapping.
	 * @dev: The device unmapper.
	 * @addr: The device address obtained when mapping.
	 */
	void (*device_unmap)(struct drm_pagemap *dpagemap,
			     struct device *dev,
			     struct drm_pagemap_device_addr addr);

};

/**
 * struct drm_pagemap: Additional information for a struct dev_pagemap
 * used for device p2p handshaking.
 * @ops: The struct drm_pagemap_ops.
 * @dev: The struct drevice owning the device-private memory.
 */
struct drm_pagemap {
	const struct drm_pagemap_ops *ops;
	struct device *dev;
};

#endif
