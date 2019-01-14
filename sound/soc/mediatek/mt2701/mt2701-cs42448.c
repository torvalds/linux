// SPDX-License-Identifier: GPL-2.0
/*
 * mt2701-cs42448.c  --  MT2701 CS42448 ALSA SoC machine driver
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ir Lian <ir.lian@mediatek.com>
 *	   Garlic Tseng <garlic.tseng@mediatek.com>
 */

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_gpio.h>

#include "mt2701-afe-common.h"

struct mt2701_cs42448_private {
	int i2s1_in_mux;
	int i2s1_in_mux_gpio_sel_1;
	int i2s1_in_mux_gpio_sel_2;
};

static const char * const i2sin_mux_switch_text[] = {
	"ADC_SDOUT2",
	"ADC_SDOUT3",
	"I2S_IN_1",
	"I2S_IN_2",
};

static const struct soc_enum i2sin_mux_enum =
	SOC_ENUM_SINGLE_EXT(4, i2sin_mux_switch_text);

static int mt2701_cs42448_i2sin1_mux_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt2701_cs42448_private *priv = snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = priv->i2s1_in_mux;
	return 0;
}

static int mt2701_cs42448_i2sin1_mux_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct mt2701_cs42448_private *priv = snd_soc_card_get_drvdata(card);

	if (ucontrol->value.integer.value[0] == priv->i2s1_in_mux)
		return 0;

	switch (ucontrol->value.integer.value[0]) {
	case 0:
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_1, 0);
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_2, 0);
		break;
	case 1:
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_1, 1);
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_2, 0);
		break;
	case 2:
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_1, 0);
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_2, 1);
		break;
	case 3:
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_1, 1);
		gpio_set_value(priv->i2s1_in_mux_gpio_sel_2, 1);
		break;
	default:
		dev_warn(card->dev, "%s invalid setting\n", __func__);
	}

	priv->i2s1_in_mux = ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_soc_dapm_widget
			mt2701_cs42448_asoc_card_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out Jack", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_LINE("Tuner In", NULL),
	SND_SOC_DAPM_LINE("Satellite Tuner In", NULL),
	SND_SOC_DAPM_LINE("AUX In", NULL),
};

static const struct snd_kcontrol_new mt2701_cs42448_controls[] = {
	SOC_DAPM_PIN_SWITCH("Line Out Jack"),
	SOC_DAPM_PIN_SWITCH("AMIC"),
	SOC_DAPM_PIN_SWITCH("Tuner In"),
	SOC_DAPM_PIN_SWITCH("Satellite Tuner In"),
	SOC_DAPM_PIN_SWITCH("AUX In"),
	SOC_ENUM_EXT("I2SIN1_MUX_Switch", i2sin_mux_enum,
		     mt2701_cs42448_i2sin1_mux_get,
		     mt2701_cs42448_i2sin1_mux_set),
};

static const unsigned int mt2701_cs42448_sampling_rates[] = {48000};

static const struct snd_pcm_hw_constraint_list mt2701_cs42448_constraints_rates = {
		.count = ARRAY_SIZE(mt2701_cs42448_sampling_rates),
		.list = mt2701_cs42448_sampling_rates,
		.mask = 0,
};

static int mt2701_cs42448_fe_ops_startup(struct snd_pcm_substream *substream)
{
	int err;

	err = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &mt2701_cs42448_constraints_rates);
	if (err < 0) {
		dev_err(substream->pcm->card->dev,
			"%s snd_pcm_hw_constraint_list failed: 0x%x\n",
			__func__, err);
		return err;
	}
	return 0;
}

static const struct snd_soc_ops mt2701_cs42448_48k_fe_ops = {
	.startup = mt2701_cs42448_fe_ops_startup,
};

