// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs42l84.c -- CS42L84 ALSA SoC audio driver
 *
 * Copyright (C) The Asahi Linux Contributors
 *
 * Based on sound/soc/codecs/cs42l42{.c,.h}
 *   Copyright 2016 Cirrus Logic, Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "cs42l84.h"
#include "cirrus_legacy.h"

struct cs42l84_private {
	struct regmap *regmap;
	struct device *dev;
	struct gpio_desc *reset_gpio;
	struct snd_soc_jack *jack;
	struct mutex irq_lock;
	u8 tip_state;
	u8 ring_state;
	int pll_config;
	int bclk;
	u8 pll_mclk_f;
	u32 srate;
	u8 stream_use;
	int hs_type;
};

static bool cs42l84_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L84_DEVID ... CS42L84_DEVID+5:
	case CS42L84_TSRS_PLUG_INT_STATUS:
	case CS42L84_PLL_LOCK_STATUS:
	case CS42L84_TSRS_PLUG_STATUS:
	case CS42L84_HS_DET_STATUS2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config cs42l84_regmap = {
	.reg_bits = 16,
	.val_bits = 8,

	.volatile_reg = cs42l84_volatile_register,

	.max_register = 0x73fe,

	.cache_type = REGCACHE_MAPLE,

	.use_single_read = true,
	.use_single_write = true,
};

static int cs42l84_put_dac_vol(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *val)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kctl);
	struct soc_mixer_control *mc = (struct soc_mixer_control *) kctl->private_value;
	int vola, volb;
	int ret, ret2, updated = 0;

	vola = val->value.integer.value[0] + mc->min;
	volb = val->value.integer.value[1] + mc->min;

	if (vola < mc->min || vola > mc->max || volb < mc->min || volb > mc->max)
		return -EINVAL;

	ret = snd_soc_component_update_bits(component, CS42L84_FRZ_CTL,
					    CS42L84_FRZ_CTL_ENGAGE,
					    CS42L84_FRZ_CTL_ENGAGE);
	if (ret < 0)
		goto bail;
	updated |= ret;

	ret = snd_soc_component_update_bits(component, CS42L84_DAC_CHA_VOL_LSB,
					    0xff, vola & 0xff);
	if (ret < 0)
		goto bail;
	updated |= ret;

	ret = snd_soc_component_update_bits(component, CS42L84_DAC_CHA_VOL_MSB,
					    0xff, (vola >> 8) & 0x01);
	if (ret < 0)
		goto bail;
	updated |= ret;

	ret = snd_soc_component_update_bits(component, CS42L84_DAC_CHB_VOL_LSB,
					    0xff, volb & 0xff);
	if (ret < 0)
		goto bail;
	updated |= ret;

	ret = snd_soc_component_update_bits(component, CS42L84_DAC_CHB_VOL_MSB,
					    0xff, (volb >> 8) & 0x01);
	if (ret < 0)
		goto bail;
	ret |= updated;

bail:
	ret2 = snd_soc_component_update_bits(component, CS42L84_FRZ_CTL,
					     CS42L84_FRZ_CTL_ENGAGE, 0);
	if (ret2 < 0 && ret >= 0)
		ret = ret2;

	return ret;
}

static int cs42l84_get_dac_vol(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_value *val)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kctl);
	struct soc_mixer_control *mc = (struct soc_mixer_control *) kctl->private_value;
	int vola, volb;
	int ret;

	ret = snd_soc_component_read(component, CS42L84_DAC_CHA_VOL_LSB);
	if (ret < 0)
		return ret;
	vola = ret;

	ret = snd_soc_component_read(component, CS42L84_DAC_CHA_VOL_MSB);
	if (ret < 0)
		return ret;
	vola |= (ret & 1) << 8;

	ret = snd_soc_component_read(component, CS42L84_DAC_CHB_VOL_LSB);
	if (ret < 0)
		return ret;
	volb = ret;

	ret = snd_soc_component_read(component, CS42L84_DAC_CHB_VOL_MSB);
	if (ret < 0)
		return ret;
	volb |= (ret & 1) << 8;

	if (vola & BIT(8))
		vola |= ~((int)(BIT(8) - 1));
	if (volb & BIT(8))
		volb |= ~((int)(BIT(8) - 1));

	val->value.integer.value[0] = vola - mc->min;
	val->value.integer.value[1] = volb - mc->min;

	return 0;
}

static const DECLARE_TLV_DB_SCALE(cs42l84_dac_tlv, -12800, 50, true);
static const DECLARE_TLV_DB_SCALE(cs42l84_adc_tlv, -1200, 50, false);
static const DECLARE_TLV_DB_SCALE(cs42l84_pre_tlv, 0, 1000, false);

static const struct snd_kcontrol_new cs42l84_snd_controls[] = {
	SOC_DOUBLE_R_S_EXT_TLV("DAC Playback Volume", CS42L84_DAC_CHA_VOL_LSB,
			CS42L84_DAC_CHB_VOL_LSB, 0, -256, 24, 8, 0,
			cs42l84_get_dac_vol, cs42l84_put_dac_vol, cs42l84_dac_tlv),
	SOC_SINGLE_TLV("ADC Preamp Capture Volume", CS42L84_ADC_CTL1,
			CS42L84_ADC_CTL1_PREAMP_GAIN_SHIFT, 2, 0, cs42l84_pre_tlv),
	SOC_SINGLE_TLV("ADC PGA Capture Volume", CS42L84_ADC_CTL1,
			CS42L84_ADC_CTL1_PGA_GAIN_SHIFT, 24, 0, cs42l84_adc_tlv),
	SOC_SINGLE("ADC WNF Switch", CS42L84_ADC_CTL4,
			CS42L84_ADC_CTL4_WNF_EN_SHIFT, 1, 0),
	SOC_SINGLE("WNF Corner Frequency", CS42L84_ADC_CTL4,
			CS42L84_ADC_CTL4_WNF_CF_SHIFT, 3, 0),
	SOC_SINGLE("ADC HPF Switch", CS42L84_ADC_CTL4,
			CS42L84_ADC_CTL4_HPF_EN_SHIFT, 1, 0),
	SOC_SINGLE("HPF Corner Frequency", CS42L84_ADC_CTL4,
			CS42L84_ADC_CTL4_HPF_CF_SHIFT, 3, 0),
};

