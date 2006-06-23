/*
 * For boards with physically mapped flash and using
 * drivers/mtd/maps/physmap.c mapping driver.
 *
 * $Id: physmap.h,v 1.4 2005/11/07 11:14:55 gleixner Exp $
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __LINUX_MTD_PHYSMAP__
#define __LINUX_MTD_PHYSMAP__

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

struct physmap_flash_data {
	unsigned int		width;
	void			(*set_vpp)(struct map_info *, int);
	unsigned int		nr_parts;
	struct mtd_partition	*parts;
};

/*
 * Board needs to specify the exact mapping during their setup time.
 */
void physmap_configure(unsigned long addr, unsigned long size,
		int bankwidth, void (*set_vpp)(struct map_info *, int) );

#ifdef CONFIG_MTD_PARTITIONS

/*
 * Machines that wish to do flash partition may want to call this function in
 * their setup routine.
 *
 *	physmap_set_partitions(mypartitions, num_parts);
 *
 * Note that one can always override this hard-coded partition with
 * command line partition (you need to enable CONFIG_MTD_CMDLINE_PARTS).
 */
void physmap_set_partitions(struct mtd_partition *parts, int num_parts);

#endif /* defined(CONFIG_MTD_PARTITIONS) */

#endif /* __LINUX_MTD_PHYSMAP__ */
