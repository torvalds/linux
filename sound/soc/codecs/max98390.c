// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max98390.c  --  MAX98390 ALSA Soc Audio driver
 *
 * Copyright (C) 2020 Maxim Integrated Products
 *
 */

#include <linux/acpi.h>
#include <linux/cdev.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max98390.h"

static struct reg_default max98390_reg_defaults[] = {
	{MAX98390_INT_EN1, 0xf0},
	{MAX98390_INT_EN2, 0x00},
	{MAX98390_INT_EN3, 0x00},
	{MAX98390_INT_FLAG_CLR1, 0x00},
	{MAX98390_INT_FLAG_CLR2, 0x00},
	{MAX98390_INT_FLAG_CLR3, 0x00},
	{MAX98390_IRQ_CTRL, 0x01},
	{MAX98390_CLK_MON, 0x6d},
	{MAX98390_DAT_MON, 0x03},
	{MAX98390_WDOG_CTRL, 0x00},
	{MAX98390_WDOG_RST, 0x00},
	{MAX98390_MEAS_ADC_THERM_WARN_THRESH, 0x75},
	{MAX98390_MEAS_ADC_THERM_SHDN_THRESH, 0x8c},
	{MAX98390_MEAS_ADC_THERM_HYSTERESIS, 0x08},
	{MAX98390_PIN_CFG, 0x55},
	{MAX98390_PCM_RX_EN_A, 0x00},
	{MAX98390_PCM_RX_EN_B, 0x00},
	{MAX98390_PCM_TX_EN_A, 0x00},
	{MAX98390_PCM_TX_EN_B, 0x00},
	{MAX98390_PCM_TX_HIZ_CTRL_A, 0xff},
	{MAX98390_PCM_TX_HIZ_CTRL_B, 0xff},
	{MAX98390_PCM_CH_SRC_1, 0x00},
	{MAX98390_PCM_CH_SRC_2, 0x00},
	{MAX98390_PCM_CH_SRC_3, 0x00},
	{MAX98390_PCM_MODE_CFG, 0xc0},
	{MAX98390_PCM_MASTER_MODE, 0x1c},
	{MAX98390_PCM_CLK_SETUP, 0x44},
	{MAX98390_PCM_SR_SETUP, 0x08},
	{MAX98390_ICC_RX_EN_A, 0x00},
	{MAX98390_ICC_RX_EN_B, 0x00},
	{MAX98390_ICC_TX_EN_A, 0x00},
	{MAX98390_ICC_TX_EN_B, 0x00},
	{MAX98390_ICC_HIZ_MANUAL_MODE, 0x00},
	{MAX98390_ICC_TX_HIZ_EN_A, 0x00},
	{MAX98390_ICC_TX_HIZ_EN_B, 0x00},
	{MAX98390_ICC_LNK_EN, 0x00},
	{MAX98390_R2039_AMP_DSP_CFG, 0x0f},
	{MAX98390_R203A_AMP_EN, 0x81},
	{MAX98390_TONE_GEN_DC_CFG, 0x00},
	{MAX98390_SPK_SRC_SEL, 0x00},
	{MAX98390_SSM_CFG, 0x85},
	{MAX98390_MEAS_EN, 0x03},
	{MAX98390_MEAS_DSP_CFG, 0x0f},
	{MAX98390_BOOST_CTRL0, 0x1c},
	{MAX98390_BOOST_CTRL3, 0x01},
	{MAX98390_BOOST_CTRL1, 0x40},
	{MAX98390_MEAS_ADC_CFG, 0x07},
	{MAX98390_MEAS_ADC_BASE_MSB, 0x00},
	{MAX98390_MEAS_ADC_BASE_LSB, 0x23},
	{MAX98390_ADC_CH0_DIVIDE, 0x00},
	{MAX98390_ADC_CH1_DIVIDE, 0x00},
	{MAX98390_ADC_CH2_DIVIDE, 0x00},
	{MAX98390_ADC_CH0_FILT_CFG, 0x00},
	{MAX98390_ADC_CH1_FILT_CFG, 0x00},
	{MAX98390_ADC_CH2_FILT_CFG, 0x00},
	{MAX98390_PWR_GATE_CTL, 0x2c},
	{MAX98390_BROWNOUT_EN, 0x00},
	{MAX98390_BROWNOUT_INFINITE_HOLD, 0x00},
	{MAX98390_BROWNOUT_INFINITE_HOLD_CLR, 0x00},
	{MAX98390_BROWNOUT_LVL_HOLD, 0x00},
	{MAX98390_BROWNOUT_LVL1_THRESH, 0x00},
	{MAX98390_BROWNOUT_LVL2_THRESH, 0x00},
	{MAX98390_BROWNOUT_LVL3_THRESH, 0x00},
	{MAX98390_BROWNOUT_LVL4_THRESH, 0x00},
	{MAX98390_BROWNOUT_THRESH_HYSTERYSIS, 0x00},
	{MAX98390_BROWNOUT_AMP_LIMITER_ATK_REL, 0x1f},
	{MAX98390_BROWNOUT_AMP_GAIN_ATK_REL, 0x00},
	{MAX98390_BROWNOUT_AMP1_CLIP_MODE, 0x00},
	{MAX98390_BROWNOUT_LVL1_CUR_LIMIT, 0x00},
	{MAX98390_BROWNOUT_LVL1_AMP1_CTRL1, 0x00},
	{MAX98390_BROWNOUT_LVL1_AMP1_CTRL2, 0x00},
	{MAX98390_BROWNOUT_LVL1_AMP1_CTRL3, 0x00},
	{MAX98390_BROWNOUT_LVL2_CUR_LIMIT, 0x00},
	{MAX98390_BROWNOUT_LVL2_AMP1_CTRL1, 0x00},
	{MAX98390_BROWNOUT_LVL2_AMP1_CTRL2, 0x00},
	{MAX98390_BROWNOUT_LVL2_AMP1_CTRL3, 0x00},
	{MAX98390_BROWNOUT_LVL3_CUR_LIMIT, 0x00},
	{MAX98390_BROWNOUT_LVL3_AMP1_CTRL1, 0x00},
	{MAX98390_BROWNOUT_LVL3_AMP1_CTRL2, 0x00},
	{MAX98390_BROWNOUT_LVL3_AMP1_CTRL3, 0x00},
	{MAX98390_BROWNOUT_LVL4_CUR_LIMIT, 0x00},
	{MAX98390_BROWNOUT_LVL4_AMP1_CTRL1, 0x00},
	{MAX98390_BROWNOUT_LVL4_AMP1_CTRL2, 0x00},
	{MAX98390_BROWNOUT_LVL4_AMP1_CTRL3, 0x00},
	{MAX98390_BROWNOUT_ILIM_HLD, 0x00},
	{MAX98390_BROWNOUT_LIM_HLD, 0x00},
	{MAX98390_BROWNOUT_CLIP_HLD, 0x00},
	{MAX98390_BROWNOUT_GAIN_HLD, 0x00},
	{MAX98390_ENV_TRACK_VOUT_HEADROOM, 0x0f},
	{MAX98390_ENV_TRACK_BOOST_VOUT_DELAY, 0x80},
	{MAX98390_ENV_TRACK_REL_RATE, 0x07},
	{MAX98390_ENV_TRACK_HOLD_RATE, 0x07},
	{MAX98390_ENV_TRACK_CTRL, 0x01},
	{MAX98390_BOOST_BYPASS1, 0x49},
	{MAX98390_BOOST_BYPASS2, 0x2b},
	{MAX98390_BOOST_BYPASS3, 0x08},
	{MAX98390_FET_SCALING1, 0x00},
	{MAX98390_FET_SCALING2, 0x03},
	{MAX98390_FET_SCALING3, 0x00},
	{MAX98390_FET_SCALING4, 0x07},
	{MAX98390_SPK_SPEEDUP, 0x00},
	{DSMIG_WB_DRC_RELEASE_TIME_1, 0x00},
	{DSMIG_WB_DRC_RELEASE_TIME_2, 0x00},
	{DSMIG_WB_DRC_ATTACK_TIME_1, 0x00},
	{DSMIG_WB_DRC_ATTACK_TIME_2, 0x00},
	{DSMIG_WB_DRC_COMPRESSION_RATIO, 0x00},
	{DSMIG_WB_DRC_COMPRESSION_THRESHOLD, 0x00},
	{DSMIG_WB_DRC_MAKEUPGAIN, 0x00},
	{DSMIG_WB_DRC_NOISE_GATE_THRESHOLD, 0x00},
	{DSMIG_WBDRC_HPF_ENABLE, 0x00},
	{DSMIG_WB_DRC_TEST_SMOOTHER_OUT_EN, 0x00},
	{DSMIG_PPR_THRESHOLD, 0x00},
	{DSM_STEREO_BASS_CHANNEL_SELECT, 0x00},
	{DSM_TPROT_THRESHOLD_BYTE0, 0x00},
	{DSM_TPROT_THRESHOLD_BYTE1, 0x00},
	{DSM_TPROT_ROOM_TEMPERATURE_BYTE0, 0x00},
	{DSM_TPROT_ROOM_TEMPERATURE_BYTE1, 0x00},
	{DSM_TPROT_RECIP_RDC_ROOM_BYTE0, 0x00},
	{DSM_TPROT_RECIP_RDC_ROOM_BYTE1, 0x00},
	{DSM_TPROT_RECIP_RDC_ROOM_BYTE2, 0x00},
	{DSM_TPROT_RECIP_TCONST_BYTE0, 0x00},
	{DSM_TPROT_RECIP_TCONST_BYTE1, 0x00},
	{DSM_TPROT_RECIP_TCONST_BYTE2, 0x00},
	{DSM_THERMAL_ATTENUATION_SETTINGS, 0x00},
	{DSM_THERMAL_PILOT_TONE_ATTENUATION, 0x00},
	{DSM_TPROT_PG_TEMP_THRESH_BYTE0, 0x00},
	{DSM_TPROT_PG_TEMP_THRESH_BYTE1, 0x00},
	{DSMIG_DEBUZZER_THRESHOLD, 0x00},
	{DSMIG_DEBUZZER_ALPHA_COEF_TEST_ONLY, 0x08},
	{DSM_VOL_ENA, 0x20},
	{DSM_VOL_CTRL, 0xa0},
	{DSMIG_EN, 0x00},
	{MAX98390_R23E1_DSP_GLOBAL_EN, 0x00},
	{MAX98390_R23FF_GLOBAL_EN, 0x00},
};