static int mt2701_cs42448_be_ops_hw_params(struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int mclk_rate;
	unsigned int rate = params_rate(params);
	unsigned int div_mclk_over_bck = rate > 192000 ? 2 : 4;
	unsigned int div_bck_over_lrck = 64;

	mclk_rate = rate * div_bck_over_lrck * div_mclk_over_bck;

	/* mt2701 mclk */
	snd_soc_dai_set_sysclk(cpu_dai, 0, mclk_rate, SND_SOC_CLOCK_OUT);

	/* codec mclk */
	snd_soc_dai_set_sysclk(codec_dai, 0, mclk_rate, SND_SOC_CLOCK_IN);

	return 0;
}

static struct snd_soc_ops mt2701_cs42448_be_ops = {
	.hw_params = mt2701_cs42448_be_ops_hw_params
};

enum {
	DAI_LINK_FE_MULTI_CH_OUT,
	DAI_LINK_FE_PCM0_IN,
	DAI_LINK_FE_PCM1_IN,
	DAI_LINK_FE_BT_OUT,
	DAI_LINK_FE_BT_IN,
	DAI_LINK_BE_I2S0,
	DAI_LINK_BE_I2S1,
	DAI_LINK_BE_I2S2,
	DAI_LINK_BE_I2S3,
	DAI_LINK_BE_MRG_BT,
};

static struct snd_soc_dai_link mt2701_cs42448_dai_links[] = {
	/* FE */
	[DAI_LINK_FE_MULTI_CH_OUT] = {
		.name = "mt2701-cs42448-multi-ch-out",
		.stream_name = "mt2701-cs42448-multi-ch-out",
		.cpu_dai_name = "PCM_multi",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ops = &mt2701_cs42448_48k_fe_ops,
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	[DAI_LINK_FE_PCM0_IN] = {
		.name = "mt2701-cs42448-pcm0",
		.stream_name = "mt2701-cs42448-pcm0-data-UL",
		.cpu_dai_name = "PCM0",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ops = &mt2701_cs42448_48k_fe_ops,
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_FE_PCM1_IN] = {
		.name = "mt2701-cs42448-pcm1-data-UL",
		.stream_name = "mt2701-cs42448-pcm1-data-UL",
		.cpu_dai_name = "PCM1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ops = &mt2701_cs42448_48k_fe_ops,
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_FE_BT_OUT] = {
		.name = "mt2701-cs42448-pcm-BT-out",
		.stream_name = "mt2701-cs42448-pcm-BT",
		.cpu_dai_name = "PCM_BT_DL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	[DAI_LINK_FE_BT_IN] = {
		.name = "mt2701-cs42448-pcm-BT-in",
		.stream_name = "mt2701-cs42448-pcm-BT",
		.cpu_dai_name = "PCM_BT_UL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	/* BE */
	[DAI_LINK_BE_I2S0] = {
		.name = "mt2701-cs42448-I2S0",
		.cpu_dai_name = "I2S0",
		.no_pcm = 1,
		.codec_dai_name = "cs42448",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS
			 | SND_SOC_DAIFMT_GATED,
		.ops = &mt2701_cs42448_be_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_BE_I2S1] = {
		.name = "mt2701-cs42448-I2S1",
		.cpu_dai_name = "I2S1",
		.no_pcm = 1,
		.codec_dai_name = "cs42448",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS
			 | SND_SOC_DAIFMT_GATED,
		.ops = &mt2701_cs42448_be_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_BE_I2S2] = {
		.name = "mt2701-cs42448-I2S2",
		.cpu_dai_name = "I2S2",
		.no_pcm = 1,
		.codec_dai_name = "cs42448",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS
			 | SND_SOC_DAIFMT_GATED,
		.ops = &mt2701_cs42448_be_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_BE_I2S3] = {
		.name = "mt2701-cs42448-I2S3",
		.cpu_dai_name = "I2S3",
		.no_pcm = 1,
		.codec_dai_name = "cs42448",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS
			 | SND_SOC_DAIFMT_GATED,
		.ops = &mt2701_cs42448_be_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	[DAI_LINK_BE_MRG_BT] = {
		.name = "mt2701-cs42448-MRG-BT",
		.cpu_dai_name = "MRG BT",
		.no_pcm = 1,
		.codec_dai_name = "bt-sco-pcm-wb",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
};

static struct snd_soc_card mt2701_cs42448_soc_card = {
	.name = "mt2701-cs42448",
	.owner = THIS_MODULE,
	.dai_link = mt2701_cs42448_dai_links,
	.num_links = ARRAY_SIZE(mt2701_cs42448_dai_links),
	.controls = mt2701_cs42448_controls,
	.num_controls = ARRAY_SIZE(mt2701_cs42448_controls),
	.dapm_widgets = mt2701_cs42448_asoc_card_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt2701_cs42448_asoc_card_dapm_widgets),
};

static int mt2701_cs42448_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt2701_cs42448_soc_card;
	int ret;
	int i;
	struct device_node *platform_node, *codec_node, *codec_node_bt_mrg;
	struct mt2701_cs42448_private *priv =
		devm_kzalloc(&pdev->dev, sizeof(struct mt2701_cs42448_private),
			     GFP_KERNEL);
	struct device *dev = &pdev->dev;
	struct snd_soc_dai_link *dai_link;

	if (!priv)
		return -ENOMEM;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->platform_name)
			continue;
		dai_link->platform_of_node = platform_node;
	}

