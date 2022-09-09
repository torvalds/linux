// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Keyon Jie <yang.jie@linux.intel.com>
//

#include <linux/module.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda_codec.h>
#include <sound/hda_i915.h>
#include <sound/sof.h>
#include "../ops.h"
#include "hda.h"
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#include "../../codecs/hdac_hda.h"
#endif /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */

#define CODEC_PROBE_RETRIES	3

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#define IDISP_VID_INTEL	0x80860000

/* load the legacy HDA codec driver */
static int request_codec_module(struct hda_codec *codec)
{
#ifdef MODULE
	char alias[MODULE_NAME_LEN];
	const char *mod = NULL;

	switch (codec->probe_id) {
	case HDA_CODEC_ID_GENERIC:
#if IS_MODULE(CONFIG_SND_HDA_GENERIC)
		mod = "snd-hda-codec-generic";
#endif
		break;
	default:
		snd_hdac_codec_modalias(&codec->core, alias, sizeof(alias));
		mod = alias;
		break;
	}

	if (mod) {
		dev_dbg(&codec->core.dev, "loading codec module: %s\n", mod);
		request_module(mod);
	}
#endif /* MODULE */
	return device_attach(hda_codec_dev(codec));
}

static int hda_codec_load_module(struct hda_codec *codec)
{
	int ret = request_codec_module(codec);

	if (ret <= 0) {
		codec->probe_id = HDA_CODEC_ID_GENERIC;
		ret = request_codec_module(codec);
	}

	return ret;
}

/* enable controller wake up event for all codecs with jack connectors */
void hda_codec_jack_wake_enable(struct snd_sof_dev *sdev, bool enable)
{
	struct hda_bus *hbus = sof_to_hbus(sdev);
	struct hdac_bus *bus = sof_to_bus(sdev);
	struct hda_codec *codec;
	unsigned int mask = 0;

	if (enable) {
		list_for_each_codec(codec, hbus)
			if (codec->jacktbl.used)
				mask |= BIT(codec->core.addr);
	}

	snd_hdac_chip_updatew(bus, WAKEEN, STATESTS_INT_MASK, mask);
}

/* check jack status after resuming from suspend mode */
void hda_codec_jack_check(struct snd_sof_dev *sdev)
{
	struct hda_bus *hbus = sof_to_hbus(sdev);
	struct hda_codec *codec;

	list_for_each_codec(codec, hbus)
		/*
		 * Wake up all jack-detecting codecs regardless whether an event
		 * has been recorded in STATESTS
		 */
		if (codec->jacktbl.used)
			pm_request_resume(&codec->core.dev);
}
#else
void hda_codec_jack_wake_enable(struct snd_sof_dev *sdev, bool enable) {}
void hda_codec_jack_check(struct snd_sof_dev *sdev) {}
#endif /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */
EXPORT_SYMBOL_NS(hda_codec_jack_wake_enable, SND_SOC_SOF_HDA_AUDIO_CODEC);
EXPORT_SYMBOL_NS(hda_codec_jack_check, SND_SOC_SOF_HDA_AUDIO_CODEC);

#if IS_ENABLED(CONFIG_SND_HDA_GENERIC)
#define is_generic_config(bus) \
	((bus)->modelname && !strcmp((bus)->modelname, "generic"))
#else
#define is_generic_config(x)	0
#endif

/* probe individual codec */
static int hda_codec_probe(struct snd_sof_dev *sdev, int address,
			   bool hda_codec_use_common_hdmi)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	struct hdac_hda_priv *hda_priv;
	struct hda_codec *codec;
	int type = HDA_DEV_LEGACY;
