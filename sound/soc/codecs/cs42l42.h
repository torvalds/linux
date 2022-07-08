/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cs42l42.h -- CS42L42 ALSA SoC audio driver header
 *
 * Copyright 2016-2022 Cirrus Logic, Inc.
 *
 * Author: James Schulman <james.schulman@cirrus.com>
 * Author: Brian Austin <brian.austin@cirrus.com>
 * Author: Michael White <michael.white@cirrus.com>
 */

#ifndef __CS42L42_H__
#define __CS42L42_H__

#include <linux/mutex.h>
#include <sound/jack.h>
#include <sound/cs42l42.h>

static const char *const cs42l42_supply_names[CS42L42_NUM_SUPPLIES] = {
	"VA",
	"VP",
	"VCP",
	"VD_FILT",
	"VL",
};

struct  cs42l42_private {
	struct regmap *regmap;
	struct device *dev;
	struct regulator_bulk_data supplies[CS42L42_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct completion pdn_done;
	struct snd_soc_jack *jack;
	struct mutex irq_lock;
	int pll_config;
	int bclk;
	u32 sclk;
	u32 srate;
	u8 plug_state;
	u8 hs_type;
	u8 ts_inv;
	u8 ts_dbnc_rise;
	u8 ts_dbnc_fall;
	u8 btn_det_init_dbnce;
	u8 btn_det_event_dbnce;
	u8 bias_thresholds[CS42L42_NUM_BIASES];
	u8 hs_bias_ramp_rate;
	u8 hs_bias_ramp_time;
	u8 hs_bias_sense_en;
	u8 stream_use;
	bool hp_adc_up_pending;
	bool suspended;
};

#endif /* __CS42L42_H__ */
