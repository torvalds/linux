// SPDX-License-Identifier: GPL-2.0
//
// JZ4740 CODEC driver
//
// Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/regmap.h>

#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define JZ4740_REG_CODEC_1 0x0
#define JZ4740_REG_CODEC_2 0x4

#define JZ4740_CODEC_1_LINE_ENABLE BIT(29)
#define JZ4740_CODEC_1_MIC_ENABLE BIT(28)
#define JZ4740_CODEC_1_SW1_ENABLE BIT(27)
#define JZ4740_CODEC_1_ADC_ENABLE BIT(26)
#define JZ4740_CODEC_1_SW2_ENABLE BIT(25)
#define JZ4740_CODEC_1_DAC_ENABLE BIT(24)
#define JZ4740_CODEC_1_VREF_DISABLE BIT(20)
#define JZ4740_CODEC_1_VREF_AMP_DISABLE BIT(19)
#define JZ4740_CODEC_1_VREF_PULLDOWN BIT(18)
#define JZ4740_CODEC_1_VREF_LOW_CURRENT BIT(17)
#define JZ4740_CODEC_1_VREF_HIGH_CURRENT BIT(16)
#define JZ4740_CODEC_1_HEADPHONE_DISABLE BIT(14)
#define JZ4740_CODEC_1_HEADPHONE_AMP_CHANGE_ANY BIT(13)
#define JZ4740_CODEC_1_HEADPHONE_CHARGE BIT(12)
#define JZ4740_CODEC_1_HEADPHONE_PULLDOWN (BIT(11) | BIT(10))
#define JZ4740_CODEC_1_HEADPHONE_POWERDOWN_M BIT(9)
#define JZ4740_CODEC_1_HEADPHONE_POWERDOWN BIT(8)
#define JZ4740_CODEC_1_SUSPEND BIT(1)
#define JZ4740_CODEC_1_RESET BIT(0)

#define JZ4740_CODEC_1_LINE_ENABLE_OFFSET 29
#define JZ4740_CODEC_1_MIC_ENABLE_OFFSET 28
#define JZ4740_CODEC_1_SW1_ENABLE_OFFSET 27
#define JZ4740_CODEC_1_ADC_ENABLE_OFFSET 26
#define JZ4740_CODEC_1_SW2_ENABLE_OFFSET 25
#define JZ4740_CODEC_1_DAC_ENABLE_OFFSET 24
#define JZ4740_CODEC_1_HEADPHONE_DISABLE_OFFSET 14
#define JZ4740_CODEC_1_HEADPHONE_POWERDOWN_OFFSET 8

#define JZ4740_CODEC_2_INPUT_VOLUME_MASK		0x1f0000
#define JZ4740_CODEC_2_SAMPLE_RATE_MASK			0x000f00
#define JZ4740_CODEC_2_MIC_BOOST_GAIN_MASK		0x000030
#define JZ4740_CODEC_2_HEADPHONE_VOLUME_MASK	0x000003

#define JZ4740_CODEC_2_INPUT_VOLUME_OFFSET		16
#define JZ4740_CODEC_2_SAMPLE_RATE_OFFSET		 8
#define JZ4740_CODEC_2_MIC_BOOST_GAIN_OFFSET	 4
#define JZ4740_CODEC_2_HEADPHONE_VOLUME_OFFSET	 0

static const struct reg_default jz4740_codec_reg_defaults[] = {
	{ JZ4740_REG_CODEC_1, 0x021b2302 },
	{ JZ4740_REG_CODEC_2, 0x00170803 },
};

struct jz4740_codec {
	struct regmap *regmap;
};

static const DECLARE_TLV_DB_RANGE(jz4740_mic_tlv,
	0, 2, TLV_DB_SCALE_ITEM(0, 600, 0),
	3, 3, TLV_DB_SCALE_ITEM(2000, 0, 0)
);

static const DECLARE_TLV_DB_SCALE(jz4740_out_tlv, 0, 200, 0);
static const DECLARE_TLV_DB_SCALE(jz4740_in_tlv, -3450, 150, 0);

