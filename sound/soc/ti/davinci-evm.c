// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASoC driver for TI DAVINCI EVM platform
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <asm/mach-types.h>

struct snd_soc_card_drvdata_davinci {
	struct clk *mclk;
	unsigned sysclk;
};

static int evm_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	return 0;
}

static void evm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	clk_disable_unprepare(drvdata->mclk);
}

static int evm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *soc_card = rtd->card;
	int ret = 0;
	unsigned sysclk = ((struct snd_soc_card_drvdata_davinci *)
			   snd_soc_card_get_drvdata(soc_card))->sysclk;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	/* set the CPU system clock */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0 && ret != -ENOTSUPP)
		return ret;

	return 0;
}

static const struct snd_soc_ops evm_ops = {
	.startup = evm_startup,
	.shutdown = evm_shutdown,
	.hw_params = evm_hw_params,
};

/* davinci-evm machine dapm widgets */
static const struct snd_soc_dapm_widget aic3x_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

/* davinci-evm machine audio_mapnections to the codec pins */
static const struct snd_soc_dapm_route audio_map[] = {
	/* Headphone connected to HPLOUT, HPROUT */
	{"Headphone Jack", NULL, "HPLOUT"},
	{"Headphone Jack", NULL, "HPROUT"},

	/* Line Out connected to LLOUT, RLOUT */
	{"Line Out", NULL, "LLOUT"},
	{"Line Out", NULL, "RLOUT"},

	/* Mic connected to (MIC3L | MIC3R) */
	{"MIC3L", NULL, "Mic Bias"},
	{"MIC3R", NULL, "Mic Bias"},
	{"Mic Bias", NULL, "Mic Jack"},

	/* Line In connected to (LINE1L | LINE2L), (LINE1R | LINE2R) */
	{"LINE1L", NULL, "Line In"},
	{"LINE2L", NULL, "Line In"},
	{"LINE1R", NULL, "Line In"},
	{"LINE2R", NULL, "Line In"},
};

/* Logic for a aic3x as connected on a davinci-evm */
static int evm_aic3x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct device_node *np = card->dev->of_node;
	int ret;

	/* Add davinci-evm specific widgets */
	snd_soc_dapm_new_controls(&card->dapm, aic3x_dapm_widgets,
				  ARRAY_SIZE(aic3x_dapm_widgets));

	if (np) {
		ret = snd_soc_of_parse_audio_routing(card, "ti,audio-routing");
		if (ret)
			return ret;
	} else {
		/* Set up davinci-evm specific audio path audio_map */
		snd_soc_dapm_add_routes(&card->dapm, audio_map,
					ARRAY_SIZE(audio_map));
	}

	/* not connected */
	snd_soc_dapm_nc_pin(&card->dapm, "MONO_LOUT");
	snd_soc_dapm_nc_pin(&card->dapm, "HPLCOM");
	snd_soc_dapm_nc_pin(&card->dapm, "HPRCOM");

	return 0;
}

/*
 * The struct is used as place holder. It will be completely
 * filled with data from dt node.
 */
SND_SOC_DAILINK_DEFS(evm,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "tlv320aic3x-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link evm_dai_tlv320aic3x = {
	.name		= "TLV320AIC3X",
	.stream_name	= "AIC3X",
	.ops            = &evm_ops,
	.init           = evm_aic3x_init,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
	SND_SOC_DAILINK_REG(evm),
};

static const struct of_device_id davinci_evm_dt_ids[] = {
	{
		.compatible = "ti,da830-evm-audio",
		.data = (void *) &evm_dai_tlv320aic3x,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, davinci_evm_dt_ids);

/* davinci evm audio machine driver */
static struct snd_soc_card evm_soc_card = {
	.owner = THIS_MODULE,
	.num_links = 1,
};

static int davinci_evm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct snd_soc_dai_link *dai;
	struct snd_soc_card_drvdata_davinci *drvdata = NULL;
	struct clk *mclk;
	int ret = 0;

	match = of_match_device(of_match_ptr(davinci_evm_dt_ids), &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}

	dai = (struct snd_soc_dai_link *) match->data;

	evm_soc_card.dai_link = dai;

	dai->codecs->of_node = of_parse_phandle(np, "ti,audio-codec", 0);
	if (!dai->codecs->of_node)
		return -EINVAL;

	dai->cpus->of_node = of_parse_phandle(np, "ti,mcasp-controller", 0);
	if (!dai->cpus->of_node)
		return -EINVAL;

	dai->platforms->of_node = dai->cpus->of_node;

	evm_soc_card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&evm_soc_card, "ti,model");
	if (ret)
		return ret;

	mclk = devm_clk_get(&pdev->dev, "mclk");
	if (PTR_ERR(mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(mclk)) {
		dev_dbg(&pdev->dev, "mclk not found.\n");
		mclk = NULL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->mclk = mclk;

	ret = of_property_read_u32(np, "ti,codec-clock-rate", &drvdata->sysclk);

	if (ret < 0) {
		if (!drvdata->mclk) {
			dev_err(&pdev->dev,
				"No clock or clock rate defined.\n");
			return -EINVAL;
		}
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
	} else if (drvdata->mclk) {
		unsigned int requestd_rate = drvdata->sysclk;
		clk_set_rate(drvdata->mclk, drvdata->sysclk);
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
		if (drvdata->sysclk != requestd_rate)
			dev_warn(&pdev->dev,
				 "Could not get requested rate %u using %u.\n",
				 requestd_rate, drvdata->sysclk);
	}

	snd_soc_card_set_drvdata(&evm_soc_card, drvdata);
	ret = devm_snd_soc_register_card(&pdev->dev, &evm_soc_card);

	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);

	return ret;
}

static struct platform_driver davinci_evm_driver = {
	.probe		= davinci_evm_probe,
	.driver		= {
		.name	= "davinci_evm",
		.pm	= &snd_soc_pm_ops,
		.of_match_table = davinci_evm_dt_ids,
	},
};

module_platform_driver(davinci_evm_driver);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI EVM ASoC driver");
MODULE_LICENSE("GPL");
