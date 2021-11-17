/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_PXA3XX_NAND_H
#define __ASM_ARCH_PXA3XX_NAND_H

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

/*
 * Current pxa3xx_nand controller has two chip select which both be workable but
 * historically all platforms remaining on platform data used only one. Switch
 * to device tree if you need more.
 */
struct pxa3xx_nand_platform_data {
	/* Keep OBM/bootloader NFC timing configuration */
	bool keep_config;
	/* Use a flash-based bad block table */
	bool flash_bbt;
	/* Requested ECC strength and ECC step size */
	int ecc_strength, ecc_step_size;
	/* Partitions */
	const struct mtd_partition *parts;
	unsigned int nr_parts;
};

extern void pxa3xx_set_nand_info(struct pxa3xx_nand_platform_data *info);

#endif /* __ASM_ARCH_PXA3XX_NAND_H */