#endif
	struct hda_bus *hbus = sof_to_hbus(sdev);
	struct hdac_device *hdev;
	u32 hda_cmd = (address << 28) | (AC_NODE_ROOT << 20) |
		(AC_VERB_PARAMETERS << 8) | AC_PAR_VENDOR_ID;
	u32 resp = -1;
	int ret, retry = 0;

	do {
		mutex_lock(&hbus->core.cmd_mutex);
		snd_hdac_bus_send_cmd(&hbus->core, hda_cmd);
		snd_hdac_bus_get_response(&hbus->core, address, &resp);
		mutex_unlock(&hbus->core.cmd_mutex);
	} while (resp == -1 && retry++ < CODEC_PROBE_RETRIES);

	if (resp == -1)
		return -EIO;
	dev_dbg(sdev->dev, "HDA codec #%d probed OK: response: %x\n",
		address, resp);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	hda_priv = devm_kzalloc(sdev->dev, sizeof(*hda_priv), GFP_KERNEL);
	if (!hda_priv)
		return -ENOMEM;

	hda_priv->codec.bus = hbus;
	hdev = &hda_priv->codec.core;
	codec = &hda_priv->codec;

	/* only probe ASoC codec drivers for HDAC-HDMI */
	if (!hda_codec_use_common_hdmi && (resp & 0xFFFF0000) == IDISP_VID_INTEL)
		type = HDA_DEV_ASOC;

	ret = snd_hdac_ext_bus_device_init(&hbus->core, address, hdev, type);
	if (ret < 0)
		return ret;

	if ((resp & 0xFFFF0000) == IDISP_VID_INTEL) {
		if (!hdev->bus->audio_component) {
			dev_dbg(sdev->dev,
				"iDisp hw present but no driver\n");
			ret = -ENOENT;
			goto out;
		}
		hda_priv->need_display_power = true;
	}

	if (is_generic_config(hbus))
		codec->probe_id = HDA_CODEC_ID_GENERIC;
	else
		codec->probe_id = 0;

	if (type == HDA_DEV_LEGACY) {
		ret = hda_codec_load_module(codec);
		/*
		 * handle ret==0 (no driver bound) as an error, but pass
		 * other return codes without modification
		 */
		if (ret == 0)
			ret = -ENOENT;
	}

out:
	if (ret < 0) {
		snd_hdac_device_unregister(hdev);
		put_device(&hdev->dev);
	}
#else
	hdev = devm_kzalloc(sdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	ret = snd_hdac_ext_bus_device_init(&hbus->core, address, hdev, HDA_DEV_ASOC);
#endif

	return ret;
}

/* Codec initialization */
void hda_codec_probe_bus(struct snd_sof_dev *sdev,
			 bool hda_codec_use_common_hdmi)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int i, ret;

	/* probe codecs in avail slots */
	for (i = 0; i < HDA_MAX_CODECS; i++) {

		if (!(bus->codec_mask & (1 << i)))
			continue;

		ret = hda_codec_probe(sdev, i, hda_codec_use_common_hdmi);
		if (ret < 0) {
			dev_warn(bus->dev, "codec #%d probe error, ret: %d\n",
				 i, ret);
			bus->codec_mask &= ~BIT(i);
		}
	}
}
EXPORT_SYMBOL_NS(hda_codec_probe_bus, SND_SOC_SOF_HDA_AUDIO_CODEC);

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI) || \
	IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)

void hda_codec_i915_display_power(struct snd_sof_dev *sdev, bool enable)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	if (HDA_IDISP_CODEC(bus->codec_mask)) {
		dev_dbg(bus->dev, "Turning i915 HDAC power %d\n", enable);
		snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, enable);
	}
}
EXPORT_SYMBOL_NS(hda_codec_i915_display_power, SND_SOC_SOF_HDA_AUDIO_CODEC_I915);

int hda_codec_i915_init(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* i915 exposes a HDA codec for HDMI audio */
	ret = snd_hdac_i915_init(bus);
	if (ret < 0)
		return ret;

	/* codec_mask not yet known, power up for probe */
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);

	return 0;
}
EXPORT_SYMBOL_NS(hda_codec_i915_init, SND_SOC_SOF_HDA_AUDIO_CODEC_I915);

int hda_codec_i915_exit(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	if (!bus->audio_component)
		return 0;

	/* power down unconditionally */
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	return snd_hdac_i915_exit(bus);
}
EXPORT_SYMBOL_NS(hda_codec_i915_exit, SND_SOC_SOF_HDA_AUDIO_CODEC_I915);

#endif

MODULE_LICENSE("Dual BSD/GPL");
