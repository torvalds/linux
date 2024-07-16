/* SPDX-License-Identifier: GPL-2.0-only */
/*
* Simple driver for Texas Instruments LM3630 LED Flash driver chip
* Copyright (C) 2012 Texas Instruments
*/

#ifndef __LINUX_LM3639_H
#define __LINUX_LM3639_H

#define LM3639_NAME "lm3639_bl"

enum lm3639_pwm {
	LM3639_PWM_DISABLE = 0x00,
	LM3639_PWM_EN_ACTLOW = 0x48,
	LM3639_PWM_EN_ACTHIGH = 0x40,
};

enum lm3639_strobe {
	LM3639_STROBE_DISABLE = 0x00,
	LM3639_STROBE_EN_ACTLOW = 0x10,
	LM3639_STROBE_EN_ACTHIGH = 0x30,
};

enum lm3639_txpin {
	LM3639_TXPIN_DISABLE = 0x00,
	LM3639_TXPIN_EN_ACTLOW = 0x04,
	LM3639_TXPIN_EN_ACTHIGH = 0x0C,
};

enum lm3639_fleds {
	LM3639_FLED_DIASBLE_ALL = 0x00,
	LM3639_FLED_EN_1 = 0x40,
	LM3639_FLED_EN_2 = 0x20,
	LM3639_FLED_EN_ALL = 0x60,
};

enum lm3639_bleds {
	LM3639_BLED_DIASBLE_ALL = 0x00,
	LM3639_BLED_EN_1 = 0x10,
	LM3639_BLED_EN_2 = 0x08,
	LM3639_BLED_EN_ALL = 0x18,
};
enum lm3639_bled_mode {
	LM3639_BLED_MODE_EXPONETIAL = 0x00,
	LM3639_BLED_MODE_LINEAR = 0x10,
};

struct lm3639_platform_data {
	unsigned int max_brt_led;
	unsigned int init_brt_led;

	/* input pins */
	enum lm3639_pwm pin_pwm;
	enum lm3639_strobe pin_strobe;
	enum lm3639_txpin pin_tx;

	/* output pins */
	enum lm3639_fleds fled_pins;
	enum lm3639_bleds bled_pins;
	enum lm3639_bled_mode bled_mode;

	void (*pwm_set_intensity) (int brightness, int max_brightness);
	int (*pwm_get_intensity) (void);
};
#endif /* __LINUX_LM3639_H */
