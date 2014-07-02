/*
 * SiRF audio card driver
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

struct sirf_audio_card {
	unsigned int            gpio_hp_pa;
	unsigned int            gpio_spk_pa;
};

static int sirf_audio_hp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_audio_card *sirf_audio_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);
	if (gpio_is_valid(sirf_audio_card->gpio_hp_pa))
		gpio_set_value(sirf_audio_card->gpio_hp_pa, on);
	return 0;
}

static int sirf_audio_spk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *ctrl, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct sirf_audio_card *sirf_audio_card = snd_soc_card_get_drvdata(card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);

	if (gpio_is_valid(sirf_audio_card->gpio_spk_pa))
		gpio_set_value(sirf_audio_card->gpio_spk_pa, on);

	return 0;
}
static const struct snd_soc_dapm_widget sirf_audio_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Hp", sirf_audio_hp_event),
	SND_SOC_DAPM_SPK("Ext Spk", sirf_audio_spk_event),
	SND_SOC_DAPM_MIC("Ext Mic", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {
	{"Hp", NULL, "HPOUTL"},
	{"Hp", NULL, "HPOUTR"},
	{"Ext Spk", NULL, "SPKOUT"},
	{"MICIN1", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Ext Mic"},
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sirf_audio_dai_link[] = {
	{
		.name = "SiRF audio card",
		.stream_name = "SiRF audio HiFi",
		.codec_dai_name = "sirf-audio-codec",
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_sirf_audio_card = {
	.name = "SiRF audio card",
	.owner = THIS_MODULE,
	.dai_link = sirf_audio_dai_link,
	.num_links = ARRAY_SIZE(sirf_audio_dai_link),
	.dapm_widgets = sirf_audio_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sirf_audio_dapm_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),
};

static int sirf_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sirf_audio_card;
	struct sirf_audio_card *sirf_audio_card;
	int ret;

	sirf_audio_card = devm_kzalloc(&pdev->dev, sizeof(struct sirf_audio_card),
			GFP_KERNEL);
	if (sirf_audio_card == NULL)
		return -ENOMEM;

	sirf_audio_dai_link[0].cpu_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,audio-platform", 0);
	sirf_audio_dai_link[0].platform_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,audio-platform", 0);
	sirf_audio_dai_link[0].codec_of_node =
		of_parse_phandle(pdev->dev.of_node, "sirf,audio-codec", 0);
	sirf_audio_card->gpio_spk_pa = of_get_named_gpio(pdev->dev.of_node,
			"spk-pa-gpios", 0);
	sirf_audio_card->gpio_hp_pa =  of_get_named_gpio(pdev->dev.of_node,
			"hp-pa-gpios", 0);
	if (gpio_is_valid(sirf_audio_card->gpio_spk_pa)) {
		ret = devm_gpio_request_one(&pdev->dev,
				sirf_audio_card->gpio_spk_pa,
				GPIOF_OUT_INIT_LOW, "SPA_PA_SD");
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request GPIO_%d for reset: %d\n",
				sirf_audio_card->gpio_spk_pa, ret);
			return ret;
		}
	}
	if (gpio_is_valid(sirf_audio_card->gpio_hp_pa)) {
		ret = devm_gpio_request_one(&pdev->dev,
				sirf_audio_card->gpio_hp_pa,
				GPIOF_OUT_INIT_LOW, "HP_PA_SD");
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request GPIO_%d for reset: %d\n",
				sirf_audio_card->gpio_hp_pa, ret);
			return ret;
		}
	}

	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, sirf_audio_card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed:%d\n", ret);

	return ret;
}

static const struct of_device_id sirf_audio_of_match[] = {
	{.compatible = "sirf,sirf-audio-card", },
	{ },
};
MODULE_DEVICE_TABLE(of, sirf_audio_of_match);

static struct platform_driver sirf_audio_driver = {
	.driver = {
		.name = "sirf-audio-card",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = sirf_audio_of_match,
	},
	.probe = sirf_audio_probe,
};
module_platform_driver(sirf_audio_driver);

MODULE_AUTHOR("RongJun Ying <RongJun.Ying@csr.com>");
MODULE_DESCRIPTION("ALSA SoC SIRF audio card driver");
MODULE_LICENSE("GPL v2");
