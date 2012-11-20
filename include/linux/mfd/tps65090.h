/*
 * Core driver interface for TI TPS65090 PMIC family
 *
 * Copyright (C) 2012 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef __LINUX_MFD_TPS65090_H
#define __LINUX_MFD_TPS65090_H

#include <linux/irq.h>
#include <linux/regmap.h>

struct tps65090 {
	struct device		*dev;
	struct regmap		*rmap;
	struct irq_chip		irq_chip;
	struct mutex		irq_lock;
	int			irq_base;
};

struct tps65090_platform_data {
	int irq_base;
};

/*
 * NOTE: the functions below are not intended for use outside
 * of the TPS65090 sub-device drivers
 */
static inline int tps65090_write(struct device *dev, int reg, uint8_t val)
{
	struct tps65090 *tps = dev_get_drvdata(dev);

	return regmap_write(tps->rmap, reg, val);
}

static inline int tps65090_read(struct device *dev, int reg, uint8_t *val)
{
	struct tps65090 *tps = dev_get_drvdata(dev);
	unsigned int temp_val;
	int ret;

	ret = regmap_read(tps->rmap, reg, &temp_val);
	if (!ret)
		*val = temp_val;
	return ret;
}

static inline int tps65090_set_bits(struct device *dev, int reg,
		uint8_t bit_num)
{
	struct tps65090 *tps = dev_get_drvdata(dev);

	return regmap_update_bits(tps->rmap, reg, BIT(bit_num), ~0u);
}

static inline int tps65090_clr_bits(struct device *dev, int reg,
		uint8_t bit_num)
{
	struct tps65090 *tps = dev_get_drvdata(dev);

	return regmap_update_bits(tps->rmap, reg, BIT(bit_num), 0u);
}

#endif /*__LINUX_MFD_TPS65090_H */
