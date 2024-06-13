/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/platform_data/uio_dmem_genirq.h
 *
 * Copyright (C) 2012 Damian Hobson-Garcia
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
