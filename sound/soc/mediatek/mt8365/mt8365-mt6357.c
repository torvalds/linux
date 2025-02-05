// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT8365 Sound Card driver
 *
 * Copyright (c) 2024 MediaTek Inc.
 * Authors: Nicolas Belin <nbelin@baylibre.com>
 */

#include <linux/array_size.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "mt8365-afe-common.h"
#include "../common/mtk-soc-card.h"
#include "../common/mtk-soundcard-driver.h"

enum pinctrl_pin_state {
	PIN_STATE_DEFAULT,
	PIN_STATE_DMIC,
	PIN_STATE_MISO_OFF,
	PIN_STATE_MISO_ON,
	PIN_STATE_MOSI_OFF,
	PIN_STATE_MOSI_ON,
	PIN_STATE_MAX
};

static const char * const mt8365_mt6357_pin_str[PIN_STATE_MAX] = {
	"default",
	"dmic",
	"miso_off",
	"miso_on",
	"mosi_off",
	"mosi_on",
};

struct mt8365_mt6357_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
};

enum {
	/* FE */
	DAI_LINK_DL1_PLAYBACK = 0,
	DAI_LINK_DL2_PLAYBACK,
	DAI_LINK_AWB_CAPTURE,
	DAI_LINK_VUL_CAPTURE,
	/* BE */
	DAI_LINK_2ND_I2S_INTF,
	DAI_LINK_DMIC,
	DAI_LINK_INT_ADDA,
	DAI_LINK_NUM
};

static const struct snd_soc_dapm_widget mt8365_mt6357_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HDMI Out"),
};

static const struct snd_soc_dapm_route mt8365_mt6357_routes[] = {
	{"HDMI Out", NULL, "2ND I2S Playback"},
	{"DMIC In", NULL, "MICBIAS0"},
};

static int mt8365_mt6357_int_adda_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mt8365_mt6357_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (IS_ERR(priv->pin_states[PIN_STATE_MOSI_ON]))
			return ret;

		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_STATE_MOSI_ON]);
		if (ret)
			dev_err(rtd->card->dev, "%s failed to select state %d\n",
				__func__, ret);
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (IS_ERR(priv->pin_states[PIN_STATE_MISO_ON]))
			return ret;

		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_STATE_MISO_ON]);
		if (ret)
			dev_err(rtd->card->dev, "%s failed to select state %d\n",
				__func__, ret);
	}

	return 0;
}

static void mt8365_mt6357_int_adda_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mt8365_mt6357_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (IS_ERR(priv->pin_states[PIN_STATE_MOSI_OFF]))
			return;

		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_STATE_MOSI_OFF]);
		if (ret)
			dev_err(rtd->card->dev, "%s failed to select state %d\n",
				__func__, ret);
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (IS_ERR(priv->pin_states[PIN_STATE_MISO_OFF]))
			return;

		ret = pinctrl_select_state(priv->pinctrl,
					   priv->pin_states[PIN_STATE_MISO_OFF]);
		if (ret)
			dev_err(rtd->card->dev, "%s failed to select state %d\n",
				__func__, ret);
	}
}

static const struct snd_soc_ops mt8365_mt6357_int_adda_ops = {
	.startup = mt8365_mt6357_int_adda_startup,
	.shutdown = mt8365_mt6357_int_adda_shutdown,
};

