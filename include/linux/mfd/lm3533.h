/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lm3533.h -- LM3533 interface
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 */

#ifndef __LINUX_MFD_LM3533_H
#define __LINUX_MFD_LM3533_H

#define LM3533_ATTR_RO(_name) \
	DEVICE_ATTR(_name, S_IRUGO, show_##_name, NULL)
#define LM3533_ATTR_RW(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR , show_##_name, store_##_name)

#define LM3533_MAX_CURRENT_MIN		5000
#define LM3533_MAX_CURRENT_MAX		29800
#define LM3533_MAX_CURRENT_STEP		800

struct device;
struct gpio_desc;
struct regmap;
struct regulator;

struct lm3533 {
	struct device *dev;

	struct regmap *regmap;

	struct gpio_desc *hwen;
	struct regulator *vin_supply;

	u32 boost_ovp;
	u32 boost_freq;

	unsigned have_als:1;
	unsigned have_backlights:1;
	unsigned have_leds:1;
};

struct lm3533_ctrlbank {
	struct regmap *regmap;
	struct device *dev;
	int id;
};

int lm3533_ctrlbank_enable(struct lm3533_ctrlbank *cb);
int lm3533_ctrlbank_disable(struct lm3533_ctrlbank *cb);

int lm3533_ctrlbank_set_brightness(struct lm3533_ctrlbank *cb, u32 val);
int lm3533_ctrlbank_get_brightness(struct lm3533_ctrlbank *cb, u32 *val);
int lm3533_ctrlbank_set_max_current(struct lm3533_ctrlbank *cb, u32 imax);
int lm3533_ctrlbank_set_pwm(struct lm3533_ctrlbank *cb, u32 val);
int lm3533_ctrlbank_get_pwm(struct lm3533_ctrlbank *cb, u32 *val);

#endif	/* __LINUX_MFD_LM3533_H */
