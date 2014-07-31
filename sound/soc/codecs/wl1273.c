/*
 * ALSA SoC WL1273 codec driver
 *
 * Author:      Matti Aaltonen, <matti.j.aaltonen@nokia.com>
 *
 * Copyright:   (C) 2010, 2011 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/mfd/wl1273-core.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "wl1273.h"

enum wl1273_mode { WL1273_MODE_BT, WL1273_MODE_FM_RX, WL1273_MODE_FM_TX };

/* codec private data */
struct wl1273_priv {
	enum wl1273_mode mode;
	struct wl1273_core *core;
	unsigned int channels;
};

static int snd_wl1273_fm_set_i2s_mode(struct wl1273_core *core,
				      int rate, int width)
{
	struct device *dev = &core->client->dev;
	int r = 0;
	u16 mode;

	dev_dbg(dev, "rate: %d\n", rate);
	dev_dbg(dev, "width: %d\n", width);

	mutex_lock(&core->lock);

	mode = core->i2s_mode & ~WL1273_IS2_WIDTH & ~WL1273_IS2_RATE;

	switch (rate) {
	case 48000:
		mode |= WL1273_IS2_RATE_48K;
		break;
	case 44100:
		mode |= WL1273_IS2_RATE_44_1K;
		break;
	case 32000:
		mode |= WL1273_IS2_RATE_32K;
		break;
	case 22050:
		mode |= WL1273_IS2_RATE_22_05K;
		break;
	case 16000:
		mode |= WL1273_IS2_RATE_16K;
		break;
	case 12000:
		mode |= WL1273_IS2_RATE_12K;
		break;
	case 11025:
		mode |= WL1273_IS2_RATE_11_025;
		break;
	case 8000:
		mode |= WL1273_IS2_RATE_8K;
		break;
	default:
		dev_err(dev, "Sampling rate: %d not supported\n", rate);
		r = -EINVAL;
		goto out;
	}

	switch (width) {
	case 16:
		mode |= WL1273_IS2_WIDTH_32;
		break;
	case 20:
		mode |= WL1273_IS2_WIDTH_40;
		break;
	case 24:
		mode |= WL1273_IS2_WIDTH_48;
		break;
	case 25:
		mode |= WL1273_IS2_WIDTH_50;
		break;
	case 30:
		mode |= WL1273_IS2_WIDTH_60;
		break;
	case 32:
		mode |= WL1273_IS2_WIDTH_64;
		break;
	case 40:
		mode |= WL1273_IS2_WIDTH_80;
		break;
	case 48:
		mode |= WL1273_IS2_WIDTH_96;
		break;
	case 64:
		mode |= WL1273_IS2_WIDTH_128;
		break;
	default:
		dev_err(dev, "Data width: %d not supported\n", width);
		r = -EINVAL;
		goto out;
	}

	dev_dbg(dev, "WL1273_I2S_DEF_MODE: 0x%04x\n",  WL1273_I2S_DEF_MODE);
	dev_dbg(dev, "core->i2s_mode: 0x%04x\n", core->i2s_mode);
	dev_dbg(dev, "mode: 0x%04x\n", mode);

	if (core->i2s_mode != mode) {
		r = core->write(core, WL1273_I2S_MODE_CONFIG_SET, mode);
		if (r)
			goto out;

		core->i2s_mode = mode;
		r = core->write(core, WL1273_AUDIO_ENABLE,
				WL1273_AUDIO_ENABLE_I2S);
		if (r)
			goto out;
	}
out:
	mutex_unlock(&core->lock);

	return r;
}

