/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PRU-ICSS sub-system specific definitions
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - http://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 */

#ifndef _PRUSS_DRIVER_H_
#define _PRUSS_DRIVER_H_

#include <linux/types.h>

/*
 * enum pruss_mem - PRUSS memory range identifiers
 */
enum pruss_mem {
	PRUSS_MEM_DRAM0 = 0,
	PRUSS_MEM_DRAM1,
	PRUSS_MEM_SHRD_RAM2,
	PRUSS_MEM_MAX,
};

/**
 * struct pruss_mem_region - PRUSS memory region structure
 * @va: kernel virtual address of the PRUSS memory region
 * @pa: physical (bus) address of the PRUSS memory region
 * @size: size of the PRUSS memory region
 */
struct pruss_mem_region {
	void __iomem *va;
	phys_addr_t pa;
	size_t size;
};

/**
 * struct pruss - PRUSS parent structure
 * @dev: pruss device pointer
 * @cfg_regmap: regmap for config region
 * @mem_regions: data for each of the PRUSS memory regions
 */
struct pruss {
	struct device *dev;
	struct regmap *cfg_regmap;
	struct pruss_mem_region mem_regions[PRUSS_MEM_MAX];
};

#endif	/* _PRUSS_DRIVER_H_ */
