/*
 * LP855x Backlight Driver
 *
 *			Copyright (C) 2011 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _LP855X_H
#define _LP855X_H

#define BL_CTL_SHFT	(0)
#define BRT_MODE_SHFT	(1)
#define BRT_MODE_MASK	(0x06)

/* Enable backlight. Only valid when BRT_MODE=10(I2C only) */
#define ENABLE_BL	(1)
#define DISABLE_BL	(0)

#define I2C_CONFIG(id)	id ## _I2C_CONFIG
#define PWM_CONFIG(id)	id ## _PWM_CONFIG

/* DEVICE CONTROL register - LP8550 */
#define LP8550_PWM_CONFIG	(LP8550_PWM_ONLY << BRT_MODE_SHFT)
#define LP8550_I2C_CONFIG	((ENABLE_BL << BL_CTL_SHFT) | \
				(LP8550_I2C_ONLY << BRT_MODE_SHFT))

/* DEVICE CONTROL register - LP8551 */
#define LP8551_PWM_CONFIG	LP8550_PWM_CONFIG
#define LP8551_I2C_CONFIG	LP8550_I2C_CONFIG

/* DEVICE CONTROL register - LP8552 */
#define LP8552_PWM_CONFIG	LP8550_PWM_CONFIG
#define LP8552_I2C_CONFIG	LP8550_I2C_CONFIG

/* DEVICE CONTROL register - LP8553 */
#define LP8553_PWM_CONFIG	LP8550_PWM_CONFIG
#define LP8553_I2C_CONFIG	LP8550_I2C_CONFIG

/* DEVICE CONTROL register - LP8556 */
#define LP8556_PWM_CONFIG	(LP8556_PWM_ONLY << BRT_MODE_SHFT)
#define LP8556_COMB1_CONFIG	(LP8556_COMBINED1 << BRT_MODE_SHFT)
#define LP8556_I2C_CONFIG	((ENABLE_BL << BL_CTL_SHFT) | \
				(LP8556_I2C_ONLY << BRT_MODE_SHFT))
#define LP8556_COMB2_CONFIG	(LP8556_COMBINED2 << BRT_MODE_SHFT)

/* ROM area boundary */
#define EEPROM_START	(0xA0)
#define EEPROM_END	(0xA7)
#define EPROM_START	(0xA0)
#define EPROM_END	(0xAF)

enum lp855x_chip_id {
	LP8550,
	LP8551,
	LP8552,
	LP8553,
	LP8556,
};

enum lp855x_brightness_ctrl_mode {
	PWM_BASED = 1,
	REGISTER_BASED,
};

enum lp8550_brighntess_source {
	LP8550_PWM_ONLY,
	LP8550_I2C_ONLY = 2,
};

enum lp8551_brighntess_source {
	LP8551_PWM_ONLY = LP8550_PWM_ONLY,
	LP8551_I2C_ONLY = LP8550_I2C_ONLY,
};

enum lp8552_brighntess_source {
	LP8552_PWM_ONLY = LP8550_PWM_ONLY,
	LP8552_I2C_ONLY = LP8550_I2C_ONLY,
};

enum lp8553_brighntess_source {
	LP8553_PWM_ONLY = LP8550_PWM_ONLY,
	LP8553_I2C_ONLY = LP8550_I2C_ONLY,
};

enum lp8556_brightness_source {
	LP8556_PWM_ONLY,
	LP8556_COMBINED1,	/* pwm + i2c before the shaper block */
	LP8556_I2C_ONLY,
	LP8556_COMBINED2,	/* pwm + i2c after the shaper block */
};

struct lp855x_pwm_data {
	void (*pwm_set_intensity) (int brightness, int max_brightness);
	int (*pwm_get_intensity) (int max_brightness);
};

struct lp855x_rom_data {
	u8 addr;
	u8 val;
};

/**
 * struct lp855x_platform_data
 * @name : Backlight driver name. If it is not defined, default name is set.
 * @mode : brightness control by pwm or lp855x register
 * @device_control : value of DEVICE CONTROL register
 * @initial_brightness : initial value of backlight brightness
 * @pwm_data : platform specific pwm generation functions.
		Only valid when mode is PWM_BASED.
 * @load_new_rom_data :
	0 : use default configuration data
	1 : update values of eeprom or eprom registers on loading driver
 * @size_program : total size of lp855x_rom_data
 * @rom_data : list of new eeprom/eprom registers
 */
struct lp855x_platform_data {
	char *name;
	enum lp855x_brightness_ctrl_mode mode;
	u8 device_control;
	int initial_brightness;
	struct lp855x_pwm_data pwm_data;
	u8 load_new_rom_data;
	int size_program;
	struct lp855x_rom_data *rom_data;
};

#endif