static int snd_wl1273_fm_set_channel_number(struct wl1273_core *core,
					    int channel_number)
{
	struct device *dev = &core->client->dev;
	int r = 0;

	dev_dbg(dev, "%s\n", __func__);

	mutex_lock(&core->lock);

	if (core->channel_number == channel_number)
		goto out;

	if (channel_number == 1 && core->mode == WL1273_MODE_RX)
		r = core->write(core, WL1273_MOST_MODE_SET, WL1273_RX_MONO);
	else if (channel_number == 1 && core->mode == WL1273_MODE_TX)
		r = core->write(core, WL1273_MONO_SET, WL1273_TX_MONO);
	else if (channel_number == 2 && core->mode == WL1273_MODE_RX)
		r = core->write(core, WL1273_MOST_MODE_SET, WL1273_RX_STEREO);
	else if (channel_number == 2 && core->mode == WL1273_MODE_TX)
		r = core->write(core, WL1273_MONO_SET, WL1273_TX_STEREO);
	else
		r = -EINVAL;
out:
	mutex_unlock(&core->lock);

	return r;
}

static int snd_wl1273_get_audio_route(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wl1273->mode;

	return 0;
}

/*
 * TODO: Implement the audio routing in the driver. Now this control
 * only indicates the setting that has been done elsewhere (in the user
 * space).
 */
static const char * const wl1273_audio_route[] = { "Bt", "FmRx", "FmTx" };

static int snd_wl1273_set_audio_route(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	if (wl1273->mode == ucontrol->value.integer.value[0])
		return 0;

	/* Do not allow changes while stream is running */
	if (snd_soc_codec_is_active(codec))
		return -EPERM;

	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] >=  ARRAY_SIZE(wl1273_audio_route))
		return -EINVAL;

	wl1273->mode = ucontrol->value.integer.value[0];

	return 1;
}

static SOC_ENUM_SINGLE_EXT_DECL(wl1273_enum, wl1273_audio_route);

static int snd_wl1273_fm_audio_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	ucontrol->value.integer.value[0] = wl1273->core->audio_mode;

	return 0;
}

static int snd_wl1273_fm_audio_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->core->audio_mode == val)
		return 0;

	r = wl1273->core->set_audio(wl1273->core, val);
	if (r < 0)
		return r;

	return 1;
}

static const char * const wl1273_audio_strings[] = { "Digital", "Analog" };

static SOC_ENUM_SINGLE_EXT_DECL(wl1273_audio_enum, wl1273_audio_strings);

static int snd_wl1273_fm_volume_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	ucontrol->value.integer.value[0] = wl1273->core->volume;

	return 0;
}

static int snd_wl1273_fm_volume_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int r;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	r = wl1273->core->set_volume(wl1273->core,
				     ucontrol->value.integer.value[0]);
	if (r)
		return r;

	return 1;
}

static const struct snd_kcontrol_new wl1273_controls[] = {
	SOC_ENUM_EXT("Codec Mode", wl1273_enum,
		     snd_wl1273_get_audio_route, snd_wl1273_set_audio_route),
	SOC_ENUM_EXT("Audio Switch", wl1273_audio_enum,
		     snd_wl1273_fm_audio_get,  snd_wl1273_fm_audio_put),
	SOC_SINGLE_EXT("Volume", 0, 0, WL1273_MAX_VOLUME, 0,
		       snd_wl1273_fm_volume_get, snd_wl1273_fm_volume_put),
};

static const struct snd_soc_dapm_widget wl1273_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),

	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route wl1273_dapm_routes[] = {
	{ "Capture", NULL, "RX" },

	{ "TX", NULL, "Playback" },
};

