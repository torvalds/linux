// SPDX-License-Identifier: GPL-2.0-only
/*
 * max98925.c -- ALSA SoC Stereo MAX98925 driver
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
#include "max98925.h"

static const char *const dai_text[] = {
	"Left", "Right", "LeftRight", "LeftRightDiv2",
};

static const char * const max98925_boost_voltage_text[] = {
	"8.5V", "8.25V", "8.0V", "7.75V", "7.5V", "7.25V", "7.0V", "6.75V",
	"6.5V", "6.5V", "6.5V", "6.5V", "6.5V", "6.5V",	"6.5V", "6.5V"
};

static SOC_ENUM_SINGLE_DECL(max98925_boost_voltage,
	MAX98925_CONFIGURATION, M98925_BST_VOUT_SHIFT,
	max98925_boost_voltage_text);

static const char *const hpf_text[] = {
	"Disable", "DC Block", "100Hz",	"200Hz", "400Hz", "800Hz",
};

static const struct reg_default max98925_reg[] = {
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
	{ 0x1A, 0x06 }, /* DAI Clock Mode 1 */
	{ 0x1B, 0xC0 }, /* DAI Clock Mode 2 */
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

static const struct soc_enum max98925_dai_enum =
	SOC_ENUM_SINGLE(MAX98925_GAIN, 5, ARRAY_SIZE(dai_text), dai_text);

static const struct soc_enum max98925_hpf_enum =
	SOC_ENUM_SINGLE(MAX98925_FILTERS, 0, ARRAY_SIZE(hpf_text), hpf_text);

static const struct snd_kcontrol_new max98925_hpf_sel_mux =
	SOC_DAPM_ENUM("Rc Filter MUX Mux", max98925_hpf_enum);

static const struct snd_kcontrol_new max98925_dai_sel_mux =
	SOC_DAPM_ENUM("DAI IN MUX Mux", max98925_dai_enum);

static int max98925_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct max98925_priv *max98925 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(max98925->regmap,
			MAX98925_BLOCK_ENABLE,
			M98925_BST_EN_MASK |
			M98925_ADC_IMON_EN_MASK | M98925_ADC_VMON_EN_MASK,
			M98925_BST_EN_MASK |
			M98925_ADC_IMON_EN_MASK | M98925_ADC_VMON_EN_MASK);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(max98925->regmap,
			MAX98925_BLOCK_ENABLE, M98925_BST_EN_MASK |
			M98925_ADC_IMON_EN_MASK | M98925_ADC_VMON_EN_MASK, 0);
		break;
	default:
		return 0;
	}
	return 0;
}

static const struct snd_soc_dapm_widget max98925_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAI_OUT", "HiFi Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("DAI IN MUX", SND_SOC_NOPM, 0, 0,
				&max98925_dai_sel_mux),
	SND_SOC_DAPM_MUX("Rc Filter MUX", SND_SOC_NOPM, 0, 0,
				&max98925_hpf_sel_mux),
	SND_SOC_DAPM_DAC_E("Amp Enable", NULL, MAX98925_BLOCK_ENABLE,
			M98925_SPK_EN_SHIFT, 0, max98925_dac_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("Global Enable", MAX98925_GLOBAL_ENABLE,
			M98925_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
};

static const struct snd_soc_dapm_route max98925_audio_map[] = {
	{"DAI IN MUX", "Left", "DAI_OUT"},
	{"DAI IN MUX", "Right", "DAI_OUT"},
	{"DAI IN MUX", "LeftRight", "DAI_OUT"},
	{"DAI IN MUX", "LeftRightDiv2", "DAI_OUT"},
	{"Rc Filter MUX", "Disable", "DAI IN MUX"},
	{"Rc Filter MUX", "DC Block", "DAI IN MUX"},
	{"Rc Filter MUX", "100Hz", "DAI IN MUX"},
	{"Rc Filter MUX", "200Hz", "DAI IN MUX"},
	{"Rc Filter MUX", "400Hz", "DAI IN MUX"},
	{"Rc Filter MUX", "800Hz", "DAI IN MUX"},
	{"Amp Enable", NULL, "Rc Filter MUX"},
	{"BE_OUT", NULL, "Amp Enable"},
	{"BE_OUT", NULL, "Global Enable"},
};

static bool max98925_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98925_VBAT_DATA:
	case MAX98925_VBST_DATA:
	case MAX98925_LIVE_STATUS0:
	case MAX98925_LIVE_STATUS1:
	case MAX98925_LIVE_STATUS2:
	case MAX98925_STATE0:
	case MAX98925_STATE1:
	case MAX98925_STATE2:
	case MAX98925_FLAG0:
	case MAX98925_FLAG1:
	case MAX98925_FLAG2:
	case MAX98925_REV_VERSION:
		return true;
	default:
		return false;
	}
}

