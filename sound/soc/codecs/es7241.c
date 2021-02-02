// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <sound/soc.h>

struct es7241_clock_mode {
	unsigned int rate_min;
	unsigned int rate_max;
	unsigned int *slv_mfs;
	unsigned int slv_mfs_num;
	unsigned int mst_mfs;
	unsigned int mst_m0:1;
	unsigned int mst_m1:1;
};

struct es7241_chip {
	const struct es7241_clock_mode *modes;
	unsigned int mode_num;
};

struct es7241_data {
	struct gpio_desc *reset;
	struct gpio_desc *m0;
	struct gpio_desc *m1;
	unsigned int fmt;
	unsigned int mclk;
	bool is_slave;
	const struct es7241_chip *chip;
};

static void es7241_set_mode(struct es7241_data *priv,  int m0, int m1)
{
	/* put the device in reset */
	gpiod_set_value_cansleep(priv->reset, 0);

	/* set the mode */
	gpiod_set_value_cansleep(priv->m0, m0);
	gpiod_set_value_cansleep(priv->m1, m1);

	/* take the device out of reset - datasheet does not specify a delay */
	gpiod_set_value_cansleep(priv->reset, 1);
}

static int es7241_set_slave_mode(struct es7241_data *priv,
				 const struct es7241_clock_mode *mode,
				 unsigned int mfs)
{
	int j;

	if (!mfs)
		goto out_ok;

	for (j = 0; j < mode->slv_mfs_num; j++) {
		if (mode->slv_mfs[j] == mfs)
			goto out_ok;
	}

	return -EINVAL;

out_ok:
	es7241_set_mode(priv, 1, 1);
	return 0;
}

static int es7241_set_master_mode(struct es7241_data *priv,
				  const struct es7241_clock_mode *mode,
				  unsigned int mfs)
{
	/*
	 * We can't really set clock ratio, if the mclk/lrclk is different
	 * from what we provide, then error out
	 */
	if (mfs && mfs != mode->mst_mfs)
		return -EINVAL;

	es7241_set_mode(priv, mode->mst_m0, mode->mst_m1);

	return 0;
}

static int es7241_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct es7241_data *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int mfs = priv->mclk / rate;
	int i;

	for (i = 0; i < priv->chip->mode_num; i++) {
		const struct es7241_clock_mode *mode = &priv->chip->modes[i];

		if (rate < mode->rate_min || rate >= mode->rate_max)
			continue;

		if (priv->is_slave)
			return es7241_set_slave_mode(priv, mode, mfs);
		else
			return es7241_set_master_mode(priv, mode, mfs);
	}

	/* should not happen */
	dev_err(dai->dev, "unsupported rate: %u\n", rate);
	return -EINVAL;
}

static int es7241_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct es7241_data *priv = snd_soc_dai_get_drvdata(dai);

	if (dir == SND_SOC_CLOCK_IN && clk_id == 0) {
		priv->mclk = freq;
		return 0;
	}

	return -ENOTSUPP;
}

static int es7241_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct es7241_data *priv = snd_soc_dai_get_drvdata(dai);

	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		dev_err(dai->dev, "Unsupported dai clock inversion\n");
		return -EINVAL;
	}

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != priv->fmt) {
		dev_err(dai->dev, "Invalid dai format\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		priv->is_slave = true;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		priv->is_slave = false;
		break;

	default:
		dev_err(dai->dev, "Unsupported clock configuration\n");
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops es7241_dai_ops = {
	.set_fmt	= es7241_set_fmt,
	.hw_params	= es7241_hw_params,
	.set_sysclk	= es7241_set_sysclk,
};

static struct snd_soc_dai_driver es7241_dai = {
	.name = "es7241-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE  |
			    SNDRV_PCM_FMTBIT_S24_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE),
	},
	.ops = &es7241_dai_ops,
};