static int wl1273_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	switch (wl1273->mode) {
	case WL1273_MODE_BT:
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_RATE,
					     8000, 8000);
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS, 1, 1);
		break;
	case WL1273_MODE_FM_RX:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			pr_err("Cannot play in RX mode.\n");
			return -EINVAL;
		}
		break;
	case WL1273_MODE_FM_TX:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			pr_err("Cannot capture in TX mode.\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

static int wl1273_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(dai->codec);
	struct wl1273_core *core = wl1273->core;
	unsigned int rate, width, r;

	if (params_width(params) != 16) {
		dev_err(dai->dev, "%d bits/sample not supported\n",
			params_width(params));
		return -EINVAL;
	}

	rate = params_rate(params);
	width =  hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;

	if (wl1273->mode == WL1273_MODE_BT) {
		if (rate != 8000) {
			pr_err("Rate %d not supported.\n", params_rate(params));
			return -EINVAL;
		}

		if (params_channels(params) != 1) {
			pr_err("Only mono supported.\n");
			return -EINVAL;
		}

		return 0;
	}

	if (wl1273->mode == WL1273_MODE_FM_TX &&
	    substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("Only playback supported with TX.\n");
		return -EINVAL;
	}

	if (wl1273->mode == WL1273_MODE_FM_RX  &&
	    substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_err("Only capture supported with RX.\n");
		return -EINVAL;
	}

	if (wl1273->mode != WL1273_MODE_FM_RX  &&
	    wl1273->mode != WL1273_MODE_FM_TX) {
		pr_err("Unexpected mode: %d.\n", wl1273->mode);
		return -EINVAL;
	}

	r = snd_wl1273_fm_set_i2s_mode(core, rate, width);
	if (r)
		return r;

	wl1273->channels = params_channels(params);
	r = snd_wl1273_fm_set_channel_number(core, wl1273->channels);
	if (r)
		return r;

	return 0;
}

static const struct snd_soc_dai_ops wl1273_dai_ops = {
	.startup	= wl1273_startup,
	.hw_params	= wl1273_hw_params,
};

static struct snd_soc_dai_driver wl1273_dai = {
	.name = "wl1273-fm",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE},
	.ops = &wl1273_dai_ops,
};

/* Audio interface format for the soc_card driver */
int wl1273_get_format(struct snd_soc_codec *codec, unsigned int *fmt)
{
	struct wl1273_priv *wl1273;

	if (codec == NULL || fmt == NULL)
		return -EINVAL;

	wl1273 = snd_soc_codec_get_drvdata(codec);

	switch (wl1273->mode) {
	case WL1273_MODE_FM_RX:
	case WL1273_MODE_FM_TX:
		*fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		break;
	case WL1273_MODE_BT:
		*fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wl1273_get_format);

static int wl1273_probe(struct snd_soc_codec *codec)
{
	struct wl1273_core **core = codec->dev->platform_data;
	struct wl1273_priv *wl1273;
	int r;

	dev_dbg(codec->dev, "%s.\n", __func__);

	if (!core) {
		dev_err(codec->dev, "Platform data is missing.\n");
		return -EINVAL;
	}

	wl1273 = kzalloc(sizeof(struct wl1273_priv), GFP_KERNEL);
	if (wl1273 == NULL) {
		dev_err(codec->dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	wl1273->mode = WL1273_MODE_BT;
	wl1273->core = *core;

	snd_soc_codec_set_drvdata(codec, wl1273);

	r = snd_soc_add_codec_controls(codec, wl1273_controls,
				 ARRAY_SIZE(wl1273_controls));
	if (r)
		kfree(wl1273);

	return r;
}

static int wl1273_remove(struct snd_soc_codec *codec)
{
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s\n", __func__);
	kfree(wl1273);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wl1273 = {
	.probe = wl1273_probe,
	.remove = wl1273_remove,

	.dapm_widgets = wl1273_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wl1273_dapm_widgets),
	.dapm_routes = wl1273_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wl1273_dapm_routes),
};

static int wl1273_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wl1273,
				      &wl1273_dai, 1);
}

static int wl1273_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

MODULE_ALIAS("platform:wl1273-codec");

static struct platform_driver wl1273_platform_driver = {
	.driver		= {
		.name	= "wl1273-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= wl1273_platform_probe,
	.remove		= wl1273_platform_remove,
};

module_platform_driver(wl1273_platform_driver);

MODULE_AUTHOR("Matti Aaltonen <matti.j.aaltonen@nokia.com>");
MODULE_DESCRIPTION("ASoC WL1273 codec driver");
MODULE_LICENSE("GPL");
