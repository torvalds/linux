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

enum cs35l41_cspl_mbox_status {
	CSPL_MBOX_STS_RUNNING = 0,
	CSPL_MBOX_STS_PAUSED = 1,
	CSPL_MBOX_STS_RDY_FOR_REINIT = 2,
};

enum cs35l41_cspl_mbox_cmd {
	CSPL_MBOX_CMD_NONE = 0,
	CSPL_MBOX_CMD_PAUSE = 1,
	CSPL_MBOX_CMD_RESUME = 2,
	CSPL_MBOX_CMD_REINIT = 3,
	CSPL_MBOX_CMD_STOP_PRE_REINIT = 4,
	CSPL_MBOX_CMD_HIBERNATE = 5,
	CSPL_MBOX_CMD_OUT_OF_HIBERNATE = 6,
	CSPL_MBOX_CMD_UNKNOWN_CMD = -1,
	CSPL_MBOX_CMD_INVALID_SEQUENCE = -2,
};

struct cs35l41_private {
	struct wm_adsp dsp; /* needs to be first member */
	struct snd_soc_codec *codec;
	struct cs35l41_platform_data pdata;
	struct device *dev;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[CS35L41_NUM_SUPPLIES];
	int irq;
	/* GPIO for /RST */
	struct gpio_desc *reset_gpio;
};

int cs35l41_probe(struct cs35l41_private *cs35l41,
		  struct cs35l41_platform_data *pdata);
void cs35l41_remove(struct cs35l41_private *cs35l41);

#endif /*__CS35L41_H__*/
