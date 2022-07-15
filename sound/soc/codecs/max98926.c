// SPDX-License-Identifier: GPL-2.0-only
/*
 * max98926.c -- ALSA SoC MAX98926 driver
 * Copyright 2013-15 Maxim Integrated Products
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "max98926.h"

static const char * const max98926_boost_voltage_txt[] = {
	"8.5V", "8.25V", "8.0V", "7.75V", "7.5V", "7.25V", "7.0V", "6.75V",
	"6.5V", "6.5V", "6.5V", "6.5V", "6.5V", "6.5V", "6.5V", "6.5V"
};

static const char *const max98926_pdm_ch_text[] = {
	"Current", "Voltage",
};

static const char *const max98926_hpf_cutoff_txt[] = {
	"Disable", "DC Block", "100Hz",
	"200Hz", "400Hz", "800Hz",
};

static const struct reg_default max98926_reg[] = {
	{ 0x0B, 0x00 }, /* IRQ Enable0 */
	{ 0x0C, 0x00 }, /* IRQ Enable1 */
	{ 0x0D, 0x00 }, /* IRQ Enable2 */
	{ 0x0E, 0x00 }, /* IRQ Clear0 */
	{ 0x0F, 0x00 }, /* IRQ Clear1 */
	{ 0x10, 0x00 }, /* IRQ Clear2 */
	{ 0x11, 0xC0 }, /* Map0 */
	{ 0x12, 0x00 }, /* Map1 */
	{ 0x13, 0x00 }, /* Map2 */
	{ 0x14, 0xF0 }, /* Map3 */
	{ 0x15, 0x00 }, /* Map4 */
	{ 0x16, 0xAB }, /* Map5 */
	{ 0x17, 0x89 }, /* Map6 */
	{ 0x18, 0x00 }, /* Map7 */
	{ 0x19, 0x00 }, /* Map8 */
	{ 0x1A, 0x04 }, /* DAI Clock Mode 1 */
	{ 0x1B, 0x00 }, /* DAI Clock Mode 2 */
	{ 0x1C, 0x00 }, /* DAI Clock Divider Denominator MSBs */
	{ 0x1D, 0x00 }, /* DAI Clock Divider Denominator LSBs */
	{ 0x1E, 0xF0 }, /* DAI Clock Divider Numerator MSBs */
	{ 0x1F, 0x00 }, /* DAI Clock Divider Numerator LSBs */
	{ 0x20, 0x50 }, /* Format */
	{ 0x21, 0x00 }, /* TDM Slot Select */
	{ 0x22, 0x00 }, /* DOUT Configuration VMON */
	{ 0x23, 0x00 }, /* DOUT Configuration IMON */
	{ 0x24, 0x00 }, /* DOUT Configuration VBAT */
	{ 0x25, 0x00 }, /* DOUT Configuration VBST */
	{ 0x26, 0x00 }, /* DOUT Configuration FLAG */
	{ 0x27, 0xFF }, /* DOUT HiZ Configuration 1 */
	{ 0x28, 0xFF }, /* DOUT HiZ Configuration 2 */
	{ 0x29, 0xFF }, /* DOUT HiZ Configuration 3 */
	{ 0x2A, 0xFF }, /* DOUT HiZ Configuration 4 */
	{ 0x2B, 0x02 }, /* DOUT Drive Strength */
	{ 0x2C, 0x90 }, /* Filters */
	{ 0x2D, 0x00 }, /* Gain */
	{ 0x2E, 0x02 }, /* Gain Ramping */
	{ 0x2F, 0x00 }, /* Speaker Amplifier */
	{ 0x30, 0x0A }, /* Threshold */
	{ 0x31, 0x00 }, /* ALC Attack */
	{ 0x32, 0x80 }, /* ALC Atten and Release */
	{ 0x33, 0x00 }, /* ALC Infinite Hold Release */
	{ 0x34, 0x92 }, /* ALC Configuration */
	{ 0x35, 0x01 }, /* Boost Converter */
	{ 0x36, 0x00 }, /* Block Enable */
	{ 0x37, 0x00 }, /* Configuration */
	{ 0x38, 0x00 }, /* Global Enable */
	{ 0x3A, 0x00 }, /* Boost Limiter */
};