static const char * const cs42l84_mux_text[] = {
	"Blank", "ADC", "ASP RX CH1", "ASP RX CH2",
};

static const unsigned int cs42l84_mux_values[] = {
	0b0000, 0b0111, 0b1101, 0b1110,
};

static SOC_VALUE_ENUM_SINGLE_DECL(cs42l84_daca_mux_enum,
		CS42L84_BUS_DAC_SRC, CS42L84_BUS_DAC_SRC_DACA_SHIFT,
		0b1111, cs42l84_mux_text, cs42l84_mux_values);

static SOC_VALUE_ENUM_SINGLE_DECL(cs42l84_dacb_mux_enum,
		CS42L84_BUS_DAC_SRC, CS42L84_BUS_DAC_SRC_DACB_SHIFT,
		0b1111, cs42l84_mux_text, cs42l84_mux_values);

static SOC_VALUE_ENUM_SINGLE_DECL(cs42l84_sdout1_mux_enum,
		CS42L84_BUS_ASP_TX_SRC, CS42L84_BUS_ASP_TX_SRC_CH1_SHIFT,
		0b1111, cs42l84_mux_text, cs42l84_mux_values);

static const struct snd_kcontrol_new cs42l84_daca_mux_ctrl =
	SOC_DAPM_ENUM("DACA Select", cs42l84_daca_mux_enum);

static const struct snd_kcontrol_new cs42l84_dacb_mux_ctrl =
	SOC_DAPM_ENUM("DACB Select", cs42l84_dacb_mux_enum);

static const struct snd_kcontrol_new cs42l84_sdout1_mux_ctrl =
	SOC_DAPM_ENUM("SDOUT1 Select", cs42l84_sdout1_mux_enum);