static bool max98925_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98925_IRQ_CLEAR0:
	case MAX98925_IRQ_CLEAR1:
	case MAX98925_IRQ_CLEAR2:
	case MAX98925_ALC_HOLD_RLS:
		return false;
	default:
		return true;
	}
}

static DECLARE_TLV_DB_SCALE(max98925_spk_tlv, -600, 100, 0);

static const struct snd_kcontrol_new max98925_snd_controls[] = {
	SOC_SINGLE_TLV("Speaker Volume", MAX98925_GAIN,
		M98925_SPK_GAIN_SHIFT, (1<<M98925_SPK_GAIN_WIDTH)-1, 0,
		max98925_spk_tlv),
	SOC_SINGLE("Ramp Switch", MAX98925_GAIN_RAMPING,
				M98925_SPK_RMP_EN_SHIFT, 1, 0),
	SOC_SINGLE("ZCD Switch", MAX98925_GAIN_RAMPING,
				M98925_SPK_ZCD_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Switch", MAX98925_THRESHOLD,
				M98925_ALC_EN_SHIFT, 1, 0),
	SOC_SINGLE("ALC Threshold", MAX98925_THRESHOLD, M98925_ALC_TH_SHIFT,
				(1<<M98925_ALC_TH_WIDTH)-1, 0),
	SOC_ENUM("Boost Output Voltage", max98925_boost_voltage),
};

/* codec sample rate and n/m dividers parameter table */
static const struct {
	int rate;
	int  sr;
	int divisors[3][2];
} rate_table[] = {
	{
		.rate = 8000,
		.sr = 0,
		.divisors = { {1, 375}, {5, 1764}, {1, 384} }
	},
	{
		.rate = 11025,
		.sr = 1,
		.divisors = { {147, 40000}, {1, 256}, {147, 40960} }
	},
	{
		.rate = 12000,
		.sr = 2,
		.divisors = { {1, 250}, {5, 1176}, {1, 256} }
	},
	{
		.rate = 16000,
		.sr = 3,
		.divisors = { {2, 375}, {5, 882}, {1, 192} }
	},
	{
		.rate = 22050,
		.sr = 4,
		.divisors = { {147, 20000}, {1, 128}, {147, 20480} }
	},
	{
		.rate = 24000,
		.sr = 5,
		.divisors = { {1, 125}, {5, 588}, {1, 128} }
	},
	{
		.rate = 32000,
		.sr = 6,
		.divisors = { {4, 375}, {5, 441}, {1, 96} }
	},
	{
		.rate = 44100,
		.sr = 7,
		.divisors = { {147, 10000}, {1, 64}, {147, 10240} }
	},
	{
		.rate = 48000,
		.sr = 8,
		.divisors = { {2, 125}, {5, 294}, {1, 64} }
	},
};

static inline int max98925_rate_value(struct snd_soc_component *component,
		int rate, int clock, int *value, int *n, int *m)
{
	int ret = -EINVAL;
	int i;

	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].rate >= rate) {
			*value = rate_table[i].sr;
			*n = rate_table[i].divisors[clock][0];
			*m = rate_table[i].divisors[clock][1];
			ret = 0;
			break;
		}
	}
	return ret;
}