static const struct soc_enum max98926_voltage_enum[] = {
	SOC_ENUM_SINGLE(MAX98926_DAI_CLK_DIV_N_LSBS, 0,
		ARRAY_SIZE(max98926_pdm_ch_text),
		max98926_pdm_ch_text),
};

static const struct snd_kcontrol_new max98926_voltage_control =
	SOC_DAPM_ENUM("Route", max98926_voltage_enum);

static const struct soc_enum max98926_current_enum[] = {
	SOC_ENUM_SINGLE(MAX98926_DAI_CLK_DIV_N_LSBS,
		MAX98926_PDM_SOURCE_1_SHIFT,
		ARRAY_SIZE(max98926_pdm_ch_text),
		max98926_pdm_ch_text),
};

static const struct snd_kcontrol_new max98926_current_control =
	SOC_DAPM_ENUM("Route", max98926_current_enum);

static const struct snd_kcontrol_new max98926_mixer_controls[] = {
	SOC_DAPM_SINGLE("PCM Single Switch", MAX98926_SPK_AMP,
		MAX98926_INSELECT_MODE_SHIFT, 0, 0),
	SOC_DAPM_SINGLE("PDM Single Switch", MAX98926_SPK_AMP,
		MAX98926_INSELECT_MODE_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new max98926_dai_controls[] = {
	SOC_DAPM_SINGLE("Left", MAX98926_GAIN,
		MAX98926_DAC_IN_SEL_SHIFT, 0, 0),
	SOC_DAPM_SINGLE("Right", MAX98926_GAIN,
		MAX98926_DAC_IN_SEL_SHIFT, 1, 0),
	SOC_DAPM_SINGLE("LeftRight", MAX98926_GAIN,
		MAX98926_DAC_IN_SEL_SHIFT, 2, 0),
	SOC_DAPM_SINGLE("(Left+Right)/2 Switch", MAX98926_GAIN,
		MAX98926_DAC_IN_SEL_SHIFT, 3, 0),
};

static const struct snd_soc_dapm_widget max98926_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAI_OUT", "HiFi Playback", 0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("Amp Enable", NULL, MAX98926_BLOCK_ENABLE,
		MAX98926_SPK_EN_SHIFT, 0),
	SND_SOC_DAPM_SUPPLY("Global Enable", MAX98926_GLOBAL_ENABLE,
		MAX98926_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VI Enable", MAX98926_BLOCK_ENABLE,
		MAX98926_ADC_IMON_EN_WIDTH |
		MAX98926_ADC_VMON_EN_SHIFT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA("BST Enable", MAX98926_BLOCK_ENABLE,
		MAX98926_BST_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
	SND_SOC_DAPM_MIXER("PCM Sel", MAX98926_SPK_AMP,
		MAX98926_INSELECT_MODE_SHIFT, 0,
		&max98926_mixer_controls[0],
		ARRAY_SIZE(max98926_mixer_controls)),
	SND_SOC_DAPM_MIXER("DAI Sel",
		MAX98926_GAIN, MAX98926_DAC_IN_SEL_SHIFT, 0,
		&max98926_dai_controls[0],
		ARRAY_SIZE(max98926_dai_controls)),
	SND_SOC_DAPM_MUX("PDM CH1 Source",
		MAX98926_DAI_CLK_DIV_N_LSBS,
		MAX98926_PDM_CURRENT_SHIFT,
		0, &max98926_current_control),
	SND_SOC_DAPM_MUX("PDM CH0 Source",
		MAX98926_DAI_CLK_DIV_N_LSBS,
		MAX98926_PDM_VOLTAGE_SHIFT,
		0, &max98926_voltage_control),
};

static const struct snd_soc_dapm_route max98926_audio_map[] = {
	{"VI Enable", NULL, "DAI_OUT"},
	{"DAI Sel", "Left", "VI Enable"},
	{"DAI Sel", "Right", "VI Enable"},
	{"DAI Sel", "LeftRight", "VI Enable"},
	{"DAI Sel", "LeftRightDiv2", "VI Enable"},
	{"PCM Sel", "PCM", "DAI Sel"},

	{"PDM CH1 Source", "Current", "DAI_OUT"},
	{"PDM CH1 Source", "Voltage", "DAI_OUT"},
	{"PDM CH0 Source", "Current", "DAI_OUT"},
	{"PDM CH0 Source", "Voltage", "DAI_OUT"},
	{"PCM Sel", "Analog", "PDM CH1 Source"},
	{"PCM Sel", "Analog", "PDM CH0 Source"},
	{"Amp Enable", NULL, "PCM Sel"},

	{"BST Enable", NULL, "Amp Enable"},
	{"BE_OUT", NULL, "BST Enable"},
};

static bool max98926_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98926_VBAT_DATA:
	case MAX98926_VBST_DATA:
	case MAX98926_LIVE_STATUS0:
	case MAX98926_LIVE_STATUS1:
	case MAX98926_LIVE_STATUS2:
	case MAX98926_STATE0:
	case MAX98926_STATE1:
	case MAX98926_STATE2:
	case MAX98926_FLAG0:
	case MAX98926_FLAG1:
	case MAX98926_FLAG2:
	case MAX98926_VERSION:
		return true;
	default:
		return false;
	}
}

