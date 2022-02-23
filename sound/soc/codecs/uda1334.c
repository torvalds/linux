// SPDX-License-Identifier: GPL-2.0-only
//
// uda1334.c  --  UDA1334 ALSA SoC Audio driver
//
// Based on WM8523 ALSA SoC Audio driver written by Mark Brown

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#define UDA1334_NUM_RATES 6

/* codec private data */
struct uda1334_priv {
	struct gpio_desc *mute;
	struct gpio_desc *deemph;
	unsigned int sysclk;
	unsigned int rate_constraint_list[UDA1334_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
};

static const struct snd_soc_dapm_widget uda1334_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route uda1334_dapm_routes[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

static int uda1334_put_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);
	int deemph = ucontrol->value.integer.value[0];

	if (deemph > 1)
		return -EINVAL;

	gpiod_set_value_cansleep(uda1334->deemph, deemph);

	return 0;
};

static int uda1334_get_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);
	int ret;

	ret = gpiod_get_value_cansleep(uda1334->deemph);
	if (ret < 0)
		return -EINVAL;

	ucontrol->value.integer.value[0] = ret;

	return 0;
};

static const struct snd_kcontrol_new uda1334_snd_controls[] = {
	SOC_SINGLE_BOOL_EXT("Playback Deemphasis Switch", 0,
			    uda1334_get_deemph, uda1334_put_deemph),
};

static const struct {
	int value;
	int ratio;
} lrclk_ratios[UDA1334_NUM_RATES] = {
	{ 1, 128 },
	{ 2, 192 },
	{ 3, 256 },
	{ 4, 384 },
	{ 5, 512 },
	{ 6, 768 },
};

static int uda1334_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);

	/*
	 * The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!uda1334->sysclk) {
		dev_err(component->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &uda1334->rate_constraint);

	gpiod_set_value_cansleep(uda1334->mute, 1);

	return 0;
}

static void uda1334_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);

	gpiod_set_value_cansleep(uda1334->mute, 0);
}

static int uda1334_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int i, j = 0;

	uda1334->sysclk = freq;

	uda1334->rate_constraint.count = 0;
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		val = freq / lrclk_ratios[i].ratio;
		/*
		 * Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */

		switch (val) {
		case 8000:
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
			dev_dbg(component->dev, "Supported sample rate: %dHz\n",
				val);
			uda1334->rate_constraint_list[j++] = val;
			uda1334->rate_constraint.count++;
			break;
		default:
			dev_dbg(component->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (uda1334->rate_constraint.count == 0)
		return -EINVAL;

	return 0;
}

static int uda1334_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	fmt &= (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK |
		SND_SOC_DAIFMT_MASTER_MASK);

	if (fmt != (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		    SND_SOC_DAIFMT_CBC_CFC)) {
		dev_err(codec_dai->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	return 0;
}

static int uda1334_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(dai->component);

	if (uda1334->mute)
		gpiod_set_value_cansleep(uda1334->mute, mute);

	return 0;
}

#define UDA1334_RATES SNDRV_PCM_RATE_8000_96000

#define UDA1334_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops uda1334_dai_ops = {
	.startup	= uda1334_startup,
	.shutdown	= uda1334_shutdown,
	.set_sysclk	= uda1334_set_dai_sysclk,
	.set_fmt	= uda1334_set_fmt,
	.mute_stream	= uda1334_mute_stream,
};

static struct snd_soc_dai_driver uda1334_dai = {
	.name = "uda1334-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = UDA1334_RATES,
		.formats = UDA1334_FORMATS,
	},
	.ops = &uda1334_dai_ops,
};

static int uda1334_probe(struct snd_soc_component *component)
{
	struct uda1334_priv *uda1334 = snd_soc_component_get_drvdata(component);

	uda1334->rate_constraint.list = &uda1334->rate_constraint_list[0];
	uda1334->rate_constraint.count =
		ARRAY_SIZE(uda1334->rate_constraint_list);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_uda1334 = {
	.probe			= uda1334_probe,
	.controls		= uda1334_snd_controls,
	.num_controls		= ARRAY_SIZE(uda1334_snd_controls),
	.dapm_widgets		= uda1334_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(uda1334_dapm_widgets),
	.dapm_routes		= uda1334_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(uda1334_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct of_device_id uda1334_of_match[] = {
	{ .compatible = "nxp,uda1334" },
	{ /* sentinel*/ }
};
MODULE_DEVICE_TABLE(of, uda1334_of_match);

static int uda1334_codec_probe(struct platform_device *pdev)
{
	struct uda1334_priv *uda1334;
	int ret;

	uda1334 = devm_kzalloc(&pdev->dev, sizeof(struct uda1334_priv),
			       GFP_KERNEL);
	if (!uda1334)
		return -ENOMEM;

	platform_set_drvdata(pdev, uda1334);

	uda1334->mute = devm_gpiod_get(&pdev->dev, "nxp,mute", GPIOD_OUT_LOW);
	if (IS_ERR(uda1334->mute)) {
		ret = PTR_ERR(uda1334->mute);
		dev_err(&pdev->dev, "Failed to get mute line: %d\n", ret);
		return ret;
	}

	uda1334->deemph = devm_gpiod_get(&pdev->dev, "nxp,deemph", GPIOD_OUT_LOW);
	if (IS_ERR(uda1334->deemph)) {
		ret = PTR_ERR(uda1334->deemph);
		dev_err(&pdev->dev, "Failed to get deemph line: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &soc_component_dev_uda1334,
					      &uda1334_dai, 1);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);

	return ret;
}

static struct platform_driver uda1334_codec_driver = {
	.probe		= uda1334_codec_probe,
	.driver		= {
		.name	= "uda1334-codec",
		.of_match_table = uda1334_of_match,
	},
};
module_platform_driver(uda1334_codec_driver);

MODULE_DESCRIPTION("ASoC UDA1334 driver");
MODULE_AUTHOR("Andra Danciu <andradanciu1997@gmail.com>");
MODULE_ALIAS("platform:uda1334-codec");
MODULE_LICENSE("GPL v2");
