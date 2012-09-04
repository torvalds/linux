/*
 * Simple driver for Texas Instruments LM3556 LED Flash driver chip (Rev0x03)
 * Copyright (C) 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_LM3556_H
#define __LINUX_LM3556_H

#define LM3556_NAME "leds-lm3556"

enum lm3556_pin_polarity {
	PIN_LOW_ACTIVE = 0,
	PIN_HIGH_ACTIVE,
};

enum lm3556_pin_enable {
	PIN_DISABLED = 0,
	PIN_ENABLED,
};

enum lm3556_strobe_usuage {
	STROBE_EDGE_DETECT = 0,
	STROBE_LEVEL_DETECT,
};

enum lm3556_indic_mode {
	INDIC_MODE_INTERNAL = 0,
	INDIC_MODE_EXTERNAL,
};

struct lm3556_platform_data {
	enum lm3556_pin_enable torch_pin_en;
	enum lm3556_pin_polarity torch_pin_polarity;

	enum lm3556_strobe_usuage strobe_usuage;
	enum lm3556_pin_enable strobe_pin_en;
	enum lm3556_pin_polarity strobe_pin_polarity;

	enum lm3556_pin_enable tx_pin_en;
	enum lm3556_pin_polarity tx_pin_polarity;

	enum lm3556_indic_mode indicator_mode;
};

#endif /* __LINUX_LM3556_H */
