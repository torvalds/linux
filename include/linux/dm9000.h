/* include/linux/dm9000.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for dm9000 platform data
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __DM9000_PLATFORM_DATA
#define __DM9000_PLATFORM_DATA __FILE__

/* IO control flags */

#define DM9000_PLATF_8BITONLY	(0x0001)
#define DM9000_PLATF_16BITONLY	(0x0002)
#define DM9000_PLATF_32BITONLY	(0x0004)

/* platfrom data for platfrom device structure's platfrom_data field */

struct dm9000_plat_data {
	unsigned int	flags;

	/* allow replacement IO routines */

	void	(*inblk)(void __iomem *reg, void *data, int len);
	void	(*outblk)(void __iomem *reg, void *data, int len);
	void	(*dumpblk)(void __iomem *reg, int len);
};

#endif /* __DM9000_PLATFORM_DATA */