static bool max98926_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98926_IRQ_CLEAR0:
	case MAX98926_IRQ_CLEAR1:
	case MAX98926_IRQ_CLEAR2:
	case MAX98926_ALC_HOLD_RLS:
		return false;
	default:
		return true;
	}
};

static DECLARE_TLV_DB_SCALE(max98926_spk_tlv, -600, 100, 0);
static DECLARE_TLV_DB_RANGE(max98926_current_tlv,
	0, 11, TLV_DB_SCALE_ITEM(20, 20, 0),
	12, 15, TLV_DB_SCALE_ITEM(320, 40, 0),
);

static SOC_ENUM_SINGLE_DECL(max98926_dac_hpf_cutoff,
		MAX98926_FILTERS, MAX98926_DAC_HPF_SHIFT,
		max98926_hpf_cutoff_txt);

static SOC_ENUM_SINGLE_DECL(max98926_boost_voltage,
		MAX98926_CONFIGURATION, MAX98926_BST_VOUT_SHIFT,
		max98926_boost_voltage_txt);

static const struct snd_kcontrol_new max98926_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Volume", MAX98926_GAIN,
		MAX98926_SPK_GAIN_SHIFT,
		(1<<MAX98926_SPK_GAIN_WIDTH)-1, 0,
		max98926_spk_tlv),
	SOC_SINGLE("Ramp Switch", MAX98926_GAIN_RAMPING,
		MAX98926_SPK_RMP_EN_SHIFT, 1, 0),
	SOC_SINGLE("ZCD Switch", MAX98926_GAIN_RAMPING,
		MAX98926_SPK_ZCD_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Switch", MAX98926_THRESHOLD,
		MAX98926_ALC_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Threshold", MAX98926_THRESHOLD,
		MAX98926_ALC_TH_SHIFT,
		(1<<MAX98926_ALC_TH_WIDTH)-1, 0),
	SOC_ENUM("Boost Output Voltage", max98926_boost_voltage),
	SOC_SINGLE_TLV("Boost Current Limit", MAX98926_BOOST_LIMITER,
		MAX98926_BST_ILIM_SHIFT,
		(1<<MAX98926_BST_ILIM_SHIFT)-1, 0,
		max98926_current_tlv),
	SOC_ENUM("DAC HPF Cutoff", max98926_dac_hpf_cutoff),
	SOC_DOUBLE("PDM Channel One", MAX98926_DAI_CLK_DIV_N_LSBS,
		MAX98926_PDM_CHANNEL_1_SHIFT,
		MAX98926_PDM_CHANNEL_1_HIZ, 1, 0),
	SOC_DOUBLE("PDM Channel Zero", MAX98926_DAI_CLK_DIV_N_LSBS,
		MAX98926_PDM_CHANNEL_0_SHIFT,
		MAX98926_PDM_CHANNEL_0_HIZ, 1, 0),
};

static const struct {
	int rate;
	int  sr;
} rate_table[] = {
	{
		.rate = 8000,
		.sr = 0,
	},
	{
		.rate = 11025,
		.sr = 1,
	},
	{
		.rate = 12000,
		.sr = 2,
	},
	{
		.rate = 16000,
		.sr = 3,
	},
	{
		.rate = 22050,
		.sr = 4,
	},
	{
		.rate = 24000,
		.sr = 5,
	},
	{
		.rate = 32000,
		.sr = 6,
	},
	{
		.rate = 44100,
		.sr = 7,
	},
	{
		.rate = 48000,
		.sr = 8,
	},
};