static int max98390_dai_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);
	unsigned int mode;
	unsigned int format;
	unsigned int invert = 0;

	dev_dbg(component->dev, "%s: fmt 0x%08X\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		mode = MAX98390_PCM_MASTER_MODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		max98390->provider = true;
		mode = MAX98390_PCM_MASTER_MODE_MASTER;
		break;
	default:
		dev_err(component->dev, "DAI clock mode unsupported\n");
		return -EINVAL;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_MASTER_MODE,
		MAX98390_PCM_MASTER_MODE_MASK,
		mode);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		invert = MAX98390_PCM_MODE_CFG_PCM_BCLKEDGE;
		break;
	default:
		dev_err(component->dev, "DAI invert mode unsupported\n");
		return -EINVAL;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_MODE_CFG,
		MAX98390_PCM_MODE_CFG_PCM_BCLKEDGE,
		invert);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = MAX98390_PCM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = MAX98390_PCM_FORMAT_LJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		format = MAX98390_PCM_FORMAT_TDM_MODE1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = MAX98390_PCM_FORMAT_TDM_MODE0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_MODE_CFG,
		MAX98390_PCM_MODE_CFG_FORMAT_MASK,
		format << MAX98390_PCM_MODE_CFG_FORMAT_SHIFT);

	return 0;
}

