/*
 * Platform data for MAX98095
 *
 * Copyright 2011 Maxim Integrated Products
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __SOUND_MAX98095_PDATA_H__
#define __SOUND_MAX98095_PDATA_H__

/* Equalizer filter response configuration */
struct max98095_eq_cfg {
	const char *name;
	unsigned int rate;
	u16 band1[5];
	u16 band2[5];
	u16 band3[5];
	u16 band4[5];
	u16 band5[5];
};

/* Biquad filter response configuration */
struct max98095_biquad_cfg {
	const char *name;
	unsigned int rate;
	u16 band1[5];
	u16 band2[5];
};

/* codec platform data */
struct max98095_pdata {

	/* Equalizers for DAI1 and DAI2 */
	struct max98095_eq_cfg *eq_cfg;
	unsigned int eq_cfgcnt;

	/* Biquad filter for DAI1 and DAI2 */
	struct max98095_biquad_cfg *bq_cfg;
	unsigned int bq_cfgcnt;

	/* Analog/digital microphone configuration:
	 * 0 = analog microphone input (normal setting)
	 * 1 = digital microphone input
	 */
	unsigned int digmic_left_mode:1;
	unsigned int digmic_right_mode:1;
};

#endif