static void max98926_set_sense_data(struct max98926_priv *max98926)
{
	regmap_update_bits(max98926->regmap,
		MAX98926_DOUT_CFG_VMON,
		MAX98926_DAI_VMON_EN_MASK,
		MAX98926_DAI_VMON_EN_MASK);
	regmap_update_bits(max98926->regmap,
		MAX98926_DOUT_CFG_IMON,
		MAX98926_DAI_IMON_EN_MASK,
		MAX98926_DAI_IMON_EN_MASK);

	if (!max98926->interleave_mode) {
		/* set VMON slots */
		regmap_update_bits(max98926->regmap,
			MAX98926_DOUT_CFG_VMON,
			MAX98926_DAI_VMON_SLOT_MASK,
			max98926->v_slot);
		/* set IMON slots */
		regmap_update_bits(max98926->regmap,
			MAX98926_DOUT_CFG_IMON,
			MAX98926_DAI_IMON_SLOT_MASK,
			max98926->i_slot);
	} else {
		/* enable interleave mode */
		regmap_update_bits(max98926->regmap,
			MAX98926_FORMAT,
			MAX98926_DAI_INTERLEAVE_MASK,
			MAX98926_DAI_INTERLEAVE_MASK);
		/* set interleave slots */
		regmap_update_bits(max98926->regmap,
			MAX98926_DOUT_CFG_VBAT,
			MAX98926_DAI_INTERLEAVE_SLOT_MASK,
			max98926->v_slot);
	}
}

static int max98926_dai_set_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98926_priv *max98926 = snd_soc_component_get_drvdata(component);
	unsigned int invert = 0;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		max98926_set_sense_data(max98926);
		break;
	default:
		dev_err(component->dev, "DAI clock mode unsupported\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		invert = MAX98926_DAI_WCI_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert = MAX98926_DAI_BCI_MASK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		invert = MAX98926_DAI_BCI_MASK | MAX98926_DAI_WCI_MASK;
		break;
	default:
		dev_err(component->dev, "DAI invert mode unsupported\n");
		return -EINVAL;
	}

	regmap_write(max98926->regmap,
			MAX98926_FORMAT, MAX98926_DAI_DLY_MASK);
	regmap_update_bits(max98926->regmap, MAX98926_FORMAT,
			MAX98926_DAI_BCI_MASK, invert);
	return 0;
}

static int max98926_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	int dai_sr = -EINVAL;
	int rate = params_rate(params), i;
	struct snd_soc_component *component = dai->component;
	struct max98926_priv *max98926 = snd_soc_component_get_drvdata(component);
	int blr_clk_ratio;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		regmap_update_bits(max98926->regmap,
			MAX98926_FORMAT,
			MAX98926_DAI_CHANSZ_MASK,
			MAX98926_DAI_CHANSZ_16);
		max98926->ch_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		regmap_update_bits(max98926->regmap,
			MAX98926_FORMAT,
			MAX98926_DAI_CHANSZ_MASK,
			MAX98926_DAI_CHANSZ_24);
		max98926->ch_size = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		regmap_update_bits(max98926->regmap,
			MAX98926_FORMAT,
			MAX98926_DAI_CHANSZ_MASK,
			MAX98926_DAI_CHANSZ_32);
		max98926->ch_size = 32;
		break;
	default:
		dev_dbg(component->dev, "format unsupported %d\n",
			params_format(params));
		return -EINVAL;
	}

	/* BCLK/LRCLK ratio calculation */
	blr_clk_ratio = params_channels(params) * max98926->ch_size;

	switch (blr_clk_ratio) {
	case 32:
		regmap_update_bits(max98926->regmap,
			MAX98926_DAI_CLK_MODE2,
			MAX98926_DAI_BSEL_MASK,
			MAX98926_DAI_BSEL_32);
		break;
	case 48:
		regmap_update_bits(max98926->regmap,
			MAX98926_DAI_CLK_MODE2,
			MAX98926_DAI_BSEL_MASK,
			MAX98926_DAI_BSEL_48);
		break;
	case 64:
		regmap_update_bits(max98926->regmap,
			MAX98926_DAI_CLK_MODE2,
			MAX98926_DAI_BSEL_MASK,
			MAX98926_DAI_BSEL_64);
		break;
	default:
		return -EINVAL;
	}

	/* find the closest rate */
	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].rate >= rate) {
			dai_sr = rate_table[i].sr;
			break;
		}
	}
	if (dai_sr < 0)
		return -EINVAL;

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98926->regmap,
		MAX98926_DAI_CLK_MODE2,
		MAX98926_DAI_SR_MASK, dai_sr << MAX98926_DAI_SR_SHIFT);
	return 0;
}