static int max98390_get_bclk_sel(int bclk)
{
	int i;
	/* BCLKs per LRCLK */
	static int bclk_sel_table[] = {
		32, 48, 64, 96, 128, 192, 256, 320, 384, 512,
	};
	/* match BCLKs per LRCLK */
	for (i = 0; i < ARRAY_SIZE(bclk_sel_table); i++) {
		if (bclk_sel_table[i] == bclk)
			return i + 2;
	}
	return 0;
}

static int max98390_set_clock(struct snd_soc_component *component,
		struct snd_pcm_hw_params *params)
{
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);
	/* codec MCLK rate in master mode */
	static int rate_table[] = {
		5644800, 6000000, 6144000, 6500000,
		9600000, 11289600, 12000000, 12288000,
		13000000, 19200000,
	};
	/* BCLK/LRCLK ratio calculation */
	int blr_clk_ratio = params_channels(params)
		* snd_pcm_format_width(params_format(params));
	int value;

	if (max98390->provider) {
		int i;
		/* match rate to closest value */
		for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
			if (rate_table[i] >= max98390->sysclk)
				break;
		}
		if (i == ARRAY_SIZE(rate_table)) {
			dev_err(component->dev, "failed to find proper clock rate.\n");
			return -EINVAL;
		}

		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_MASTER_MODE,
			MAX98390_PCM_MASTER_MODE_MCLK_MASK,
			i << MAX98390_PCM_MASTER_MODE_MCLK_RATE_SHIFT);
	}

	if (!max98390->tdm_mode) {
		/* BCLK configuration */
		value = max98390_get_bclk_sel(blr_clk_ratio);
		if (!value) {
			dev_err(component->dev, "format unsupported %d\n",
				params_format(params));
			return -EINVAL;
		}

		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_CLK_SETUP,
			MAX98390_PCM_CLK_SETUP_BSEL_MASK,
			value);
	}
	return 0;
}

static int max98390_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_component *component =
		dai->component;
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	unsigned int sampling_rate;
	unsigned int chan_sz;

	/* pcm mode configuration */
	switch (snd_pcm_format_width(params_format(params))) {
	case 16:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			params_format(params));
		goto err;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_MODE_CFG,
		MAX98390_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	dev_dbg(component->dev, "format supported %d",
		params_format(params));

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 8000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_8000;
		break;
	case 11025:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_11025;
		break;
	case 12000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_12000;
		break;
	case 16000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_16000;
		break;
	case 22050:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_22050;
		break;
	case 24000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_24000;
		break;
	case 32000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_32000;
		break;
	case 44100:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_44100;
		break;
	case 48000:
		sampling_rate = MAX98390_PCM_SR_SET1_SR_48000;
		break;
	default:
		dev_err(component->dev, "rate %d not supported\n",
			params_rate(params));
		goto err;
	}

	/* set DAI_SR to correct LRCLK frequency */
	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_SR_SETUP,
		MAX98390_PCM_SR_SET1_SR_MASK,
		sampling_rate);

	return max98390_set_clock(component, params);
err:
	return -EINVAL;
}

