/*
 * lm3533.h -- LM3533 interface
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __LINUX_MFD_LM3533_H
#define __LINUX_MFD_LM3533_H

#define LM3533_ATTR_RO(_name) \
	DEVICE_ATTR(_name, S_IRUGO, show_##_name, NULL)
#define LM3533_ATTR_RW(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR , show_##_name, store_##_name)

struct device;
struct regmap;

struct lm3533 {
	struct device *dev;

	struct regmap *regmap;

	int gpio_hwen;
	int irq;

	unsigned have_als:1;
	unsigned have_backlights:1;
	unsigned have_leds:1;
};

struct lm3533_ctrlbank {
	struct lm3533 *lm3533;
	struct device *dev;
	int id;
};

struct lm3533_als_platform_data {
	unsigned pwm_mode:1;		/* PWM input mode (default analog) */
};

struct lm3533_bl_platform_data {
	char *name;
	u8 default_brightness;		/* 0 - 255 */
	u8 max_current;			/* 0 - 31 */
	u8 pwm;				/* 0 - 0x3f */
};

struct lm3533_led_platform_data {
	char *name;
	const char *default_trigger;
	u8 max_current;			/* 0 - 31 */
	u8 pwm;				/* 0 - 0x3f */
};

struct lm3533_platform_data {
	int gpio_hwen;

	struct lm3533_als_platform_data *als;

	struct lm3533_bl_platform_data *backlights;
	int num_backlights;

	struct lm3533_led_platform_data *leds;
	int num_leds;
};

extern int lm3533_ctrlbank_enable(struct lm3533_ctrlbank *cb);
extern int lm3533_ctrlbank_disable(struct lm3533_ctrlbank *cb);

extern int lm3533_ctrlbank_set_brightness(struct lm3533_ctrlbank *cb, u8 val);
extern int lm3533_ctrlbank_get_brightness(struct lm3533_ctrlbank *cb, u8 *val);
extern int lm3533_ctrlbank_set_max_current(struct lm3533_ctrlbank *cb, u8 val);
extern int lm3533_ctrlbank_get_max_current(struct lm3533_ctrlbank *cb,
								u8 *val);
extern int lm3533_ctrlbank_set_pwm(struct lm3533_ctrlbank *cb, u8 val);
extern int lm3533_ctrlbank_get_pwm(struct lm3533_ctrlbank *cb, u8 *val);

extern int lm3533_read(struct lm3533 *lm3533, u8 reg, u8 *val);
extern int lm3533_write(struct lm3533 *lm3533, u8 reg, u8 val);
extern int lm3533_update(struct lm3533 *lm3533, u8 reg, u8 val, u8 mask);

#endif	/* __LINUX_MFD_LM3533_H */
