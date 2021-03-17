/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/cs42l52.h -- Platform data for CS42L52
 *
 * Copyright (c) 2012 Cirrus Logic Inc.
 */

#ifndef __CS42L52_H
#define __CS42L52_H

struct cs42l52_platform_data {

	/* MICBIAS Level. Check datasheet Pg48 */
	unsigned int micbias_lvl;

	/* MICA mode selection Differential or Single-ended */
	bool mica_diff_cfg;

	/* MICB mode selection Differential or Single-ended */
	bool micb_diff_cfg;

	/* Charge Pump Freq. Check datasheet Pg73 */
	unsigned int chgfreq;

	/* Reset GPIO */
	unsigned int reset_gpio;
};

#endif /* __CS42L52_H */
