/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mach-davinci/nand.h
 *
 * Copyright © 2006 Texas Instruments.
 *
 * Ported to 2.6.23 Copyright © 2008 by
 *   Sander Huijsen <Shuijsen@optelecom-nkf.com>
 *   Troy Kisky <troy.kisky@boundarydevices.com>
 *   Dirk Behme <Dirk.Behme@gmail.com>
 *
 * --------------------------------------------------------------------------
 */

#ifndef __ARCH_ARM_DAVINCI_NAND_H
#define __ARCH_ARM_DAVINCI_NAND_H

#include <linux/mtd/rawnand.h>

#define NANDFCR_OFFSET		0x60
#define NANDFSR_OFFSET		0x64
#define NANDF1ECC_OFFSET	0x70

/* 4-bit ECC syndrome registers */
#define NAND_4BIT_ECC_LOAD_OFFSET	0xbc
#define NAND_4BIT_ECC1_OFFSET		0xc0
#define NAND_4BIT_ECC2_OFFSET		0xc4
#define NAND_4BIT_ECC3_OFFSET		0xc8
#define NAND_4BIT_ECC4_OFFSET		0xcc
#define NAND_ERR_ADD1_OFFSET		0xd0
#define NAND_ERR_ADD2_OFFSET		0xd4
#define NAND_ERR_ERRVAL1_OFFSET		0xd8
#define NAND_ERR_ERRVAL2_OFFSET		0xdc

/* NOTE:  boards don't need to use these address bits
 * for ALE/CLE unless they support booting from NAND.
 * They're used unless platform data overrides them.
 */
#define	MASK_ALE		0x08
#define	MASK_CLE		0x10

struct davinci_nand_pdata {		/* platform_data */
	uint32_t		mask_ale;
	uint32_t		mask_cle;

	/*
	 * 0-indexed chip-select number of the asynchronous
	 * interface to which the NAND device has been connected.
	 *
	 * So, if you have NAND connected to CS3 of DA850, you
	 * will pass '1' here. Since the asynchronous interface
	 * on DA850 starts from CS2.
	 */
	uint32_t		core_chipsel;

	/* for packages using two chipselects */
	uint32_t		mask_chipsel;

	/* board's default static partition info */
	struct mtd_partition	*parts;
	unsigned		nr_parts;

	/* none  == NAND_ECC_NONE (strongly *not* advised!!)
	 * soft  == NAND_ECC_SOFT
	 * else  == NAND_ECC_HW, according to ecc_bits
	 *
	 * All DaVinci-family chips support 1-bit hardware ECC.
	 * Newer ones also support 4-bit ECC, but are awkward
	 * using it with large page chips.
	 */
	nand_ecc_modes_t	ecc_mode;
	u8			ecc_bits;

	/* e.g. NAND_BUSWIDTH_16 */
	unsigned		options;
	/* e.g. NAND_BBT_USE_FLASH */
	unsigned		bbt_options;

	/* Main and mirror bbt descriptor overrides */
	struct nand_bbt_descr	*bbt_td;
	struct nand_bbt_descr	*bbt_md;

	/* Access timings */
	struct davinci_aemif_timing	*timing;
};

#endif	/* __ARCH_ARM_DAVINCI_NAND_H */
