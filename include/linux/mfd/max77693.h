/*
 * max77693.h - Driver for the Maxim 77693
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  SangYoung Son <hello.son@samsung.com>
 *
 * This program is not provided / owned by Maxim Integrated Products.
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
 * This driver is based on max8997.h
 *
 * MAX77693 has PMIC, Charger, Flash LED, Haptic, MUIC devices.
 * The devices share the same I2C bus and included in
 * this mfd driver.
 */

#ifndef __LINUX_MFD_MAX77693_H
#define __LINUX_MFD_MAX77693_H

/* MAX77686 regulator IDs */
enum max77693_regulators {
	MAX77693_ESAFEOUT1 = 0,
	MAX77693_ESAFEOUT2,
	MAX77693_CHARGER,
	MAX77693_REG_MAX,
};

struct max77693_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *of_node;
};

struct max77693_reg_data {
	u8 addr;
	u8 data;
};

struct max77693_muic_platform_data {
	struct max77693_reg_data *init_data;
	int num_init_data;

	int detcable_delay_ms;

	/*
	 * Default usb/uart path whether UART/USB or AUX_UART/AUX_USB
	 * h/w path of COMP2/COMN1 on CONTROL1 register.
	 */
	int path_usb;
	int path_uart;
};

struct max77693_platform_data {
	/* regulator data */
	struct max77693_regulator_data *regulators;
	int num_regulators;

	/* muic data */
	struct max77693_muic_platform_data *muic_data;
};
#endif	/* __LINUX_MFD_MAX77693_H */
