// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2020-2025 NVIDIA CORPORATION. All rights reserved.
//
// tegra_audio_graph_card.c - Audio Graph based Tegra Machine Driver

#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/graph_card.h>
#include <sound/pcm_params.h>
#include <sound/soc-dai.h>

#define MAX_PLLA_OUT0_DIV 128

#define simple_to_tegra_priv(simple) \
		container_of(simple, struct tegra_audio_priv, simple)

enum srate_type {
	/*
	 * Sample rates multiple of 8000 Hz and below are supported:
	 * ( 8000, 16000, 32000, 48000, 96000, 192000 Hz )
	 */
	x8_RATE,

	/*
	 * Sample rates multiple of 11025 Hz and below are supported:
	 * ( 11025, 22050, 44100, 88200, 176400 Hz )
	 */
	x11_RATE,

	NUM_RATE_TYPE,
};

struct tegra_audio_priv {
	struct simple_util_priv simple;
	struct clk *clk_plla_out0;
	struct clk *clk_plla;
};

/* Tegra audio chip data */
struct tegra_audio_cdata {
	unsigned int plla_rates[NUM_RATE_TYPE];
	unsigned int plla_out0_rates[NUM_RATE_TYPE];
};

static bool need_clk_update(struct snd_soc_dai *dai)
{
	if (snd_soc_dai_is_dummy(dai) ||
	    !dai->driver->ops ||
	    !dai->driver->name)
		return false;

	if (strstr(dai->driver->name, "I2S") ||
	    strstr(dai->driver->name, "DMIC") ||
	    strstr(dai->driver->name, "DSPK"))
		return true;

	return false;
}

/* Setup PLL clock as per the given sample rate */
static int tegra_audio_graph_update_pll(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct simple_util_priv *simple = snd_soc_card_get_drvdata(rtd->card);
	struct tegra_audio_priv *priv = simple_to_tegra_priv(simple);
	struct device *dev = rtd->card->dev;
	const struct tegra_audio_cdata *data = of_device_get_match_data(dev);
	unsigned int plla_rate, plla_out0_rate, bclk;
	unsigned int srate = params_rate(params);
	int err;

	switch (srate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		plla_out0_rate = data->plla_out0_rates[x11_RATE];
		plla_rate = data->plla_rates[x11_RATE];
		break;
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		plla_out0_rate = data->plla_out0_rates[x8_RATE];
		plla_rate = data->plla_rates[x8_RATE];
		break;
	default:
		dev_err(rtd->card->dev, "Unsupported sample rate %u\n",
			srate);
		return -EINVAL;
	}

	/*
	 * Below is the clock relation:
	 *
	 *	PLLA
	 *	  |
	 *	  |--> PLLA_OUT0
	 *		  |
	 *		  |---> I2S modules
	 *		  |
	 *		  |---> DMIC modules
	 *		  |
	 *		  |---> DSPK modules
	 *
	 *
	 * Default PLLA_OUT0 rate might be too high when I/O is running
	 * at minimum PCM configurations. This may result in incorrect
	 * clock rates and glitchy audio. The maximum divider is 128
	 * and any thing higher than that won't work. Thus reduce PLLA_OUT0
	 * to work for lower configurations.
	 *
	 * This problem is seen for I2S only, as DMIC and DSPK minimum
	 * clock requirements are under allowed divider limits.
	 */
	bclk = srate * params_channels(params) * params_width(params);
	if (div_u64(plla_out0_rate, bclk) > MAX_PLLA_OUT0_DIV)
		plla_out0_rate >>= 1;

	dev_dbg(rtd->card->dev,
		"Update clock rates: PLLA(= %u Hz) and PLLA_OUT0(= %u Hz)\n",
		plla_rate, plla_out0_rate);

	/* Set PLLA rate */
	err = clk_set_rate(priv->clk_plla, plla_rate);
	if (err) {
		dev_err(rtd->card->dev,
			"Can't set plla rate for %u, err: %d\n",
			plla_rate, err);
		return err;
	}

	/* Set PLLA_OUT0 rate */
	err = clk_set_rate(priv->clk_plla_out0, plla_out0_rate);
	if (err) {
		dev_err(rtd->card->dev,
			"Can't set plla_out0 rate %u, err: %d\n",
			plla_out0_rate, err);
		return err;
	}

	return err;
}

