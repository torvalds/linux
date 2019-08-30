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

struct apq8016_sbc_data {
	void __iomem *mic_iomux;
	void __iomem *spkr_iomux;
	struct snd_soc_jack jack;
	bool jack_setup;
	struct snd_soc_dai_link dai_link[];	/* dynamically allocated */
};

#define MIC_CTRL_TER_WS_SLAVE_SEL	BIT(21)
#define MIC_CTRL_QUA_WS_SLAVE_SEL_10	BIT(17)
#define MIC_CTRL_TLMM_SCLK_EN		BIT(1)
#define	SPKR_CTL_PRI_WS_SLAVE_SEL_11	(BIT(17) | BIT(16))
#define DEFAULT_MCLK_RATE		9600000

static int apq8016_sbc_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_component *component;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_card *card = rtd->card;
	struct apq8016_sbc_data *pdata = snd_soc_card_get_drvdata(card);
	int i, rval;

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

	for (i = 0 ; i < dai_link->num_codecs; i++) {
		struct snd_soc_dai *dai = rtd->codec_dais[i];

		component = dai->component;
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

static struct apq8016_sbc_data *apq8016_sbc_parse_of(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct device_node *np, *codec, *cpu, *node  = dev->of_node;
	struct apq8016_sbc_data *data;
	int ret, num_links;

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ERR_PTR(ret);
	}

	/* DAPM routes */
	if (of_property_read_bool(node, "qcom,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card,
					"qcom,audio-routing");
		if (ret)
			return ERR_PTR(ret);
	}


	/* Populate links */
	num_links = of_get_child_count(node);

	/* Allocate the private data and the DAI link array */
	data = devm_kzalloc(dev,
			    struct_size(data, dai_link, num_links),
			    GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	card->dai_link	= &data->dai_link[0];
	card->num_links	= num_links;

	link = data->dai_link;

	for_each_child_of_node(node, np) {
		cpu = of_get_child_by_name(np, "cpu");
		codec = of_get_child_by_name(np, "codec");

		if (!cpu || !codec) {
			dev_err(dev, "Can't find cpu/codec DT node\n");
			ret = -EINVAL;
			goto error;
		}

		link->cpu_of_node = of_parse_phandle(cpu, "sound-dai", 0);
		if (!link->cpu_of_node) {
			dev_err(card->dev, "error getting cpu phandle\n");
			ret = -EINVAL;
			goto error;
		}

		ret = snd_soc_of_get_dai_name(cpu, &link->cpu_dai_name);
		if (ret) {
			dev_err(card->dev, "error getting cpu dai name\n");
			goto error;
		}

		ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);

		if (ret < 0) {
			dev_err(card->dev, "error getting codec dai name\n");
			goto error;
		}

		link->platform_of_node = link->cpu_of_node;
		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto error;
		}

		link->stream_name = link->name;
		link->init = apq8016_sbc_dai_init;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
	}

	return data;

 error:
	of_node_put(np);
	of_node_put(cpu);
	of_node_put(codec);
	return ERR_PTR(ret);
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
	struct resource *res;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	card->dapm_widgets = apq8016_sbc_dapm_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(apq8016_sbc_dapm_widgets);
	data = apq8016_sbc_parse_of(card);
	if (IS_ERR(data)) {
		dev_err(&pdev->dev, "Error resolving dai links: %ld\n",
			PTR_ERR(data));
		return PTR_ERR(data);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mic-iomux");
	data->mic_iomux = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->mic_iomux))
		return PTR_ERR(data->mic_iomux);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spkr-iomux");
	data->spkr_iomux = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->spkr_iomux))
		return PTR_ERR(data->spkr_iomux);

	snd_soc_card_set_drvdata(card, data);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id apq8016_sbc_device_id[]  = {
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
