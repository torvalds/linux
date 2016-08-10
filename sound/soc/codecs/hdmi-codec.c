/*
 * ALSA SoC codec for HDMI encoder drivers
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/pcm_drm_eld.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm_iec958.h>

#include <drm/drm_crtc.h> /* This is only to get MAX_ELD_BYTES */

struct hdmi_codec_priv {
	struct hdmi_codec_pdata hcd;
	struct snd_soc_dai_driver *daidrv;
	struct hdmi_codec_daifmt daifmt[2];
	struct mutex current_stream_lock;
	struct snd_pcm_substream *current_stream;
	struct snd_pcm_hw_constraint_list ratec;
	uint8_t eld[MAX_ELD_BYTES];
};

static const struct snd_soc_dapm_widget hdmi_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route hdmi_routes[] = {
	{ "TX", NULL, "Playback" },
};

enum {
	DAI_ID_I2S = 0,
	DAI_ID_SPDIF,
};

static int hdmi_eld_ctl_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(hcp->eld);

	return 0;
}

static int hdmi_eld_ctl_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct hdmi_codec_priv *hcp = snd_soc_component_get_drvdata(component);

	memcpy(ucontrol->value.bytes.data, hcp->eld, sizeof(hcp->eld));

	return 0;
}

static const struct snd_kcontrol_new hdmi_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "ELD",
		.info = hdmi_eld_ctl_info,
		.get = hdmi_eld_ctl_get,
	},
};

static int hdmi_codec_new_stream(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	mutex_lock(&hcp->current_stream_lock);
	if (!hcp->current_stream) {
		hcp->current_stream = substream;
	} else if (hcp->current_stream != substream) {
		dev_err(dai->dev, "Only one simultaneous stream supported!\n");
		ret = -EINVAL;
	}
	mutex_unlock(&hcp->current_stream_lock);

	return ret;
}

static int hdmi_codec_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	dev_dbg(dai->dev, "%s()\n", __func__);

	ret = hdmi_codec_new_stream(substream, dai);
	if (ret)
		return ret;

	if (hcp->hcd.ops->audio_startup) {
		ret = hcp->hcd.ops->audio_startup(dai->dev->parent, hcp->hcd.data);
		if (ret) {
			mutex_lock(&hcp->current_stream_lock);
			hcp->current_stream = NULL;
			mutex_unlock(&hcp->current_stream_lock);
			return ret;
		}
	}

	if (hcp->hcd.ops->get_eld) {
		ret = hcp->hcd.ops->get_eld(dai->dev->parent, hcp->hcd.data,
					    hcp->eld, sizeof(hcp->eld));

		if (!ret) {
			ret = snd_pcm_hw_constraint_eld(substream->runtime,
							hcp->eld);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static void hdmi_codec_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dai->dev, "%s()\n", __func__);

	WARN_ON(hcp->current_stream != substream);

	hcp->hcd.ops->audio_shutdown(dai->dev->parent, hcp->hcd.data);

	mutex_lock(&hcp->current_stream_lock);
	hcp->current_stream = NULL;
	mutex_unlock(&hcp->current_stream_lock);
}

static int hdmi_codec_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_params hp = {
		.iec = {
			.status = { 0 },
			.subcode = { 0 },
			.pad = 0,
			.dig_subframe = { 0 },
		}
	};
	int ret;

	dev_dbg(dai->dev, "%s() width %d rate %d channels %d\n", __func__,
		params_width(params), params_rate(params),
		params_channels(params));

	if (params_width(params) > 24)
		params->msbits = 24;

	ret = snd_pcm_create_iec958_consumer_hw_params(params, hp.iec.status,
						       sizeof(hp.iec.status));
	if (ret < 0) {
		dev_err(dai->dev, "Creating IEC958 channel status failed %d\n",
			ret);
		return ret;
	}

	ret = hdmi_codec_new_stream(substream, dai);
	if (ret)
		return ret;

	hdmi_audio_infoframe_init(&hp.cea);
	hp.cea.channels = params_channels(params);
	hp.cea.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	hp.cea.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	hp.cea.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;

	hp.sample_width = params_width(params);
	hp.sample_rate = params_rate(params);
	hp.channels = params_channels(params);

	return hcp->hcd.ops->hw_params(dai->dev->parent, hcp->hcd.data,
				       &hcp->daifmt[dai->id], &hp);
}