static const struct snd_soc_dapm_widget cs42l84_dapm_widgets[] = {
	/* Playback Path */
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_DAC("DAC", NULL, CS42L84_MSM_BLOCK_EN2, CS42L84_MSM_BLOCK_EN2_DAC_SHIFT, 0),
	SND_SOC_DAPM_MUX("DACA Select", SND_SOC_NOPM, 0, 0, &cs42l84_daca_mux_ctrl),
	SND_SOC_DAPM_MUX("DACB Select", SND_SOC_NOPM, 0, 0, &cs42l84_dacb_mux_ctrl),
	SND_SOC_DAPM_AIF_IN("SDIN1", NULL, 0, CS42L84_ASP_RX_EN, CS42L84_ASP_RX_EN_CH1_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("SDIN2", NULL, 1, CS42L84_ASP_RX_EN, CS42L84_ASP_RX_EN_CH2_SHIFT, 0),

	/* Capture Path */
	SND_SOC_DAPM_INPUT("HS"),
	SND_SOC_DAPM_ADC("ADC", NULL, CS42L84_MSM_BLOCK_EN2, CS42L84_MSM_BLOCK_EN2_ADC_SHIFT, 0),
	SND_SOC_DAPM_MUX("SDOUT1 Select", SND_SOC_NOPM, 0, 0, &cs42l84_sdout1_mux_ctrl),
	SND_SOC_DAPM_AIF_OUT("SDOUT1", NULL, 0, CS42L84_ASP_TX_EN, CS42L84_ASP_TX_EN_CH1_SHIFT, 0),

	/* Playback/Capture Requirements */
	SND_SOC_DAPM_SUPPLY("BUS", CS42L84_MSM_BLOCK_EN2, CS42L84_MSM_BLOCK_EN2_BUS_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASP", CS42L84_MSM_BLOCK_EN2, CS42L84_MSM_BLOCK_EN2_ASP_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BCLK", CS42L84_ASP_CTL, CS42L84_ASP_CTL_BCLK_EN_SHIFT, 0, NULL, 0),
};

static const struct snd_soc_dapm_route cs42l84_audio_map[] = {
	/* Playback Path */
	{"HP", NULL, "DAC"},
	{"DAC", NULL, "DACA Select"},
	{"DAC", NULL, "DACB Select"},
	{"DACA Select", "ASP RX CH1", "SDIN1"},
	{"DACA Select", "ASP RX CH2", "SDIN2"},
	{"DACB Select", "ASP RX CH1", "SDIN1"},
	{"DACB Select", "ASP RX CH2", "SDIN2"},
	{"SDIN1", NULL, "Playback"},
	{"SDIN2", NULL, "Playback"},

	{"ADC", NULL, "HS"},
	{"SDOUT1 Select", "ADC", "ADC"},
	{"SDOUT1", NULL, "SDOUT1 Select"},
	{"Capture", NULL, "SDOUT1"},

	/* Playback Requirements */
	{"DAC", NULL, "BUS"},
	{"SDIN1", NULL, "ASP"},
	{"SDIN2", NULL, "ASP"},
	{"SDIN1", NULL, "BCLK"},
	{"SDIN2", NULL, "BCLK"},

	/* Capture Requirements */
	{"SDOUT1", NULL, "BUS"},
	{"SDOUT1", NULL, "ASP"},
	{"SDOUT1", NULL, "BCLK"},
};

static int cs42l84_set_jack(struct snd_soc_component *component, struct snd_soc_jack *jk, void *d)
{
	struct cs42l84_private *cs42l84 = snd_soc_component_get_drvdata(component);

	/* Prevent race with interrupt handler */
	mutex_lock(&cs42l84->irq_lock);
	cs42l84->jack = jk;
	snd_soc_jack_report(jk, cs42l84->hs_type, SND_JACK_HEADSET);
	mutex_unlock(&cs42l84->irq_lock);

	return 0;
}

static int cs42l84_component_probe(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, CS42L84_ASP_CTL,
			CS42L84_ASP_CTL_TDM_MODE, 0);
	snd_soc_component_update_bits(component, CS42L84_HP_VOL_CTL,
			CS42L84_HP_VOL_CTL_SOFT | CS42L84_HP_VOL_CTL_ZERO_CROSS,
			CS42L84_HP_VOL_CTL_ZERO_CROSS);

	/* TDM settings */
	snd_soc_component_update_bits(component, CS42L84_ASP_RX_CH1_CTL1,
			CS42L84_ASP_RX_CHx_CTL1_EDGE |
			CS42L84_ASP_RX_CHx_CTL1_SLOT_START_LSB, 0);
	snd_soc_component_update_bits(component, CS42L84_ASP_RX_CH1_CTL2,
			CS42L84_ASP_RX_CHx_CTL2_SLOT_START_MSB, 0);
	snd_soc_component_update_bits(component, CS42L84_ASP_RX_CH2_CTL1,
			CS42L84_ASP_RX_CHx_CTL1_EDGE |
			CS42L84_ASP_RX_CHx_CTL1_SLOT_START_LSB,
			CS42L84_ASP_RX_CHx_CTL1_EDGE);
	snd_soc_component_update_bits(component, CS42L84_ASP_RX_CH2_CTL2,
			CS42L84_ASP_RX_CHx_CTL2_SLOT_START_MSB, 0);
	snd_soc_component_update_bits(component, CS42L84_ASP_TX_CH1_CTL1,
			CS42L84_ASP_RX_CHx_CTL1_EDGE | \
			CS42L84_ASP_RX_CHx_CTL1_SLOT_START_LSB, 0);
	snd_soc_component_update_bits(component, CS42L84_ASP_TX_CH1_CTL2,
			CS42L84_ASP_RX_CHx_CTL2_SLOT_START_MSB, 0);
	snd_soc_component_update_bits(component, CS42L84_ASP_TX_CH2_CTL1,
			CS42L84_ASP_RX_CHx_CTL1_EDGE | \
			CS42L84_ASP_RX_CHx_CTL1_SLOT_START_LSB,
			CS42L84_ASP_RX_CHx_CTL1_EDGE);
	snd_soc_component_update_bits(component, CS42L84_ASP_TX_CH2_CTL2,
			CS42L84_ASP_RX_CHx_CTL2_SLOT_START_MSB, 0);
	/* Routing defaults */
	snd_soc_component_write(component, CS42L84_BUS_DAC_SRC,
			0b1101 << CS42L84_BUS_DAC_SRC_DACA_SHIFT |
			0b1110 << CS42L84_BUS_DAC_SRC_DACB_SHIFT);
	snd_soc_component_write(component, CS42L84_BUS_ASP_TX_SRC,
			0b0111 << CS42L84_BUS_ASP_TX_SRC_CH1_SHIFT);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs42l84 = {
	.set_jack		= cs42l84_set_jack,
	.probe			= cs42l84_component_probe,
	.controls		= cs42l84_snd_controls,
	.num_controls		= ARRAY_SIZE(cs42l84_snd_controls),
	.dapm_widgets		= cs42l84_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs42l84_dapm_widgets),
	.dapm_routes		= cs42l84_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs42l84_audio_map),
	.endianness		= 1,
};

struct cs42l84_pll_params {
	u32 bclk;
	u8 mclk_src_sel;
	u8 bclk_prediv;
	u8 pll_div_int;
	u32 pll_div_frac;
	u8 pll_mode;
	u8 pll_divout;
	u32 mclk_int;
};

/*
 * Common PLL Settings for given BCLK
 */
static const struct cs42l84_pll_params pll_ratio_table[] = {
	{  3072000, 1, 0, 0x40, 0x000000, 0x03, 0x10, 12288000},
	{  6144000, 1, 1, 0x40, 0x000000, 0x03, 0x10, 12288000},
	{ 12288000, 0, 0, 0, 0, 0, 0,                 12288000},
	{ 24576000, 1, 3, 0x40, 0x000000, 0x03, 0x10, 12288000},
};