static void max98925_set_sense_data(struct max98925_priv *max98925)
{
	/* set VMON slots */
	regmap_update_bits(max98925->regmap,
		MAX98925_DOUT_CFG_VMON,
		M98925_DAI_VMON_EN_MASK, M98925_DAI_VMON_EN_MASK);
	regmap_update_bits(max98925->regmap,
		MAX98925_DOUT_CFG_VMON,
		M98925_DAI_VMON_SLOT_MASK,
		max98925->v_slot << M98925_DAI_VMON_SLOT_SHIFT);
	/* set IMON slots */
	regmap_update_bits(max98925->regmap,
		MAX98925_DOUT_CFG_IMON,
		M98925_DAI_IMON_EN_MASK, M98925_DAI_IMON_EN_MASK);
	regmap_update_bits(max98925->regmap,
		MAX98925_DOUT_CFG_IMON,
		M98925_DAI_IMON_SLOT_MASK,
		max98925->i_slot << M98925_DAI_IMON_SLOT_SHIFT);
}

static int max98925_dai_set_fmt(struct snd_soc_dai *codec_dai,
				 unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98925_priv *max98925 = snd_soc_component_get_drvdata(component);
	unsigned int invert = 0;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_MAS_MASK, 0);
		max98925_set_sense_data(max98925);
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		/*
		 * set left channel DAI to provider mode,
		 * right channel always consumer
		 */
		regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_MAS_MASK, M98925_DAI_MAS_MASK);
		break;
	default:
		dev_err(component->dev, "DAI clock mode unsupported");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		invert = M98925_DAI_WCI_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert = M98925_DAI_BCI_MASK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		invert = M98925_DAI_BCI_MASK | M98925_DAI_WCI_MASK;
		break;
	default:
		dev_err(component->dev, "DAI invert mode unsupported");
		return -EINVAL;
	}

	regmap_update_bits(max98925->regmap, MAX98925_FORMAT,
			M98925_DAI_BCI_MASK | M98925_DAI_WCI_MASK, invert);
	return 0;
}

static int max98925_set_clock(struct max98925_priv *max98925,
		struct snd_pcm_hw_params *params)
{
	unsigned int dai_sr = 0, clock, mdll, n, m;
	struct snd_soc_component *component = max98925->component;
	int rate = params_rate(params);
	/* BCLK/LRCLK ratio calculation */
	int blr_clk_ratio = params_channels(params) * max98925->ch_size;

	switch (blr_clk_ratio) {
	case 32:
		regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_BSEL_MASK, M98925_DAI_BSEL_32);
		break;
	case 48:
		regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_BSEL_MASK, M98925_DAI_BSEL_48);
		break;
	case 64:
		regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_BSEL_MASK, M98925_DAI_BSEL_64);
		break;
	default:
		return -EINVAL;
	}

	switch (max98925->sysclk) {
	case 6000000:
		clock = 0;
		mdll  = M98925_MDLL_MULT_MCLKx16;
		break;
	case 11289600:
		clock = 1;
		mdll  = M98925_MDLL_MULT_MCLKx8;
		break;
	case 12000000:
		clock = 0;
		mdll  = M98925_MDLL_MULT_MCLKx8;
		break;
	case 12288000:
		clock = 2;
		mdll  = M98925_MDLL_MULT_MCLKx8;
		break;
	default:
		dev_info(max98925->component->dev, "unsupported sysclk %d\n",
					max98925->sysclk);
		return -EINVAL;
	}

	if (max98925_rate_value(component, rate, clock, &dai_sr, &n, &m))
		return -EINVAL;

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98925->regmap,
			MAX98925_DAI_CLK_MODE2,
			M98925_DAI_SR_MASK, dai_sr << M98925_DAI_SR_SHIFT);
	/* set DAI m divider */
	regmap_write(max98925->regmap,
		MAX98925_DAI_CLK_DIV_M_MSBS, m >> 8);
	regmap_write(max98925->regmap,
		MAX98925_DAI_CLK_DIV_M_LSBS, m & 0xFF);
	/* set DAI n divider */
	regmap_write(max98925->regmap,
		MAX98925_DAI_CLK_DIV_N_MSBS, n >> 8);
	regmap_write(max98925->regmap,
		MAX98925_DAI_CLK_DIV_N_LSBS, n & 0xFF);
	/* set MDLL */
	regmap_update_bits(max98925->regmap, MAX98925_DAI_CLK_MODE1,
			M98925_MDLL_MULT_MASK, mdll << M98925_MDLL_MULT_SHIFT);
	return 0;
}

