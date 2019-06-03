/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 */

#ifndef __ASM_ARCH_NAND_H
#define __ASM_ARCH_NAND_H

#include <linux/mtd/partitions.h>

struct mxc_nand_platform_data {
	unsigned int width;	/* data bus width in bytes */
	unsigned int hw_ecc:1;	/* 0 if suppress hardware ECC */
	unsigned int flash_bbt:1; /* set to 1 to use a flash based bbt */
	struct mtd_partition *parts;	/* partition table */
	int nr_parts;			/* size of parts */
};
#endif /* __ASM_ARCH_NAND_H */
