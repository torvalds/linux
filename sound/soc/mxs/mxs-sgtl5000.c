// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include "../codecs/sgtl5000.h"
#include "mxs-saif.h"

static int mxs_sgtl5000_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int rate = params_rate(params);
	u32 mclk;
	int ret;

	/* sgtl5000 does not support 512*rate when in 96000 fs */
	switch (rate) {
	case 96000:
		mclk = 256 * rate;
		break;
	default:
		mclk = 512 * rate;
		break;
	}

	/* Set SGTL5000's SYSCLK (provided by SAIF MCLK) */
	ret = snd_soc_dai_set_sysclk(codec_dai, SGTL5000_SYSCLK, mclk, 0);
	if (ret) {
		dev_err(codec_dai->dev, "Failed to set sysclk to %u.%03uMHz\n",
			mclk / 1000000, mclk / 1000 % 1000);
		return ret;
	}

	/* The SAIF MCLK should be the same as SGTL5000_SYSCLK */
	ret = snd_soc_dai_set_sysclk(cpu_dai, MXS_SAIF_MCLK, mclk, 0);
	if (ret) {
		dev_err(cpu_dai->dev, "Failed to set sysclk to %u.%03uMHz\n",
			mclk / 1000000, mclk / 1000 % 1000);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops mxs_sgtl5000_hifi_ops = {
	.hw_params = mxs_sgtl5000_hw_params,
};

#define MXS_SGTL5000_DAI_FMT (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | \
	SND_SOC_DAIFMT_CBS_CFS)


SND_SOC_DAILINK_DEFS(hifi_tx,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "sgtl5000")));

SND_SOC_DAILINK_DEFS(hifi_rx,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "sgtl5000")));

static struct snd_soc_dai_link mxs_sgtl5000_dai[] = {
	{
		.name		= "HiFi Tx",
		.stream_name	= "HiFi Playback",
		.dai_fmt	= MXS_SGTL5000_DAI_FMT,
		.ops		= &mxs_sgtl5000_hifi_ops,
		.playback_only	= true,
		SND_SOC_DAILINK_REG(hifi_tx),
	}, {
		.name		= "HiFi Rx",
		.stream_name	= "HiFi Capture",
		.dai_fmt	= MXS_SGTL5000_DAI_FMT,
		.ops		= &mxs_sgtl5000_hifi_ops,
		.capture_only	= true,
		SND_SOC_DAILINK_REG(hifi_rx),
	},
};

static const struct snd_soc_dapm_widget mxs_sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static struct snd_soc_card mxs_sgtl5000 = {
	.name		= "mxs_sgtl5000",
	.owner		= THIS_MODULE,
	.dai_link	= mxs_sgtl5000_dai,
	.num_links	= ARRAY_SIZE(mxs_sgtl5000_dai),
};

static int mxs_sgtl5000_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mxs_sgtl5000;
	int ret, i;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *saif_np[2], *codec_np;

	saif_np[0] = of_parse_phandle(np, "saif-controllers", 0);
	saif_np[1] = of_parse_phandle(np, "saif-controllers", 1);
	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (!saif_np[0] || !saif_np[1] || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		mxs_sgtl5000_dai[i].codecs->name = NULL;
		mxs_sgtl5000_dai[i].codecs->of_node = codec_np;
		mxs_sgtl5000_dai[i].cpus->dai_name = NULL;
		mxs_sgtl5000_dai[i].cpus->of_node = saif_np[i];
	}

	of_node_put(codec_np);
	of_node_put(saif_np[0]);
	of_node_put(saif_np[1]);

	/*
	 * Set an init clock(11.28Mhz) for sgtl5000 initialization(i2c r/w).
	 * The Sgtl5000 sysclk is derived from saif0 mclk and it's range
	 * should be >= 8MHz and <= 27M.
	 */
	ret = mxs_saif_get_mclk(0, 44100 * 256, 44100);
	if (ret) {
		dev_err(&pdev->dev, "failed to get mclk\n");
		return ret;
	}

	card->dev = &pdev->dev;

	if (of_find_property(np, "audio-routing", NULL)) {
		card->dapm_widgets = mxs_sgtl5000_dapm_widgets;
		card->num_dapm_widgets = ARRAY_SIZE(mxs_sgtl5000_dapm_widgets);

		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret) {
			dev_err(&pdev->dev, "failed to parse audio-routing (%d)\n",
				ret);
			return ret;
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
				ret);
		return ret;
	}

	return 0;
}

static int mxs_sgtl5000_remove(struct platform_device *pdev)
{
	mxs_saif_put_mclk(0);

	return 0;
}

static const struct of_device_id mxs_sgtl5000_dt_ids[] = {
	{ .compatible = "fsl,mxs-audio-sgtl5000", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_sgtl5000_dt_ids);

static struct platform_driver mxs_sgtl5000_audio_driver = {
	.driver = {
		.name = "mxs-sgtl5000",
		.of_match_table = mxs_sgtl5000_dt_ids,
	},
	.probe = mxs_sgtl5000_probe,
	.remove = mxs_sgtl5000_remove,
};

module_platform_driver(mxs_sgtl5000_audio_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXS ALSA SoC Machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-sgtl5000");