static const struct snd_kcontrol_new jz4740_codec_controls[] = {
	SOC_SINGLE_TLV("Master Playback Volume", JZ4740_REG_CODEC_2,
			JZ4740_CODEC_2_HEADPHONE_VOLUME_OFFSET, 3, 0,
			jz4740_out_tlv),
	SOC_SINGLE_TLV("Master Capture Volume", JZ4740_REG_CODEC_2,
			JZ4740_CODEC_2_INPUT_VOLUME_OFFSET, 31, 0,
			jz4740_in_tlv),
	SOC_SINGLE("Master Playback Switch", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_HEADPHONE_DISABLE_OFFSET, 1, 1),
	SOC_SINGLE_TLV("Mic Capture Volume", JZ4740_REG_CODEC_2,
			JZ4740_CODEC_2_MIC_BOOST_GAIN_OFFSET, 3, 0,
			jz4740_mic_tlv),
};

static const struct snd_kcontrol_new jz4740_codec_output_controls[] = {
	SOC_DAPM_SINGLE("Bypass Switch", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_SW1_ENABLE_OFFSET, 1, 0),
	SOC_DAPM_SINGLE("DAC Switch", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_SW2_ENABLE_OFFSET, 1, 0),
};

static const struct snd_kcontrol_new jz4740_codec_input_controls[] = {
	SOC_DAPM_SINGLE("Line Capture Switch", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_LINE_ENABLE_OFFSET, 1, 0),
	SOC_DAPM_SINGLE("Mic Capture Switch", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_MIC_ENABLE_OFFSET, 1, 0),
};

static const struct snd_soc_dapm_widget jz4740_codec_dapm_widgets[] = {
	SND_SOC_DAPM_ADC("ADC", "Capture", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_ADC_ENABLE_OFFSET, 0),
	SND_SOC_DAPM_DAC("DAC", "Playback", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_DAC_ENABLE_OFFSET, 0),

	SND_SOC_DAPM_MIXER("Output Mixer", JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_HEADPHONE_POWERDOWN_OFFSET, 1,
			jz4740_codec_output_controls,
			ARRAY_SIZE(jz4740_codec_output_controls)),

	SND_SOC_DAPM_MIXER_NAMED_CTL("Input Mixer", SND_SOC_NOPM, 0, 0,
			jz4740_codec_input_controls,
			ARRAY_SIZE(jz4740_codec_input_controls)),
	SND_SOC_DAPM_MIXER("Line Input", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),

	SND_SOC_DAPM_INPUT("MIC"),
	SND_SOC_DAPM_INPUT("LIN"),
	SND_SOC_DAPM_INPUT("RIN"),
};

static const struct snd_soc_dapm_route jz4740_codec_dapm_routes[] = {
	{"Line Input", NULL, "LIN"},
	{"Line Input", NULL, "RIN"},

	{"Input Mixer", "Line Capture Switch", "Line Input"},
	{"Input Mixer", "Mic Capture Switch", "MIC"},

	{"ADC", NULL, "Input Mixer"},

	{"Output Mixer", "Bypass Switch", "Input Mixer"},
	{"Output Mixer", "DAC Switch", "DAC"},

	{"LOUT", NULL, "Output Mixer"},
	{"ROUT", NULL, "Output Mixer"},
};

static int jz4740_codec_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct jz4740_codec *jz4740_codec = snd_soc_component_get_drvdata(dai->component);
	uint32_t val;

	switch (params_rate(params)) {
	case 8000:
		val = 0;
		break;
	case 11025:
		val = 1;
		break;
	case 12000:
		val = 2;
		break;
	case 16000:
		val = 3;
		break;
	case 22050:
		val = 4;
		break;
	case 24000:
		val = 5;
		break;
	case 32000:
		val = 6;
		break;
	case 44100:
		val = 7;
		break;
	case 48000:
		val = 8;
		break;
	default:
		return -EINVAL;
	}

	val <<= JZ4740_CODEC_2_SAMPLE_RATE_OFFSET;

	regmap_update_bits(jz4740_codec->regmap, JZ4740_REG_CODEC_2,
				JZ4740_CODEC_2_SAMPLE_RATE_MASK, val);

	return 0;
}

static const struct snd_soc_dai_ops jz4740_codec_dai_ops = {
	.hw_params = jz4740_codec_hw_params,
};