static int cs42l84_pll_config(struct snd_soc_component *component)
{
	struct cs42l84_private *cs42l84 = snd_soc_component_get_drvdata(component);
	int i;
	u32 clk;
	u32 fsync;

	clk = cs42l84->bclk;

	/* Don't reconfigure if there is an audio stream running */
	if (cs42l84->stream_use) {
		if (pll_ratio_table[cs42l84->pll_config].bclk == clk)
			return 0;
		else
			return -EBUSY;
	}

	for (i = 0; i < ARRAY_SIZE(pll_ratio_table); i++) {
		if (pll_ratio_table[i].bclk == clk) {
			cs42l84->pll_config = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(pll_ratio_table))
		return -EINVAL;

	/* Set up the LRCLK */
	fsync = clk / cs42l84->srate;
	if (((fsync * cs42l84->srate) != clk)
			|| ((fsync % 2) != 0)) {
		dev_err(component->dev,
			"Unsupported bclk %d/sample rate %d\n",
			clk, cs42l84->srate);
		return -EINVAL;
	}

	/* Set the LRCLK period */
	snd_soc_component_update_bits(component, CS42L84_ASP_FSYNC_CTL2,
		CS42L84_ASP_FSYNC_CTL2_BCLK_PERIOD_LO,
		FIELD_PREP(CS42L84_ASP_FSYNC_CTL2_BCLK_PERIOD_LO, fsync & 0x7f));
	snd_soc_component_update_bits(component, CS42L84_ASP_FSYNC_CTL3,
		CS42L84_ASP_FSYNC_CTL3_BCLK_PERIOD_HI,
		FIELD_PREP(CS42L84_ASP_FSYNC_CTL3_BCLK_PERIOD_HI, fsync >> 7));

	/* Save what the MCLK will be */
	switch (pll_ratio_table[i].mclk_int) {
	case 12000000:
		cs42l84->pll_mclk_f = CS42L84_CCM_CTL1_MCLK_F_12MHZ;
		break;
	case 12288000:
		cs42l84->pll_mclk_f = CS42L84_CCM_CTL1_MCLK_F_12_288KHZ;
		break;
	case 24000000:
		cs42l84->pll_mclk_f = CS42L84_CCM_CTL1_MCLK_F_24MHZ;
		break;
	case 24576000:
		cs42l84->pll_mclk_f = CS42L84_CCM_CTL1_MCLK_F_24_576KHZ;
		break;
	}

	snd_soc_component_update_bits(component, CS42L84_PLL_CTL1, CS42L84_PLL_CTL1_EN, 0);

	if (pll_ratio_table[i].mclk_src_sel) {
		/* Configure PLL */
		snd_soc_component_update_bits(component,
			CS42L84_CCM_CTL3, CS42L84_CCM_CTL3_REFCLK_DIV,
			FIELD_PREP(CS42L84_CCM_CTL3_REFCLK_DIV, pll_ratio_table[i].bclk_prediv));
		snd_soc_component_write(component,
			CS42L84_PLL_DIV_INT,
			pll_ratio_table[i].pll_div_int);
		snd_soc_component_write(component,
			CS42L84_PLL_DIV_FRAC0,
			pll_ratio_table[i].pll_div_frac);
		snd_soc_component_write(component,
			CS42L84_PLL_DIV_FRAC1,
			pll_ratio_table[i].pll_div_frac >> 8);
		snd_soc_component_write(component,
			CS42L84_PLL_DIV_FRAC2,
			pll_ratio_table[i].pll_div_frac >> 16);
		snd_soc_component_update_bits(component,
			CS42L84_PLL_CTL1, CS42L84_PLL_CTL1_MODE,
			FIELD_PREP(CS42L84_PLL_CTL1_MODE, pll_ratio_table[i].pll_mode));
		snd_soc_component_write(component,
			CS42L84_PLL_DIVOUT,
			pll_ratio_table[i].pll_divout);
	}

	return 0;
}

static int cs42l84_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		return -EINVAL;
	}

	/* Bitclock/frame inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cs42l84_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l84_private *cs42l84 = snd_soc_component_get_drvdata(component);
	int ret;
	u32 ccm_samp_rate;

	cs42l84->srate = params_rate(params);

	ret = cs42l84_pll_config(component);
	if (ret)
		return ret;

	switch (params_rate(params)) {
	case 44100:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_44K1HZ;
		break;
	case 48000:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_48KHZ;
		break;
	case 88200:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_88K2HZ;
		break;
	case 96000:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_96KHZ;
		break;
	case 176400:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_176K4HZ;
		break;
	case 192000:
		ccm_samp_rate = CS42L84_CCM_SAMP_RATE_RATE_192KHZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, CS42L84_CCM_SAMP_RATE, ccm_samp_rate);

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		snd_soc_component_write(component, CS42L84_ASP_RX_CH1_WIDTH,
					params_width(params) - 1);
		snd_soc_component_write(component, CS42L84_ASP_RX_CH2_WIDTH,
					params_width(params) - 1);
		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		snd_soc_component_write(component, CS42L84_ASP_TX_CH1_WIDTH,
					params_width(params) - 1);
		snd_soc_component_write(component, CS42L84_ASP_TX_CH2_WIDTH,
					params_width(params) - 1);
		break;
	}

	return 0;
}

static int cs42l84_set_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l84_private *cs42l84 = snd_soc_component_get_drvdata(component);
	int i;

	if (freq == 0) {
		cs42l84->bclk = 0;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(pll_ratio_table); i++) {
		if (pll_ratio_table[i].bclk == freq) {
			cs42l84->bclk = freq;
			return 0;
		}
	}

	dev_err(component->dev, "BCLK %u not supported\n", freq);

	return -EINVAL;
}

static int cs42l84_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct cs42l84_private *cs42l84 = snd_soc_component_get_drvdata(component);
	unsigned int regval;
	int ret;

	if (mute) {
		/* Mute the headphone */
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_soc_component_update_bits(component, CS42L84_DAC_CTL1,
						      CS42L84_DAC_CTL1_UNMUTE, 0);
		cs42l84->stream_use &= ~(1 << stream);
		if (!cs42l84->stream_use) {
			/* Must disconnect PLL before stopping it */
			snd_soc_component_write(component, CS42L84_CCM_CTL1,
						CS42L84_CCM_CTL1_RCO);

			usleep_range(150, 300);

			snd_soc_component_update_bits(component, CS42L84_PLL_CTL1,
							CS42L84_PLL_CTL1_EN, 0);

			snd_soc_component_update_bits(component, CS42L84_CCM_CTL4,
							CS42L84_CCM_CTL4_REFCLK_EN, 0);
		}
	} else {
		if (!cs42l84->stream_use) {
			/* SCLK must be running before codec unmute.
			 *
			 * Note carried over from CS42L42:
			 *
			 * PLL must not be started with ADC and HP both off
			 * otherwise the FILT+ supply will not charge properly.
			 * DAPM widgets power-up before stream unmute so at least
			 * one of the "DAC" or "ADC" widgets will already have
			 * powered-up.
			 */

			snd_soc_component_update_bits(component, CS42L84_CCM_CTL4,
						      CS42L84_CCM_CTL4_REFCLK_EN,
						      CS42L84_CCM_CTL4_REFCLK_EN);

			if (pll_ratio_table[cs42l84->pll_config].mclk_src_sel) {
				snd_soc_component_update_bits(component, CS42L84_PLL_CTL1,
							      CS42L84_PLL_CTL1_EN,
							      CS42L84_PLL_CTL1_EN);
				/* TODO: should we be doing something with divout here? */

				ret = regmap_read_poll_timeout(cs42l84->regmap,
							       CS42L84_PLL_LOCK_STATUS,
							       regval,
							       (regval & CS42L84_PLL_LOCK_STATUS_LOCKED),
							       CS42L84_PLL_LOCK_POLL_US,
							       CS42L84_PLL_LOCK_TIMEOUT_US);
				if (ret < 0)
					dev_warn(component->dev, "PLL failed to lock: %d\n", ret);

				if (regval & CS42L84_PLL_LOCK_STATUS_ERROR)
					dev_warn(component->dev, "PLL lock error\n");

				/* PLL must be running to drive glitchless switch logic */
				snd_soc_component_update_bits(component,
					CS42L84_CCM_CTL1,
					CS42L84_CCM_CTL1_MCLK_SRC | CS42L84_CCM_CTL1_MCLK_FREQ,
					FIELD_PREP(CS42L84_CCM_CTL1_MCLK_SRC, CS42L84_CCM_CTL1_MCLK_SRC_PLL)
					| FIELD_PREP(CS42L84_CCM_CTL1_MCLK_FREQ, cs42l84->pll_mclk_f));
				usleep_range(CS42L84_CLOCK_SWITCH_DELAY_US, CS42L84_CLOCK_SWITCH_DELAY_US*2);
			} else {
				snd_soc_component_update_bits(component,
					CS42L84_CCM_CTL1,
					CS42L84_CCM_CTL1_MCLK_SRC | CS42L84_CCM_CTL1_MCLK_FREQ,
					FIELD_PREP(CS42L84_CCM_CTL1_MCLK_SRC, CS42L84_CCM_CTL1_MCLK_SRC_BCLK)
					| FIELD_PREP(CS42L84_CCM_CTL1_MCLK_FREQ, cs42l84->pll_mclk_f));
				usleep_range(CS42L84_CLOCK_SWITCH_DELAY_US, CS42L84_CLOCK_SWITCH_DELAY_US*2);
			}
		}
		cs42l84->stream_use |= 1 << stream;

		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			/* Un-mute the headphone */
			snd_soc_component_update_bits(component, CS42L84_DAC_CTL1,
						      CS42L84_DAC_CTL1_UNMUTE,
						      CS42L84_DAC_CTL1_UNMUTE);
	}

	return 0;
}

