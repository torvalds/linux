/*
 * cs42l42.h -- CS42L42 ALSA SoC audio driver DT bindings header
 *
 * Copyright 2016 Cirrus Logic, Inc.
 *
 * Author: James Schulman <james.schulman@cirrus.com>
 * Author: Brian Austin <brian.austin@cirrus.com>
 * Author: Michael White <michael.white@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __DT_CS42L42_H
#define __DT_CS42L42_H

/* HPOUT Load Capacity */
#define CS42L42_HPOUT_LOAD_1NF		0
#define CS42L42_HPOUT_LOAD_10NF		1

/* HPOUT Clamp to GND Overide */
#define CS42L42_HPOUT_CLAMP_EN		0
#define CS42L42_HPOUT_CLAMP_DIS		1

/* Tip Sense Inversion */
#define CS42L42_TS_INV_DIS			0
#define CS42L42_TS_INV_EN			1

/* Tip Sense Debounce */
#define CS42L42_TS_DBNCE_0			0
#define CS42L42_TS_DBNCE_125			1
#define CS42L42_TS_DBNCE_250			2
#define CS42L42_TS_DBNCE_500			3
#define CS42L42_TS_DBNCE_750			4
#define CS42L42_TS_DBNCE_1000			5
#define CS42L42_TS_DBNCE_1250			6
#define CS42L42_TS_DBNCE_1500			7

/* Button Press Software Debounce Times */
#define CS42L42_BTN_DET_INIT_DBNCE_MIN		0
#define CS42L42_BTN_DET_INIT_DBNCE_DEFAULT	100
#define CS42L42_BTN_DET_INIT_DBNCE_MAX		200

#define CS42L42_BTN_DET_EVENT_DBNCE_MIN		0
#define CS42L42_BTN_DET_EVENT_DBNCE_DEFAULT	10
#define CS42L42_BTN_DET_EVENT_DBNCE_MAX		20

/* Button Detect Level Sensitivities */
#define CS42L42_NUM_BIASES		4

#define CS42L42_HS_DET_LEVEL_15		0x0F
#define CS42L42_HS_DET_LEVEL_8		0x08
#define CS42L42_HS_DET_LEVEL_4		0x04
#define CS42L42_HS_DET_LEVEL_1		0x01

#define CS42L42_HS_DET_LEVEL_MIN	0
#define CS42L42_HS_DET_LEVEL_MAX	0x3F

/* HS Bias Ramp Rate */

#define CS42L42_HSBIAS_RAMP_FAST_RISE_SLOW_FALL		0
#define CS42L42_HSBIAS_RAMP_FAST			1
#define CS42L42_HSBIAS_RAMP_SLOW			2
#define CS42L42_HSBIAS_RAMP_SLOWEST			3

#define CS42L42_HSBIAS_RAMP_TIME0			10
#define CS42L42_HSBIAS_RAMP_TIME1			40
#define CS42L42_HSBIAS_RAMP_TIME2			90
#define CS42L42_HSBIAS_RAMP_TIME3			170

#endif /* __DT_CS42L42_H */