static int max98390_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	int bsel;
	unsigned int chan_sz;

	if (!tx_mask && !rx_mask && !slots && !slot_width)
		max98390->tdm_mode = false;
	else
		max98390->tdm_mode = true;

	dev_dbg(component->dev,
		"Tdm mode : %d\n", max98390->tdm_mode);

	/* BCLK configuration */
	bsel = max98390_get_bclk_sel(slots * slot_width);
	if (!bsel) {
		dev_err(component->dev, "BCLK %d not supported\n",
			slots * slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_CLK_SETUP,
		MAX98390_PCM_CLK_SETUP_BSEL_MASK,
		bsel);

	/* Channel size configuration */
	switch (slot_width) {
	case 16:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_16;
		break;
	case 24:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_24;
		break;
	case 32:
		chan_sz = MAX98390_PCM_MODE_CFG_CHANSZ_32;
		break;
	default:
		dev_err(component->dev, "format unsupported %d\n",
			slot_width);
		return -EINVAL;
	}

	regmap_update_bits(max98390->regmap,
		MAX98390_PCM_MODE_CFG,
		MAX98390_PCM_MODE_CFG_CHANSZ_MASK, chan_sz);

	/* Rx slot configuration */
	regmap_write(max98390->regmap,
		MAX98390_PCM_RX_EN_A,
		rx_mask & 0xFF);
	regmap_write(max98390->regmap,
		MAX98390_PCM_RX_EN_B,
		(rx_mask & 0xFF00) >> 8);

	/* Tx slot Hi-Z configuration */
	regmap_write(max98390->regmap,
		MAX98390_PCM_TX_HIZ_CTRL_A,
		~tx_mask & 0xFF);
	regmap_write(max98390->regmap,
		MAX98390_PCM_TX_HIZ_CTRL_B,
		(~tx_mask & 0xFF00) >> 8);

	return 0;
}

static int max98390_dai_set_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	max98390->sysclk = freq;
	return 0;
}

static const struct snd_soc_dai_ops max98390_dai_ops = {
	.set_sysclk = max98390_dai_set_sysclk,
	.set_fmt = max98390_dai_set_fmt,
	.hw_params = max98390_dai_hw_params,
	.set_tdm_slot = max98390_dai_tdm_slot,
};

static int max98390_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(max98390->regmap,
			MAX98390_R203A_AMP_EN,
			MAX98390_AMP_EN_MASK, 1);
		regmap_update_bits(max98390->regmap,
			MAX98390_R23FF_GLOBAL_EN,
			MAX98390_GLOBAL_EN_MASK, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(max98390->regmap,
			MAX98390_R23FF_GLOBAL_EN,
			MAX98390_GLOBAL_EN_MASK, 0);
		regmap_update_bits(max98390->regmap,
			MAX98390_R203A_AMP_EN,
			MAX98390_AMP_EN_MASK, 0);
		break;
	}
	return 0;
}

static const char * const max98390_switch_text[] = {
	"Left", "Right", "LeftRight"};

static const char * const max98390_boost_voltage_text[] = {
	"6.5V", "6.625V", "6.75V", "6.875V", "7V", "7.125V", "7.25V", "7.375V",
	"7.5V", "7.625V", "7.75V", "7.875V", "8V", "8.125V", "8.25V", "8.375V",
	"8.5V", "8.625V", "8.75V", "8.875V", "9V", "9.125V", "9.25V", "9.375V",
	"9.5V", "9.625V", "9.75V", "9.875V", "10V"
};

static SOC_ENUM_SINGLE_DECL(max98390_boost_voltage,
		MAX98390_BOOST_CTRL0, 0,
		max98390_boost_voltage_text);

static DECLARE_TLV_DB_SCALE(max98390_spk_tlv, 300, 300, 0);
static DECLARE_TLV_DB_SCALE(max98390_digital_tlv, -8000, 50, 0);

static const char * const max98390_current_limit_text[] = {
	"0.00A", "0.50A", "1.00A", "1.05A", "1.10A", "1.15A", "1.20A", "1.25A",
	"1.30A", "1.35A", "1.40A", "1.45A", "1.50A", "1.55A", "1.60A", "1.65A",
	"1.70A", "1.75A", "1.80A", "1.85A", "1.90A", "1.95A", "2.00A", "2.05A",
	"2.10A", "2.15A", "2.20A", "2.25A", "2.30A", "2.35A", "2.40A", "2.45A",
	"2.50A", "2.55A", "2.60A", "2.65A", "2.70A", "2.75A", "2.80A", "2.85A",
	"2.90A", "2.95A", "3.00A", "3.05A", "3.10A", "3.15A", "3.20A", "3.25A",
	"3.30A", "3.35A", "3.40A", "3.45A", "3.50A", "3.55A", "3.60A", "3.65A",
	"3.70A", "3.75A", "3.80A", "3.85A", "3.90A", "3.95A", "4.00A", "4.05A",
	"4.10A"
};

static SOC_ENUM_SINGLE_DECL(max98390_current_limit,
		MAX98390_BOOST_CTRL1, 0,
		max98390_current_limit_text);

