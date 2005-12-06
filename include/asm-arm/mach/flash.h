/*
 *  linux/include/asm-arm/mach/flash.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASMARM_MACH_FLASH_H
#define ASMARM_MACH_FLASH_H

struct mtd_partition;
struct mtd_info;

/*
 * map_name:	the map probe function name
 * name:	flash device name (eg, as used with mtdparts=)
 * width:	width of mapped device
 * init:	method called at driver/device initialisation
 * exit:	method called at driver/device removal
 * set_vpp:	method called to enable or disable VPP
 * mmcontrol:	method called to enable or disable Sync. Burst Read in OneNAND
 * parts:	optional array of mtd_partitions for static partitioning
 * nr_parts:	number of mtd_partitions for static partitoning
 */
struct flash_platform_data {
	const char	*map_name;
	const char	*name;
	unsigned int	width;
	int		(*init)(void);
	void		(*exit)(void);
	void		(*set_vpp)(int on);
	void		(*mmcontrol)(struct mtd_info *mtd, int sync_read);
	struct mtd_partition *parts;
	unsigned int	nr_parts;
};

#endif
