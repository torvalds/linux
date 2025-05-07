// SPDX-License-Identifier: GPL-2.0
//
// TAS2781 HDA Shared Lib for I2C&SPI driver
//
// Copyright 2025 Texas Instruments, Inc.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>

#include <linux/component.h>
#include <linux/crc8.h>
#include <linux/crc32.h>
#include <linux/efi.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/tas2781.h>

#include "tas2781_hda.h"

void tas2781_hda_remove(struct device *dev,
	const struct component_ops *ops)
{
	struct tas2781_hda *tas_hda = dev_get_drvdata(dev);

	component_del(tas_hda->dev, ops);

	pm_runtime_get_sync(tas_hda->dev);
	pm_runtime_disable(tas_hda->dev);

	pm_runtime_put_noidle(tas_hda->dev);

	tasdevice_remove(tas_hda->priv);
}
EXPORT_SYMBOL_NS_GPL(tas2781_hda_remove, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_profile(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_priv->rcabin.ncfgs - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_profile, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_programs(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_priv->fmw->nr_programs - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_programs, "SND_HDA_SCODEC_TAS2781");

int tasdevice_info_config(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_fw->nr_configurations - 1;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_info_config, "SND_HDA_SCODEC_TAS2781");

int tasdevice_get_profile_id(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->rcabin.profile_cfg_id;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->rcabin.profile_cfg_id);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_get_profile_id, "SND_HDA_SCODEC_TAS2781");

int tasdevice_set_profile_id(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	int profile_id = ucontrol->value.integer.value[0];
	int max = tas_priv->rcabin.ncfgs - 1;
	int val, ret = 0;

	val = clamp(profile_id, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->rcabin.profile_cfg_id, val);

	if (tas_priv->rcabin.profile_cfg_id != val) {
		tas_priv->rcabin.profile_cfg_id = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_set_profile_id, "SND_HDA_SCODEC_TAS2781");

int tasdevice_program_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->cur_prog;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_prog);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_program_get, "SND_HDA_SCODEC_TAS2781");

int tasdevice_program_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;
	int nr_program = ucontrol->value.integer.value[0];
	int max = tas_fw->nr_programs - 1;
	int val, ret = 0;

	val = clamp(nr_program, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_prog, val);

	if (tas_priv->cur_prog != val) {
		tas_priv->cur_prog = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_program_put, "SND_HDA_SCODEC_TAS2781");

int tasdevice_config_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = tas_priv->cur_conf;

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_conf);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_config_get, "SND_HDA_SCODEC_TAS2781");

int tasdevice_config_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tasdevice_priv *tas_priv = snd_kcontrol_chip(kcontrol);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;
	int nr_config = ucontrol->value.integer.value[0];
	int max = tas_fw->nr_configurations - 1;
	int val, ret = 0;

	val = clamp(nr_config, 0, max);

	guard(mutex)(&tas_priv->codec_lock);

	dev_dbg(tas_priv->dev, "%s: kcontrol %s: %d -> %d\n", __func__,
		kcontrol->id.name, tas_priv->cur_conf, val);

	if (tas_priv->cur_conf != val) {
		tas_priv->cur_conf = val;
		ret = 1;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(tasdevice_config_put, "SND_HDA_SCODEC_TAS2781");

MODULE_DESCRIPTION("TAS2781 HDA Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shenghao Ding, TI, <shenghao-ding@ti.com>");