static int max98390_ref_rdc_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	max98390->ref_rdc_value = ucontrol->value.integer.value[0];

	regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE0,
		max98390->ref_rdc_value & 0x000000ff);
	regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE1,
		(max98390->ref_rdc_value >> 8) & 0x000000ff);
	regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE2,
		(max98390->ref_rdc_value >> 16) & 0x000000ff);

	return 0;
}

static int max98390_ref_rdc_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = max98390->ref_rdc_value;

	return 0;
}

static int max98390_ambient_temp_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	max98390->ambient_temp_value = ucontrol->value.integer.value[0];

	regmap_write(max98390->regmap, DSM_TPROT_ROOM_TEMPERATURE_BYTE1,
		(max98390->ambient_temp_value >> 8) & 0x000000ff);
	regmap_write(max98390->regmap, DSM_TPROT_ROOM_TEMPERATURE_BYTE0,
		(max98390->ambient_temp_value) & 0x000000ff);

	return 0;
}

static int max98390_ambient_temp_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = max98390->ambient_temp_value;

	return 0;
}

static int max98390_adaptive_rdc_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);

	dev_warn(component->dev, "Put adaptive rdc not supported\n");

	return 0;
}

static int max98390_adaptive_rdc_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int rdc, rdc0;
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	regmap_read(max98390->regmap, THERMAL_RDC_RD_BACK_BYTE1, &rdc);
	regmap_read(max98390->regmap, THERMAL_RDC_RD_BACK_BYTE0, &rdc0);
	ucontrol->value.integer.value[0] = rdc0 | rdc << 8;

	return 0;
}

static int max98390_dsm_calib_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	/* Do nothing */
	return 0;
}

static int max98390_dsm_calib_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct max98390_priv *max98390 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	unsigned int rdc, rdc_cal_result, rdc_integer, rdc_factor, temp, val;

	snd_soc_dapm_mutex_lock(dapm);

	regmap_read(max98390->regmap, MAX98390_R23FF_GLOBAL_EN, &val);
	if (!val) {
		/* Enable the codec for the duration of calibration readout */
		regmap_update_bits(max98390->regmap, MAX98390_R203A_AMP_EN,
				   MAX98390_AMP_EN_MASK, 1);
		regmap_update_bits(max98390->regmap, MAX98390_R23FF_GLOBAL_EN,
				   MAX98390_GLOBAL_EN_MASK, 1);
	}

	regmap_read(max98390->regmap, THERMAL_RDC_RD_BACK_BYTE1, &rdc);
	regmap_read(max98390->regmap, THERMAL_RDC_RD_BACK_BYTE0, &rdc_cal_result);
	regmap_read(max98390->regmap, MAX98390_MEAS_ADC_CH2_READ, &temp);

	if (!val) {
		/* Disable the codec if it was disabled */
		regmap_update_bits(max98390->regmap, MAX98390_R23FF_GLOBAL_EN,
				   MAX98390_GLOBAL_EN_MASK, 0);
		regmap_update_bits(max98390->regmap, MAX98390_R203A_AMP_EN,
				   MAX98390_AMP_EN_MASK, 0);
	}

	snd_soc_dapm_mutex_unlock(dapm);

	rdc_cal_result |= (rdc << 8) & 0x0000FFFF;
	if (rdc_cal_result)
		max98390->ref_rdc_value = 268435456U / rdc_cal_result;

	max98390->ambient_temp_value = temp * 52 - 1188;

	rdc_integer =  rdc_cal_result * 937  / 65536;
	rdc_factor = ((rdc_cal_result * 937 * 100) / 65536) - (rdc_integer * 100);

	dev_info(component->dev,
		 "rdc resistance about %d.%02d ohm, reg=0x%X temp reg=0x%X\n",
		 rdc_integer, rdc_factor, rdc_cal_result, temp);

	return 0;
}

static const struct snd_kcontrol_new max98390_snd_controls[] = {
	SOC_SINGLE_TLV("Digital Volume", DSM_VOL_CTRL,
		0, 184, 0,
		max98390_digital_tlv),
	SOC_SINGLE_TLV("Speaker Volume", MAX98390_R203D_SPK_GAIN,
		0, 6, 0,
		max98390_spk_tlv),
	SOC_SINGLE("Ramp Up Bypass Switch", MAX98390_R2039_AMP_DSP_CFG,
		MAX98390_AMP_DSP_CFG_RMP_UP_SHIFT, 1, 0),
	SOC_SINGLE("Ramp Down Bypass Switch", MAX98390_R2039_AMP_DSP_CFG,
		MAX98390_AMP_DSP_CFG_RMP_DN_SHIFT, 1, 0),
	SOC_SINGLE("Boost Clock Phase", MAX98390_BOOST_CTRL3,
		MAX98390_BOOST_CLK_PHASE_CFG_SHIFT, 3, 0),
	SOC_ENUM("Boost Output Voltage", max98390_boost_voltage),
	SOC_ENUM("Current Limit", max98390_current_limit),
	SOC_SINGLE_EXT("DSM Rdc", SND_SOC_NOPM, 0, 0xffffff, 0,
		max98390_ref_rdc_get, max98390_ref_rdc_put),
	SOC_SINGLE_EXT("DSM Ambient Temp", SND_SOC_NOPM, 0, 0xffff, 0,
		max98390_ambient_temp_get, max98390_ambient_temp_put),
	SOC_SINGLE_EXT("DSM Adaptive Rdc", SND_SOC_NOPM, 0, 0xffff, 0,
		max98390_adaptive_rdc_get, max98390_adaptive_rdc_put),
	SOC_SINGLE_EXT("DSM Calibration", SND_SOC_NOPM, 0, 1, 0,
		max98390_dsm_calib_get, max98390_dsm_calib_put),
};

