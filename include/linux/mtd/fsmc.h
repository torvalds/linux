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

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/types.h>
#include <linux/mtd/partitions.h>
#include <asm/param.h>

#define FSMC_NAND_BW8		1
#define FSMC_NAND_BW16		2

/*
 * The placement of the Command Latch Enable (CLE) and
 * Address Latch Enable (ALE) is twised around in the
 * SPEAR310 implementation.
 */
#if defined(CONFIG_MACH_SPEAR310)
#define PLAT_NAND_CLE		(1 << 17)
#define PLAT_NAND_ALE		(1 << 16)
#else
#define PLAT_NAND_CLE		(1 << 16)
#define PLAT_NAND_ALE		(1 << 17)
#endif

#define FSMC_MAX_NOR_BANKS	4
#define FSMC_MAX_NAND_BANKS	4

#define FSMC_FLASH_WIDTH8	1
#define FSMC_FLASH_WIDTH16	2

struct fsmc_nor_bank_regs {
	uint32_t ctrl;
	uint32_t ctrl_tim;
};

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

/* ctrl_tim register definitions */

struct fsms_nand_bank_regs {
	uint32_t pc;
	uint32_t sts;
	uint32_t comm;
	uint32_t attrib;
	uint32_t ioata;
	uint32_t ecc1;
	uint32_t ecc2;
	uint32_t ecc3;
};

#define FSMC_NOR_REG_SIZE	0x40

struct fsmc_regs {
	struct fsmc_nor_bank_regs nor_bank_regs[FSMC_MAX_NOR_BANKS];
	uint8_t reserved_1[0x40 - 0x20];
	struct fsms_nand_bank_regs bank_regs[FSMC_MAX_NAND_BANKS];
	uint8_t reserved_2[0xfe0 - 0xc0];
	uint32_t peripid0;			/* 0xfe0 */
	uint32_t peripid1;			/* 0xfe4 */
	uint32_t peripid2;			/* 0xfe8 */
	uint32_t peripid3;			/* 0xfec */
	uint32_t pcellid0;			/* 0xff0 */
	uint32_t pcellid1;			/* 0xff4 */
	uint32_t pcellid2;			/* 0xff8 */
	uint32_t pcellid3;			/* 0xffc */
};

#define FSMC_BUSY_WAIT_TIMEOUT	(1 * HZ)

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
#define FSMC_TCLR_1		(1 << 9)
#define FSMC_TAR_1		(1 << 13)

/* sts register definitions */
#define FSMC_CODE_RDY		(1 << 15)

/* comm register definitions */
#define FSMC_TSET_0		(0 << 0)
#define FSMC_TWAIT_6		(6 << 8)
#define FSMC_THOLD_4		(4 << 16)
#define FSMC_THIZ_1		(1 << 24)

/* peripid2 register definitions */
#define FSMC_REVISION_MSK	(0xf)
#define FSMC_REVISION_SHFT	(0x4)

#define FSMC_VER1		1
#define FSMC_VER2		2
#define FSMC_VER3		3
#define FSMC_VER4		4
#define FSMC_VER5		5
#define FSMC_VER6		6
#define FSMC_VER7		7
#define FSMC_VER8		8

static inline uint32_t get_fsmc_version(struct fsmc_regs *regs)
{
	return (readl(&regs->peripid2) >> FSMC_REVISION_SHFT) &
				FSMC_REVISION_MSK;
}

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
	struct mtd_partition	*partitions;
	unsigned int		nr_partitions;
	unsigned int		options;
	unsigned int		width;
	unsigned int		bank;
	void			(*select_bank)(uint32_t bank, uint32_t busw);
};

extern int __init fsmc_nor_init(struct platform_device *pdev,
		unsigned long base, uint32_t bank, uint32_t width);
extern void __init fsmc_init_board_info(struct platform_device *pdev,
		struct mtd_partition *partitions, unsigned int nr_partitions,
		unsigned int width);

#endif /* __MTD_FSMC_H */