static const struct snd_soc_dai_ops cs42l84_ops = {
	.hw_params	= cs42l84_pcm_hw_params,
	.set_fmt	= cs42l84_set_dai_fmt,
	.set_sysclk	= cs42l84_set_sysclk,
	.mute_stream	= cs42l84_mute_stream,
};

#define CS42L84_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver cs42l84_dai = {
		.name = "cs42l84",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
			.formats = CS42L84_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000,
			.formats = CS42L84_FORMATS,
		},
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
		.ops = &cs42l84_ops,
};

struct cs42l84_irq_params {
	u16 status_addr;
	u16 mask_addr;
	u8 mask;
};

static const struct cs42l84_irq_params irq_params_table[] = {
	{CS42L84_TSRS_PLUG_INT_STATUS, CS42L84_TSRS_PLUG_INT_MASK,
		CS42L84_TSRS_PLUG_VAL_MASK}
};

static void cs42l84_detect_hs(struct cs42l84_private *cs42l84)
{
	unsigned int reg;

	/* Power up HSBIAS */
	regmap_update_bits(cs42l84->regmap,
		CS42L84_MISC_DET_CTL,
		CS42L84_MISC_DET_CTL_HSBIAS_CTL | CS42L84_MISC_DET_CTL_DETECT_MODE,
		FIELD_PREP(CS42L84_MISC_DET_CTL_HSBIAS_CTL, 3) | /* 2.7 V */
		FIELD_PREP(CS42L84_MISC_DET_CTL_DETECT_MODE, 0));

	/* Power up level detection circuitry */
	regmap_update_bits(cs42l84->regmap,
		CS42L84_MISC_DET_CTL,
		CS42L84_MISC_DET_CTL_PDN_MIC_LVL_DET, 0);

	/* TODO: Optimize */
	msleep(50);

	/* Connect HSBIAS in CTIA wiring */
	/* TODO: Should likely be subject of detection */
	regmap_write(cs42l84->regmap,
		CS42L84_HS_SWITCH_CTL,
		CS42L84_HS_SWITCH_CTL_REF_HS3 | \
		CS42L84_HS_SWITCH_CTL_HSB_FILT_HS3 | \
		CS42L84_HS_SWITCH_CTL_GNDHS_HS3 | \
		CS42L84_HS_SWITCH_CTL_HSB_HS4);
	regmap_update_bits(cs42l84->regmap,
		CS42L84_HS_DET_CTL2,
		CS42L84_HS_DET_CTL2_SET,
		FIELD_PREP(CS42L84_HS_DET_CTL2_SET, 0));

	regmap_update_bits(cs42l84->regmap,
		CS42L84_MISC_DET_CTL,
		CS42L84_MISC_DET_CTL_DETECT_MODE,
		FIELD_PREP(CS42L84_MISC_DET_CTL_DETECT_MODE, 3));

	/* TODO: Optimize */
	msleep(50);

	regmap_read(cs42l84->regmap, CS42L84_HS_DET_STATUS2, &reg);
	regmap_update_bits(cs42l84->regmap,
		CS42L84_MISC_DET_CTL,
		CS42L84_MISC_DET_CTL_PDN_MIC_LVL_DET,
		CS42L84_MISC_DET_CTL_PDN_MIC_LVL_DET);

	switch (reg & 0b11) {
	case 0b11: /* shorted */
	case 0b00: /* open */
		/* Power down HSBIAS */
		regmap_update_bits(cs42l84->regmap,
			CS42L84_MISC_DET_CTL,
			CS42L84_MISC_DET_CTL_HSBIAS_CTL,
			FIELD_PREP(CS42L84_MISC_DET_CTL_HSBIAS_CTL, 1)); /* 0.0 V */
		break;
	}

	switch (reg & 0b11) {
	case 0b10: /* load */
		dev_dbg(cs42l84->dev, "Detected mic\n");
		cs42l84->hs_type = SND_JACK_HEADSET;
		snd_soc_jack_report(cs42l84->jack, SND_JACK_HEADSET,
				SND_JACK_HEADSET);
		break;

	case 0b00: /* open */
		dev_dbg(cs42l84->dev, "Detected open circuit on HS4\n");
		fallthrough;
	case 0b11: /* shorted */
	default:
		snd_soc_jack_report(cs42l84->jack, SND_JACK_HEADPHONE,
				SND_JACK_HEADSET);
		cs42l84->hs_type = SND_JACK_HEADPHONE;
		dev_dbg(cs42l84->dev, "Detected bare headphone (no mic)\n");
		break;
	}
}

