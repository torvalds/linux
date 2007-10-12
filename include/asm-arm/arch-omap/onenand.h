/*
 * include/asm-arm/arch-omap/onenand.h
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Juha Yrjola
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mtd/partitions.h>

struct omap_onenand_platform_data {
	int			cs;
	int			gpio_irq;
	struct mtd_partition	*parts;
	int			nr_parts;
	int                     (*onenand_setup)(void __iomem *);
	int			dma_channel;
};
