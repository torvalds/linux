/*
 * incude/mtd/fsmc.h
 *
 * ST Microelectronics
 * Flexible Static Memory Controller (FSMC)
 * platform data interface and header file
 *
 * Copyright © 2010 ST Microelectronics
 * Vipin Kumar <vipin.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MTD_FSMC_H
#define __MTD_FSMC_H

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/types.h>
#include <linux/mtd/partitions.h>
#include <asm/param.h>

#define FSMC_NAND_BW8		1
#define FSMC_NAND_BW16		2

#define FSMC_MAX_NOR_BANKS	4
#define FSMC_MAX_NAND_BANKS	4

#define FSMC_FLASH_WIDTH8	1
#define FSMC_FLASH_WIDTH16	2

/* fsmc controller registers for NOR flash */
#define CTRL			0x0
	/* ctrl register definitions */
	#define BANK_ENABLE		(1 << 0)
	#define MUXED			(1 << 1)
	#define NOR_DEV			(2 << 2)
	#define WIDTH_8			(0 << 4)
	#define WIDTH_16		(1 << 4)
	#define RSTPWRDWN		(1 << 6)
	#define WPROT			(1 << 7)
	#define WRT_ENABLE		(1 << 12)
	#define WAIT_ENB		(1 << 13)

#define CTRL_TIM		0x4
	/* ctrl_tim register definitions */

#define FSMC_NOR_BANK_SZ	0x8
#define FSMC_NOR_REG_SIZE	0x40

#define FSMC_NOR_REG(base, bank, reg)		(base + \
						FSMC_NOR_BANK_SZ * (bank) + \
						reg)

/* fsmc controller registers for NAND flash */
#define PC			0x00
	/* pc register definitions */
	#define FSMC_RESET		(1 << 0)
	#define FSMC_WAITON		(1 << 1)
	#define FSMC_ENABLE		(1 << 2)
	#define FSMC_DEVTYPE_NAND	(1 << 3)
	#define FSMC_DEVWID_8		(0 << 4)
	#define FSMC_DEVWID_16		(1 << 4)
	#define FSMC_ECCEN		(1 << 6)
	#define FSMC_ECCPLEN_512	(0 << 7)
	#define FSMC_ECCPLEN_256	(1 << 7)
	#define FSMC_TCLR_1		(1)
	#define FSMC_TCLR_SHIFT		(9)
	#define FSMC_TCLR_MASK		(0xF)
	#define FSMC_TAR_1		(1)
	#define FSMC_TAR_SHIFT		(13)
	#define FSMC_TAR_MASK		(0xF)
#define STS			0x04
	/* sts register definitions */
	#define FSMC_CODE_RDY		(1 << 15)
#define COMM			0x08
	/* comm register definitions */
	#define FSMC_TSET_0		0
	#define FSMC_TSET_SHIFT		0
	#define FSMC_TSET_MASK		0xFF
	#define FSMC_TWAIT_6		6
	#define FSMC_TWAIT_SHIFT	8
	#define FSMC_TWAIT_MASK		0xFF
	#define FSMC_THOLD_4		4
	#define FSMC_THOLD_SHIFT	16
	#define FSMC_THOLD_MASK		0xFF
	#define FSMC_THIZ_1		1
	#define FSMC_THIZ_SHIFT		24
	#define FSMC_THIZ_MASK		0xFF
#define ATTRIB			0x0C
#define IOATA			0x10
#define ECC1			0x14
#define ECC2			0x18
#define ECC3			0x1C
#define FSMC_NAND_BANK_SZ	0x20

#define FSMC_NAND_REG(base, bank, reg)		(base + FSMC_NOR_REG_SIZE + \
						(FSMC_NAND_BANK_SZ * (bank)) + \
						reg)

#define FSMC_BUSY_WAIT_TIMEOUT	(1 * HZ)

/*
 * There are 13 bytes of ecc for every 512 byte block in FSMC version 8
 * and it has to be read consecutively and immediately after the 512
 * byte data block for hardware to generate the error bit offsets
 * Managing the ecc bytes in the following way is easier. This way is
 * similar to oobfree structure maintained already in u-boot nand driver
 */
#define MAX_ECCPLACE_ENTRIES	32

struct fsmc_nand_eccplace {
	uint8_t offset;
	uint8_t length;
};

struct fsmc_eccplace {
	struct fsmc_nand_eccplace eccplace[MAX_ECCPLACE_ENTRIES];
};

struct fsmc_nand_timings {
	uint8_t tclr;
	uint8_t tar;
	uint8_t thiz;
	uint8_t thold;
	uint8_t twait;
	uint8_t tset;
};

enum access_mode {
	USE_DMA_ACCESS = 1,
	USE_WORD_ACCESS,
};

/**
 * fsmc_nand_platform_data - platform specific NAND controller config
 * @partitions: partition table for the platform, use a default fallback
 * if this is NULL
 * @nr_partitions: the number of partitions in the previous entry
 * @options: different options for the driver
 * @width: bus width
 * @bank: default bank
 * @select_bank: callback to select a certain bank, this is
 * platform-specific. If the controller only supports one bank
 * this may be set to NULL
 */
struct fsmc_nand_platform_data {
	struct fsmc_nand_timings *nand_timings;
	struct mtd_partition	*partitions;
	unsigned int		nr_partitions;
	unsigned int		options;
	unsigned int		width;
	unsigned int		bank;

	enum access_mode	mode;

	void			(*select_bank)(uint32_t bank, uint32_t busw);

	/* priv structures for dma accesses */
	void			*read_dma_priv;
	void			*write_dma_priv;
};

extern int __init fsmc_nor_init(struct platform_device *pdev,
		unsigned long base, uint32_t bank, uint32_t width);
extern void __init fsmc_init_board_info(struct platform_device *pdev,
		struct mtd_partition *partitions, unsigned int nr_partitions,
		unsigned int width);

#endif /* __MTD_FSMC_H */