static const struct soc_enum dai_sel_enum =
	SOC_ENUM_SINGLE(MAX98390_PCM_CH_SRC_1,
		MAX98390_PCM_RX_CH_SRC_SHIFT,
		3, max98390_switch_text);

static const struct snd_kcontrol_new max98390_dai_controls =
	SOC_DAPM_ENUM("DAI Sel", dai_sel_enum);

static const struct snd_soc_dapm_widget max98390_dapm_widgets[] = {
	SND_SOC_DAPM_DAC_E("Amp Enable", "HiFi Playback",
		SND_SOC_NOPM, 0, 0, max98390_dac_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("DAI Sel Mux", SND_SOC_NOPM, 0, 0,
		&max98390_dai_controls),
	SND_SOC_DAPM_OUTPUT("BE_OUT"),
};

static const struct snd_soc_dapm_route max98390_audio_map[] = {
	/* Plabyack */
	{"DAI Sel Mux", "Left", "Amp Enable"},
	{"DAI Sel Mux", "Right", "Amp Enable"},
	{"DAI Sel Mux", "LeftRight", "Amp Enable"},
	{"BE_OUT", NULL, "DAI Sel Mux"},
};

static bool max98390_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98390_SOFTWARE_RESET ... MAX98390_INT_EN3:
	case MAX98390_IRQ_CTRL ... MAX98390_WDOG_CTRL:
	case MAX98390_MEAS_ADC_THERM_WARN_THRESH
		... MAX98390_BROWNOUT_INFINITE_HOLD:
	case MAX98390_BROWNOUT_LVL_HOLD ... DSMIG_DEBUZZER_THRESHOLD:
	case DSM_VOL_ENA ... MAX98390_R24FF_REV_ID:
		return true;
	default:
		return false;
	}
};

static bool max98390_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98390_SOFTWARE_RESET ... MAX98390_INT_EN3:
	case MAX98390_MEAS_ADC_CH0_READ ... MAX98390_MEAS_ADC_CH2_READ:
	case MAX98390_PWR_GATE_STATUS ... MAX98390_BROWNOUT_STATUS:
	case MAX98390_BROWNOUT_LOWEST_STATUS:
	case MAX98390_ENV_TRACK_BOOST_VOUT_READ:
	case DSM_STBASS_HPF_B0_BYTE0 ... DSM_DEBUZZER_ATTACK_TIME_BYTE2:
	case THERMAL_RDC_RD_BACK_BYTE1 ... DSMIG_DEBUZZER_THRESHOLD:
	case DSM_THERMAL_GAIN ... DSM_WBDRC_GAIN:
		return true;
	default:
		return false;
	}
}

#define MAX98390_RATES SNDRV_PCM_RATE_8000_48000

#define MAX98390_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver max98390_dai[] = {
	{
		.name = "max98390-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98390_RATES,
			.formats = MAX98390_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98390_RATES,
			.formats = MAX98390_FORMATS,
		},
		.ops = &max98390_dai_ops,
	}
};

static int max98390_dsm_init(struct snd_soc_component *component)
{
	int ret;
	int param_size, param_start_addr;
	char filename[128];
	const char *vendor, *product;
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);
	const struct firmware *fw;
	char *dsm_param;

	vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	product = dmi_get_system_info(DMI_PRODUCT_NAME);

	if (!strcmp(max98390->dsm_param_name, "default")) {
		if (vendor && product) {
			snprintf(filename, sizeof(filename),
				"dsm_param_%s_%s.bin", vendor, product);
		} else {
			sprintf(filename, "dsm_param.bin");
		}
	} else {
		snprintf(filename, sizeof(filename), "%s",
			max98390->dsm_param_name);
	}
	ret = request_firmware(&fw, filename, component->dev);
	if (ret) {
		ret = request_firmware(&fw, "dsm_param.bin", component->dev);
		if (ret) {
			ret = request_firmware(&fw, "dsmparam.bin",
				component->dev);
			if (ret)
				goto err;
		}
	}

	dev_dbg(component->dev,
		"max98390: param fw size %zd\n",
		fw->size);
	if (fw->size < MAX98390_DSM_PARAM_MIN_SIZE) {
		dev_err(component->dev,
			"param fw is invalid.\n");
		ret = -EINVAL;
		goto err_alloc;
	}
	dsm_param = (char *)fw->data;
	param_start_addr = (dsm_param[0] & 0xff) | (dsm_param[1] & 0xff) << 8;
	param_size = (dsm_param[2] & 0xff) | (dsm_param[3] & 0xff) << 8;
	if (param_size > MAX98390_DSM_PARAM_MAX_SIZE ||
		param_start_addr < MAX98390_IRQ_CTRL ||
		fw->size < param_size + MAX98390_DSM_PAYLOAD_OFFSET) {
		dev_err(component->dev,
			"param fw is invalid.\n");
		ret = -EINVAL;
		goto err_alloc;
	}
	regmap_write(max98390->regmap, MAX98390_R203A_AMP_EN, 0x80);
	dsm_param += MAX98390_DSM_PAYLOAD_OFFSET;
	regmap_bulk_write(max98390->regmap, param_start_addr,
		dsm_param, param_size);
	regmap_write(max98390->regmap, MAX98390_R23E1_DSP_GLOBAL_EN, 0x01);

