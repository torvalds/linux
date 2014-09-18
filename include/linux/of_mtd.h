/*
 * Copyright 2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * OF helpers for mtd.
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_OF_MTD_H
#define __LINUX_OF_MTD_H

#ifdef CONFIG_OF_MTD

#include <linux/of.h>
int of_get_nand_ecc_mode(struct device_node *np);
int of_get_nand_ecc_step_size(struct device_node *np);
int of_get_nand_ecc_strength(struct device_node *np);
int of_get_nand_bus_width(struct device_node *np);
bool of_get_nand_on_flash_bbt(struct device_node *np);

#else /* CONFIG_OF_MTD */

static inline int of_get_nand_ecc_mode(struct device_node *np)
{
	return -ENOSYS;
}

static inline int of_get_nand_ecc_step_size(struct device_node *np)
{
	return -ENOSYS;
}

static inline int of_get_nand_ecc_strength(struct device_node *np)
{
	return -ENOSYS;
}

static inline int of_get_nand_bus_width(struct device_node *np)
{
	return -ENOSYS;
}

static inline bool of_get_nand_on_flash_bbt(struct device_node *np)
{
	return false;
}

#endif /* CONFIG_OF_MTD */

#endif /* __LINUX_OF_MTD_H */