#define MAX98926_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops max98926_dai_ops = {
	.set_fmt = max98926_dai_set_fmt,
	.hw_params = max98926_dai_hw_params,
};

static struct snd_soc_dai_driver max98926_dai[] = {
{
	.name = "max98926-aif1",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = MAX98926_FORMATS,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = MAX98926_FORMATS,
	},
	.ops = &max98926_dai_ops,
}
};

static int max98926_probe(struct snd_soc_component *component)
{
	struct max98926_priv *max98926 = snd_soc_component_get_drvdata(component);

	max98926->component = component;

	/* Hi-Z all the slots */
	regmap_write(max98926->regmap, MAX98926_DOUT_HIZ_CFG4, 0xF0);
	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_max98926 = {
	.probe			= max98926_probe,
	.controls		= max98926_snd_controls,
	.num_controls		= ARRAY_SIZE(max98926_snd_controls),
	.dapm_routes		= max98926_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98926_audio_map),
	.dapm_widgets		= max98926_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98926_dapm_widgets),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config max98926_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX98926_VERSION,
	.reg_defaults	= max98926_reg,
	.num_reg_defaults = ARRAY_SIZE(max98926_reg),
	.volatile_reg	= max98926_volatile_register,
	.readable_reg	= max98926_readable_register,
	.cache_type		= REGCACHE_RBTREE,
};

static int max98926_i2c_probe(struct i2c_client *i2c)
{
	int ret, reg;
	u32 value;
	struct max98926_priv *max98926;

	max98926 = devm_kzalloc(&i2c->dev,
			sizeof(*max98926), GFP_KERNEL);
	if (!max98926)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max98926);
	max98926->regmap = devm_regmap_init_i2c(i2c, &max98926_regmap);
	if (IS_ERR(max98926->regmap)) {
		ret = PTR_ERR(max98926->regmap);
		dev_err(&i2c->dev,
				"Failed to allocate regmap: %d\n", ret);
		goto err_out;
	}
	if (of_property_read_bool(i2c->dev.of_node, "interleave-mode"))
		max98926->interleave_mode = true;

	if (!of_property_read_u32(i2c->dev.of_node, "vmon-slot-no", &value)) {
		if (value > MAX98926_DAI_VMON_SLOT_1E_1F) {
			dev_err(&i2c->dev, "vmon slot number is wrong:\n");
			return -EINVAL;
		}
		max98926->v_slot = value;
	}
	if (!of_property_read_u32(i2c->dev.of_node, "imon-slot-no", &value)) {
		if (value > MAX98926_DAI_IMON_SLOT_1E_1F) {
			dev_err(&i2c->dev, "imon slot number is wrong:\n");
			return -EINVAL;
		}
		max98926->i_slot = value;
	}
	ret = regmap_read(max98926->regmap,
			MAX98926_VERSION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read: %x\n", reg);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_max98926,
			max98926_dai, ARRAY_SIZE(max98926_dai));
	if (ret < 0)
		dev_err(&i2c->dev,
				"Failed to register component: %d\n", ret);
	dev_info(&i2c->dev, "device version: %x\n", reg);
err_out:
	return ret;
}

static const struct i2c_device_id max98926_i2c_id[] = {
	{ "max98926", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98926_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id max98926_of_match[] = {
	{ .compatible = "maxim,max98926", },
	{ }
};
MODULE_DEVICE_TABLE(of, max98926_of_match);
#endif

static struct i2c_driver max98926_i2c_driver = {
	.driver = {
		.name = "max98926",
		.of_match_table = of_match_ptr(max98926_of_match),
	},
	.probe_new = max98926_i2c_probe,
	.id_table = max98926_i2c_id,
};

module_i2c_driver(max98926_i2c_driver)
MODULE_DESCRIPTION("ALSA SoC MAX98926 driver");
MODULE_AUTHOR("Anish kumar <anish.kumar@maximintegrated.com>");
MODULE_LICENSE("GPL");
