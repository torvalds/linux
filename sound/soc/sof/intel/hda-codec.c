// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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
#include <sound/hda_codec.h>
#include <sound/hda_i915.h>
#include <sound/sof.h>
#include "../ops.h"
#include "hda.h"
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#include "../../codecs/hdac_hda.h"
#endif /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#define IDISP_VID_INTEL	0x80860000

/* load the legacy HDA codec driver */
#ifdef MODULE
static void hda_codec_load_module(struct hda_codec *codec)
{
	char alias[MODULE_NAME_LEN];
	const char *module = alias;

	snd_hdac_codec_modalias(&codec->core, alias, sizeof(alias));
	dev_dbg(&codec->core.dev, "loading codec module: %s\n", module);
	request_module(module);
}
#else
static void hda_codec_load_module(struct hda_codec *codec) {}
#endif

#endif /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */

/* probe individual codec */
static int hda_codec_probe(struct snd_sof_dev *sdev, int address)
{
	struct hda_bus *hbus = sof_to_hbus(sdev);
	struct hdac_device *hdev;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	struct hdac_hda_priv *hda_priv;
#endif
	u32 hda_cmd = (address << 28) | (AC_NODE_ROOT << 20) |
		(AC_VERB_PARAMETERS << 8) | AC_PAR_VENDOR_ID;
	u32 resp = -1;
	int ret;

	mutex_lock(&hbus->core.cmd_mutex);
	snd_hdac_bus_send_cmd(&hbus->core, hda_cmd);
	snd_hdac_bus_get_response(&hbus->core, address, &resp);
	mutex_unlock(&hbus->core.cmd_mutex);
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

	ret = snd_hdac_ext_bus_device_init(&hbus->core, address, hdev);
	if (ret < 0)
		return ret;

	/* use legacy bus only for HDA codecs, idisp uses ext bus */
	if ((resp & 0xFFFF0000) != IDISP_VID_INTEL) {
		hdev->type = HDA_DEV_LEGACY;
		hda_codec_load_module(&hda_priv->codec);
	}

	return 0;
#else
	hdev = devm_kzalloc(sdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	ret = snd_hdac_ext_bus_device_init(&hbus->core, address, hdev);

	return ret;
#endif
}

/* Codec initialization */
int hda_codec_probe_bus(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int i, ret;

	/* probe codecs in avail slots */
	for (i = 0; i < HDA_MAX_CODECS; i++) {

		if (!(bus->codec_mask & (1 << i)))
			continue;

		ret = hda_codec_probe(sdev, i);
		if (ret < 0) {
			dev_err(bus->dev, "error: codec #%d probe error, ret: %d\n",
				i, ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(hda_codec_probe_bus);

#if IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)

void hda_codec_i915_get(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	dev_dbg(bus->dev, "Turning i915 HDAC power on\n");
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, true);
}
EXPORT_SYMBOL(hda_codec_i915_get);

void hda_codec_i915_put(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

	dev_dbg(bus->dev, "Turning i915 HDAC power off\n");
	snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);
}
EXPORT_SYMBOL(hda_codec_i915_put);

int hda_codec_i915_init(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	/* i915 exposes a HDA codec for HDMI audio */
	ret = snd_hdac_i915_init(bus);
	if (ret < 0)
		return ret;

	hda_codec_i915_get(sdev);

	return 0;
}
EXPORT_SYMBOL(hda_codec_i915_init);

int hda_codec_i915_exit(struct snd_sof_dev *sdev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);
	int ret;

	hda_codec_i915_put(sdev);

	ret = snd_hdac_i915_exit(bus);

	return ret;
}
EXPORT_SYMBOL(hda_codec_i915_exit);

#endif /* CONFIG_SND_SOC_HDAC_HDMI */

MODULE_LICENSE("Dual BSD/GPL");
