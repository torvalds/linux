/*
 * max14577.h - Driver for the Maxim 14577/77836
 *
 * Copyright (C) 2014 Samsung Electrnoics
 * Chanwoo Choi <cw00.choi@samsung.com>
 * Krzysztof Kozlowski <k.kozlowski@samsung.com>
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
 * This driver is based on max8997.h
 *
 * MAX14577 has MUIC, Charger devices.
 * The devices share the same I2C bus and interrupt line
 * included in this mfd driver.
 *
 * MAX77836 has additional PMIC and Fuel-Gauge on different I2C slave
 * addresses.
 */

#ifndef __MAX14577_H__
#define __MAX14577_H__

#include <linux/regulator/consumer.h>

/* MAX14577 regulator IDs */
enum max14577_regulators {
	MAX14577_SAFEOUT = 0,
	MAX14577_CHARGER,

	MAX14577_REGULATOR_NUM,
};

/* MAX77836 regulator IDs */
enum max77836_regulators {
	MAX77836_SAFEOUT = 0,
	MAX77836_CHARGER,
	MAX77836_LDO1,
	MAX77836_LDO2,

	MAX77836_REGULATOR_NUM,
};

struct max14577_regulator_platform_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *of_node;
};

/*
 * MAX14577 MFD platform data
 */
struct max14577_platform_data {
	/* IRQ */
	int irq_base;

	/* current control GPIOs */
	int gpio_pogo_vbatt_en;
	int gpio_pogo_vbus_en;

	/* current control GPIO control function */
	int (*set_gpio_pogo_vbatt_en) (int gpio_val);
	int (*set_gpio_pogo_vbus_en) (int gpio_val);

	int (*set_gpio_pogo_cb) (int new_dev);

	struct max14577_regulator_platform_data *regulators;
};

#endif /* __MAX14577_H__ */
