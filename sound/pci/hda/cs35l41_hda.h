/* SPDX-License-Identifier: GPL-2.0
 *
 * CS35L41 ALSA HDA audio driver
 *
 * Copyright 2021 Cirrus Logic, Inc.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 */

#ifndef __CS35L41_HDA_H__
#define __CS35L41_HDA_H__

#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <sound/cs35l41.h>

enum cs35l41_hda_spk_pos {
	CS35l41_LEFT,
	CS35l41_RIGHT,
};

enum cs35l41_hda_gpio_function {
	CS35L41_NOT_USED,
	CS35l41_VSPK_SWITCH,
	CS35L41_INTERRUPT,
	CS35l41_SYNC,
};

struct cs35l41_hda {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct cs35l41_hw_cfg hw_cfg;

	int irq;
	int index;
	int channel_index;
	unsigned volatile long irq_errors;
	const char *amp_name;
	struct regmap_irq_chip_data *irq_data;
};

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap);
void cs35l41_hda_remove(struct device *dev);

#endif /*__CS35L41_HDA_H__*/
