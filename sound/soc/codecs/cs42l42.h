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

#include <dt-bindings/sound/cs42l42.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soundwire/sdw.h>
#include <sound/jack.h>
#include <sound/cs42l42.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>

struct  cs42l42_private {
	struct regmap *regmap;
	struct device *dev;
	struct regulator_bulk_data supplies[CS42L42_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct completion pdn_done;
	struct snd_soc_jack *jack;
	struct sdw_slave *sdw_peripheral;
	struct mutex irq_lock;
	int devid;
	int irq;
	int pll_config;
	u32 sclk;
	u32 sample_rate;
	u32 bclk_ratio;
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
	bool sdw_waiting_first_unattach;
	bool init_done;
};

extern const struct regmap_range_cfg cs42l42_page_range;
extern const struct regmap_config cs42l42_regmap;
extern const struct snd_soc_component_driver cs42l42_soc_component;
extern struct snd_soc_dai_driver cs42l42_dai;

bool cs42l42_readable_register(struct device *dev, unsigned int reg);
bool cs42l42_volatile_register(struct device *dev, unsigned int reg);

int cs42l42_pll_config(struct snd_soc_component *component,
		       unsigned int clk, unsigned int sample_rate);
void cs42l42_src_config(struct snd_soc_component *component, unsigned int sample_rate);
int cs42l42_mute_stream(struct snd_soc_dai *dai, int mute, int stream);
irqreturn_t cs42l42_irq_thread(int irq, void *data);
int cs42l42_suspend(struct device *dev);
int cs42l42_resume(struct device *dev);
void cs42l42_resume_restore(struct device *dev);
int cs42l42_common_probe(struct cs42l42_private *cs42l42,
			 const struct snd_soc_component_driver *component_drv,
			 struct snd_soc_dai_driver *dai);
int cs42l42_init(struct cs42l42_private *cs42l42);
void cs42l42_common_remove(struct cs42l42_private *cs42l42);

#endif /* __CS42L42_H__ */
