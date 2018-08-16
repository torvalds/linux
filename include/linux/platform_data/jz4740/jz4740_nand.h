/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC NAND controller driver
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __JZ4740_NAND_H__
#define __JZ4740_NAND_H__

#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#define JZ_NAND_NUM_BANKS 4

struct jz_nand_platform_data {
	int			num_partitions;
	struct mtd_partition	*partitions;

	unsigned char banks[JZ_NAND_NUM_BANKS];

	void (*ident_callback)(struct platform_device *, struct mtd_info *,
				struct mtd_partition **, int *num_partitions);
};

#endif