static int max98925_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct max98925_priv *max98925 = snd_soc_component_get_drvdata(component);

	switch (params_width(params)) {
	case 16:
		regmap_update_bits(max98925->regmap,
				MAX98925_FORMAT,
				M98925_DAI_CHANSZ_MASK, M98925_DAI_CHANSZ_16);
		max98925->ch_size = 16;
		break;
	case 24:
		regmap_update_bits(max98925->regmap,
				MAX98925_FORMAT,
				M98925_DAI_CHANSZ_MASK, M98925_DAI_CHANSZ_24);
		max98925->ch_size = 24;
		break;
	case 32:
		regmap_update_bits(max98925->regmap,
				MAX98925_FORMAT,
				M98925_DAI_CHANSZ_MASK, M98925_DAI_CHANSZ_32);
		max98925->ch_size = 32;
		break;
	default:
		pr_err("%s: format unsupported %d",
				__func__, params_format(params));
		return -EINVAL;
	}
	dev_dbg(component->dev, "%s: format supported %d",
				__func__, params_format(params));
	return max98925_set_clock(max98925, params);
}

static int max98925_dai_set_sysclk(struct snd_soc_dai *dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct max98925_priv *max98925 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case 0:
		/* use MCLK for Left channel, right channel always BCLK */
		regmap_update_bits(max98925->regmap,
				MAX98925_DAI_CLK_MODE1,
				M98925_DAI_CLK_SOURCE_MASK, 0);
		break;
	case 1:
		/* configure dai clock source to BCLK instead of MCLK */
		regmap_update_bits(max98925->regmap,
				MAX98925_DAI_CLK_MODE1,
				M98925_DAI_CLK_SOURCE_MASK,
				M98925_DAI_CLK_SOURCE_MASK);
		break;
	default:
		return -EINVAL;
	}
	max98925->sysclk = freq;
	return 0;
}

#define MAX98925_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops max98925_dai_ops = {
	.set_sysclk = max98925_dai_set_sysclk,
	.set_fmt = max98925_dai_set_fmt,
	.hw_params = max98925_dai_hw_params,
};

static struct snd_soc_dai_driver max98925_dai[] = {
	{
		.name = "max98925-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = MAX98925_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = MAX98925_FORMATS,
		},
		.ops = &max98925_dai_ops,
	}
};

