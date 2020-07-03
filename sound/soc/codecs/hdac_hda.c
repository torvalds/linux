// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-18 Intel Corporation.

/*
 * hdac_hda.c - ASoC extensions to reuse the legacy HDA codec drivers
 * with ASoC platform drivers. These APIs are called by the legacy HDA
 * codec drivers using hdac_ext_bus_ops ops.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_i915.h>
#include <sound/hda_codec.h>
#include <sound/hda_register.h>

#include "hdac_hda.h"

#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_U8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_U24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE | \
			SNDRV_PCM_FMTBIT_U32_LE | \
			SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

#define STUB_HDMI_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
				 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
				 SNDRV_PCM_RATE_192000)

static int hdac_hda_dai_open(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai);
static void hdac_hda_dai_close(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai);
static int hdac_hda_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai);
static int hdac_hda_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai);
static int hdac_hda_dai_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai);
static int hdac_hda_dai_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width);
static struct hda_pcm *snd_soc_find_pcm_from_dai(struct hdac_hda_priv *hda_pvt,
						 struct snd_soc_dai *dai);

static const struct snd_soc_dai_ops hdac_hda_dai_ops = {
	.startup = hdac_hda_dai_open,
	.shutdown = hdac_hda_dai_close,
	.prepare = hdac_hda_dai_prepare,
	.hw_params = hdac_hda_dai_hw_params,
	.hw_free = hdac_hda_dai_hw_free,
	.set_tdm_slot = hdac_hda_dai_set_tdm_slot,
};

static struct snd_soc_dai_driver hdac_hda_dais[] = {
{
	.id = HDAC_ANALOG_DAI_ID,
	.name = "Analog Codec DAI",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name	= "Analog Codec Playback",
		.channels_min	= 1,
		.channels_max	= 16,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= STUB_FORMATS,
		.sig_bits	= 24,
	},
	.capture = {
		.stream_name    = "Analog Codec Capture",
		.channels_min   = 1,
		.channels_max   = 16,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_DIGITAL_DAI_ID,
	.name = "Digital Codec DAI",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name    = "Digital Codec Playback",
		.channels_min   = 1,
		.channels_max   = 16,
		.rates          = SNDRV_PCM_RATE_8000_192000,
		.formats        = STUB_FORMATS,
		.sig_bits = 24,
	},
	.capture = {
		.stream_name    = "Digital Codec Capture",
		.channels_min   = 1,
		.channels_max   = 16,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_ALT_ANALOG_DAI_ID,
	.name = "Alt Analog Codec DAI",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name	= "Alt Analog Codec Playback",
		.channels_min	= 1,
		.channels_max	= 16,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= STUB_FORMATS,
		.sig_bits	= 24,
	},
	.capture = {
		.stream_name    = "Alt Analog Codec Capture",
		.channels_min   = 1,
		.channels_max   = 16,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_HDMI_0_DAI_ID,
	.name = "intel-hdmi-hifi1",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name    = "hifi1",
		.channels_min   = 1,
		.channels_max   = 32,
		.rates          = STUB_HDMI_RATES,
		.formats        = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_HDMI_1_DAI_ID,
	.name = "intel-hdmi-hifi2",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name    = "hifi2",
		.channels_min   = 1,
		.channels_max   = 32,
		.rates          = STUB_HDMI_RATES,
		.formats        = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_HDMI_2_DAI_ID,
	.name = "intel-hdmi-hifi3",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name    = "hifi3",
		.channels_min   = 1,
		.channels_max   = 32,
		.rates          = STUB_HDMI_RATES,
		.formats        = STUB_FORMATS,
		.sig_bits = 24,
	},
},
{
	.id = HDAC_HDMI_3_DAI_ID,
	.name = "intel-hdmi-hifi4",
	.ops = &hdac_hda_dai_ops,
	.playback = {
		.stream_name    = "hifi4",
		.channels_min   = 1,
		.channels_max   = 32,
		.rates          = STUB_HDMI_RATES,
		.formats        = STUB_FORMATS,
		.sig_bits = 24,
	},
},

};

static int hdac_hda_dai_set_tdm_slot(struct snd_soc_dai *dai,
				     unsigned int tx_mask, unsigned int rx_mask,
				     int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hda_priv *hda_pvt;
	struct hdac_hda_pcm *pcm;

	hda_pvt = snd_soc_component_get_drvdata(component);
	pcm = &hda_pvt->pcm[dai->id];

	if (tx_mask)
		pcm->stream_tag[SNDRV_PCM_STREAM_PLAYBACK] = tx_mask;
	else
		pcm->stream_tag[SNDRV_PCM_STREAM_CAPTURE] = rx_mask;

	return 0;
}

static int hdac_hda_dai_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hda_priv *hda_pvt;
	unsigned int format_val;
	unsigned int maxbps;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		maxbps = dai->driver->playback.sig_bits;
	else
		maxbps = dai->driver->capture.sig_bits;

	hda_pvt = snd_soc_component_get_drvdata(component);
	format_val = snd_hdac_calc_stream_format(params_rate(params),
						 params_channels(params),
						 params_format(params),
						 maxbps,
						 0);
	if (!format_val) {
		dev_err(dai->dev,
			"invalid format_val, rate=%d, ch=%d, format=%d, maxbps=%d\n",
			params_rate(params), params_channels(params),
			params_format(params), maxbps);

		return -EINVAL;
	}

	hda_pvt->pcm[dai->id].format_val[substream->stream] = format_val;
	return 0;
}

static int hdac_hda_dai_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hda_priv *hda_pvt;
	struct hda_pcm_stream *hda_stream;
	struct hda_pcm *pcm;

	hda_pvt = snd_soc_component_get_drvdata(component);
	pcm = snd_soc_find_pcm_from_dai(hda_pvt, dai);
	if (!pcm)
		return -EINVAL;

	hda_stream = &pcm->stream[substream->stream];
	snd_hda_codec_cleanup(&hda_pvt->codec, hda_stream, substream);

	return 0;
}

static int hdac_hda_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct hda_pcm_stream *hda_stream;
	struct hdac_hda_priv *hda_pvt;
	struct hdac_device *hdev;
	unsigned int format_val;
	struct hda_pcm *pcm;
	unsigned int stream;
	int ret = 0;

	hda_pvt = snd_soc_component_get_drvdata(component);
	hdev = &hda_pvt->codec.core;
	pcm = snd_soc_find_pcm_from_dai(hda_pvt, dai);
	if (!pcm)
		return -EINVAL;

	hda_stream = &pcm->stream[substream->stream];

	stream = hda_pvt->pcm[dai->id].stream_tag[substream->stream];
	format_val = hda_pvt->pcm[dai->id].format_val[substream->stream];

	ret = snd_hda_codec_prepare(&hda_pvt->codec, hda_stream,
				    stream, format_val, substream);
	if (ret < 0)
		dev_err(&hdev->dev, "codec prepare failed %d\n", ret);

	return ret;
}

static int hdac_hda_dai_open(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hda_priv *hda_pvt;
	struct hda_pcm_stream *hda_stream;
	struct hda_pcm *pcm;
	int ret;

	hda_pvt = snd_soc_component_get_drvdata(component);
	pcm = snd_soc_find_pcm_from_dai(hda_pvt, dai);
	if (!pcm)
		return -EINVAL;

	snd_hda_codec_pcm_get(pcm);

	hda_stream = &pcm->stream[substream->stream];

	ret = hda_stream->ops.open(hda_stream, &hda_pvt->codec, substream);
	if (ret < 0)
		snd_hda_codec_pcm_put(pcm);

	return ret;
}

static void hdac_hda_dai_close(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct hdac_hda_priv *hda_pvt;
	struct hda_pcm_stream *hda_stream;
	struct hda_pcm *pcm;

	hda_pvt = snd_soc_component_get_drvdata(component);
	pcm = snd_soc_find_pcm_from_dai(hda_pvt, dai);
	if (!pcm)
		return;

	hda_stream = &pcm->stream[substream->stream];

	hda_stream->ops.close(hda_stream, &hda_pvt->codec, substream);

	snd_hda_codec_pcm_put(pcm);
}

static struct hda_pcm *snd_soc_find_pcm_from_dai(struct hdac_hda_priv *hda_pvt,
						 struct snd_soc_dai *dai)
{
	struct hda_codec *hcodec = &hda_pvt->codec;
	struct hda_pcm *cpcm;
	const char *pcm_name;

	/*
	 * map DAI ID to the closest matching PCM name, using the naming
	 * scheme used by hda-codec snd_hda_gen_build_pcms() and for
	 * HDMI in hda_codec patch_hdmi.c)
	 */

	switch (dai->id) {
	case HDAC_ANALOG_DAI_ID:
		pcm_name = "Analog";
		break;
	case HDAC_DIGITAL_DAI_ID:
		pcm_name = "Digital";
		break;
	case HDAC_ALT_ANALOG_DAI_ID:
		pcm_name = "Alt Analog";
		break;
	case HDAC_HDMI_0_DAI_ID:
		pcm_name = "HDMI 0";
		break;
	case HDAC_HDMI_1_DAI_ID:
		pcm_name = "HDMI 1";
		break;
	case HDAC_HDMI_2_DAI_ID:
		pcm_name = "HDMI 2";
		break;
	case HDAC_HDMI_3_DAI_ID:
		pcm_name = "HDMI 3";
		break;
	default:
		dev_err(&hcodec->core.dev, "invalid dai id %d\n", dai->id);
		return NULL;
	}

	list_for_each_entry(cpcm, &hcodec->pcm_list_head, list) {
		if (strstr(cpcm->name, pcm_name))
			return cpcm;
	}

	dev_err(&hcodec->core.dev, "didn't find PCM for DAI %s\n", dai->name);
	return NULL;
}

