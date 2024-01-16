/*
 * Platform data for MAX9768
 * Copyright (C) 2011, 2012 by Wolfram Sang, Pengutronix e.K.
 * same licence as the driver
 */

#ifndef __SOUND_MAX9768_PDATA_H__
#define __SOUND_MAX9768_PDATA_H__

/**
 * struct max9768_pdata - optional platform specific MAX9768 configuration
 * @shdn_gpio:	GPIO to SHDN pin. If not valid, pin must be hardwired HIGH
 * @mute_gpio:	GPIO to MUTE pin. If not valid, control for mute won't be added
 * @flags: configuration flags, e.g. set classic PWM mode (check datasheet
 *         regarding "filterless modulation" which is default).
 */
struct max9768_pdata {
	int shdn_gpio;
	int mute_gpio;
	unsigned flags;
#define MAX9768_FLAG_CLASSIC_PWM	(1 << 0)
};

#endif /* __SOUND_MAX9768_PDATA_H__*/