	card->dev = dev;

	codec_node = of_parse_phandle(pdev->dev.of_node,
				      "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev,
			"Property 'audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	for_each_card_prelinks(card, i, dai_link) {
		if (dai_link->codec_name)
			continue;
		dai_link->codec_of_node = codec_node;
	}

	codec_node_bt_mrg = of_parse_phandle(pdev->dev.of_node,
					     "mediatek,audio-codec-bt-mrg", 0);
	if (!codec_node_bt_mrg) {
		dev_err(&pdev->dev,
			"Property 'audio-codec-bt-mrg' missing or invalid\n");
		return -EINVAL;
	}
	mt2701_cs42448_dai_links[DAI_LINK_BE_MRG_BT].codec_of_node
							= codec_node_bt_mrg;

	ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "failed to parse audio-routing: %d\n", ret);
		return ret;
	}

	priv->i2s1_in_mux_gpio_sel_1 =
		of_get_named_gpio(dev->of_node, "i2s1-in-sel-gpio1", 0);
	if (gpio_is_valid(priv->i2s1_in_mux_gpio_sel_1)) {
		ret = devm_gpio_request(dev, priv->i2s1_in_mux_gpio_sel_1,
					"i2s1_in_mux_gpio_sel_1");
		if (ret)
			dev_warn(&pdev->dev, "%s devm_gpio_request fail %d\n",
				 __func__, ret);
		gpio_direction_output(priv->i2s1_in_mux_gpio_sel_1, 0);
	}

	priv->i2s1_in_mux_gpio_sel_2 =
		of_get_named_gpio(dev->of_node, "i2s1-in-sel-gpio2", 0);
	if (gpio_is_valid(priv->i2s1_in_mux_gpio_sel_2)) {
		ret = devm_gpio_request(dev, priv->i2s1_in_mux_gpio_sel_2,
					"i2s1_in_mux_gpio_sel_2");
		if (ret)
			dev_warn(&pdev->dev, "%s devm_gpio_request fail2 %d\n",
				 __func__, ret);
		gpio_direction_output(priv->i2s1_in_mux_gpio_sel_2, 0);
	}
	snd_soc_card_set_drvdata(card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, card);

	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id mt2701_cs42448_machine_dt_match[] = {
	{.compatible = "mediatek,mt2701-cs42448-machine",},
	{}
};
#endif

static struct platform_driver mt2701_cs42448_machine = {
	.driver = {
		.name = "mt2701-cs42448",
		   #ifdef CONFIG_OF
		   .of_match_table = mt2701_cs42448_machine_dt_match,
		   #endif
	},
	.probe = mt2701_cs42448_machine_probe,
};

module_platform_driver(mt2701_cs42448_machine);

/* Module information */
MODULE_DESCRIPTION("MT2701 CS42448 ALSA SoC machine driver");
MODULE_AUTHOR("Ir Lian <ir.lian@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mt2701 cs42448 soc card");