static int hdmi_codec_set_fmt(struct snd_soc_dai *dai,
			      unsigned int fmt)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);
	struct hdmi_codec_daifmt cf = { 0 };
	int ret = 0;

	dev_dbg(dai->dev, "%s()\n", __func__);

	if (dai->id == DAI_ID_SPDIF) {
		cf.fmt = HDMI_SPDIF;
	} else {
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:
			cf.bit_clk_master = 1;
			cf.frame_clk_master = 1;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
			cf.frame_clk_master = 1;
			break;
		case SND_SOC_DAIFMT_CBM_CFS:
			cf.bit_clk_master = 1;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:
			break;
		default:
			return -EINVAL;
		}

		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_NB_IF:
			cf.frame_clk_inv = 1;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			cf.bit_clk_inv = 1;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			cf.frame_clk_inv = 1;
			cf.bit_clk_inv = 1;
			break;
		}

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			cf.fmt = HDMI_I2S;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			cf.fmt = HDMI_DSP_A;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			cf.fmt = HDMI_DSP_B;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			cf.fmt = HDMI_RIGHT_J;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			cf.fmt = HDMI_LEFT_J;
			break;
		case SND_SOC_DAIFMT_AC97:
			cf.fmt = HDMI_AC97;
			break;
		default:
			dev_err(dai->dev, "Invalid DAI interface format\n");
			return -EINVAL;
		}
	}

	hcp->daifmt[dai->id] = cf;

	return ret;
}

static int hdmi_codec_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct hdmi_codec_priv *hcp = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dai->dev, "%s()\n", __func__);

	if (hcp->hcd.ops->digital_mute)
		return hcp->hcd.ops->digital_mute(dai->dev->parent,
						  hcp->hcd.data, mute);

	return 0;
}

static const struct snd_soc_dai_ops hdmi_dai_ops = {
	.startup	= hdmi_codec_startup,
	.shutdown	= hdmi_codec_shutdown,
	.hw_params	= hdmi_codec_hw_params,
	.set_fmt	= hdmi_codec_set_fmt,
	.digital_mute	= hdmi_codec_digital_mute,
};


#define HDMI_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
			 SNDRV_PCM_RATE_192000)

#define SPDIF_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE)

/*
 * This list is only for formats allowed on the I2S bus. So there is
 * some formats listed that are not supported by HDMI interface. For
 * instance allowing the 32-bit formats enables 24-precision with CPU
 * DAIs that do not support 24-bit formats. If the extra formats cause
 * problems, we should add the video side driver an option to disable
 * them.
 */
#define I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)

static struct snd_soc_dai_driver hdmi_i2s_dai = {
	.name = "i2s-hifi",
	.id = DAI_ID_I2S,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.ops = &hdmi_dai_ops,
};

static const struct snd_soc_dai_driver hdmi_spdif_dai = {
	.name = "spdif-hifi",
	.id = DAI_ID_SPDIF,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = HDMI_RATES,
		.formats = SPDIF_FORMATS,
	},
	.ops = &hdmi_dai_ops,
};

static struct snd_soc_codec_driver hdmi_codec = {
	.controls = hdmi_controls,
	.num_controls = ARRAY_SIZE(hdmi_controls),
	.dapm_widgets = hdmi_widgets,
	.num_dapm_widgets = ARRAY_SIZE(hdmi_widgets),
	.dapm_routes = hdmi_routes,
	.num_dapm_routes = ARRAY_SIZE(hdmi_routes),
};

static int hdmi_codec_probe(struct platform_device *pdev)
{
	struct hdmi_codec_pdata *hcd = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct hdmi_codec_priv *hcp;
	int dai_count, i = 0;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	if (!hcd) {
		dev_err(dev, "%s: No plalform data\n", __func__);
		return -EINVAL;
	}

	dai_count = hcd->i2s + hcd->spdif;
	if (dai_count < 1 || !hcd->ops || !hcd->ops->hw_params ||
	    !hcd->ops->audio_shutdown) {
		dev_err(dev, "%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}

	hcp = devm_kzalloc(dev, sizeof(*hcp), GFP_KERNEL);
	if (!hcp)
		return -ENOMEM;

	hcp->hcd = *hcd;
	mutex_init(&hcp->current_stream_lock);

	hcp->daidrv = devm_kzalloc(dev, dai_count * sizeof(*hcp->daidrv),
				   GFP_KERNEL);
	if (!hcp->daidrv)
		return -ENOMEM;

	if (hcd->i2s) {
		hcp->daidrv[i] = hdmi_i2s_dai;
		hcp->daidrv[i].playback.channels_max =
			hcd->max_i2s_channels;
		i++;
	}

	if (hcd->spdif)
		hcp->daidrv[i] = hdmi_spdif_dai;

	ret = snd_soc_register_codec(dev, &hdmi_codec, hcp->daidrv,
				     dai_count);
	if (ret) {
		dev_err(dev, "%s: snd_soc_register_codec() failed (%d)\n",
			__func__, ret);
		return ret;
	}

	dev_set_drvdata(dev, hcp);
	return 0;
}

static int hdmi_codec_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver hdmi_codec_driver = {
	.driver = {
		.name = HDMI_CODEC_DRV_NAME,
	},
	.probe = hdmi_codec_probe,
	.remove = hdmi_codec_remove,
};

module_platform_driver(hdmi_codec_driver);

MODULE_AUTHOR("Jyri Sarha <jsarha@ti.com>");
MODULE_DESCRIPTION("HDMI Audio Codec Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" HDMI_CODEC_DRV_NAME);
