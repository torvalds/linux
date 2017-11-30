/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_PXA3XX_NAND_H
#define __ASM_ARCH_PXA3XX_NAND_H

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

/*
 * Current pxa3xx_nand controller has two chip select which
 * both be workable.
 *
 * Notice should be taken that:
 * When you want to use this feature, you should not enable the
 * keep configuration feature, for two chip select could be
 * attached with different nand chip. The different page size
 * and timing requirement make the keep configuration impossible.
 */

/* The max num of chip select current support */
#define NUM_CHIP_SELECT		(2)
struct pxa3xx_nand_platform_data {

	/* the data flash bus is shared between the Static Memory
	 * Controller and the Data Flash Controller,  the arbiter
	 * controls the ownership of the bus
	 */
	int	enable_arbiter;

	/* allow platform code to keep OBM/bootloader defined NFC config */
	int	keep_config;

	/* indicate how many chip selects will be used */
	int	num_cs;

	/* use an flash-based bad block table */
	bool	flash_bbt;

	/* requested ECC strength and ECC step size */
	int ecc_strength, ecc_step_size;

	const struct mtd_partition		*parts[NUM_CHIP_SELECT];
	unsigned int				nr_parts[NUM_CHIP_SELECT];
};

extern void pxa3xx_set_nand_info(struct pxa3xx_nand_platform_data *info);
#endif /* __ASM_ARCH_PXA3XX_NAND_H */
