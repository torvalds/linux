// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
//
// ALSA SoC Machine driver for sc7280

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/rt5682s.h>
#include <linux/soundwire/sdw.h>

#include "../codecs/rt5682.h"
#include "../codecs/rt5682s.h"
#include "common.h"
#include "lpass.h"

#define DEFAULT_MCLK_RATE              19200000
#define RT5682_PLL_FREQ (48000 * 512)

struct sc7280_snd_data {
	struct snd_soc_card card;
	struct sdw_stream_runtime *sruntime[LPASS_MAX_PORTS];
	u32 pri_mi2s_clk_count;
	struct snd_soc_jack hs_jack;
	struct snd_soc_jack hdmi_jack;
	bool jack_setup;
	bool stream_prepared[LPASS_MAX_PORTS];
};

static void sc7280_jack_free(struct snd_jack *jack)
{
	struct snd_soc_component *component = jack->private_data;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sc7280_headset_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sc7280_snd_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_jack *jack;
	int rval, i;

	if (!pdata->jack_setup) {
		rval = snd_soc_card_jack_new(card, "Headset Jack",
					     SND_JACK_HEADSET | SND_JACK_LINEOUT |
					     SND_JACK_MECHANICAL |
					     SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					     SND_JACK_BTN_2 | SND_JACK_BTN_3 |
					     SND_JACK_BTN_4 | SND_JACK_BTN_5,
					     &pdata->hs_jack);

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headset Jack\n");
			return rval;
		}

		jack = pdata->hs_jack.jack;

		snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

		jack->private_data = component;
		jack->private_free = sc7280_jack_free;
		pdata->jack_setup = true;
	}
	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
	case LPASS_CDC_DMA_RX0:
	case LPASS_CDC_DMA_TX3:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_component_set_jack(component, &pdata->hs_jack, NULL);
			if (rval != 0 && rval != -ENOTSUPP) {
				dev_err(card->dev, "Failed to set jack: %d\n", rval);
				return rval;
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int sc7280_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sc7280_snd_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_jack *jack;
	int rval;

	rval = snd_soc_card_jack_new(card, "HDMI Jack",	SND_JACK_LINEOUT,
				     &pdata->hdmi_jack);

	if (rval < 0) {
		dev_err(card->dev, "Unable to add HDMI Jack\n");
		return rval;
	}

	jack = pdata->hdmi_jack.jack;
	jack->private_data = component;
	jack->private_free = sc7280_jack_free;

	return snd_soc_component_set_jack(component, &pdata->hdmi_jack, NULL);
}

static int sc7280_rt5682_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct sc7280_snd_data *data = snd_soc_card_get_drvdata(card);
	int ret;

	if (++data->pri_mi2s_clk_count == 1) {
		snd_soc_dai_set_sysclk(cpu_dai,
			LPASS_MCLK0,
			DEFAULT_MCLK_RATE,
			SNDRV_PCM_STREAM_PLAYBACK);
	}
	snd_soc_dai_set_fmt(codec_dai,
				SND_SOC_DAIFMT_CBC_CFC |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_I2S);

	ret = snd_soc_dai_set_pll(codec_dai, RT5682S_PLL2, RT5682S_PLL_S_MCLK,
					DEFAULT_MCLK_RATE, RT5682_PLL_FREQ);
	if (ret) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682S_SCLK_S_PLL2,
					RT5682_PLL_FREQ,
					SND_SOC_CLOCK_IN);

	if (ret) {
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int sc7280_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
	case LPASS_CDC_DMA_TX3:
		return sc7280_headset_init(rtd);
	case LPASS_CDC_DMA_RX0:
	case LPASS_CDC_DMA_VA_TX0:
	case MI2S_SECONDARY:
		return 0;
	case LPASS_DP_RX:
		return sc7280_hdmi_init(rtd);
	default:
		dev_err(rtd->dev, "%s: invalid dai id 0x%x\n", __func__, cpu_dai->id);
	}

	return -EINVAL;
}

static int sc7280_snd_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai;
	const struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sc7280_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime;
	int i;

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_RATE, 48000, 48000);

	switch (cpu_dai->id) {
	case LPASS_CDC_DMA_TX3:
	case LPASS_CDC_DMA_RX0:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			sruntime = snd_soc_dai_get_stream(codec_dai, substream->stream);
			if (sruntime != ERR_PTR(-ENOTSUPP))
				pdata->sruntime[cpu_dai->id] = sruntime;
		}
		break;
	}

	return 0;
}

static int sc7280_snd_swr_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	const struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sc7280_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];
	int ret;

	if (!sruntime)
		return 0;

	if (data->stream_prepared[cpu_dai->id]) {
		sdw_disable_stream(sruntime);
		sdw_deprepare_stream(sruntime);
		data->stream_prepared[cpu_dai->id] = false;
	}

	ret = sdw_prepare_stream(sruntime);
	if (ret)
		return ret;

	ret = sdw_enable_stream(sruntime);
	if (ret) {
		sdw_deprepare_stream(sruntime);
		return ret;
	}
	data->stream_prepared[cpu_dai->id] = true;

	return ret;
}

static int sc7280_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	const struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case LPASS_CDC_DMA_RX0:
	case LPASS_CDC_DMA_TX3:
		return sc7280_snd_swr_prepare(substream);
	default:
		break;
	}

	return 0;
}

static int sc7280_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sc7280_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	const struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	switch (cpu_dai->id) {
	case LPASS_CDC_DMA_RX0:
	case LPASS_CDC_DMA_TX3:
		if (sruntime && data->stream_prepared[cpu_dai->id]) {
			sdw_disable_stream(sruntime);
			sdw_deprepare_stream(sruntime);
			data->stream_prepared[cpu_dai->id] = false;
		}
		break;
	default:
		break;
	}
	return 0;
}

static void sc7280_snd_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct sc7280_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		if (--data->pri_mi2s_clk_count == 0) {
			snd_soc_dai_set_sysclk(cpu_dai,
					       LPASS_MCLK0,
					       0,
					       SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	default:
		break;
	}
}

static int sc7280_snd_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret = 0;

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		ret = sc7280_rt5682_init(rtd);
		break;
	default:
		break;
	}
	return ret;
}

static const struct snd_soc_ops sc7280_ops = {
	.startup = sc7280_snd_startup,
	.hw_params = sc7280_snd_hw_params,
	.hw_free = sc7280_snd_hw_free,
	.prepare = sc7280_snd_prepare,
	.shutdown = sc7280_snd_shutdown,
};

static const struct snd_soc_dapm_widget sc7280_snd_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static int sc7280_snd_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sc7280_snd_data *data;
	struct device *dev = &pdev->dev;
	struct snd_soc_dai_link *link;
	int ret, i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	snd_soc_card_set_drvdata(card, data);

	card->owner = THIS_MODULE;
	card->driver_name = "SC7280";
	card->dev = dev;

	card->dapm_widgets = sc7280_snd_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(sc7280_snd_widgets);

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	for_each_card_prelinks(card, i, link) {
		link->init = sc7280_init;
		link->ops = &sc7280_ops;
	}

	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id sc7280_snd_device_id[]  = {
	{ .compatible = "google,sc7280-herobrine" },
	{}
};
MODULE_DEVICE_TABLE(of, sc7280_snd_device_id);

static struct platform_driver sc7280_snd_driver = {
	.probe = sc7280_snd_platform_probe,
	.driver = {
		.name = "msm-snd-sc7280",
		.of_match_table = sc7280_snd_device_id,
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(sc7280_snd_driver);

MODULE_DESCRIPTION("sc7280 ASoC Machine Driver");
MODULE_LICENSE("GPL");
