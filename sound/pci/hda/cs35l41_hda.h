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

#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>

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
	struct hda_codec *codec;

	int irq;
	int index;
	int channel_index;
	unsigned volatile long irq_errors;
	const char *amp_name;
	const char *acpi_subsystem_id;
	int speaker_id;
	struct mutex fw_mutex;
	struct regmap_irq_chip_data *irq_data;
	bool firmware_running;
	bool halo_initialized;
	struct cs_dsp cs_dsp;
};

enum halo_state {
	HALO_STATE_CODE_INIT_DOWNLOAD = 0,
	HALO_STATE_CODE_START,
	HALO_STATE_CODE_RUN
};

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap);
void cs35l41_hda_remove(struct device *dev);

#endif /*__CS35L41_HDA_H__*/
