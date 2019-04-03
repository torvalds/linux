// SPDX-License-Identifier: GPL-2.0
//
// mt8183-da7219-max98357.c
//	--  MT8183-DA7219-MAX98357 ALSA SoC machine driver
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Shunli Wang <shunli.wang@mediatek.com>

#include <linux/module.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/pinctrl/consumer.h>

#include "mt8183-afe-common.h"
#include "../../codecs/da7219-aad.h"
#include "../../codecs/da7219.h"

static struct snd_soc_jack headset_jack;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin headset_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static struct snd_soc_dai_link_component
mt8183_da7219_max98357_external_codecs[] = {
	{
		.name = "max98357a",
		.dai_name = "HiFi",
	},
	{
		.name = "da7219.5-001a",
		.dai_name = "da7219-hifi",
	},
};

static int mt8183_mt6358_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 128;
	unsigned int mclk_fs = rate * mclk_fs_ratio;

	return snd_soc_dai_set_sysclk(rtd->cpu_dai,
				      0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8183_mt6358_i2s_ops = {
	.hw_params = mt8183_mt6358_i2s_hw_params,
};

static int mt8183_da7219_i2s_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 256;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	unsigned int freq;
	int ret = 0, j;

	ret = snd_soc_dai_set_sysclk(rtd->cpu_dai, 0,
				     mclk_fs, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		dev_err(rtd->dev, "failed to set cpu dai sysclk\n");

	for (j = 0; j < rtd->num_codecs; j++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[j];

		if (!strcmp(codec_dai->component->name, "da7219.5-001a")) {
			ret = snd_soc_dai_set_sysclk(codec_dai,
						     DA7219_CLKSRC_MCLK,
						     mclk_fs,
						     SND_SOC_CLOCK_IN);
			if (ret < 0)
				dev_err(rtd->dev, "failed to set sysclk\n");

			if ((rate % 8000) == 0)
				freq = DA7219_PLL_FREQ_OUT_98304;
			else
				freq = DA7219_PLL_FREQ_OUT_90316;

			ret = snd_soc_dai_set_pll(codec_dai, 0,
						  DA7219_SYSCLK_PLL_SRM,
						  0, freq);
			if (ret)
				dev_err(rtd->dev, "failed to start PLL: %d\n",
					ret);
		}
	}

	return ret;
}

static int mt8183_da7219_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret = 0, j;

	for (j = 0; j < rtd->num_codecs; j++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[j];

		if (!strcmp(codec_dai->component->name, "da7219.5-001a")) {
			ret = snd_soc_dai_set_pll(codec_dai,
						  0, DA7219_SYSCLK_MCLK, 0, 0);
			if (ret < 0) {
				dev_err(rtd->dev, "failed to stop PLL: %d\n",
					ret);
				break;
			}
		}
	}

	return ret;
}

static const struct snd_soc_ops mt8183_da7219_i2s_ops = {
	.hw_params = mt8183_da7219_i2s_hw_params,
	.hw_free = mt8183_da7219_hw_free,
};

static int mt8183_i2s_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	/* fix BE i2s format to 32bit, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S32_LE);

	return 0;
}

static const struct snd_soc_dapm_widget
mt8183_da7219_max98357_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("IT6505_8CH"),
};

static const struct snd_soc_dapm_route mt8183_da7219_max98357_dapm_routes[] = {
	{"IT6505_8CH", NULL, "TDM"},
};

static struct snd_soc_dai_link mt8183_da7219_max98357_dai_links[] = {
	/* FE */
	{
		.name = "Playback_1",
		.stream_name = "Playback_1",
		.cpu_dai_name = "DL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "Playback_2",
		.stream_name = "Playback_2",
		.cpu_dai_name = "DL2",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "Playback_3",
		.stream_name = "Playback_3",
		.cpu_dai_name = "DL3",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "Capture_1",
		.stream_name = "Capture_1",
		.cpu_dai_name = "UL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "Capture_2",
		.stream_name = "Capture_2",
		.cpu_dai_name = "UL2",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "Capture_3",
		.stream_name = "Capture_3",
		.cpu_dai_name = "UL3",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "Capture_Mono_1",
		.stream_name = "Capture_Mono_1",
		.cpu_dai_name = "UL_MONO_1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "Playback_HDMI",
		.stream_name = "Playback_HDMI",
		.cpu_dai_name = "HDMI",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_PRE,
			    SND_SOC_DPCM_TRIGGER_PRE},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	/* BE */
	{
		.name = "Primary Codec",
		.cpu_dai_name = "ADDA",
		.codec_dai_name = "mt6358-snd-codec-aif1",
		.codec_name = "mt6358-sound",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "PCM 1",
		.cpu_dai_name = "PCM 1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "PCM 2",
		.cpu_dai_name = "PCM 2",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "I2S0",
		.cpu_dai_name = "I2S0",
		.codec_dai_name = "bt-sco-pcm",
		.codec_name = "bt-sco",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
	},
	{
		.name = "I2S1",
		.cpu_dai_name = "I2S1",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
	},
	{
		.name = "I2S2",
		.cpu_dai_name = "I2S2",
		.codec_dai_name = "da7219-hifi",
		.codec_name = "da7219.5-001a",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_da7219_i2s_ops,
	},
	{
		.name = "I2S3",
		.cpu_dai_name = "I2S3",
		.codecs = mt8183_da7219_max98357_external_codecs,
		.num_codecs =
			ARRAY_SIZE(mt8183_da7219_max98357_external_codecs),
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_da7219_i2s_ops,
	},
	{
		.name = "I2S5",
		.cpu_dai_name = "I2S5",
		.codec_dai_name = "bt-sco-pcm",
		.codec_name = "bt-sco",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
		.be_hw_params_fixup = mt8183_i2s_hw_params_fixup,
		.ops = &mt8183_mt6358_i2s_ops,
	},
	{
		.name = "TDM",
		.cpu_dai_name = "TDM",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ignore_suspend = 1,
	},
};

