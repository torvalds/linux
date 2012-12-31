/*
 * max8698.h - Voltage regulator driver for the Maxim 8698
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  2010.10.25
 *  Modified by Taekki Kim <taekki.kim@samsung.com>
 */

#ifndef __LINUX_MFD_MAX8698_PRIV_H
#define __LINUX_MFD_MAX8698_PRIV_H

/* MAX 8698 registers */
enum {
	MAX8698_REG_ONOFF1,
	MAX8698_REG_ONOFF2,
	MAX8698_REG_ADISCHG_EN1,
	MAX8698_REG_ADISCHG_EN2,
	MAX8698_REG_DVSARM12,
	MAX8698_REG_DVSARM34,
	MAX8698_REG_DVSINT12,
	MAX8698_REG_BUCK3,
	MAX8698_REG_LDO2_LDO3,
	MAX8698_REG_LDO4,
	MAX8698_REG_LDO5,
	MAX8698_REG_LDO6,
	MAX8698_REG_LDO7,
	MAX8698_REG_LDO8_BKCHAR,
	MAX8698_REG_LDO9,
	MAX8698_REG_LBCNFG,
};

/* ONOFF1 */
#define MAX8698_SHIFT_EN1	7
#define MAX8698_SHIFT_EN2	6
#define MAX8698_SHIFT_EN3	5
#define MAX8698_SHIFT_ELDO2	4
#define MAX8698_SHIFT_ELDO3	3
#define MAX8698_SHIFT_ELDO4	2
#define MAX8698_SHIFT_ELDO5	1

#define MAX8698_MASK_EN1	(0x1 << MAX8698_SHIFT_EN1)
#define MAX8698_MASK_EN2	(0x1 << MAX8698_SHIFT_EN2)
#define MAX8698_MASK_EN3	(0x1 << MAX8698_SHIFT_EN3)
#define MAX8698_MASK_ELDO2	(0x1 << MAX8698_SHIFT_ELDO2)
#define MAX8698_MASK_ELDO3	(0x1 << MAX8698_SHIFT_ELDO3)
#define MAX8698_MASK_ELDO4	(0x1 << MAX8698_SHIFT_ELDO4)
#define MAX8698_MASK_ELDO5	(0x1 << MAX8698_SHIFT_ELDO5)

/* ONOFF2 */
#define MAX8698_SHIFT_ELDO6	7
#define MAX8698_SHIFT_ELDO7	6
#define MAX8698_SHIFT_ELDO8	5
#define MAX8698_SHIFT_ELDO9	4
#define MAX8698_SHIFT_ELBCNFG	0

#define MAX8698_MASK_ELDO6	(0x1 << MAX8698_SHIFT_ELDO6)
#define MAX8698_MASK_ELDO7	(0x1 << MAX8698_SHIFT_ELDO7)
#define MAX8698_MASK_ELDO8	(0x1 << MAX8698_SHIFT_ELDO8)
#define MAX8698_MASK_ELDO9	(0x1 << MAX8698_SHIFT_ELDO9)
#define MAX8698_MASK_ELBCNFG	(0x1 << MAX8698_SHIFT_ELBCNFG)


/**
 * struct max8698_dev - max8698 master device for sub-drivers
 * @dev: master device of the chip (can be used to access platform data)
 * @i2c_client: i2c client private data
 * @dev_read():	chip register read function
 * @dev_write(): chip register write function
 * @dev_update(): chip register update function
 * @iolock: mutex for serializing io access
 */

struct max8698_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*dev_read)(struct max8698_dev *max8698, u8 reg, u8 *dest);
	int (*dev_write)(struct max8698_dev *max8698, u8 reg, u8 val);
	int (*dev_update)(struct max8698_dev *max8698, u8 reg, u8 val, u8 mask);
	struct mutex iolock;
};

static inline int max8698_read_reg(struct max8698_dev *max8698, u8 reg,
				   u8 *value)
{
	return max8698->dev_read(max8698, reg, value);
}

static inline int max8698_write_reg(struct max8698_dev *max8698, u8 reg,
				    u8 value)
{
	return max8698->dev_write(max8698, reg, value);
}

static inline int max8698_update_reg(struct max8698_dev *max8698, u8 reg,
				     u8 value, u8 mask)
{
	return max8698->dev_update(max8698, reg, value, mask);
}

#endif /*  __LINUX_MFD_MAX8698_PRIV_H */
