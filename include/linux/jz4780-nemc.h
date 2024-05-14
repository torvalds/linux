/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * JZ4780 NAND/external memory controller (NEMC)
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex@alex-smith.me.uk>
 */

#ifndef __LINUX_JZ4780_NEMC_H__
#define __LINUX_JZ4780_NEMC_H__

#include <linux/types.h>

struct device;

/*
 * Number of NEMC banks. Note that there are actually 6, but they are numbered
 * from 1.
 */
#define JZ4780_NEMC_NUM_BANKS	7

/**
 * enum jz4780_nemc_bank_type - device types which can be connected to a bank
 * @JZ4780_NEMC_BANK_SRAM: SRAM
 * @JZ4780_NEMC_BANK_NAND: NAND
 */
enum jz4780_nemc_bank_type {
	JZ4780_NEMC_BANK_SRAM,
	JZ4780_NEMC_BANK_NAND,
};

extern unsigned int jz4780_nemc_num_banks(struct device *dev);

extern void jz4780_nemc_set_type(struct device *dev, unsigned int bank,
				 enum jz4780_nemc_bank_type type);
extern void jz4780_nemc_assert(struct device *dev, unsigned int bank,
			       bool assert);

#endif /* __LINUX_JZ4780_NEMC_H__ */