static int
mt8183_da7219_max98357_headset_init(struct snd_soc_component *component);

static struct snd_soc_aux_dev mt8183_da7219_max98357_headset_dev = {
	.name = "Headset Chip",
	.init = mt8183_da7219_max98357_headset_init,
};

static struct snd_soc_codec_conf mt6358_codec_conf[] = {
	{
		.dev_name = "mt6358-sound",
		.name_prefix = "Mt6358",
	},
};

static struct snd_soc_card mt8183_da7219_max98357_card = {
	.name = "mt8183_da7219_max98357",
	.owner = THIS_MODULE,
	.dai_link = mt8183_da7219_max98357_dai_links,
	.num_links = ARRAY_SIZE(mt8183_da7219_max98357_dai_links),
	.aux_dev = &mt8183_da7219_max98357_headset_dev,
	.num_aux_devs = 1,
	.codec_conf = mt6358_codec_conf,
	.num_configs = ARRAY_SIZE(mt6358_codec_conf),
};

static int
mt8183_da7219_max98357_headset_init(struct snd_soc_component *component)
{
	int ret;

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(&mt8183_da7219_max98357_card,
				    "Headset Jack",
				    SND_JACK_HEADSET |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &headset_jack,
				    headset_jack_pins,
				    ARRAY_SIZE(headset_jack_pins));
	if (ret)
		return ret;

	da7219_aad_jack_det(component, &headset_jack);

	return ret;
}

static int mt8183_da7219_max98357_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8183_da7219_max98357_card;
	struct device_node *platform_node;
	struct snd_soc_dai_link *dai_link;
	struct pinctrl *default_pins;
	int ret, i;

	card->dev = &pdev->dev;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for_each_card_prelinks(card, i, dai_link) {
		/* In the alsa soc-core, the "platform" will be
		 * allocated by devm_kzalloc if null.
		 * There is a special case that registerring
		 * sound card is failed at the first time, but
		 * the "platform" will not null when probe is trying
		 * again. It's not expected normally.
		 */
		dai_link->platforms = NULL;

		if (dai_link->platform_name)
			continue;
		dai_link->platform_of_node = platform_node;
	}

	mt8183_da7219_max98357_headset_dev.codec_of_node =
		of_parse_phandle(pdev->dev.of_node,
				 "mediatek,headset-codec", 0);
	if (!mt8183_da7219_max98357_headset_dev.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'mediatek,headset-codec' missing/invalid\n");
		return -EINVAL;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);
		return ret;
	}

	default_pins =
		devm_pinctrl_get_select(&pdev->dev, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(default_pins)) {
		dev_err(&pdev->dev, "%s set pins failed\n",
			__func__);
		return PTR_ERR(default_pins);
	}

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id mt8183_da7219_max98357_dt_match[] = {
	{.compatible = "mediatek,mt8183_da7219_max98357",},
	{}
};
#endif

static struct platform_driver mt8183_da7219_max98357_driver = {
	.driver = {
		.name = "mt8183_da7219_max98357",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = mt8183_da7219_max98357_dt_match,
#endif
	},
	.probe = mt8183_da7219_max98357_dev_probe,
};

module_platform_driver(mt8183_da7219_max98357_driver);

/* Module information */
MODULE_DESCRIPTION("MT8183-DA7219-MAX98357 ALSA SoC machine driver");
MODULE_AUTHOR("Shunli Wang <shunli.wang@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt8183_da7219_max98357 soc card");

