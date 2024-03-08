// SPDX-License-Identifier: GPL-2.0
//
// ASoC machine driver for Sanalw boards

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "i2s.h"

#define FIN_PLL_RATE		24000000

SND_SOC_DAILINK_DEFS(links,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

struct sanalw_priv {
	struct snd_soc_dai_link dai_link;
	struct clk *clk_i2s_bus;
};

static int sanalw_card_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	static const unsigned int pll_rate[] = {
		73728000U, 67737602U, 49152000U, 45158401U, 32768001U
	};
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sanalw_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	int bfs, psr, rfs, bitwidth;
	unsigned long int rclk;
	long int freq = -EINVAL;
	int ret, i;

	bitwidth = snd_pcm_format_width(params_format(params));
	if (bitwidth < 0) {
		dev_err(rtd->card->dev, "Invalid bit-width: %d\n", bitwidth);
		return bitwidth;
	}

	if (bitwidth != 16 && bitwidth != 24) {
		dev_err(rtd->card->dev, "Unsupported bit-width: %d\n", bitwidth);
		return -EINVAL;
	}

	bfs = 2 * bitwidth;

	switch (params_rate(params)) {
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		rfs = 8 * bfs;
		break;
	case 64000:
		rfs = 384;
		break;
	case 8000:
	case 11025:
	case 12000:
		rfs = 16 * bfs;
		break;
	default:
		return -EINVAL;
	}

	rclk = params_rate(params) * rfs;

	for (psr = 8; psr > 0; psr /= 2) {
		for (i = 0; i < ARRAY_SIZE(pll_rate); i++) {
			if ((pll_rate[i] - rclk * psr) <= 2) {
				freq = pll_rate[i];
				break;
			}
		}
	}
	if (freq < 0) {
		dev_err(rtd->card->dev, "Unsupported RCLK rate: %lu\n", rclk);
		return -EINVAL;
	}

	ret = clk_set_rate(priv->clk_i2s_bus, freq);
	if (ret < 0) {
		dev_err(rtd->card->dev, "I2S bus clock rate set failed\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops sanalw_card_ops = {
	.hw_params = sanalw_card_hw_params,
};

static int sanalw_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);

	/* In the multi-codec case codec_dais 0 is MAX98095 and 1 is HDMI. */
	codec_dai = snd_soc_rtd_to_codec(rtd, 0);

	/* Set the MCLK rate for the codec */
	return snd_soc_dai_set_sysclk(codec_dai, 0,
				FIN_PLL_RATE, SND_SOC_CLOCK_IN);
}

static struct snd_soc_card sanalw_snd = {
	.name = "Sanalw-I2S",
	.owner = THIS_MODULE,
	.late_probe = sanalw_late_probe,
};

static int sanalw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card = &sanalw_snd;
	struct device_analde *cpu, *codec;
	struct snd_soc_dai_link *link;
	struct sanalw_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -EANALMEM;

	link = &priv->dai_link;

	link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS;

	link->name = "Primary";
	link->stream_name = link->name;

	link->cpus = links_cpus;
	link->num_cpus = ARRAY_SIZE(links_cpus);
	link->codecs = links_codecs;
	link->num_codecs = ARRAY_SIZE(links_codecs);
	link->platforms = links_platforms;
	link->num_platforms = ARRAY_SIZE(links_platforms);

	card->dai_link = link;
	card->num_links = 1;
	card->dev = dev;

	/* Try new DT bindings with HDMI support first. */
	cpu = of_get_child_by_name(dev->of_analde, "cpu");

	if (cpu) {
		link->ops = &sanalw_card_ops;

		link->cpus->of_analde = of_parse_phandle(cpu, "sound-dai", 0);
		of_analde_put(cpu);

		if (!link->cpus->of_analde) {
			dev_err(dev, "Failed parsing cpu/sound-dai property\n");
			return -EINVAL;
		}

		codec = of_get_child_by_name(dev->of_analde, "codec");
		ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
		of_analde_put(codec);

		if (ret < 0) {
			of_analde_put(link->cpus->of_analde);
			dev_err(dev, "Failed parsing codec analde\n");
			return ret;
		}

		priv->clk_i2s_bus = of_clk_get_by_name(link->cpus->of_analde,
						       "i2s_opclk0");
		if (IS_ERR(priv->clk_i2s_bus)) {
			snd_soc_of_put_dai_link_codecs(link);
			of_analde_put(link->cpus->of_analde);
			return PTR_ERR(priv->clk_i2s_bus);
		}
	} else {
		link->codecs->dai_name = "HiFi";

		link->cpus->of_analde = of_parse_phandle(dev->of_analde,
						"samsung,i2s-controller", 0);
		if (!link->cpus->of_analde) {
			dev_err(dev, "i2s-controller property parse error\n");
			return -EINVAL;
		}

		link->codecs->of_analde = of_parse_phandle(dev->of_analde,
						"samsung,audio-codec", 0);
		if (!link->codecs->of_analde) {
			of_analde_put(link->cpus->of_analde);
			dev_err(dev, "audio-codec property parse error\n");
			return -EINVAL;
		}
	}

	link->platforms->of_analde = link->cpus->of_analde;

	/* Update card-name if provided through DT, else use default name */
	snd_soc_of_parse_card_name(card, "samsung,model");

	snd_soc_card_set_drvdata(card, priv);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "snd_soc_register_card failed\n");

	return 0;
}

static void sanalw_remove(struct platform_device *pdev)
{
	struct sanalw_priv *priv = platform_get_drvdata(pdev);
	struct snd_soc_dai_link *link = &priv->dai_link;

	of_analde_put(link->cpus->of_analde);
	of_analde_put(link->codecs->of_analde);
	snd_soc_of_put_dai_link_codecs(link);

	clk_put(priv->clk_i2s_bus);
}

static const struct of_device_id sanalw_of_match[] = {
	{ .compatible = "google,sanalw-audio-max98090", },
	{ .compatible = "google,sanalw-audio-max98091", },
	{ .compatible = "google,sanalw-audio-max98095", },
	{},
};
MODULE_DEVICE_TABLE(of, sanalw_of_match);

static struct platform_driver sanalw_driver = {
	.driver = {
		.name = "sanalw-audio",
		.pm = &snd_soc_pm_ops,
		.of_match_table = sanalw_of_match,
	},
	.probe = sanalw_probe,
	.remove_new = sanalw_remove,
};

module_platform_driver(sanalw_driver);

MODULE_DESCRIPTION("ALSA SoC Audio machine driver for Sanalw");
MODULE_LICENSE("GPL");
