/*
 * da9052 declarations for DA9052 PMICs.
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __MFD_DA9052_DA9052_H
#define __MFD_DA9052_DA9052_H

#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mfd/core.h>

#include <linux/mfd/da9052/reg.h>

#define DA9052_IRQ_DCIN	0
#define DA9052_IRQ_VBUS	1
#define DA9052_IRQ_DCINREM	2
#define DA9052_IRQ_VBUSREM	3
#define DA9052_IRQ_VDDLOW	4
#define DA9052_IRQ_ALARM	5
#define DA9052_IRQ_SEQRDY	6
#define DA9052_IRQ_COMP1V2	7
#define DA9052_IRQ_NONKEY	8
#define DA9052_IRQ_IDFLOAT	9
#define DA9052_IRQ_IDGND	10
#define DA9052_IRQ_CHGEND	11
#define DA9052_IRQ_TBAT	12
#define DA9052_IRQ_ADC_EOM	13
#define DA9052_IRQ_PENDOWN	14
#define DA9052_IRQ_TSIREADY	15
#define DA9052_IRQ_GPI0	16
#define DA9052_IRQ_GPI1	17
#define DA9052_IRQ_GPI2	18
#define DA9052_IRQ_GPI3	19
#define DA9052_IRQ_GPI4	20
#define DA9052_IRQ_GPI5	21
#define DA9052_IRQ_GPI6	22
#define DA9052_IRQ_GPI7	23
#define DA9052_IRQ_GPI8	24
#define DA9052_IRQ_GPI9	25
#define DA9052_IRQ_GPI10	26
#define DA9052_IRQ_GPI11	27
#define DA9052_IRQ_GPI12	28
#define DA9052_IRQ_GPI13	29
#define DA9052_IRQ_GPI14	30
#define DA9052_IRQ_GPI15	31

enum da9052_chip_id {
	DA9052,
	DA9053_AA,
	DA9053_BA,
	DA9053_BB,
};

struct da9052_pdata;

struct da9052 {
	struct mutex io_lock;

	struct device *dev;
	struct regmap *regmap;

	int irq_base;
	u8 chip_id;

	int chip_irq;
};

/* Device I/O API */
static inline int da9052_reg_read(struct da9052 *da9052, unsigned char reg)
{
	int val, ret;

	ret = regmap_read(da9052->regmap, reg, &val);
	if (ret < 0)
		return ret;
	return val;
}

static inline int da9052_reg_write(struct da9052 *da9052, unsigned char reg,
				    unsigned char val)
{
	return regmap_write(da9052->regmap, reg, val);
}

static inline int da9052_group_read(struct da9052 *da9052, unsigned char reg,
				     unsigned reg_cnt, unsigned char *val)
{
	return regmap_bulk_read(da9052->regmap, reg, val, reg_cnt);
}

static inline int da9052_group_write(struct da9052 *da9052, unsigned char reg,
				      unsigned reg_cnt, unsigned char *val)
{
	return regmap_raw_write(da9052->regmap, reg, val, reg_cnt);
}

static inline int da9052_reg_update(struct da9052 *da9052, unsigned char reg,
				     unsigned char bit_mask,
				     unsigned char reg_val)
{
	return regmap_update_bits(da9052->regmap, reg, bit_mask, reg_val);
}

int da9052_device_init(struct da9052 *da9052, u8 chip_id);
void da9052_device_exit(struct da9052 *da9052);

extern struct regmap_config da9052_regmap_config;

#endif /* __MFD_DA9052_DA9052_H */