static bool is_hdmi_codec(struct hda_codec *hcodec)
{
	struct hda_pcm *cpcm;

	list_for_each_entry(cpcm, &hcodec->pcm_list_head, list) {
		if (cpcm->pcm_type == HDA_PCM_TYPE_HDMI)
			return true;
	}

	return false;
}

static int hdac_hda_codec_probe(struct snd_soc_component *component)
{
	struct hdac_hda_priv *hda_pvt =
			snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
	struct hdac_device *hdev = &hda_pvt->codec.core;
	struct hda_codec *hcodec = &hda_pvt->codec;
	struct hdac_ext_link *hlink;
	hda_codec_patch_t patch;
	int ret;

	hlink = snd_hdac_ext_bus_get_link(hdev->bus, dev_name(&hdev->dev));
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return -EIO;
	}

	snd_hdac_ext_bus_link_get(hdev->bus, hlink);

	/*
	 * Ensure any HDA display is powered at codec probe.
	 * After snd_hda_codec_device_new(), display power is
	 * managed by runtime PM.
	 */
	if (hda_pvt->need_display_power)
		snd_hdac_display_power(hdev->bus,
				       HDA_CODEC_IDX_CONTROLLER, true);

	ret = snd_hda_codec_device_new(hcodec->bus, component->card->snd_card,
				       hdev->addr, hcodec);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to create hda codec %d\n", ret);
		goto error_no_pm;
	}
	/*
	 * Overwrite type to HDA_DEV_ASOC since it is a ASoC driver
	 * hda_codec.c will check this flag to determine if unregister
	 * device is needed.
	 */
	hdev->type = HDA_DEV_ASOC;

	/*
	 * snd_hda_codec_device_new decrements the usage count so call get pm
	 * else the device will be powered off
	 */
	pm_runtime_get_noresume(&hdev->dev);

	hcodec->bus->card = dapm->card->snd_card;

	ret = snd_hda_codec_set_name(hcodec, hcodec->preset->name);
	if (ret < 0) {
		dev_err(&hdev->dev, "name failed %s\n", hcodec->preset->name);
		goto error_pm;
	}

	ret = snd_hdac_regmap_init(&hcodec->core);
	if (ret < 0) {
		dev_err(&hdev->dev, "regmap init failed\n");
		goto error_pm;
	}

	patch = (hda_codec_patch_t)hcodec->preset->driver_data;
	if (patch) {
		ret = patch(hcodec);
		if (ret < 0) {
			dev_err(&hdev->dev, "patch failed %d\n", ret);
			goto error_regmap;
		}
	} else {
		dev_dbg(&hdev->dev, "no patch file found\n");
	}

	/* configure codec for 1:1 PCM:DAI mapping */
	hcodec->mst_no_extra_pcms = 1;

	ret = snd_hda_codec_parse_pcms(hcodec);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to map pcms to dai %d\n", ret);
		goto error_regmap;
	}

	/* HDMI controls need to be created in machine drivers */
	if (!is_hdmi_codec(hcodec)) {
		ret = snd_hda_codec_build_controls(hcodec);
		if (ret < 0) {
			dev_err(&hdev->dev, "unable to create controls %d\n",
				ret);
			goto error_regmap;
		}
	}

	hcodec->core.lazy_cache = true;

	if (hda_pvt->need_display_power)
		snd_hdac_display_power(hdev->bus,
				       HDA_CODEC_IDX_CONTROLLER, false);

	/*
	 * hdac_device core already sets the state to active and calls
	 * get_noresume. So enable runtime and set the device to suspend.
	 * pm_runtime_enable is also called during codec registeration
	 */
	pm_runtime_put(&hdev->dev);
	pm_runtime_suspend(&hdev->dev);

	return 0;

