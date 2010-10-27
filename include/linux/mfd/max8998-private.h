/*
 * max8698.h - Voltage regulator driver for the Maxim 8998
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
 */

#ifndef __LINUX_MFD_MAX8998_PRIV_H
#define __LINUX_MFD_MAX8998_PRIV_H

/* MAX 8998 registers */
enum {
	MAX8998_REG_IRQ1,
	MAX8998_REG_IRQ2,
	MAX8998_REG_IRQ3,
	MAX8998_REG_IRQ4,
	MAX8998_REG_IRQM1,
	MAX8998_REG_IRQM2,
	MAX8998_REG_IRQM3,
	MAX8998_REG_IRQM4,
	MAX8998_REG_STATUS1,
	MAX8998_REG_STATUS2,
	MAX8998_REG_STATUSM1,
	MAX8998_REG_STATUSM2,
	MAX8998_REG_CHGR1,
	MAX8998_REG_CHGR2,
	MAX8998_REG_LDO_ACTIVE_DISCHARGE1,
	MAX8998_REG_LDO_ACTIVE_DISCHARGE2,
	MAX8998_REG_BUCK_ACTIVE_DISCHARGE3,
	MAX8998_REG_ONOFF1,
	MAX8998_REG_ONOFF2,
	MAX8998_REG_ONOFF3,
	MAX8998_REG_ONOFF4,
	MAX8998_REG_BUCK1_DVSARM1,
	MAX8998_REG_BUCK1_DVSARM2,
	MAX8998_REG_BUCK1_DVSARM3,
	MAX8998_REG_BUCK1_DVSARM4,
	MAX8998_REG_BUCK2_DVSINT1,
	MAX8998_REG_BUCK2_DVSINT2,
	MAX8998_REG_BUCK3,
	MAX8998_REG_BUCK4,
	MAX8998_REG_LDO2_LDO3,
	MAX8998_REG_LDO4,
	MAX8998_REG_LDO5,
	MAX8998_REG_LDO6,
	MAX8998_REG_LDO7,
	MAX8998_REG_LDO8_LDO9,
	MAX8998_REG_LDO10_LDO11,
	MAX8998_REG_LDO12,
	MAX8998_REG_LDO13,
	MAX8998_REG_LDO14,
	MAX8998_REG_LDO15,
	MAX8998_REG_LDO16,
	MAX8998_REG_LDO17,
	MAX8998_REG_BKCHR,
	MAX8998_REG_LBCNFG1,
	MAX8998_REG_LBCNFG2,
};

/**
 * struct max8998_dev - max8998 master device for sub-drivers
 * @dev: master device of the chip (can be used to access platform data)
 * @i2c_client: i2c client private data
 * @dev_read():	chip register read function
 * @dev_write(): chip register write function
 * @dev_update(): chip register update function
 * @iolock: mutex for serializing io access
 */

struct max8998_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*dev_read)(struct max8998_dev *max8998, u8 reg, u8 *dest);
	int (*dev_write)(struct max8998_dev *max8998, u8 reg, u8 val);
	int (*dev_update)(struct max8998_dev *max8998, u8 reg, u8 val, u8 mask);
	struct mutex iolock;
};

static inline int max8998_read_reg(struct max8998_dev *max8998, u8 reg,
				   u8 *value)
{
	return max8998->dev_read(max8998, reg, value);
}

static inline int max8998_write_reg(struct max8998_dev *max8998, u8 reg,
				    u8 value)
{
	return max8998->dev_write(max8998, reg, value);
}

static inline int max8998_update_reg(struct max8998_dev *max8998, u8 reg,
				     u8 value, u8 mask)
{
	return max8998->dev_update(max8998, reg, value, mask);
}

#endif /*  __LINUX_MFD_MAX8998_PRIV_H */