static struct snd_soc_dai_driver jz4740_codec_dai = {
	.name = "jz4740-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
	},
	.ops = &jz4740_codec_dai_ops,
	.symmetric_rate = 1,
};

static void jz4740_codec_wakeup(struct regmap *regmap)
{
	regmap_set_bits(regmap, JZ4740_REG_CODEC_1, JZ4740_CODEC_1_RESET);
	udelay(2);

	regmap_clear_bits(regmap, JZ4740_REG_CODEC_1,
			  JZ4740_CODEC_1_SUSPEND | JZ4740_CODEC_1_RESET);

	regcache_sync(regmap);
}

static int jz4740_codec_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct jz4740_codec *jz4740_codec = snd_soc_component_get_drvdata(component);
	struct regmap *regmap = jz4740_codec->regmap;
	unsigned int mask;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		mask = JZ4740_CODEC_1_VREF_DISABLE |
				JZ4740_CODEC_1_VREF_AMP_DISABLE |
				JZ4740_CODEC_1_HEADPHONE_POWERDOWN_M;

		regmap_clear_bits(regmap, JZ4740_REG_CODEC_1, mask);
		break;
	case SND_SOC_BIAS_STANDBY:
		/* The only way to clear the suspend flag is to reset the codec */
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			jz4740_codec_wakeup(regmap);

		mask = JZ4740_CODEC_1_VREF_DISABLE |
			JZ4740_CODEC_1_VREF_AMP_DISABLE |
			JZ4740_CODEC_1_HEADPHONE_POWERDOWN_M;

		regmap_set_bits(regmap, JZ4740_REG_CODEC_1, mask);
		break;
	case SND_SOC_BIAS_OFF:
		mask = JZ4740_CODEC_1_SUSPEND;
		regmap_set_bits(regmap, JZ4740_REG_CODEC_1, mask);
		regcache_mark_dirty(regmap);
		break;
	default:
		break;
	}

	return 0;
}

static int jz4740_codec_dev_probe(struct snd_soc_component *component)
{
	struct jz4740_codec *jz4740_codec = snd_soc_component_get_drvdata(component);

	regmap_update_bits(jz4740_codec->regmap, JZ4740_REG_CODEC_1,
			JZ4740_CODEC_1_SW2_ENABLE, JZ4740_CODEC_1_SW2_ENABLE);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_dev_jz4740_codec = {
	.probe			= jz4740_codec_dev_probe,
	.set_bias_level		= jz4740_codec_set_bias_level,
	.controls		= jz4740_codec_controls,
	.num_controls		= ARRAY_SIZE(jz4740_codec_controls),
	.dapm_widgets		= jz4740_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(jz4740_codec_dapm_widgets),
	.dapm_routes		= jz4740_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(jz4740_codec_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config jz4740_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = JZ4740_REG_CODEC_2,

	.reg_defaults = jz4740_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(jz4740_codec_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static int jz4740_codec_probe(struct platform_device *pdev)
{
	int ret;
	struct jz4740_codec *jz4740_codec;
	void __iomem *base;

	jz4740_codec = devm_kzalloc(&pdev->dev, sizeof(*jz4740_codec),
				    GFP_KERNEL);
	if (!jz4740_codec)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	jz4740_codec->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &jz4740_codec_regmap_config);
	if (IS_ERR(jz4740_codec->regmap))
		return PTR_ERR(jz4740_codec->regmap);

	platform_set_drvdata(pdev, jz4740_codec);

	ret = devm_snd_soc_register_component(&pdev->dev,
			&soc_codec_dev_jz4740_codec, &jz4740_codec_dai, 1);
	if (ret)
		dev_err(&pdev->dev, "Failed to register codec\n");

	return ret;
}

static const struct of_device_id jz4740_codec_of_matches[] = {
	{ .compatible = "ingenic,jz4740-codec", },
	{ }
};
MODULE_DEVICE_TABLE(of, jz4740_codec_of_matches);

static struct platform_driver jz4740_codec_driver = {
	.probe = jz4740_codec_probe,
	.driver = {
		.name = "jz4740-codec",
		.of_match_table = jz4740_codec_of_matches,
	},
};

module_platform_driver(jz4740_codec_driver);

MODULE_DESCRIPTION("JZ4740 SoC internal codec driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:jz4740-codec");