static const struct es7241_clock_mode es7241_modes[] = {
	{
		/* Single speed mode */
		.rate_min = 8000,
		.rate_max = 50000,
		.slv_mfs = (unsigned int[]) { 256, 384, 512, 768, 1024 },
		.slv_mfs_num = 5,
		.mst_mfs = 256,
		.mst_m0 = 0,
		.mst_m1 = 0,
	}, {
		/* Double speed mode */
		.rate_min = 50000,
		.rate_max = 100000,
		.slv_mfs = (unsigned int[]) { 128, 192 },
		.slv_mfs_num = 2,
		.mst_mfs = 128,
		.mst_m0 = 1,
		.mst_m1 = 0,
	}, {
		/* Quad speed mode */
		.rate_min = 100000,
		.rate_max = 200000,
		.slv_mfs = (unsigned int[]) { 64 },
		.slv_mfs_num = 1,
		.mst_mfs = 64,
		.mst_m0 = 0,
		.mst_m1 = 1,
	},
};

static const struct es7241_chip es7241_chip __maybe_unused = {
	.modes = es7241_modes,
	.mode_num = ARRAY_SIZE(es7241_modes),
};

static const struct snd_soc_dapm_widget es7241_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("AINL"),
	SND_SOC_DAPM_INPUT("AINR"),
	SND_SOC_DAPM_DAC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VDDP", 0, 0),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VDDD", 0, 0),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VDDA", 0, 0),
};

static const struct snd_soc_dapm_route es7241_dapm_routes[] = {
	{ "ADC", NULL, "AINL", },
	{ "ADC", NULL, "AINR", },
	{ "ADC", NULL, "VDDA", },
	{ "Capture", NULL, "VDDP", },
	{ "Capture", NULL, "VDDD", },
};

static const struct snd_soc_component_driver es7241_component_driver = {
	.dapm_widgets		= es7241_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es7241_dapm_widgets),
	.dapm_routes		= es7241_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es7241_dapm_routes),
	.idle_bias_on		= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static void es7241_parse_fmt(struct device *dev, struct es7241_data *priv)
{
	bool is_leftj;

	/*
	 * The format is given by a pull resistor on the SDOUT pin:
	 * pull-up for i2s, pull-down for left justified.
	 */
	is_leftj = of_property_read_bool(dev->of_node,
					 "everest,sdout-pull-down");
	if (is_leftj)
		priv->fmt = SND_SOC_DAIFMT_LEFT_J;
	else
		priv->fmt = SND_SOC_DAIFMT_I2S;
}

static int es7241_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct es7241_data *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->chip = of_device_get_match_data(dev);
	if (!priv->chip) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	es7241_parse_fmt(dev, priv);

	priv->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset)) {
		err = PTR_ERR(priv->reset);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'reset' gpio: %d", err);
		return err;
	}

	priv->m0 = devm_gpiod_get_optional(dev, "m0", GPIOD_OUT_LOW);
	if (IS_ERR(priv->m0)) {
		err = PTR_ERR(priv->m0);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'm0' gpio: %d", err);
		return err;
	}

	priv->m1 = devm_gpiod_get_optional(dev, "m1", GPIOD_OUT_LOW);
	if (IS_ERR(priv->m1)) {
		err = PTR_ERR(priv->m1);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'm1' gpio: %d", err);
		return err;
	}

	return devm_snd_soc_register_component(&pdev->dev,
				      &es7241_component_driver,
				      &es7241_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id es7241_ids[] = {
	{ .compatible = "everest,es7241", .data = &es7241_chip },
	{ }
};
MODULE_DEVICE_TABLE(of, es7241_ids);
#endif

static struct platform_driver es7241_driver = {
	.driver = {
		.name = "es7241",
		.of_match_table = of_match_ptr(es7241_ids),
	},
	.probe = es7241_probe,
};

module_platform_driver(es7241_driver);

MODULE_DESCRIPTION("ASoC ES7241 audio codec driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