error_regmap:
	snd_hdac_regmap_exit(hdev);
error_pm:
	pm_runtime_put(&hdev->dev);
error_no_pm:
	snd_hdac_ext_bus_link_put(hdev->bus, hlink);
	return ret;
}

static void hdac_hda_codec_remove(struct snd_soc_component *component)
{
	struct hdac_hda_priv *hda_pvt =
		      snd_soc_component_get_drvdata(component);
	struct hdac_device *hdev = &hda_pvt->codec.core;
	struct hdac_ext_link *hlink = NULL;

	hlink = snd_hdac_ext_bus_get_link(hdev->bus, dev_name(&hdev->dev));
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return;
	}

	pm_runtime_disable(&hdev->dev);
	snd_hdac_ext_bus_link_put(hdev->bus, hlink);

	snd_hdac_regmap_exit(hdev);
}

static const struct snd_soc_dapm_route hdac_hda_dapm_routes[] = {
	{"AIF1TX", NULL, "Codec Input Pin1"},
	{"AIF2TX", NULL, "Codec Input Pin2"},
	{"AIF3TX", NULL, "Codec Input Pin3"},

	{"Codec Output Pin1", NULL, "AIF1RX"},
	{"Codec Output Pin2", NULL, "AIF2RX"},
	{"Codec Output Pin3", NULL, "AIF3RX"},
};

