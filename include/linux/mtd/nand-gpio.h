/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MTD_NAND_GPIO_H
#define __LINUX_MTD_NAND_GPIO_H

#include <linux/mtd/rawnand.h>

struct gpio_nand_platdata {
	void	(*adjust_parts)(struct gpio_nand_platdata *, size_t);
	struct mtd_partition *parts;
	unsigned int num_parts;
	unsigned int options;
	int	chip_delay;
};

#endif