static int tegra_audio_graph_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int err;

	if (need_clk_update(cpu_dai)) {
		err = tegra_audio_graph_update_pll(substream, params);
		if (err)
			return err;
	}

	return simple_util_hw_params(substream, params);
}

static const struct snd_soc_ops tegra_audio_graph_ops = {
	.startup	= simple_util_startup,
	.shutdown	= simple_util_shutdown,
	.hw_params	= tegra_audio_graph_hw_params,
};

static int tegra_audio_graph_card_probe(struct snd_soc_card *card)
{
	struct simple_util_priv *simple = snd_soc_card_get_drvdata(card);
	struct tegra_audio_priv *priv = simple_to_tegra_priv(simple);

	priv->clk_plla = devm_clk_get(card->dev, "pll_a");
	if (IS_ERR(priv->clk_plla)) {
		dev_err(card->dev, "Can't retrieve clk pll_a\n");
		return PTR_ERR(priv->clk_plla);
	}

	priv->clk_plla_out0 = devm_clk_get(card->dev, "plla_out0");
	if (IS_ERR(priv->clk_plla_out0)) {
		dev_err(card->dev, "Can't retrieve clk plla_out0\n");
		return PTR_ERR(priv->clk_plla_out0);
	}

	return graph_util_card_probe(card);
}

static int tegra_audio_graph_probe(struct platform_device *pdev)
{
	struct tegra_audio_priv *priv;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = simple_priv_to_card(&priv->simple);
	card->driver_name = "tegra-ape";

	card->probe = tegra_audio_graph_card_probe;

	/* audio_graph_parse_of() depends on below */
	card->component_chaining = 1;
	priv->simple.ops = &tegra_audio_graph_ops;
	priv->simple.force_dpcm = 1;

	return audio_graph_parse_of(&priv->simple, dev);
}

static const struct tegra_audio_cdata tegra210_data = {
	/* PLLA */
	.plla_rates[x8_RATE] = 368640000,
	.plla_rates[x11_RATE] = 338688000,
	/* PLLA_OUT0 */
	.plla_out0_rates[x8_RATE] = 49152000,
	.plla_out0_rates[x11_RATE] = 45158400,
};

static const struct tegra_audio_cdata tegra186_data = {
	/* PLLA */
	.plla_rates[x8_RATE] = 245760000,
	.plla_rates[x11_RATE] = 270950400,
	/* PLLA_OUT0 */
	.plla_out0_rates[x8_RATE] = 49152000,
	.plla_out0_rates[x11_RATE] = 45158400,
};

static const struct tegra_audio_cdata tegra264_data = {
	/* PLLA1 */
	.plla_rates[x8_RATE] = 983040000,
	.plla_rates[x11_RATE] = 993484800,
	/* PLLA1_OUT1 */
	.plla_out0_rates[x8_RATE] = 49152000,
	.plla_out0_rates[x11_RATE] = 45158400,
};

static const struct of_device_id graph_of_tegra_match[] = {
	{ .compatible = "nvidia,tegra210-audio-graph-card",
	  .data = &tegra210_data },
	{ .compatible = "nvidia,tegra186-audio-graph-card",
	  .data = &tegra186_data },
	{ .compatible = "nvidia,tegra264-audio-graph-card",
	  .data = &tegra264_data },
	{},
};
MODULE_DEVICE_TABLE(of, graph_of_tegra_match);

static struct platform_driver tegra_audio_graph_card = {
	.driver = {
		.name = "tegra-audio-graph-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = graph_of_tegra_match,
	},
	.probe = tegra_audio_graph_probe,
	.remove = simple_util_remove,
};
module_platform_driver(tegra_audio_graph_card);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Tegra Audio Graph Sound Card");
MODULE_AUTHOR("Sameer Pujar <spujar@nvidia.com>");
