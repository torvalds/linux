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
extern int tps65090_write(struct device *dev, int reg, uint8_t val);
extern int tps65090_read(struct device *dev, int reg, uint8_t *val);
extern int tps65090_set_bits(struct device *dev, int reg, uint8_t bit_num);
extern int tps65090_clr_bits(struct device *dev, int reg, uint8_t bit_num);

#endif /*__LINUX_MFD_TPS65090_H */
