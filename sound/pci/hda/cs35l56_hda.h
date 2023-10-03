/* SPDX-License-Identifier: GPL-2.0-only
 *
 * HDA audio driver for Cirrus Logic CS35L56 smart amp
 *
 * Copyright (C) 2023 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __CS35L56_HDA_H__
#define __CS35L56_HDA_H__

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/regulator/consumer.h>
#include <sound/cs35l56.h>

struct dentry;

struct cs35l56_hda {
	struct cs35l56_base base;
	struct hda_codec *codec;

	int index;
	const char *system_name;
	const char *amp_name;

	struct cs_dsp cs_dsp;
	bool playing;
	bool suspended;
	u8 asp_tx_mask;

	struct snd_kcontrol *posture_ctl;
	struct snd_kcontrol *volume_ctl;
	struct snd_kcontrol *mixer_ctl[4];

#if IS_ENABLED(CONFIG_SND_DEBUG)
	struct dentry *debugfs_root;
#endif
};

extern const struct dev_pm_ops cs35l56_hda_pm_ops;

int cs35l56_hda_common_probe(struct cs35l56_hda *cs35l56, int id);
void cs35l56_hda_remove(struct device *dev);

#endif /*__CS35L56_HDA_H__*/
