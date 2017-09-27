/*
 * Common data shared between Maxim 77693 and 77843 drivers
 *
 * Copyright (C) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_MFD_MAX77693_COMMON_H
#define __LINUX_MFD_MAX77693_COMMON_H

enum max77693_types {
	TYPE_MAX77693_UNKNOWN,
	TYPE_MAX77693,
	TYPE_MAX77843,

	TYPE_MAX77693_NUM,
};

/*
 * Shared also with max77843.
 */
struct max77693_dev {
	struct device *dev;
	struct i2c_client *i2c;		/* 0xCC , PMIC, Charger, Flash LED */
	struct i2c_client *i2c_muic;	/* 0x4A , MUIC */
	struct i2c_client *i2c_haptic;	/* MAX77693: 0x90 , Haptic */
	struct i2c_client *i2c_chg;	/* MAX77843: 0xD2, Charger */

	enum max77693_types type;

	struct regmap *regmap;
	struct regmap *regmap_muic;
	struct regmap *regmap_haptic;	/* Only MAX77693 */
	struct regmap *regmap_chg;	/* Only MAX77843 */

	struct regmap_irq_chip_data *irq_data_led;
	struct regmap_irq_chip_data *irq_data_topsys;
	struct regmap_irq_chip_data *irq_data_chg; /* Only MAX77693 */
	struct regmap_irq_chip_data *irq_data_muic;

	int irq;
};


#endif /*  __LINUX_MFD_MAX77693_COMMON_H */