static void cs42l84_revert_hs(struct cs42l84_private *cs42l84)
{
	/* Power down HSBIAS */
	regmap_update_bits(cs42l84->regmap,
		CS42L84_MISC_DET_CTL,
		CS42L84_MISC_DET_CTL_HSBIAS_CTL | CS42L84_MISC_DET_CTL_DETECT_MODE,
		FIELD_PREP(CS42L84_MISC_DET_CTL_HSBIAS_CTL, 1) | /* 0.0 V */
		FIELD_PREP(CS42L84_MISC_DET_CTL_DETECT_MODE, 0));

	/* Disconnect HSBIAS */
	regmap_write(cs42l84->regmap,
		CS42L84_HS_SWITCH_CTL,
		CS42L84_HS_SWITCH_CTL_REF_HS3 | \
		CS42L84_HS_SWITCH_CTL_REF_HS4 | \
		CS42L84_HS_SWITCH_CTL_HSB_FILT_HS3 | \
		CS42L84_HS_SWITCH_CTL_HSB_FILT_HS4 | \
		CS42L84_HS_SWITCH_CTL_GNDHS_HS3 | \
		CS42L84_HS_SWITCH_CTL_GNDHS_HS4);
	regmap_update_bits(cs42l84->regmap,
		CS42L84_HS_DET_CTL2,
		CS42L84_HS_DET_CTL2_SET,
		FIELD_PREP(CS42L84_HS_DET_CTL2_SET, 2));
}

static void cs42l84_set_interrupt_masks(struct cs42l84_private *cs42l84,
					unsigned int val)
{
	regmap_update_bits(cs42l84->regmap, CS42L84_TSRS_PLUG_INT_MASK,
			CS42L84_RS_PLUG | CS42L84_RS_UNPLUG |
			CS42L84_TS_PLUG | CS42L84_TS_UNPLUG,
			val);
}

