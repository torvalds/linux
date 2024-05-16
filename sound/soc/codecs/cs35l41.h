/* SPDX-License-Identifier: GPL-2.0
 *
 * cs35l41.h -- CS35L41 ALSA SoC audio driver
 *
 * Copyright 2017-2021 Cirrus Logic, Inc.
 *
 * Author: David Rhodes <david.rhodes@cirrus.com>
 */

#ifndef __CS35L41_H__
#define __CS35L41_H__

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/cs35l41.h>

#include "wm_adsp.h"

#define CS35L41_RX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)
#define CS35L41_TX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

extern const struct dev_pm_ops cs35l41_pm_ops;

struct cs35l41_private {
	struct wm_adsp dsp; /* needs to be first member */
	struct snd_soc_codec *codec;
	struct cs35l41_hw_cfg hw_cfg;
	struct device *dev;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[CS35L41_NUM_SUPPLIES];
	int irq;
	/* GPIO for /RST */
	struct gpio_desc *reset_gpio;
};

int cs35l41_probe(struct cs35l41_private *cs35l41, const struct cs35l41_hw_cfg *hw_cfg);
void cs35l41_remove(struct cs35l41_private *cs35l41);

#endif /*__CS35L41_H__*/
