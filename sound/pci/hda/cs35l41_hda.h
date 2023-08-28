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

#include <linux/efi.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <sound/cs35l41.h>

#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>

struct cs35l41_amp_cal_data {
	u32 calTarget[2];
	u32 calTime[2];
	s8 calAmbient;
	u8 calStatus;
	u16 calR;
} __packed;

struct cs35l41_amp_efi_data {
	u32 size;
	u32 count;
	struct cs35l41_amp_cal_data data[];
} __packed;

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
	int firmware_type;
	int speaker_id;
	struct mutex fw_mutex;
	struct work_struct fw_load_work;

	struct regmap_irq_chip_data *irq_data;
	bool firmware_running;
	bool request_fw_load;
	bool fw_request_ongoing;
	bool halo_initialized;
	bool playback_started;
	struct cs_dsp cs_dsp;
};

enum halo_state {
	HALO_STATE_CODE_INIT_DOWNLOAD = 0,
	HALO_STATE_CODE_START,
	HALO_STATE_CODE_RUN
};

extern const struct dev_pm_ops cs35l41_hda_pm_ops;

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap);
void cs35l41_hda_remove(struct device *dev);
int cs35l41_get_speaker_id(struct device *dev, int amp_index, int num_amps, int fixed_gpio_id);

#endif /*__CS35L41_HDA_H__*/
