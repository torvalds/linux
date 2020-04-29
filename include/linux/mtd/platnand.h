/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 *			  Steven J. Hill <sjhill@realitydiluted.com>
 *			  Thomas Gleixner <tglx@linutronix.de>
 *
 * Contains all platform NAND related definitions.
 */

#ifndef __LINUX_MTD_PLATNAND_H
#define __LINUX_MTD_PLATNAND_H

#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/platform_device.h>

/**
 * struct platform_nand_chip - chip level device structure
 * @nr_chips: max. number of chips to scan for
 * @chip_offset: chip number offset
 * @nr_partitions: number of partitions pointed to by partitions (or zero)
 * @partitions: mtd partition list
 * @chip_delay: R/B delay value in us
 * @options: Option flags, e.g. 16bit buswidth
 * @bbt_options: BBT option flags, e.g. NAND_BBT_USE_FLASH
 * @part_probe_types: NULL-terminated array of probe types
 */
struct platform_nand_chip {
	int nr_chips;
	int chip_offset;
	int nr_partitions;
	struct mtd_partition *partitions;
	int chip_delay;
	unsigned int options;
	unsigned int bbt_options;
	const char **part_probe_types;
};

/**
 * struct platform_nand_ctrl - controller level device structure
 * @probe: platform specific function to probe/setup hardware
 * @remove: platform specific function to remove/teardown hardware
 * @dev_ready: platform specific function to read ready/busy pin
 * @select_chip: platform specific chip select function
 * @cmd_ctrl: platform specific function for controlling
 *	      ALE/CLE/nCE. Also used to write command and address
 * @write_buf: platform specific function for write buffer
 * @read_buf: platform specific function for read buffer
 * @priv: private data to transport driver specific settings
 *
 * All fields are optional and depend on the hardware driver requirements
 */
struct platform_nand_ctrl {
	int (*probe)(struct platform_device *pdev);
	void (*remove)(struct platform_device *pdev);
	int (*dev_ready)(struct nand_chip *chip);
	void (*select_chip)(struct nand_chip *chip, int cs);
	void (*cmd_ctrl)(struct nand_chip *chip, int dat, unsigned int ctrl);
	void (*write_buf)(struct nand_chip *chip, const uint8_t *buf, int len);
	void (*read_buf)(struct nand_chip *chip, uint8_t *buf, int len);
	void *priv;
};

/**
 * struct platform_nand_data - container structure for platform-specific data
 * @chip: chip level chip structure
 * @ctrl: controller level device structure
 */
struct platform_nand_data {
	struct platform_nand_chip chip;
	struct platform_nand_ctrl ctrl;
};

#endif /* __LINUX_MTD_PLATNAND_H */
