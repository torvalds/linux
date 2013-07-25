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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <linux/atmel-ssc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>

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

static struct clk *mclk;

static int at91sam9g20ek_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops at91sam9g20ek_ops = {
	.hw_params = at91sam9g20ek_hw_params,
};

static int at91sam9g20ek_set_bias_level(struct snd_soc_card *card,
					struct snd_soc_dapm_context *dapm,
					enum snd_soc_bias_level level)
{
	static int mclk_on;
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		if (!mclk_on)
			ret = clk_enable(mclk);
		if (ret == 0)
			mclk_on = 1;
		break;

	case SND_SOC_BIAS_OFF:
	case SND_SOC_BIAS_STANDBY:
		if (mclk_on)
			clk_disable(mclk);
		mclk_on = 0;
		break;
	}

	return ret;
}

static const struct snd_soc_dapm_widget at91sam9g20ek_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route intercon[] = {

	/* speaker connected to LHPOUT */
	{"Ext Spk", NULL, "LHPOUT"},

	/* mic is connected to Mic Jack, with WM8731 Mic Bias */
	{"MICIN", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Int Mic"},
};

/*
 * Logic for a wm8731 as connected on a at91sam9g20ek board.
 */
static int at91sam9g20ek_wm8731_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	printk(KERN_DEBUG
			"at91sam9g20ek_wm8731 "
			": at91sam9g20ek_wm8731_init() called\n");

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8731_SYSCLK_MCLK,
		MCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		printk(KERN_ERR "Failed to set WM8731 SYSCLK: %d\n", ret);
		return ret;
	}

	/* Add specific widgets */
	snd_soc_dapm_new_controls(dapm, at91sam9g20ek_dapm_widgets,
				  ARRAY_SIZE(at91sam9g20ek_dapm_widgets));
	/* Set up specific audio path interconnects */
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	/* not connected */
	snd_soc_dapm_nc_pin(dapm, "RLINEIN");
	snd_soc_dapm_nc_pin(dapm, "LLINEIN");

#ifdef ENABLE_MIC_INPUT
	snd_soc_dapm_enable_pin(dapm, "Int Mic");
#else
	snd_soc_dapm_nc_pin(dapm, "Int Mic");
#endif

	/* always connected */
	snd_soc_dapm_enable_pin(dapm, "Ext Spk");

	return 0;
}

static struct snd_soc_dai_link at91sam9g20ek_dai = {
	.name = "WM8731",
	.stream_name = "WM8731 PCM",
	.cpu_dai_name = "at91rm9200_ssc.0",
	.codec_dai_name = "wm8731-hifi",
	.init = at91sam9g20ek_wm8731_init,
	.platform_name = "at91rm9200_ssc.0",
	.codec_name = "wm8731.0-001b",
	.ops = &at91sam9g20ek_ops,
};

static struct snd_soc_card snd_soc_at91sam9g20ek = {
	.name = "AT91SAMG20-EK",
	.owner = THIS_MODULE,
	.dai_link = &at91sam9g20ek_dai,
	.num_links = 1,
	.set_bias_level = at91sam9g20ek_set_bias_level,
};

static int at91sam9g20ek_audio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *cpu_np;
	struct clk *pllb;
	struct snd_soc_card *card = &snd_soc_at91sam9g20ek;
	int ret;

	if (!np) {
		if (!(machine_is_at91sam9g20ek() ||
			machine_is_at91sam9g20ek_2mmc()))
			return -ENODEV;
	}

	ret = atmel_ssc_set_audio(0);
	if (ret) {
		dev_err(&pdev->dev, "ssc channel is not valid\n");
		return -EINVAL;
	}

	/*
	 * Codec MCLK is supplied by PCK0 - set it up.
	 */
	mclk = clk_get(NULL, "pck0");
	if (IS_ERR(mclk)) {
		printk(KERN_ERR "ASoC: Failed to get MCLK\n");
		ret = PTR_ERR(mclk);
		goto err;
	}

	pllb = clk_get(NULL, "pllb");
	if (IS_ERR(pllb)) {
		printk(KERN_ERR "ASoC: Failed to get PLLB\n");
		ret = PTR_ERR(pllb);
		goto err_mclk;
	}
	ret = clk_set_parent(mclk, pllb);
	clk_put(pllb);
	if (ret != 0) {
		printk(KERN_ERR "ASoC: Failed to set MCLK parent\n");
		goto err_mclk;
	}

	clk_set_rate(mclk, MCLK_RATE);

	card->dev = &pdev->dev;

	/* Parse device node info */
	if (np) {
		ret = snd_soc_of_parse_card_name(card, "atmel,model");
		if (ret)
			goto err;

		ret = snd_soc_of_parse_audio_routing(card,
			"atmel,audio-routing");
		if (ret)
			goto err;

		/* Parse codec info */
		at91sam9g20ek_dai.codec_name = NULL;
		codec_np = of_parse_phandle(np, "atmel,audio-codec", 0);
		if (!codec_np) {
			dev_err(&pdev->dev, "codec info missing\n");
			return -EINVAL;
		}
		at91sam9g20ek_dai.codec_of_node = codec_np;

		/* Parse dai and platform info */
		at91sam9g20ek_dai.cpu_dai_name = NULL;
		at91sam9g20ek_dai.platform_name = NULL;
		cpu_np = of_parse_phandle(np, "atmel,ssc-controller", 0);
		if (!cpu_np) {
			dev_err(&pdev->dev, "dai and pcm info missing\n");
			return -EINVAL;
		}
		at91sam9g20ek_dai.cpu_of_node = cpu_np;
		at91sam9g20ek_dai.platform_of_node = cpu_np;

		of_node_put(codec_np);
		of_node_put(cpu_np);
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		printk(KERN_ERR "ASoC: snd_soc_register_card() failed\n");
	}

	return ret;

err_mclk:
	clk_put(mclk);
	mclk = NULL;
err:
	atmel_ssc_put_audio(0);
	return ret;
}

static int at91sam9g20ek_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	clk_disable(mclk);
	mclk = NULL;
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
		.owner	= THIS_MODULE,
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