static irqreturn_t cs42l84_irq_thread(int irq, void *data)
{
	struct cs42l84_private *cs42l84 = (struct cs42l84_private *)data;
	unsigned int stickies[1];
	unsigned int masks[1];
	unsigned int reg;
	u8 current_tip_state;
	u8 current_ring_state;
	int i;

	mutex_lock(&cs42l84->irq_lock);
	/* Read sticky registers to clear interrupt */
	for (i = 0; i < ARRAY_SIZE(stickies); i++) {
		regmap_read(cs42l84->regmap, irq_params_table[i].status_addr,
				&(stickies[i]));
		regmap_read(cs42l84->regmap, irq_params_table[i].mask_addr,
				&(masks[i]));
		stickies[i] = stickies[i] & (~masks[i]) &
				irq_params_table[i].mask;
	}

	/* When handling plug sene IRQs, we only care about EITHER tip OR ring.
	 * Ring is useless on remove, and is only useful on insert for
	 * detecting if the plug state has changed AFTER we have handled the
	 * tip sense IRQ, e.g. if the plug was not fully seated within the tip
	 * sense debounce time.
	 */

	if ((~masks[0]) & irq_params_table[0].mask) {
		regmap_read(cs42l84->regmap, CS42L84_TSRS_PLUG_STATUS, &reg);

		current_tip_state = (((char) reg) &
		      (CS42L84_TS_PLUG | CS42L84_TS_UNPLUG)) >>
		      CS42L84_TS_PLUG_SHIFT;

		if (current_tip_state != cs42l84->tip_state) {
			cs42l84->tip_state = current_tip_state;
			switch (current_tip_state) {
			case CS42L84_PLUG:
				dev_dbg(cs42l84->dev, "Plug event\n");

				cs42l84_detect_hs(cs42l84);

				/*
				 * Check the tip sense status again, and possibly invalidate
				 * the detection result
				 *
				 * Thanks to debounce, this should reliably indicate if the tip
				 * was disconnected at any point during the detection procedure.
				 */
				regmap_read(cs42l84->regmap, CS42L84_TSRS_PLUG_STATUS, &reg);
				current_tip_state = (((char) reg) &
				      (CS42L84_TS_PLUG | CS42L84_TS_UNPLUG)) >>
				      CS42L84_TS_PLUG_SHIFT;
				if (current_tip_state != CS42L84_PLUG) {
					dev_dbg(cs42l84->dev, "Wobbly connection, detection invalidated\n");
					cs42l84->tip_state = CS42L84_UNPLUG;
					cs42l84_revert_hs(cs42l84);
				}

				/* Unmask ring sense interrupts */
				cs42l84_set_interrupt_masks(cs42l84, 0);
				break;
			case CS42L84_UNPLUG:
				cs42l84->ring_state = CS42L84_UNPLUG;
				dev_dbg(cs42l84->dev, "Unplug event\n");

				cs42l84_revert_hs(cs42l84);
				cs42l84->hs_type = 0;
				snd_soc_jack_report(cs42l84->jack, 0,
						    SND_JACK_HEADSET);

				/* Mask ring sense interrupts */
				cs42l84_set_interrupt_masks(cs42l84,
							    CS42L84_RS_PLUG | CS42L84_RS_UNPLUG);
				break;
			default:
				cs42l84->ring_state = CS42L84_TRANS;
				break;
			}

			mutex_unlock(&cs42l84->irq_lock);

			return IRQ_HANDLED;
		}

		/* Tip state didn't change, we must've got a ring sense IRQ */
		current_ring_state = (((char) reg) &
		      (CS42L84_RS_PLUG | CS42L84_RS_UNPLUG)) >>
		      CS42L84_RS_PLUG_SHIFT;

		if (current_ring_state != cs42l84->ring_state) {
			cs42l84->ring_state = current_ring_state;
			if (current_ring_state == CS42L84_PLUG)
				cs42l84_detect_hs(cs42l84);
		}
	}

	mutex_unlock(&cs42l84->irq_lock);

	return IRQ_HANDLED;
}

static void cs42l84_setup_plug_detect(struct cs42l84_private *cs42l84)
{
	unsigned int reg;

	/* Set up plug detection */
	regmap_update_bits(cs42l84->regmap, CS42L84_MIC_DET_CTL4,
			CS42L84_MIC_DET_CTL4_LATCH_TO_VP,
			CS42L84_MIC_DET_CTL4_LATCH_TO_VP);
	regmap_update_bits(cs42l84->regmap, CS42L84_TIP_SENSE_CTL2,
			CS42L84_TIP_SENSE_CTL2_MODE,
			FIELD_PREP(CS42L84_TIP_SENSE_CTL2_MODE, CS42L84_TIP_SENSE_CTL2_MODE_SHORT_DET));
	regmap_update_bits(cs42l84->regmap, CS42L84_RING_SENSE_CTL,
			CS42L84_RING_SENSE_CTL_INV | CS42L84_RING_SENSE_CTL_UNK1 |
			CS42L84_RING_SENSE_CTL_RISETIME | CS42L84_RING_SENSE_CTL_FALLTIME,
			CS42L84_RING_SENSE_CTL_INV | CS42L84_RING_SENSE_CTL_UNK1 |
			FIELD_PREP(CS42L84_RING_SENSE_CTL_RISETIME, CS42L84_DEBOUNCE_TIME_125MS) |
			FIELD_PREP(CS42L84_RING_SENSE_CTL_FALLTIME, CS42L84_DEBOUNCE_TIME_125MS));
	regmap_update_bits(cs42l84->regmap, CS42L84_TIP_SENSE_CTL,
			CS42L84_TIP_SENSE_CTL_INV |
			CS42L84_TIP_SENSE_CTL_RISETIME | CS42L84_TIP_SENSE_CTL_FALLTIME,
			CS42L84_TIP_SENSE_CTL_INV |
			FIELD_PREP(CS42L84_TIP_SENSE_CTL_RISETIME, CS42L84_DEBOUNCE_TIME_500MS) |
			FIELD_PREP(CS42L84_TIP_SENSE_CTL_FALLTIME, CS42L84_DEBOUNCE_TIME_125MS));
	regmap_update_bits(cs42l84->regmap, CS42L84_MSM_BLOCK_EN3,
			CS42L84_MSM_BLOCK_EN3_TR_SENSE,
			CS42L84_MSM_BLOCK_EN3_TR_SENSE);

	/* Save the initial status of the tip sense */
	regmap_read(cs42l84->regmap, CS42L84_TSRS_PLUG_STATUS, &reg);
	cs42l84->tip_state = (((char) reg) &
		      (CS42L84_TS_PLUG | CS42L84_TS_UNPLUG)) >>
		      CS42L84_TS_PLUG_SHIFT;

	/* Set mic-detection threshold */
	regmap_update_bits(cs42l84->regmap,
		CS42L84_MIC_DET_CTL1, CS42L84_MIC_DET_CTL1_HS_DET_LEVEL,
		FIELD_PREP(CS42L84_MIC_DET_CTL1_HS_DET_LEVEL, 0x2c)); /* ~1.9 V */

	/* Disconnect HSBIAS (initially) */
	regmap_write(cs42l84->regmap,
		CS42L84_HS_SWITCH_CTL,
		CS42L84_HS_SWITCH_CTL_REF_HS3 | \
		CS42L84_HS_SWITCH_CTL_REF_HS4 | \
		CS42L84_HS_SWITCH_CTL_HSB_FILT_HS3 | \
		CS42L84_HS_SWITCH_CTL_HSB_FILT_HS4 | \
		CS42L84_HS_SWITCH_CTL_GNDHS_HS3 | \
		CS42L84_HS_SWITCH_CTL_GNDHS_HS4);
	regmap_update_bits(cs42l84->regmap,
		CS42L84_HS_DET_CTL2,
		CS42L84_HS_DET_CTL2_SET | CS42L84_HS_DET_CTL2_CTL,
		FIELD_PREP(CS42L84_HS_DET_CTL2_SET, 2) |
		FIELD_PREP(CS42L84_HS_DET_CTL2_CTL, 0));
	regmap_update_bits(cs42l84->regmap,
		CS42L84_HS_CLAMP_DISABLE, 1, 1);

}

