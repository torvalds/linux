// SPDX-License-Identifier: GPL-2.0
//
// Lochnagar sound card driver
//
// Copyright (c) 2017-2019 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.
//
// Author: Charles Keepax <ckeepax@opensource.cirrus.com>
//         Piotr Stankiewicz <piotrs@opensource.cirrus.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <sound/soc.h>

#include <linux/mfd/lochnagar.h>
#include <linux/mfd/lochnagar1_regs.h>
#include <linux/mfd/lochnagar2_regs.h>

struct lochnagar_sc_priv {
	struct clk *mclk;
};

static const struct snd_soc_dapm_widget lochnagar_sc_widgets[] = {
	SND_SOC_DAPM_LINE("Line Jack", NULL),
	SND_SOC_DAPM_LINE("USB Audio", NULL),
};

static const struct snd_soc_dapm_route lochnagar_sc_routes[] = {
	{ "Line Jack", NULL, "AIF1 Playback" },
	{ "AIF1 Capture", NULL, "Line Jack" },

	{ "USB Audio", NULL, "USB1 Playback" },
	{ "USB Audio", NULL, "USB2 Playback" },
	{ "USB1 Capture", NULL, "USB Audio" },
	{ "USB2 Capture", NULL, "USB Audio" },
};

static const unsigned int lochnagar_sc_chan_vals[] = {
	4, 8,
};

static const struct snd_pcm_hw_constraint_list lochnagar_sc_chan_constraint = {
	.count = ARRAY_SIZE(lochnagar_sc_chan_vals),
	.list = lochnagar_sc_chan_vals,
};

static const unsigned int lochnagar_sc_rate_vals[] = {
	8000, 16000, 24000, 32000, 48000, 96000, 192000,
	22050, 44100, 88200, 176400,
};

static const struct snd_pcm_hw_constraint_list lochnagar_sc_rate_constraint = {
	.count = ARRAY_SIZE(lochnagar_sc_rate_vals),
	.list = lochnagar_sc_rate_vals,
};

static int lochnagar_sc_hw_rule_rate(struct snd_pcm_hw_params *params,
				     struct snd_pcm_hw_rule *rule)
{
	struct snd_interval range = {
		.min = 8000,
		.max = 24576000 / hw_param_interval(params, rule->deps[0])->max,
	};

	return snd_interval_refine(hw_param_interval(params, rule->var),
				   &range);
}

static int lochnagar_sc_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct lochnagar_sc_priv *priv = snd_soc_component_get_drvdata(comp);
	int ret;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &lochnagar_sc_rate_constraint);
	if (ret)
		return ret;

	return snd_pcm_hw_rule_add(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   lochnagar_sc_hw_rule_rate, priv,
				   SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
}

static int lochnagar_sc_line_startup(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct lochnagar_sc_priv *priv = snd_soc_component_get_drvdata(comp);
	int ret;

	ret = clk_prepare_enable(priv->mclk);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to enable MCLK: %d\n", ret);
		return ret;
	}

	ret = lochnagar_sc_startup(substream, dai);
	if (ret)
		return ret;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_CHANNELS,
					  &lochnagar_sc_chan_constraint);
}

static void lochnagar_sc_line_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct lochnagar_sc_priv *priv = snd_soc_component_get_drvdata(comp);

	clk_disable_unprepare(priv->mclk);
}

static int lochnagar_sc_check_fmt(struct snd_soc_dai *dai, unsigned int fmt,
				  unsigned int tar)
{
	tar |= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF;

	if ((fmt & ~SND_SOC_DAIFMT_CLOCK_MASK) != tar)
		return -EINVAL;

	return 0;
}

static int lochnagar_sc_set_line_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return lochnagar_sc_check_fmt(dai, fmt, SND_SOC_DAIFMT_CBS_CFS);
}

static int lochnagar_sc_set_usb_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return lochnagar_sc_check_fmt(dai, fmt, SND_SOC_DAIFMT_CBM_CFM);
}

static const struct snd_soc_dai_ops lochnagar_sc_line_ops = {
	.startup = lochnagar_sc_line_startup,
	.shutdown = lochnagar_sc_line_shutdown,
	.set_fmt = lochnagar_sc_set_line_fmt,
};

static const struct snd_soc_dai_ops lochnagar_sc_usb_ops = {
	.startup = lochnagar_sc_startup,
	.set_fmt = lochnagar_sc_set_usb_fmt,
};

static struct snd_soc_dai_driver lochnagar_sc_dai[] = {
	{
		.name = "lochnagar-line",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 4,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 4,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &lochnagar_sc_line_ops,
		.symmetric_rate = true,
		.symmetric_sample_bits = true,
	},
	{
		.name = "lochnagar-usb1",
		.playback = {
			.stream_name = "USB1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "USB1 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &lochnagar_sc_usb_ops,
		.symmetric_rate = true,
		.symmetric_sample_bits = true,
	},
	{
		.name = "lochnagar-usb2",
		.playback = {
			.stream_name = "USB2 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "USB2 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &lochnagar_sc_usb_ops,
		.symmetric_rate = true,
		.symmetric_sample_bits = true,
	},
};

static const struct snd_soc_component_driver lochnagar_sc_driver = {
	.dapm_widgets = lochnagar_sc_widgets,
	.num_dapm_widgets = ARRAY_SIZE(lochnagar_sc_widgets),
	.dapm_routes = lochnagar_sc_routes,
	.num_dapm_routes = ARRAY_SIZE(lochnagar_sc_routes),

	.endianness = 1,
};

static int lochnagar_sc_probe(struct platform_device *pdev)
{
	struct lochnagar_sc_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err(&pdev->dev, "Failed to get MCLK: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return devm_snd_soc_register_component(&pdev->dev,
					       &lochnagar_sc_driver,
					       lochnagar_sc_dai,
					       ARRAY_SIZE(lochnagar_sc_dai));
}

static const struct of_device_id lochnagar_of_match[] = {
	{ .compatible = "cirrus,lochnagar2-soundcard" },
	{}
};
MODULE_DEVICE_TABLE(of, lochnagar_of_match);

static struct platform_driver lochnagar_sc_codec_driver = {
	.driver = {
		.name = "lochnagar-soundcard",
		.of_match_table = lochnagar_of_match,
	},

	.probe = lochnagar_sc_probe,
};
module_platform_driver(lochnagar_sc_codec_driver);

MODULE_DESCRIPTION("ASoC Lochnagar Sound Card Driver");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:lochnagar-soundcard");
