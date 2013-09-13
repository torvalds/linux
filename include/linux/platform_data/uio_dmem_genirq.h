/*
 * include/linux/platform_data/uio_dmem_genirq.h
 *
 * Copyright (C) 2012 Damian Hobson-Garcia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _UIO_DMEM_GENIRQ_H
#define _UIO_DMEM_GENIRQ_H

#include <linux/uio_driver.h>

struct uio_dmem_genirq_pdata {
	struct uio_info	uioinfo;
	unsigned int *dynamic_region_sizes;
	unsigned int num_dynamic_regions;
};
#endif /* _UIO_DMEM_GENIRQ_H */
