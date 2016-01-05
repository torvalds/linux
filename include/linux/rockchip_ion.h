/*
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
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

struct ion_phys_data {
	ion_user_handle_t handle;
	unsigned long phys;
	unsigned long size;
};

#define ION_IOC_ROCKCHIP_MAGIC 'R'

/* Get phys addr of the handle specified. */
#define ION_IOC_GET_PHYS	_IOWR(ION_IOC_ROCKCHIP_MAGIC, 0, \
						struct ion_phys_data)

extern struct ion_device *rockchip_ion_dev;

struct ion_client *rockchip_ion_client_create(const char *name);

#endif
