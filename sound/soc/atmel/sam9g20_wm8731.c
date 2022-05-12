// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sam9g20_wm8731  --  SoC audio for AT91SAM9G20-based
 * 			ATMEL AT91SAM9G20ek board.
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2008 Atmel
 *
 * Authors: Sedji Gaouaou <sedji.gaouaou@atmel.com>
 *
 * Based on ati_b1_wm8731.c by:
 * Frank Mandarino <fmandarino@endrelia.com>
 * Copyright 2006 Endrelia Technologies Inc.
 * Based on corgi.c by:
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include <linux/atmel-ssc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/wm8731.h"
#include "atmel-pcm.h"
#include "atmel_ssc_dai.h"

#define MCLK_RATE 12000000

/*
 * As shipped the board does not have inputs.  However, it is relatively
 * straightforward to modify the board to hook them up so support is left
 * in the driver.
 */
#undef ENABLE_MIC_INPUT

static const struct snd_soc_dapm_widget at91sam9g20ek_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {

	/* speaker connected to LHPOUT/RHPOUT */
	{"Ext Spk", NULL, "LHPOUT"},
	{"Ext Spk", NULL, "RHPOUT"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"MICIN", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Int Mic"},
};

/*
 * Logic for a wm8731 as connected on a at91sam9g20ek board.
 */
static int at91sam9g20ek_wm8731_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct device *dev = rtd->dev;
	int ret;

	dev_dbg(dev, "%s called\n", __func__);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_MCLK,
				     MCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(dev, "Failed to set WM8731 SYSCLK: %d\n", ret);
		return ret;
	}

#ifndef ENABLE_MIC_INPUT
	snd_soc_dapm_nc_pin(&rtd->card->dapm, "Int Mic");
#endif

	return 0;
}

SND_SOC_DAILINK_DEFS(pcm,
	DAILINK_COMP_ARRAY(COMP_CPU("at91rm9200_ssc.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm8731.0-001b", "wm8731-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("at91rm9200_ssc.0")));

static struct snd_soc_dai_link at91sam9g20ek_dai = {
	.name = "WM8731",
	.stream_name = "WM8731 PCM",
	.init = at91sam9g20ek_wm8731_init,
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBM_CFM,
	SND_SOC_DAILINK_REG(pcm),
};

static struct snd_soc_card snd_soc_at91sam9g20ek = {
	.name = "AT91SAMG20-EK",
	.owner = THIS_MODULE,
	.dai_link = &at91sam9g20ek_dai,
	.num_links = 1,

	.dapm_widgets = at91sam9g20ek_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(at91sam9g20ek_dapm_widgets),
	.dapm_routes = intercon,
	.num_dapm_routes = ARRAY_SIZE(intercon),
	.fully_routed = true,
};

static int at91sam9g20ek_audio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *cpu_np;
	struct snd_soc_card *card = &snd_soc_at91sam9g20ek;
	int ret;

	if (!np) {
		return -ENODEV;
	}

	ret = atmel_ssc_set_audio(0);
	if (ret) {
		dev_err(&pdev->dev, "ssc channel is not valid\n");
		return -EINVAL;
	}

	card->dev = &pdev->dev;

	/* Parse device node info */
	ret = snd_soc_of_parse_card_name(card, "atmel,model");
	if (ret)
		goto err;

	ret = snd_soc_of_parse_audio_routing(card,
		"atmel,audio-routing");
	if (ret)
		goto err;

	/* Parse codec info */
	at91sam9g20ek_dai.codecs->name = NULL;
	codec_np = of_parse_phandle(np, "atmel,audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "codec info missing\n");
		return -EINVAL;
	}
	at91sam9g20ek_dai.codecs->of_node = codec_np;

	/* Parse dai and platform info */
	at91sam9g20ek_dai.cpus->dai_name = NULL;
	at91sam9g20ek_dai.platforms->name = NULL;
	cpu_np = of_parse_phandle(np, "atmel,ssc-controller", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "dai and pcm info missing\n");
		of_node_put(codec_np);
		return -EINVAL;
	}
	at91sam9g20ek_dai.cpus->of_node = cpu_np;
	at91sam9g20ek_dai.platforms->of_node = cpu_np;

	of_node_put(codec_np);
	of_node_put(cpu_np);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed\n");
	}

	return ret;

err:
	atmel_ssc_put_audio(0);
	return ret;
}

static int at91sam9g20ek_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	atmel_ssc_put_audio(0);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id at91sam9g20ek_wm8731_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g20ek-wm8731-audio", },
	{ }
};
MODULE_DEVICE_TABLE(of, at91sam9g20ek_wm8731_dt_ids);
#endif

static struct platform_driver at91sam9g20ek_audio_driver = {
	.driver = {
		.name	= "at91sam9g20ek-audio",
		.of_match_table = of_match_ptr(at91sam9g20ek_wm8731_dt_ids),
	},
	.probe	= at91sam9g20ek_audio_probe,
	.remove	= at91sam9g20ek_audio_remove,
};

module_platform_driver(at91sam9g20ek_audio_driver);

/* Module information */
MODULE_AUTHOR("Sedji Gaouaou <sedji.gaouaou@atmel.com>");
MODULE_DESCRIPTION("ALSA SoC AT91SAM9G20EK_WM8731");
MODULE_ALIAS("platform:at91sam9g20ek-audio");
MODULE_LICENSE("GPL");
