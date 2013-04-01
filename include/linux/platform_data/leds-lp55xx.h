/*
 * LP55XX Platform Data Header
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Derived from leds-lp5521.h, leds-lp5523.h
 */

#ifndef _LEDS_LP55XX_H
#define _LEDS_LP55XX_H

/* Clock configuration */
#define LP55XX_CLOCK_AUTO	0
#define LP55XX_CLOCK_INT	1
#define LP55XX_CLOCK_EXT	2

/* Bits in LP5521 CONFIG register. 'update_config' in lp55xx_platform_data */
#define LP5521_PWM_HF			0x40	/* PWM: 0 = 256Hz, 1 = 558Hz */
#define LP5521_PWRSAVE_EN		0x20	/* 1 = Power save mode */
#define LP5521_CP_MODE_OFF		0	/* Charge pump (CP) off */
#define LP5521_CP_MODE_BYPASS		8	/* CP forced to bypass mode */
#define LP5521_CP_MODE_1X5		0x10	/* CP forced to 1.5x mode */
#define LP5521_CP_MODE_AUTO		0x18	/* Automatic mode selection */
#define LP5521_R_TO_BATT		4	/* R out: 0 = CP, 1 = Vbat */
#define LP5521_CLK_SRC_EXT		0	/* Ext-clk source (CLK_32K) */
#define LP5521_CLK_INT			1	/* Internal clock */
#define LP5521_CLK_AUTO			2	/* Automatic clock selection */

struct lp55xx_led_config {
	const char *name;
	u8 chan_nr;
	u8 led_current; /* mA x10, 0 if led is not connected */
	u8 max_current;
};

struct lp55xx_predef_pattern {
	u8 *r;
	u8 *g;
	u8 *b;
	u8 size_r;
	u8 size_g;
	u8 size_b;
};

/*
 * struct lp55xx_platform_data
 * @led_config        : Configurable led class device
 * @num_channels      : Number of LED channels
 * @label             : Used for naming LEDs
 * @clock_mode        : Input clock mode. LP55XX_CLOCK_AUTO or _INT or _EXT
 * @setup_resources   : Platform specific function before enabling the chip
 * @release_resources : Platform specific function after  disabling the chip
 * @enable            : EN pin control by platform side
 * @patterns          : Predefined pattern data for RGB channels
 * @num_patterns      : Number of patterns
 * @update_config     : Value of CONFIG register
 */
struct lp55xx_platform_data {

	/* LED channel configuration */
	struct lp55xx_led_config *led_config;
	u8 num_channels;
	const char *label;

	/* Clock configuration */
	u8 clock_mode;

	/* Platform specific functions */
	int (*setup_resources)(void);
	void (*release_resources)(void);
	void (*enable)(bool state);

	/* Predefined pattern data */
	struct lp55xx_predef_pattern *patterns;
	unsigned int num_patterns;

	/* _CONFIG register */
	u8 update_config;
};

#endif /* _LEDS_LP55XX_H */