static const struct snd_soc_dapm_widget hdac_hda_dapm_widgets[] = {
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "Analog Codec Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "Digital Codec Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "Alt Analog Codec Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "Analog Codec Capture", 0,
			     SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "Digital Codec Capture", 0,
			     SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "Alt Analog Codec Capture", 0,
			     SND_SOC_NOPM, 0, 0),

	/* Input Pins */
	SND_SOC_DAPM_INPUT("Codec Input Pin1"),
	SND_SOC_DAPM_INPUT("Codec Input Pin2"),
	SND_SOC_DAPM_INPUT("Codec Input Pin3"),

	/* Output Pins */
	SND_SOC_DAPM_OUTPUT("Codec Output Pin1"),
	SND_SOC_DAPM_OUTPUT("Codec Output Pin2"),
	SND_SOC_DAPM_OUTPUT("Codec Output Pin3"),
};

static const struct snd_soc_component_driver hdac_hda_codec = {
	.probe		= hdac_hda_codec_probe,
	.remove		= hdac_hda_codec_remove,
	.idle_bias_on	= false,
	.dapm_widgets           = hdac_hda_dapm_widgets,
	.num_dapm_widgets       = ARRAY_SIZE(hdac_hda_dapm_widgets),
	.dapm_routes            = hdac_hda_dapm_routes,
	.num_dapm_routes        = ARRAY_SIZE(hdac_hda_dapm_routes),
};

static int hdac_hda_dev_probe(struct hdac_device *hdev)
{
	struct hdac_ext_link *hlink;
	struct hdac_hda_priv *hda_pvt;
	int ret;

	/* hold the ref while we probe */
	hlink = snd_hdac_ext_bus_get_link(hdev->bus, dev_name(&hdev->dev));
	if (!hlink) {
		dev_err(&hdev->dev, "hdac link not found\n");
		return -EIO;
	}
	snd_hdac_ext_bus_link_get(hdev->bus, hlink);

	hda_pvt = hdac_to_hda_priv(hdev);
	if (!hda_pvt)
		return -ENOMEM;

	/* ASoC specific initialization */
	ret = devm_snd_soc_register_component(&hdev->dev,
					 &hdac_hda_codec, hdac_hda_dais,
					 ARRAY_SIZE(hdac_hda_dais));
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to register HDA codec %d\n", ret);
		return ret;
	}

	dev_set_drvdata(&hdev->dev, hda_pvt);
	snd_hdac_ext_bus_link_put(hdev->bus, hlink);

	return ret;
}

static int hdac_hda_dev_remove(struct hdac_device *hdev)
{
	struct hdac_hda_priv *hda_pvt;

	hda_pvt = dev_get_drvdata(&hdev->dev);
	if (hda_pvt && hda_pvt->codec.registered)
		cancel_delayed_work_sync(&hda_pvt->codec.jackpoll_work);

	return 0;
}

static struct hdac_ext_bus_ops hdac_ops = {
	.hdev_attach = hdac_hda_dev_probe,
	.hdev_detach = hdac_hda_dev_remove,
};

struct hdac_ext_bus_ops *snd_soc_hdac_hda_get_ops(void)
{
	return &hdac_ops;
}
EXPORT_SYMBOL_GPL(snd_soc_hdac_hda_get_ops);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Extensions for legacy HDA Drivers");
MODULE_AUTHOR("Rakesh Ughreja<rakesh.a.ughreja@intel.com>");
