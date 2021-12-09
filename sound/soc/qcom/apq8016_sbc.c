// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <uapi/linux/input-event-codes.h>
#include <dt-bindings/sound/apq8016-lpass.h>
#include "common.h"

struct apq8016_sbc_data {
	struct snd_soc_card card;
	void __iomem *mic_iomux;
	void __iomem *spkr_iomux;
	struct snd_soc_jack jack;
	bool jack_setup;
};

#define MIC_CTRL_TER_WS_SLAVE_SEL	BIT(21)
#define MIC_CTRL_QUA_WS_SLAVE_SEL_10	BIT(17)
#define MIC_CTRL_TLMM_SCLK_EN		BIT(1)
#define	SPKR_CTL_PRI_WS_SLAVE_SEL_11	(BIT(17) | BIT(16))
#define SPKR_CTL_TLMM_MCLK_EN		BIT(1)
#define SPKR_CTL_TLMM_SCLK_EN		BIT(2)
#define SPKR_CTL_TLMM_DATA1_EN		BIT(3)
#define SPKR_CTL_TLMM_WS_OUT_SEL_MASK	GENMASK(7, 6)
#define SPKR_CTL_TLMM_WS_OUT_SEL_SEC	BIT(6)
#define SPKR_CTL_TLMM_WS_EN_SEL_MASK	GENMASK(19, 18)
#define SPKR_CTL_TLMM_WS_EN_SEL_SEC	BIT(18)
#define DEFAULT_MCLK_RATE		9600000

static int apq8016_sbc_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_component *component;
	struct snd_soc_card *card = rtd->card;
	struct apq8016_sbc_data *pdata = snd_soc_card_get_drvdata(card);
	int i, rval;
	u32 value;

	switch (cpu_dai->id) {
	case MI2S_PRIMARY:
		writel(readl(pdata->spkr_iomux) | SPKR_CTL_PRI_WS_SLAVE_SEL_11,
			pdata->spkr_iomux);
		break;

	case MI2S_QUATERNARY:
		/* Configure the Quat MI2S to TLMM */
		writel(readl(pdata->mic_iomux) | MIC_CTRL_QUA_WS_SLAVE_SEL_10 |
			MIC_CTRL_TLMM_SCLK_EN,
			pdata->mic_iomux);
		break;
	case MI2S_SECONDARY:
		/* Clear TLMM_WS_OUT_SEL and TLMM_WS_EN_SEL fields */
		value = readl(pdata->spkr_iomux) &
			~(SPKR_CTL_TLMM_WS_OUT_SEL_MASK | SPKR_CTL_TLMM_WS_EN_SEL_MASK);
		/* Configure the Sec MI2S to TLMM */
		writel(value | SPKR_CTL_TLMM_MCLK_EN | SPKR_CTL_TLMM_SCLK_EN |
			SPKR_CTL_TLMM_DATA1_EN | SPKR_CTL_TLMM_WS_OUT_SEL_SEC |
			SPKR_CTL_TLMM_WS_EN_SEL_SEC, pdata->spkr_iomux);
		break;
	case MI2S_TERTIARY:
		writel(readl(pdata->mic_iomux) | MIC_CTRL_TER_WS_SLAVE_SEL |
			MIC_CTRL_TLMM_SCLK_EN,
			pdata->mic_iomux);

		break;

	default:
		dev_err(card->dev, "unsupported cpu dai configuration\n");
		return -EINVAL;

	}

	if (!pdata->jack_setup) {
		struct snd_jack *jack;

		rval = snd_soc_card_jack_new(card, "Headset Jack",
					     SND_JACK_HEADSET |
					     SND_JACK_HEADPHONE |
					     SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					     SND_JACK_BTN_2 | SND_JACK_BTN_3 |
					     SND_JACK_BTN_4,
					     &pdata->jack, NULL, 0);

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		jack = pdata->jack.jack;

		snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		pdata->jack_setup = true;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {

		component = codec_dai->component;
		/* Set default mclk for internal codec */
		rval = snd_soc_component_set_sysclk(component, 0, 0, DEFAULT_MCLK_RATE,
				       SND_SOC_CLOCK_IN);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set mclk: %d\n", rval);
			return rval;
		}
		rval = snd_soc_component_set_jack(component, &pdata->jack, NULL);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set jack: %d\n", rval);
			return rval;
		}
	}

	return 0;
}

static void apq8016_sbc_add_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link)
		link->init = apq8016_sbc_dai_init;
}

static const struct snd_soc_dapm_widget apq8016_sbc_dapm_widgets[] = {

	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Secondary Mic", NULL),
	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),
};

static int apq8016_sbc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct apq8016_sbc_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dapm_widgets = apq8016_sbc_dapm_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(apq8016_sbc_dapm_widgets);

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	data->mic_iomux = devm_platform_ioremap_resource_byname(pdev, "mic-iomux");
	if (IS_ERR(data->mic_iomux))
		return PTR_ERR(data->mic_iomux);

	data->spkr_iomux = devm_platform_ioremap_resource_byname(pdev, "spkr-iomux");
	if (IS_ERR(data->spkr_iomux))
		return PTR_ERR(data->spkr_iomux);

	snd_soc_card_set_drvdata(card, data);

	apq8016_sbc_add_ops(card);
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id apq8016_sbc_device_id[] __maybe_unused = {
	{ .compatible = "qcom,apq8016-sbc-sndcard" },
	{},
};
MODULE_DEVICE_TABLE(of, apq8016_sbc_device_id);

static struct platform_driver apq8016_sbc_platform_driver = {
	.driver = {
		.name = "qcom-apq8016-sbc",
		.of_match_table = of_match_ptr(apq8016_sbc_device_id),
	},
	.probe = apq8016_sbc_platform_probe,
};
module_platform_driver(apq8016_sbc_platform_driver);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("APQ8016 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
