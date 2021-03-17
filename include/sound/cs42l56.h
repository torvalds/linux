/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/cs42l56.h -- Platform data for CS42L56
 *
 * Copyright (c) 2014 Cirrus Logic Inc.
 */

#ifndef __CS42L56_H
#define __CS42L56_H

struct cs42l56_platform_data {

	/* GPIO for Reset */
	unsigned int gpio_nreset;

	/* MICBIAS Level. Check datasheet Pg48 */
	unsigned int micbias_lvl;

	/* Analog Input 1A Reference 0=Single 1=Pseudo-Differential */
	unsigned int ain1a_ref_cfg;

	/* Analog Input 2A Reference 0=Single 1=Pseudo-Differential */
	unsigned int ain2a_ref_cfg;

	/* Analog Input 1B Reference 0=Single 1=Pseudo-Differential */
	unsigned int ain1b_ref_cfg;

	/* Analog Input 2B Reference 0=Single 1=Pseudo-Differential */
	unsigned int ain2b_ref_cfg;

	/* Charge Pump Freq. Check datasheet Pg62 */
	unsigned int chgfreq;

	/* HighPass Filter Right Channel Corner Frequency */
	unsigned int hpfb_freq;

	/* HighPass Filter Left Channel Corner Frequency */
	unsigned int hpfa_freq;

	/* Adaptive Power Control for LO/HP */
	unsigned int adaptive_pwr;

};

#endif /* __CS42L56_H */