err_alloc:
	release_firmware(fw);
err:
	return ret;
}

static void max98390_init_regs(struct snd_soc_component *component)
{
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	regmap_write(max98390->regmap, MAX98390_CLK_MON, 0x6f);
	regmap_write(max98390->regmap, MAX98390_DAT_MON, 0x00);
	regmap_write(max98390->regmap, MAX98390_PWR_GATE_CTL, 0x00);
	regmap_write(max98390->regmap, MAX98390_PCM_RX_EN_A, 0x03);
	regmap_write(max98390->regmap, MAX98390_ENV_TRACK_VOUT_HEADROOM, 0x0e);
	regmap_write(max98390->regmap, MAX98390_BOOST_BYPASS1, 0x46);
	regmap_write(max98390->regmap, MAX98390_FET_SCALING3, 0x03);

	/* voltage, current slot configuration */
	regmap_write(max98390->regmap,
		MAX98390_PCM_CH_SRC_2,
		(max98390->i_l_slot << 4 |
		max98390->v_l_slot)&0xFF);

	if (max98390->v_l_slot < 8) {
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_HIZ_CTRL_A,
			1 << max98390->v_l_slot, 0);
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_EN_A,
			1 << max98390->v_l_slot,
			1 << max98390->v_l_slot);
	} else {
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_HIZ_CTRL_B,
			1 << (max98390->v_l_slot - 8), 0);
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_EN_B,
			1 << (max98390->v_l_slot - 8),
			1 << (max98390->v_l_slot - 8));
	}

	if (max98390->i_l_slot < 8) {
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_HIZ_CTRL_A,
			1 << max98390->i_l_slot, 0);
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_EN_A,
			1 << max98390->i_l_slot,
			1 << max98390->i_l_slot);
	} else {
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_HIZ_CTRL_B,
			1 << (max98390->i_l_slot - 8), 0);
		regmap_update_bits(max98390->regmap,
			MAX98390_PCM_TX_EN_B,
			1 << (max98390->i_l_slot - 8),
			1 << (max98390->i_l_slot - 8));
	}
}

static int max98390_probe(struct snd_soc_component *component)
{
	struct max98390_priv *max98390 =
		snd_soc_component_get_drvdata(component);

	regmap_write(max98390->regmap, MAX98390_SOFTWARE_RESET, 0x01);
	/* Sleep reset settle time */
	msleep(20);

	/* Amp init setting */
	max98390_init_regs(component);
	/* Update dsm bin param */
	max98390_dsm_init(component);

	/* Dsm Setting */
	if (max98390->ref_rdc_value) {
		regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE0,
			max98390->ref_rdc_value & 0x000000ff);
		regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE1,
			(max98390->ref_rdc_value >> 8) & 0x000000ff);
		regmap_write(max98390->regmap, DSM_TPROT_RECIP_RDC_ROOM_BYTE2,
			(max98390->ref_rdc_value >> 16) & 0x000000ff);
	}
	if (max98390->ambient_temp_value) {
		regmap_write(max98390->regmap, DSM_TPROT_ROOM_TEMPERATURE_BYTE1,
			(max98390->ambient_temp_value >> 8) & 0x000000ff);
		regmap_write(max98390->regmap, DSM_TPROT_ROOM_TEMPERATURE_BYTE0,
			(max98390->ambient_temp_value) & 0x000000ff);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max98390_suspend(struct device *dev)
{
	struct max98390_priv *max98390 = dev_get_drvdata(dev);

	dev_dbg(dev, "%s:Enter\n", __func__);

	regcache_cache_only(max98390->regmap, true);
	regcache_mark_dirty(max98390->regmap);

	return 0;
}

static int max98390_resume(struct device *dev)
{
	struct max98390_priv *max98390 = dev_get_drvdata(dev);

	dev_dbg(dev, "%s:Enter\n", __func__);

	regcache_cache_only(max98390->regmap, false);
	regcache_sync(max98390->regmap);

	return 0;
}
#endif

static const struct dev_pm_ops max98390_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(max98390_suspend, max98390_resume)
};