static int cs42l84_i2c_probe(struct i2c_client *i2c_client)
{
	struct cs42l84_private *cs42l84;
	int ret, devid;
	unsigned int reg;

	cs42l84 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs42l84_private),
			       GFP_KERNEL);
	if (!cs42l84)
		return -ENOMEM;

	cs42l84->dev = &i2c_client->dev;
	i2c_set_clientdata(i2c_client, cs42l84);
	mutex_init(&cs42l84->irq_lock);

	cs42l84->regmap = devm_regmap_init_i2c(i2c_client, &cs42l84_regmap);
	if (IS_ERR(cs42l84->regmap)) {
		ret = PTR_ERR(cs42l84->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	/* Reset the Device */
	cs42l84->reset_gpio = devm_gpiod_get_optional(&i2c_client->dev,
		"reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs42l84->reset_gpio)) {
		ret = PTR_ERR(cs42l84->reset_gpio);
		goto err_disable_noreset;
	}

	if (cs42l84->reset_gpio) {
		dev_dbg(&i2c_client->dev, "Found reset GPIO\n");
		gpiod_set_value_cansleep(cs42l84->reset_gpio, 1);
	}
	usleep_range(CS42L84_BOOT_TIME_US, CS42L84_BOOT_TIME_US * 2);

	/* Request IRQ if one was specified */
	if (i2c_client->irq) {
		ret = request_threaded_irq(i2c_client->irq,
					   NULL, cs42l84_irq_thread,
					   IRQF_ONESHOT,
					   "cs42l84", cs42l84);
		if (ret == -EPROBE_DEFER) {
			goto err_disable_noirq;
		} else if (ret != 0) {
			dev_err(&i2c_client->dev,
				"Failed to request IRQ: %d\n", ret);
			goto err_disable_noirq;
		}
	}

	/* initialize codec */
	devid = cirrus_read_device_id(cs42l84->regmap, CS42L84_DEVID);
	if (devid < 0) {
		ret = devid;
		dev_err(&i2c_client->dev, "Failed to read device ID: %d\n", ret);
		goto err_disable;
	}

	if (devid != CS42L84_CHIP_ID) {
		dev_err(&i2c_client->dev,
			"CS42L84 Device ID (%X). Expected %X\n",
			devid, CS42L84_CHIP_ID);
		ret = -EINVAL;
		goto err_disable;
	}

	ret = regmap_read(cs42l84->regmap, CS42L84_REVID, &reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Get Revision ID failed\n");
		goto err_shutdown;
	}

	dev_info(&i2c_client->dev,
		 "Cirrus Logic CS42L84, Revision: %02X\n", reg & 0xFF);

	/* Setup plug detection */
	cs42l84_setup_plug_detect(cs42l84);

	/* Mask ring sense interrupts */
	cs42l84_set_interrupt_masks(cs42l84, CS42L84_RS_PLUG | CS42L84_RS_UNPLUG);

	/* Register codec for machine driver */
	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_dev_cs42l84, &cs42l84_dai, 1);
	if (ret < 0)
		goto err_shutdown;

	return 0;

err_shutdown:
	/* Nothing to do */

err_disable:
	if (i2c_client->irq)
		free_irq(i2c_client->irq, cs42l84);

err_disable_noirq:
	gpiod_set_value_cansleep(cs42l84->reset_gpio, 0);
err_disable_noreset:
	return ret;
}

static void cs42l84_i2c_remove(struct i2c_client *i2c_client)
{
	struct cs42l84_private *cs42l84 = i2c_get_clientdata(i2c_client);

	if (i2c_client->irq)
		free_irq(i2c_client->irq, cs42l84);

	gpiod_set_value_cansleep(cs42l84->reset_gpio, 0);
}

static const struct of_device_id cs42l84_of_match[] = {
	{ .compatible = "cirrus,cs42l84", },
	{}
};
MODULE_DEVICE_TABLE(of, cs42l84_of_match);

static const struct i2c_device_id cs42l84_id[] = {
	{"cs42l84", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs42l84_id);

static struct i2c_driver cs42l84_i2c_driver = {
	.driver = {
		.name = "cs42l84",
		.of_match_table = cs42l84_of_match,
	},
	.id_table = cs42l84_id,
	.probe = cs42l84_i2c_probe,
	.remove = cs42l84_i2c_remove,
};

module_i2c_driver(cs42l84_i2c_driver);

MODULE_DESCRIPTION("ASoC CS42L84 driver");
MODULE_AUTHOR("Martin Povišer <povik+lin@cutebit.org>");
MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_AUTHOR("James Calligeros <jcalligeros99@gmail.com>");
MODULE_LICENSE("GPL");
