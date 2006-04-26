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


#if defined(CONFIG_MTD_PHYSMAP)

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

/*
 * The map_info for physmap.  Board can override size, buswidth, phys,
 * (*set_vpp)(), etc in their initial setup routine.
 */
extern struct map_info physmap_map;

/*
 * Board needs to specify the exact mapping during their setup time.
 */
static inline void physmap_configure(unsigned long addr, unsigned long size, int bankwidth, void (*set_vpp)(struct map_info *, int) )
{
	physmap_map.phys = addr;
	physmap_map.size = size;
	physmap_map.bankwidth = bankwidth;
	physmap_map.set_vpp = set_vpp;
}

#if defined(CONFIG_MTD_PARTITIONS)

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
#endif /* defined(CONFIG_MTD) */

#endif /* __LINUX_MTD_PHYSMAP__ */

