/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * max77693.h - Driver for the Maxim 77693
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  SangYoung Son <hello.son@samsung.com>
 *
 * This program is not provided / owned by Maxim Integrated Products.
 *
 * This driver is based on max8997.h
 *
 * MAX77693 has PMIC, Charger, Flash LED, Haptic, MUIC devices.
 * The devices share the same I2C bus and included in
 * this mfd driver.
 */

#ifndef __LINUX_MFD_MAX77693_H
#define __LINUX_MFD_MAX77693_H

/* MAX77693 regulator IDs */
enum max77693_regulators {
	MAX77693_ESAFEOUT1 = 0,
	MAX77693_ESAFEOUT2,
	MAX77693_CHARGER,
	MAX77693_REG_MAX,
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

/* MAX77693 led flash */

/* triggers */
enum max77693_led_trigger {
	MAX77693_LED_TRIG_OFF,
	MAX77693_LED_TRIG_FLASH,
	MAX77693_LED_TRIG_TORCH,
	MAX77693_LED_TRIG_EXT,
	MAX77693_LED_TRIG_SOFT,
};

/* trigger types */
enum max77693_led_trigger_type {
	MAX77693_LED_TRIG_TYPE_EDGE,
	MAX77693_LED_TRIG_TYPE_LEVEL,
};

/* boost modes */
enum max77693_led_boost_mode {
	MAX77693_LED_BOOST_NONE,
	MAX77693_LED_BOOST_ADAPTIVE,
	MAX77693_LED_BOOST_FIXED,
};

/* MAX77693 */

struct max77693_platform_data {
	/* muic data */
	struct max77693_muic_platform_data *muic_data;
	struct max77693_led_platform_data *led_data;
};
#endif	/* __LINUX_MFD_MAX77693_H */