static const struct snd_soc_component_driver soc_codec_dev_max98390 = {
	.probe			= max98390_probe,
	.controls		= max98390_snd_controls,
	.num_controls		= ARRAY_SIZE(max98390_snd_controls),
	.dapm_widgets		= max98390_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98390_dapm_widgets),
	.dapm_routes		= max98390_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98390_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config max98390_regmap = {
	.reg_bits         = 16,
	.val_bits         = 8,
	.max_register     = MAX98390_R24FF_REV_ID,
	.reg_defaults     = max98390_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(max98390_reg_defaults),
	.readable_reg	  = max98390_readable_register,
	.volatile_reg	  = max98390_volatile_reg,
	.cache_type       = REGCACHE_RBTREE,
};

static void max98390_slot_config(struct i2c_client *i2c,
	struct max98390_priv *max98390)
{
	int value;
	struct device *dev = &i2c->dev;

	if (!device_property_read_u32(dev, "maxim,vmon-slot-no", &value))
		max98390->v_l_slot = value & 0xF;
	else
		max98390->v_l_slot = 0;

	if (!device_property_read_u32(dev, "maxim,imon-slot-no", &value))
		max98390->i_l_slot = value & 0xF;
	else
		max98390->i_l_slot = 1;
}

static int max98390_i2c_probe(struct i2c_client *i2c)
{
	int ret = 0;
	int reg = 0;

	struct max98390_priv *max98390 = NULL;
	struct i2c_adapter *adapter = i2c->adapter;
	struct gpio_desc *reset_gpio;

	ret = i2c_check_functionality(adapter,
		I2C_FUNC_SMBUS_BYTE
		| I2C_FUNC_SMBUS_BYTE_DATA);
	if (!ret) {
		dev_err(&i2c->dev, "I2C check functionality failed\n");
		return -ENXIO;
	}

	max98390 = devm_kzalloc(&i2c->dev, sizeof(*max98390), GFP_KERNEL);
	if (!max98390) {
		ret = -ENOMEM;
		return ret;
	}
	i2c_set_clientdata(i2c, max98390);

	ret = device_property_read_u32(&i2c->dev, "maxim,temperature_calib",
				       &max98390->ambient_temp_value);
	if (ret) {
		dev_info(&i2c->dev,
			 "no optional property 'temperature_calib' found, default:\n");
	}
	ret = device_property_read_u32(&i2c->dev, "maxim,r0_calib",
				       &max98390->ref_rdc_value);
	if (ret) {
		dev_info(&i2c->dev,
			 "no optional property 'r0_calib' found, default:\n");
	}

	dev_info(&i2c->dev,
		"%s: r0_calib: 0x%x,temperature_calib: 0x%x",
		__func__, max98390->ref_rdc_value,
		max98390->ambient_temp_value);

	ret = device_property_read_string(&i2c->dev, "maxim,dsm_param_name",
				       &max98390->dsm_param_name);
	if (ret)
		max98390->dsm_param_name = "default";

	/* voltage/current slot configuration */
	max98390_slot_config(i2c, max98390);

	/* regmap initialization */
	max98390->regmap = devm_regmap_init_i2c(i2c, &max98390_regmap);
	if (IS_ERR(max98390->regmap)) {
		ret = PTR_ERR(max98390->regmap);
		dev_err(&i2c->dev,
			"Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	reset_gpio = devm_gpiod_get_optional(&i2c->dev,
					     "reset", GPIOD_OUT_HIGH);

	/* Power on device */
	if (reset_gpio) {
		usleep_range(1000, 2000);
		/* bring out of reset */
		gpiod_set_value_cansleep(reset_gpio, 0);
		usleep_range(1000, 2000);
	}

	/* Check Revision ID */
	ret = regmap_read(max98390->regmap,
		MAX98390_R24FF_REV_ID, &reg);
	if (ret) {
		dev_err(&i2c->dev,
			"ret=%d, Failed to read: 0x%02X\n",
			ret, MAX98390_R24FF_REV_ID);
		return ret;
	}
	dev_info(&i2c->dev, "MAX98390 revisionID: 0x%02X\n", reg);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_max98390,
			max98390_dai, ARRAY_SIZE(max98390_dai));

	return ret;
}

static const struct i2c_device_id max98390_i2c_id[] = {
	{ "max98390", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, max98390_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id max98390_of_match[] = {
	{ .compatible = "maxim,max98390", },
	{}
};
MODULE_DEVICE_TABLE(of, max98390_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id max98390_acpi_match[] = {
	{ "MX98390", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, max98390_acpi_match);
#endif

static struct i2c_driver max98390_i2c_driver = {
	.driver = {
		.name = "max98390",
		.of_match_table = of_match_ptr(max98390_of_match),
		.acpi_match_table = ACPI_PTR(max98390_acpi_match),
		.pm = &max98390_pm,
	},
	.probe = max98390_i2c_probe,
	.id_table = max98390_i2c_id,
};

module_i2c_driver(max98390_i2c_driver)

MODULE_DESCRIPTION("ALSA SoC MAX98390 driver");
MODULE_AUTHOR("Steve Lee <steves.lee@maximintegrated.com>");
MODULE_LICENSE("GPL");