static int max98925_probe(struct snd_soc_component *component)
{
	struct max98925_priv *max98925 = snd_soc_component_get_drvdata(component);

	max98925->component = component;
	regmap_write(max98925->regmap, MAX98925_GLOBAL_ENABLE, 0x00);
	/* It's not the default but we need to set DAI_DLY */
	regmap_write(max98925->regmap,
			MAX98925_FORMAT, M98925_DAI_DLY_MASK);
	regmap_write(max98925->regmap, MAX98925_TDM_SLOT_SELECT, 0xC8);
	regmap_write(max98925->regmap, MAX98925_DOUT_HIZ_CFG1, 0xFF);
	regmap_write(max98925->regmap, MAX98925_DOUT_HIZ_CFG2, 0xFF);
	regmap_write(max98925->regmap, MAX98925_DOUT_HIZ_CFG3, 0xFF);
	regmap_write(max98925->regmap, MAX98925_DOUT_HIZ_CFG4, 0xF0);
	regmap_write(max98925->regmap, MAX98925_FILTERS, 0xD8);
	regmap_write(max98925->regmap, MAX98925_ALC_CONFIGURATION, 0xF8);
	regmap_write(max98925->regmap, MAX98925_CONFIGURATION, 0xF0);
	/* Disable ALC muting */
	regmap_write(max98925->regmap, MAX98925_BOOST_LIMITER, 0xF8);
	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_max98925 = {
	.probe			= max98925_probe,
	.controls		= max98925_snd_controls,
	.num_controls		= ARRAY_SIZE(max98925_snd_controls),
	.dapm_routes		= max98925_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98925_audio_map),
	.dapm_widgets		= max98925_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98925_dapm_widgets),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config max98925_regmap = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = MAX98925_REV_VERSION,
	.reg_defaults     = max98925_reg,
	.num_reg_defaults = ARRAY_SIZE(max98925_reg),
	.volatile_reg     = max98925_volatile_register,
	.readable_reg     = max98925_readable_register,
	.cache_type       = REGCACHE_RBTREE,
};

static int max98925_i2c_probe(struct i2c_client *i2c)
{
	int ret, reg;
	u32 value;
	struct max98925_priv *max98925;

	max98925 = devm_kzalloc(&i2c->dev,
			sizeof(*max98925), GFP_KERNEL);
	if (!max98925)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max98925);
	max98925->regmap = devm_regmap_init_i2c(i2c, &max98925_regmap);
	if (IS_ERR(max98925->regmap)) {
		ret = PTR_ERR(max98925->regmap);
		dev_err(&i2c->dev,
				"Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	if (!of_property_read_u32(i2c->dev.of_node, "vmon-slot-no", &value)) {
		if (value > M98925_DAI_VMON_SLOT_1E_1F) {
			dev_err(&i2c->dev, "vmon slot number is wrong:\n");
			return -EINVAL;
		}
		max98925->v_slot = value;
	}
	if (!of_property_read_u32(i2c->dev.of_node, "imon-slot-no", &value)) {
		if (value > M98925_DAI_IMON_SLOT_1E_1F) {
			dev_err(&i2c->dev, "imon slot number is wrong:\n");
			return -EINVAL;
		}
		max98925->i_slot = value;
	}

	ret = regmap_read(max98925->regmap, MAX98925_REV_VERSION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Read revision failed\n");
		return ret;
	}

	if ((reg != MAX98925_VERSION) && (reg != MAX98925_VERSION1)) {
		ret = -ENODEV;
		dev_err(&i2c->dev, "Invalid revision (%d 0x%02X)\n",
			ret, reg);
		return ret;
	}

	dev_info(&i2c->dev, "device version 0x%02X\n", reg);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_max98925,
			max98925_dai, ARRAY_SIZE(max98925_dai));
	if (ret < 0)
		dev_err(&i2c->dev,
				"Failed to register component: %d\n", ret);
	return ret;
}

static const struct i2c_device_id max98925_i2c_id[] = {
	{ "max98925", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98925_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id max98925_of_match[] = {
	{ .compatible = "maxim,max98925", },
	{ }
};
MODULE_DEVICE_TABLE(of, max98925_of_match);
#endif

static struct i2c_driver max98925_i2c_driver = {
	.driver = {
		.name = "max98925",
		.of_match_table = of_match_ptr(max98925_of_match),
	},
	.probe_new  = max98925_i2c_probe,
	.id_table = max98925_i2c_id,
};

module_i2c_driver(max98925_i2c_driver)

MODULE_DESCRIPTION("ALSA SoC MAX98925 driver");
MODULE_AUTHOR("Ralph Birt <rdbirt@gmail.com>, Anish kumar <anish.kumar@maximintegrated.com>");
MODULE_LICENSE("GPL");