SND_SOC_DAILINK_DEFS(playback1,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(playback2,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(awb_capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("AWB")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(vul,
		     DAILINK_COMP_ARRAY(COMP_CPU("VUL")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(i2s3,
		     DAILINK_COMP_ARRAY(COMP_CPU("2ND I2S")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(dmic,
		     DAILINK_COMP_ARRAY(COMP_CPU("DMIC")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(primary_codec,
		     DAILINK_COMP_ARRAY(COMP_CPU("INT ADDA")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6357-sound", "mt6357-snd-codec-aif1")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8365_mt6357_dais[] = {
	/* Front End DAI links */
	[DAI_LINK_DL1_PLAYBACK] = {
		.name = "DL1_FE",
		.stream_name = "MultiMedia1_PLayback",
		.id = DAI_LINK_DL1_PLAYBACK,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(playback1),
	},
	[DAI_LINK_DL2_PLAYBACK] = {
		.name = "DL2_FE",
		.stream_name = "MultiMedia2_PLayback",
		.id = DAI_LINK_DL2_PLAYBACK,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.playback_only = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(playback2),
	},
	[DAI_LINK_AWB_CAPTURE] = {
		.name = "AWB_FE",
		.stream_name = "DL1_AWB_Record",
		.id = DAI_LINK_AWB_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(awb_capture),
	},
	[DAI_LINK_VUL_CAPTURE] = {
		.name = "VUL_FE",
		.stream_name = "MultiMedia1_Capture",
		.id = DAI_LINK_VUL_CAPTURE,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.capture_only = 1,
		.dpcm_merged_rate = 1,
		SND_SOC_DAILINK_REG(vul),
	},
	/* Back End DAI links */
	[DAI_LINK_2ND_I2S_INTF] = {
		.name = "I2S_OUT_BE",
		.no_pcm = 1,
		.id = DAI_LINK_2ND_I2S_INTF,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
				SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
		SND_SOC_DAILINK_REG(i2s3),
	},
	[DAI_LINK_DMIC] = {
		.name = "DMIC_BE",
		.no_pcm = 1,
		.id = DAI_LINK_DMIC,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(dmic),
	},
	[DAI_LINK_INT_ADDA] = {
		.name = "MTK_Codec",
		.no_pcm = 1,
		.id = DAI_LINK_INT_ADDA,
		.ops = &mt8365_mt6357_int_adda_ops,
		SND_SOC_DAILINK_REG(primary_codec),
	},
};

static int mt8365_mt6357_gpio_probe(struct snd_soc_card *card)
{
	struct mt8365_mt6357_priv *priv = snd_soc_card_get_drvdata(card);
	int ret, i;

	priv->pinctrl = devm_pinctrl_get(card->dev);
	if (IS_ERR(priv->pinctrl)) {
		ret = PTR_ERR(priv->pinctrl);
		return dev_err_probe(card->dev, ret,
				     "Failed to get pinctrl\n");
	}

	for (i = PIN_STATE_DEFAULT ; i < PIN_STATE_MAX ; i++) {
		priv->pin_states[i] = pinctrl_lookup_state(priv->pinctrl,
							   mt8365_mt6357_pin_str[i]);
		if (IS_ERR(priv->pin_states[i])) {
			dev_info(card->dev, "No pin state for %s\n",
				 mt8365_mt6357_pin_str[i]);
		} else {
			ret = pinctrl_select_state(priv->pinctrl,
						   priv->pin_states[i]);
			if (ret) {
				dev_err_probe(card->dev, ret,
					      "Failed to select pin state %s\n",
					      mt8365_mt6357_pin_str[i]);
				return ret;
			}
		}
	}
	return 0;
}

static struct snd_soc_card mt8365_mt6357_soc_card = {
	.name = "mt8365-evk",
	.owner = THIS_MODULE,
	.dai_link = mt8365_mt6357_dais,
	.num_links = ARRAY_SIZE(mt8365_mt6357_dais),
	.dapm_widgets = mt8365_mt6357_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8365_mt6357_widgets),
	.dapm_routes = mt8365_mt6357_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8365_mt6357_routes),
};

static int mt8365_mt6357_dev_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct device *dev = card->dev;
	struct mt8365_mt6357_priv *mach_priv;
	int ret;

	card->dev = dev;
	ret = parse_dai_link_info(card);
	if (ret)
		goto err;

	mach_priv = devm_kzalloc(dev, sizeof(*mach_priv),
				 GFP_KERNEL);
	if (!mach_priv)
		return -ENOMEM;
	soc_card_data->mach_priv = mach_priv;
	snd_soc_card_set_drvdata(card, soc_card_data);
	mt8365_mt6357_gpio_probe(card);
	return 0;

err:
	clean_card_reference(card);
	return ret;
}

static const struct mtk_soundcard_pdata mt8365_mt6357_card = {
	.card_name = "mt8365-mt6357",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8365_mt6357_soc_card,
	},
	.soc_probe = mt8365_mt6357_dev_probe
};

static const struct of_device_id mt8365_mt6357_dt_match[] = {
	{ .compatible = "mediatek,mt8365-mt6357", .data = &mt8365_mt6357_card },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt8365_mt6357_dt_match);

static struct platform_driver mt8365_mt6357_driver = {
	.driver = {
		   .name = "mt8365_mt6357",
		   .of_match_table = mt8365_mt6357_dt_match,
		   .pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};

module_platform_driver(mt8365_mt6357_driver);

/* Module information */
MODULE_DESCRIPTION("MT8365 EVK SoC machine driver");
MODULE_AUTHOR("Nicolas Belin <nbelin@baylibre.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: mt8365_mt6357");
