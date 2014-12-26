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

#define ROCKCHIP_ION_VERSION	"v1.1"

/*
 * ion_heap_ids order by ion_heap_type
 */
enum ion_heap_ids {
       ION_VMALLOC_HEAP_ID = 0,
       ION_CARVEOUT_HEAP_ID = 2,
       ION_CMA_HEAP_ID = 4,
       ION_DRM_HEAP_ID = 5,
};

#define ION_HEAP(bit) (1 << (bit))

struct ion_phys_data {
	ion_user_handle_t handle;
	unsigned long phys;
	unsigned long size;
};

#define ION_IOC_ROCKCHIP_MAGIC 'R'

/**
 * Get phys addr of the handle specified.
 */
#define ION_IOC_GET_PHYS	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 0, \
						struct ion_phys_data)

#endif
