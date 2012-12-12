/*
 * rt5631.c  --  RT5631 ALSA Soc Audio driver
 *
 * Copyright 2011 Realtek Microelectronics
 *
 * Author: flove <flove@realtek.com>
 *
 * Based on WM8753.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5631_phone.h"

static struct snd_soc_codec *rt5631_codec;

struct rt5631_init_reg {
	u8 reg;
	u16 val;
};

static struct rt5631_init_reg init_list[] = {
	{RT5631_SPK_OUT_VOL		, 0xc8c8},
	{RT5631_HP_OUT_VOL		, 0xc0c0},
	{RT5631_MONO_AXO_1_2_VOL	, 0xa080},
	{RT5631_ADC_REC_MIXER		, 0xb0b0},
	{RT5631_MIC_CTRL_2		, 0x5588},

	{RT5631_OUTMIXER_L_CTRL		, 0xdfC0},
	{RT5631_OUTMIXER_R_CTRL		, 0xdfC0},
	{RT5631_SPK_MIXER_CTRL		, 0xd8d8},
	{RT5631_SPK_MONO_OUT_CTRL	, 0x6c00},
	{RT5631_GEN_PUR_CTRL_REG	, 0x4e00},
	//{RT5631_SPK_MONO_HP_OUT_CTRL	, 0x0000},
	{RT5631_SPK_MONO_HP_OUT_CTRL	, 0x000c},//HP from DAC,speaker out from SpeakerOut Mixer
	{RT5631_INT_ST_IRQ_CTRL_2	, 0x0f18},
	{RT5631_MIC_CTRL_1		, 0x8080},
	{RT5631_INDEX_ADD		, 0x0048},
	{RT5631_INDEX_DATA		, 0xf73c},
};
#define RT5631_INIT_REG_LEN ARRAY_SIZE(init_list)

static int rt5631_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5631_INIT_REG_LEN; i++)
		snd_soc_write(codec, init_list[i].reg, init_list[i].val);

	return 0;
}

static int rt5631_index_sync(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5631_INIT_REG_LEN; i++)
		if (RT5631_INDEX_ADD == init_list[i].reg ||
			RT5631_INDEX_DATA == init_list[i].reg)
			snd_soc_write(codec, init_list[i].reg,
					init_list[i].val);

	return 0;
}

struct rt5631_priv {
	struct snd_soc_codec *codec;
	int codec_version;
	int master[RT5631_AIFS];
	int sysclk[RT5631_SCLKS];
	int rate;
	int rx_rate;
	int bclk_rate;
	int dmic_used_flag;
};

static const u16 rt5631_reg[RT5631_VENDOR_ID2 + 1] = {
	[RT5631_SPK_OUT_VOL] = 0x8888,
	[RT5631_HP_OUT_VOL] = 0x8080,
	[RT5631_MONO_AXO_1_2_VOL] = 0xa080,
	[RT5631_AUX_IN_VOL] = 0x0808,
	[RT5631_ADC_REC_MIXER] = 0xf0f0,
	[RT5631_VDAC_DIG_VOL] = 0x0010,
	[RT5631_OUTMIXER_L_CTRL] = 0xffc0,
	[RT5631_OUTMIXER_R_CTRL] = 0xffc0,
	[RT5631_AXO1MIXER_CTRL] = 0x88c0,
	[RT5631_AXO2MIXER_CTRL] = 0x88c0,
	[RT5631_DIG_MIC_CTRL] = 0x3000,
	[RT5631_MONO_INPUT_VOL] = 0x8808,
	[RT5631_SPK_MIXER_CTRL] = 0xf8f8,
	[RT5631_SPK_MONO_OUT_CTRL] = 0xfc00,
	[RT5631_SPK_MONO_HP_OUT_CTRL] = 0x4440,
	[RT5631_SDP_CTRL] = 0x8000,
	[RT5631_MONO_SDP_CTRL] = 0x8000,
	[RT5631_STEREO_AD_DA_CLK_CTRL] = 0x2010,
	[RT5631_GEN_PUR_CTRL_REG] = 0x0e00,
	[RT5631_INT_ST_IRQ_CTRL_2] = 0x0710,
	[RT5631_MISC_CTRL] = 0x2040,
	[RT5631_DEPOP_FUN_CTRL_2] = 0x8000,
	[RT5631_SOFT_VOL_CTRL] = 0x07e0,
	[RT5631_ALC_CTRL_1] = 0x0206,
	[RT5631_ALC_CTRL_3] = 0x2000,
	[RT5631_PSEUDO_SPATL_CTRL] = 0x0553,
};

void codec_set_spk(bool on)
{
	struct snd_soc_codec *codec = rt5631_codec;

	printk("%s: %d\n", __func__, on);
	
	if(!codec)
		return;
	
	mutex_lock(&codec->mutex);
	if(on){
		printk("snd_soc_dapm_enable_pin\n");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
	}
	else{
		printk("snd_soc_dapm_disable_pin\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
	}

	snd_soc_dapm_sync(&codec->dapm);
	mutex_unlock(&codec->mutex);
	return;
}

/**
 * rt5631_index_write - Write private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 * @value: Private register Data.
 *
 * Modify private register for advanced setting. It can be written through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5631_index_write(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int ret;

	ret = snd_soc_write(codec, RT5631_INDEX_ADD, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5631_INDEX_DATA, value);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private value: %d\n", ret);
		goto err;
	}
	return 0;

err:
	return ret;
}

/**
 * rt5631_index_read - Read private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 *
 * Read advanced setting from private register. It can be read through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns private register value or negative error code.
 */
static unsigned int rt5631_index_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;

	ret = snd_soc_write(codec, RT5631_INDEX_ADD, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		return ret;
	}
	return snd_soc_read(codec, RT5631_INDEX_DATA);
}

/**
 * rt5631_index_update_bits - update private register bits
 * @codec: audio codec
 * @reg: Private register index.
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
static int rt5631_index_update_bits(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int old, new;
	int change, ret;

	ret = rt5631_index_read(codec, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read private reg: %d\n", ret);
		goto err;
	}

	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		ret = rt5631_index_write(codec, reg, new);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to write private reg: %d\n", ret);
			goto err;
		}
	}
	return change;

err:
	return ret;
}

static int rt5631_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT5631_RESET, 0);
}

static int rt5631_volatile_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	switch (reg) {
	case RT5631_RESET:
	case RT5631_INT_ST_IRQ_CTRL_2:
	case RT5631_INDEX_ADD:
	case RT5631_INDEX_DATA:
	case RT5631_EQ_CTRL:
	case RT5631_VENDOR_ID:
	case RT5631_VENDOR_ID1:
	case RT5631_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

static int rt5631_readable_register(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	switch (reg) {
	case RT5631_RESET:
	case RT5631_SPK_OUT_VOL:
	case RT5631_HP_OUT_VOL:
	case RT5631_MONO_AXO_1_2_VOL:
	case RT5631_AUX_IN_VOL:
	case RT5631_STEREO_DAC_VOL_1:
	case RT5631_MIC_CTRL_1:
	case RT5631_STEREO_DAC_VOL_2:
	case RT5631_ADC_CTRL_1:
	case RT5631_ADC_REC_MIXER:
	case RT5631_ADC_CTRL_2:
	case RT5631_VDAC_DIG_VOL:
	case RT5631_OUTMIXER_L_CTRL:
	case RT5631_OUTMIXER_R_CTRL:
	case RT5631_AXO1MIXER_CTRL:
	case RT5631_AXO2MIXER_CTRL:
	case RT5631_MIC_CTRL_2:
	case RT5631_DIG_MIC_CTRL:
	case RT5631_MONO_INPUT_VOL:
	case RT5631_SPK_MIXER_CTRL:
	case RT5631_SPK_MONO_OUT_CTRL:
	case RT5631_SPK_MONO_HP_OUT_CTRL:
	case RT5631_SDP_CTRL:
	case RT5631_MONO_SDP_CTRL:
	case RT5631_STEREO_AD_DA_CLK_CTRL:
	case RT5631_PWR_MANAG_ADD1:
	case RT5631_PWR_MANAG_ADD2:
	case RT5631_PWR_MANAG_ADD3:
	case RT5631_PWR_MANAG_ADD4:
	case RT5631_GEN_PUR_CTRL_REG:
	case RT5631_GLOBAL_CLK_CTRL:
	case RT5631_PLL_CTRL:
	case RT5631_INT_ST_IRQ_CTRL_1:
	case RT5631_INT_ST_IRQ_CTRL_2:
	case RT5631_GPIO_CTRL:
	case RT5631_MISC_CTRL:
	case RT5631_DEPOP_FUN_CTRL_1:
	case RT5631_DEPOP_FUN_CTRL_2:
	case RT5631_JACK_DET_CTRL:
	case RT5631_SOFT_VOL_CTRL:
	case RT5631_ALC_CTRL_1:
	case RT5631_ALC_CTRL_2:
	case RT5631_ALC_CTRL_3:
	case RT5631_PSEUDO_SPATL_CTRL:
	case RT5631_INDEX_ADD:
	case RT5631_INDEX_DATA:
	case RT5631_EQ_CTRL:
	case RT5631_VENDOR_ID:
	case RT5631_VENDOR_ID1:
	case RT5631_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

/**
 * rt5631_headset_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
int rt5631_headset_detect(struct snd_soc_codec *codec, int jack_insert)
{
	int jack_type;

	if(jack_insert) {
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS1_VOL, RT5631_PWR_MICBIAS1_VOL);
		snd_soc_update_bits(codec, RT5631_MIC_CTRL_2,
			RT5631_MICBIAS1_S_C_DET_MASK |
			RT5631_MICBIAS1_SHORT_CURR_DET_MASK,
			RT5631_MICBIAS1_S_C_DET_ENA |
			RT5631_MICBIAS1_SHORT_CURR_DET_600UA);
		msleep(50);
		if (rt5631_index_read(codec, 0x4a) & 0x2)
			jack_type = RT5631_HEADPHO_DET;
		else
			jack_type = RT5631_HEADSET_DET;
	} else {
		snd_soc_update_bits(codec, RT5631_MIC_CTRL_2,
			RT5631_MICBIAS1_S_C_DET_MASK,
			RT5631_MICBIAS1_S_C_DET_DIS);
		jack_type = RT5631_NO_JACK;
	}

	return jack_type;
}
EXPORT_SYMBOL(rt5631_headset_detect);

int rt5631_headset_mic_detect(bool headset_status)
{
	struct rt5631_priv *rt5631 = NULL;
	int jack_type = 0;
	printk("%s::%d headset_status=%d\n",__FUNCTION__,__LINE__,headset_status);
	
	if(rt5631_codec == NULL)
		return -1;
	rt5631 = snd_soc_codec_get_drvdata(rt5631_codec);
	if(rt5631 == NULL)
		return -1;
	if(headset_status)
	{
		while(rt5631_codec->dapm.bias_level == SND_SOC_BIAS_OFF)
		{
			printk("----------rt5631 unnot standby-----------------\n");
			msleep(300);
		}
		snd_soc_update_bits(rt5631_codec, RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_VREF|RT5631_PWR_MAIN_BIAS ,
			RT5631_PWR_VREF|RT5631_PWR_MAIN_BIAS);
		msleep(10);
		snd_soc_update_bits(rt5631_codec, RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS2_VOL ,
			RT5631_PWR_MICBIAS2_VOL);
		msleep(400);
	}
	else
	{// headset is out,disable MIC2 Bias
		printk("headset is out,disable Mic2 Bias\n");
		snd_soc_update_bits(rt5631_codec, RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS2_VOL ,
			0);	
	}
	return jack_type;
}
EXPORT_SYMBOL(rt5631_headset_mic_detect);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -95625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static unsigned int mic_bst_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
};

static int rt5631_dmic_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt5631->dmic_used_flag;

	return 0;
}

static int rt5631_dmic_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);

	rt5631->dmic_used_flag = ucontrol->value.integer.value[0];
	return 0;
}

/* MIC Input Type */
static const char *rt5631_input_mode[] = {
	"Single ended", "Differential"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_mic1_mode_enum, RT5631_MIC_CTRL_1,
	RT5631_MIC1_DIFF_INPUT_SHIFT, rt5631_input_mode);

static const SOC_ENUM_SINGLE_DECL(
	rt5631_mic2_mode_enum, RT5631_MIC_CTRL_1,
	RT5631_MIC2_DIFF_INPUT_SHIFT, rt5631_input_mode);

/* MONO Input Type */
static const SOC_ENUM_SINGLE_DECL(
	rt5631_monoin_mode_enum, RT5631_MONO_INPUT_VOL,
	RT5631_MONO_DIFF_INPUT_SHIFT, rt5631_input_mode);

/* SPK Ratio Gain Control */
static const char *rt5631_spk_ratio[] = {"1.00x", "1.09x", "1.27x", "1.44x",
			"1.56x", "1.68x", "1.99x", "2.34x"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_spk_ratio_enum, RT5631_GEN_PUR_CTRL_REG,
	RT5631_SPK_AMP_RATIO_CTRL_SHIFT, rt5631_spk_ratio);

/* ADC Data Select Control */
static const char *rt5631_adc_data_sel[] = {"Stereo", "Left ADC", "Right ADC"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_adc_data_enum, RT5631_INT_ST_IRQ_CTRL_2,
	RT5631_ADC_DATA_SEL_SHIFT, rt5631_adc_data_sel);

/* ADCR Function Select Control */
static const char *rt5631_adcr_fun_sel[] = {"Stereo ADC", "Voice ADC"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_adcr_fun_enum, RT5631_GLOBAL_CLK_CTRL,
	RT5631_ADCR_FUN_SFT, rt5631_adcr_fun_sel);

/* VDAC Clock Select Control */
static const char *rt5631_vdac_clk_sel[] = {"SYSCLK1", "SYSCLK2"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_vdac_clk_enum, RT5631_GLOBAL_CLK_CTRL,
	RT5631_VDAC_CLK_SOUR_SFT, rt5631_vdac_clk_sel);

static const struct snd_kcontrol_new rt5631_snd_controls[] = {
	/* MIC */
	SOC_ENUM("MIC1 Mode Control",  rt5631_mic1_mode_enum),
	SOC_SINGLE_TLV("MIC1 Boost", RT5631_MIC_CTRL_2,
		RT5631_MIC1_BOOST_SHIFT, 8, 0, mic_bst_tlv),
	SOC_ENUM("MIC2 Mode Control", rt5631_mic2_mode_enum),
	SOC_SINGLE_TLV("MIC2 Boost", RT5631_MIC_CTRL_2,
		RT5631_MIC2_BOOST_SHIFT, 8, 0, mic_bst_tlv),
	/* MONO IN */
	SOC_ENUM("MONOIN Mode Control", rt5631_monoin_mode_enum),
	SOC_DOUBLE_TLV("MONOIN_RX Capture Volume", RT5631_MONO_INPUT_VOL,
			RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
			RT5631_VOL_MASK, 1, in_vol_tlv),
	/* AXI */
	SOC_DOUBLE_TLV("AXI Capture Volume", RT5631_AUX_IN_VOL,
			RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
			RT5631_VOL_MASK, 1, in_vol_tlv),
	/* ADC */
	SOC_DOUBLE_TLV("PCM Record Volume", RT5631_ADC_CTRL_2,
			RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
			RT5631_DAC_VOL_MASK, 1, dac_vol_tlv),
	SOC_DOUBLE("PCM Record Switch", RT5631_ADC_CTRL_1,
			RT5631_L_MUTE_SHIFT, RT5631_R_MUTE_SHIFT, 1, 1),
	/* DAC */
	SOC_DOUBLE_TLV("PCM Playback Volume", RT5631_STEREO_DAC_VOL_2,
			RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
			RT5631_DAC_VOL_MASK, 1, dac_vol_tlv),
	SOC_DOUBLE("PCM Playback Switch", RT5631_STEREO_DAC_VOL_1,
			RT5631_L_MUTE_SHIFT, RT5631_R_MUTE_SHIFT, 1, 1),
	/* AXO */
	SOC_SINGLE("AXO1 Playback Switch", RT5631_MONO_AXO_1_2_VOL,
				RT5631_L_MUTE_SHIFT, 1, 1),
	SOC_SINGLE("AXO2 Playback Switch", RT5631_MONO_AXO_1_2_VOL,
				RT5631_R_VOL_SHIFT, 1, 1),
	SOC_SINGLE("AXO Playback Switch", RT5631_MONO_AXO_1_2_VOL,
				RT5631_L_MUTE_SHIFT, 1, 1),
	/* OUTVOL */
	SOC_DOUBLE("OUTVOL Channel Switch", RT5631_SPK_OUT_VOL,
		RT5631_L_EN_SHIFT, RT5631_R_EN_SHIFT, 1, 0),

	/* SPK */
	SOC_DOUBLE("Speaker Playback Switch", RT5631_SPK_OUT_VOL,
		RT5631_L_MUTE_SHIFT, RT5631_R_MUTE_SHIFT, 1, 1),
	SOC_DOUBLE_TLV("Speaker Playback Volume", RT5631_SPK_OUT_VOL,
		RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT, 39, 1, out_vol_tlv),	
	/* MONO OUT */
	SOC_SINGLE("MONO Playback Switch", RT5631_MONO_AXO_1_2_VOL,
				RT5631_MUTE_MONO_SHIFT, 1, 1),
	/* HP */
	SOC_DOUBLE("HP Playback Switch", RT5631_HP_OUT_VOL,
		RT5631_L_MUTE_SHIFT, RT5631_R_MUTE_SHIFT, 1, 1),
	SOC_DOUBLE_TLV("HP Playback Volume", RT5631_HP_OUT_VOL,
		RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
		RT5631_VOL_MASK, 1, out_vol_tlv),
	//add	
	SOC_DOUBLE_TLV("Speaker Volume", RT5631_SPK_OUT_VOL,
		RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT, 39, 1, out_vol_tlv),		
	SOC_DOUBLE_TLV("Earpiece Volume", RT5631_SPK_OUT_VOL,
		RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT, 39, 1, out_vol_tlv),				
	SOC_DOUBLE_TLV("Headphone Volume", RT5631_HP_OUT_VOL,
		RT5631_L_VOL_SHIFT, RT5631_R_VOL_SHIFT,
		RT5631_VOL_MASK, 1, out_vol_tlv),
		
	/* DMIC */
	SOC_SINGLE_EXT("DMIC Switch", 0, 0, 1, 0,
		rt5631_dmic_get, rt5631_dmic_put),
	SOC_DOUBLE("DMIC Capture Switch", RT5631_DIG_MIC_CTRL,
		RT5631_DMIC_L_CH_MUTE_SHIFT,
		RT5631_DMIC_R_CH_MUTE_SHIFT, 1, 1),
	/* SPK Ratio Gain Control */
	SOC_ENUM("SPK Ratio Control", rt5631_spk_ratio_enum),
	/* ADC Data Select Control */
	SOC_ENUM("ADC Data Select", rt5631_adc_data_enum),
	/* ADCR Function Select Control */
	SOC_ENUM("ADCR Function Select", rt5631_adcr_fun_enum),
	/* VDAC Function Select Control */
	SOC_ENUM("Voice DAC Clock Select", rt5631_vdac_clk_enum),
};

static int check_sysclk1_source(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg;

	reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);
	return (reg & RT5631_SYSCLK_SOUR_SEL_PLL);
}

static int check_adcr_pll1(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);

	if (reg & RT5631_ADCR_FUN_VADC) {
		if (reg & RT5631_VDAC_CLK_SOUR_SCLK2)
			return 0;
		else
			return (reg & RT5631_SYSCLK_SOUR_SEL_PLL);
	} else
		return (reg & RT5631_SYSCLK_SOUR_SEL_PLL);
}

static int check_adcr_stereo(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);
	return (RT5631_ADCR_FUN_ADC == (reg & RT5631_ADCR_FUN_MASK));
}

static int check_adcr_voice(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);
	return (RT5631_ADCR_FUN_VADC == (reg & RT5631_ADCR_FUN_MASK));
}

static int check_adcr_pll2(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);

	if (reg & RT5631_ADCR_FUN_VADC) {
		if (reg & RT5631_VDAC_CLK_SOUR_SCLK2)
			return (reg & RT5631_SYSCLK2_SOUR_SEL_PLL2);
		else
			return 0;
	} else
		return 0;
}

static int check_vdac_pll1(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);

	if (reg & RT5631_VDAC_CLK_SOUR_SCLK2)
		return 0;
	else
		return (reg & RT5631_SYSCLK_SOUR_SEL_PLL);
}

static int check_vdac_pll2(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg = snd_soc_read(source->codec, RT5631_GLOBAL_CLK_CTRL);

	if (reg & RT5631_VDAC_CLK_SOUR_SCLK2)
		return (reg & RT5631_SYSCLK2_SOUR_SEL_PLL2);
	else
		return 0;
}

static int check_dmic_used(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(source->codec);
	return rt5631->dmic_used_flag;
}

static int check_dacl_to_outmixl(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg;

	reg = snd_soc_read(source->codec, RT5631_OUTMIXER_L_CTRL);
	return !(reg & RT5631_M_DAC_L_TO_OUTMIXER_L);
}

static int check_dacr_to_outmixr(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg;

	reg = snd_soc_read(source->codec, RT5631_OUTMIXER_R_CTRL);
	return !(reg & RT5631_M_DAC_R_TO_OUTMIXER_R);
}

static int check_dacl_to_spkmixl(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg;

	reg = snd_soc_read(source->codec, RT5631_SPK_MIXER_CTRL);
	return !(reg & RT5631_M_DAC_L_TO_SPKMIXER_L);
}

static int check_dacr_to_spkmixr(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg;

	reg = snd_soc_read(source->codec, RT5631_SPK_MIXER_CTRL);
	return !(reg & RT5631_M_DAC_R_TO_SPKMIXER_R);
}

static int check_vdac_to_outmix(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, ret = 1;

	reg = snd_soc_read(source->codec, RT5631_OUTMIXER_L_CTRL);
	if (reg & RT5631_M_VDAC_TO_OUTMIXER_L) {
		reg = snd_soc_read(source->codec, RT5631_OUTMIXER_R_CTRL);
		if (reg & RT5631_M_VDAC_TO_OUTMIXER_R)
			ret = 0;
	}
	return ret;
}

static int check_adcl_select(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, ret = 0;

	reg = snd_soc_read(source->codec, RT5631_ADC_REC_MIXER);
	if (reg & RT5631_M_MIC2_TO_RECMIXER_R)
		if (!(reg & RT5631_M_MIC1_TO_RECMIXER_L))
			ret = 1;
	return ret;
}

static int check_adcr_select(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, ret = 0;

	reg = snd_soc_read(source->codec, RT5631_ADC_REC_MIXER);
	if (reg & RT5631_M_MIC1_TO_RECMIXER_L)
		if (!(reg & RT5631_M_MIC2_TO_RECMIXER_R))
			ret = 1;
	return ret;
}

/**
 * onebit_depop_power_stage - auto depop in power stage.
 * @enable: power on/off
 *
 * When power on/off headphone, the depop sequence is done by hardware.
 */
static void onebit_depop_power_stage(struct snd_soc_codec *codec, int enable)
{
	unsigned int soft_vol, hp_zc;

	/* enable one-bit depop function */
	snd_soc_update_bits(codec, RT5631_DEPOP_FUN_CTRL_2,
				RT5631_EN_ONE_BIT_DEPOP, 0);

	/* keep soft volume and zero crossing setting */
	soft_vol = snd_soc_read(codec, RT5631_SOFT_VOL_CTRL);
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, 0);
	hp_zc = snd_soc_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc & 0xf7ff);
	if (enable) {
		/* config one-bit depop parameter */
		rt5631_index_write(codec, RT5631_TEST_MODE_CTRL, 0x84c0);
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x309f);
		rt5631_index_write(codec, RT5631_CP_INTL_REG2, 0x6530);
		/* power on capless block */
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_2,
				RT5631_EN_CAP_FREE_DEPOP);
	} else {
		/* power off capless block */
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_2, 0);
		msleep(100);
	}

	/* recover soft volume and zero crossing setting */
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, soft_vol);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc);
}

/**
 * onebit_depop_mute_stage - auto depop in mute stage.
 * @enable: mute/unmute
 *
 * When mute/unmute headphone, the depop sequence is done by hardware.
 */
static void onebit_depop_mute_stage(struct snd_soc_codec *codec, int enable)
{
	unsigned int soft_vol, hp_zc;

	/* enable one-bit depop function */
	snd_soc_update_bits(codec, RT5631_DEPOP_FUN_CTRL_2,
				RT5631_EN_ONE_BIT_DEPOP, 0);

	/* keep soft volume and zero crossing setting */
	soft_vol = snd_soc_read(codec, RT5631_SOFT_VOL_CTRL);
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, 0);
	hp_zc = snd_soc_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc & 0xf7ff);
	if (enable) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(10));
		/* config one-bit depop parameter */
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x307f);
		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
				RT5631_L_MUTE | RT5631_R_MUTE, 0);
		msleep(300);
	} else {
		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,
			RT5631_L_MUTE | RT5631_R_MUTE);
		msleep(100);
	}

	/* recover soft volume and zero crossing setting */
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, soft_vol);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc);
}

/**
 * onebit_depop_power_stage - step by step depop sequence in power stage.
 * @enable: power on/off
 *
 * When power on/off headphone, the depop sequence is done in step by step.
 */
static void depop_seq_power_stage(struct snd_soc_codec *codec, int enable)
{
	unsigned int soft_vol, hp_zc;

	/* depop control by register */
	snd_soc_update_bits(codec, RT5631_DEPOP_FUN_CTRL_2,
		RT5631_EN_ONE_BIT_DEPOP, RT5631_EN_ONE_BIT_DEPOP);

	/* keep soft volume and zero crossing setting */
	soft_vol = snd_soc_read(codec, RT5631_SOFT_VOL_CTRL);
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, 0);
	hp_zc = snd_soc_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc & 0xf7ff);
	if (enable) {
		/* config depop sequence parameter */
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x303e);

		/* power on headphone and charge pump */
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_CHARGE_PUMP | RT5631_PWR_HP_L_AMP |
			RT5631_PWR_HP_R_AMP,
			RT5631_PWR_CHARGE_PUMP | RT5631_PWR_HP_L_AMP |
			RT5631_PWR_HP_R_AMP);

		/* power on soft generator and depop mode2 */
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_EN_DEPOP2_FOR_HP);
		msleep(100);

		/* stop depop mode */
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_HP_DEPOP_DIS, RT5631_PWR_HP_DEPOP_DIS);
	} else {
		/* config depop sequence parameter */
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x303F);
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_EN_MUTE_UNMUTE_DEPOP |
			RT5631_PD_HPAMP_L_ST_UP | RT5631_PD_HPAMP_R_ST_UP);
		msleep(75);
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_PD_HPAMP_L_ST_UP |
			RT5631_PD_HPAMP_R_ST_UP);

		/* start depop mode */
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
				RT5631_PWR_HP_DEPOP_DIS, 0);

		/* config depop sequence parameter */
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_EN_DEPOP2_FOR_HP |
			RT5631_PD_HPAMP_L_ST_UP | RT5631_PD_HPAMP_R_ST_UP);
		msleep(80);
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN);

		/* power down headphone and charge pump */
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_CHARGE_PUMP | RT5631_PWR_HP_L_AMP |
			RT5631_PWR_HP_R_AMP, 0);
	}

	/* recover soft volume and zero crossing setting */
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, soft_vol);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc);
}

/**
 * depop_seq_mute_stage - step by step depop sequence in mute stage.
 * @enable: mute/unmute
 *
 * When mute/unmute headphone, the depop sequence is done in step by step.
 */
static void depop_seq_mute_stage(struct snd_soc_codec *codec, int enable)
{
	unsigned int soft_vol, hp_zc;

	/* depop control by register */
	snd_soc_update_bits(codec, RT5631_DEPOP_FUN_CTRL_2,
		RT5631_EN_ONE_BIT_DEPOP, RT5631_EN_ONE_BIT_DEPOP);

	/* keep soft volume and zero crossing setting */
	soft_vol = snd_soc_read(codec, RT5631_SOFT_VOL_CTRL);
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, 0);
	hp_zc = snd_soc_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc & 0xf7ff);
	if (enable) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(10));

		/* config depop sequence parameter */
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x302f);
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_EN_MUTE_UNMUTE_DEPOP |
			RT5631_EN_HP_R_M_UN_MUTE_DEPOP |
			RT5631_EN_HP_L_M_UN_MUTE_DEPOP);

		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
				RT5631_L_MUTE | RT5631_R_MUTE, 0);
		msleep(160);
	} else {
		/* config depop sequence parameter */
		rt5631_index_write(codec, RT5631_SPK_INTL_CTRL, 0x302f);
		snd_soc_write(codec, RT5631_DEPOP_FUN_CTRL_1,
			RT5631_POW_ON_SOFT_GEN | RT5631_EN_MUTE_UNMUTE_DEPOP |
			RT5631_EN_HP_R_M_UN_MUTE_DEPOP |
			RT5631_EN_HP_L_M_UN_MUTE_DEPOP);

		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,
			RT5631_L_MUTE | RT5631_R_MUTE);
		msleep(150);
	}

	/* recover soft volume and zero crossing setting */
	snd_soc_write(codec, RT5631_SOFT_VOL_CTRL, soft_vol);
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, hp_zc);
}

static int hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
	//	printk("hp_event  before widget power down\n");
		if (rt5631->codec_version) {
			onebit_depop_mute_stage(codec, 0);
			onebit_depop_power_stage(codec, 0);
		} else {
			depop_seq_mute_stage(codec, 0);
			depop_seq_power_stage(codec, 0);
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		if (rt5631->codec_version) {
			onebit_depop_power_stage(codec, 1);
			onebit_depop_mute_stage(codec, 1);
		} else {
			depop_seq_power_stage(codec, 1);
			depop_seq_mute_stage(codec, 1);
		}
		break;

	default:
		break;
	}

	return 0;
}

static int spk_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	printk("Enter %s::%s---%d\n",__FILE__,__FUNCTION__,__LINE__);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		
		break;

	case SND_SOC_DAPM_POST_PMD:

		break;
	default:
		return 0;
	}
	return 0;
}

static int auxo_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *control, int event)
{
//	struct snd_soc_codec *codec = w->codec;
//	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);
	printk("Enter %s::%s---%d\n",__FILE__,__FUNCTION__,__LINE__);
#ifdef CONFIG_PHONE_INCALL_IS_SUSPEND	

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		printk("rt5631v is incall status\n");
		snd_soc_incall_status(1,1);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		printk("rt5631v exit incall status\n");
		snd_soc_incall_status(1,0);
		break;

	default:
		BUG();
		break;
	}
#endif
	return 0;
}

static int set_dmic_params(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);

	switch (rt5631->rx_rate) {
	case 44100:
	case 48000:
		snd_soc_update_bits(codec, RT5631_DIG_MIC_CTRL,
			RT5631_DMIC_CLK_CTRL_MASK,
			RT5631_DMIC_CLK_CTRL_TO_32FS);
		break;

	case 32000:
	case 22050:
		snd_soc_update_bits(codec, RT5631_DIG_MIC_CTRL,
			RT5631_DMIC_CLK_CTRL_MASK,
			RT5631_DMIC_CLK_CTRL_TO_64FS);
		break;

	case 16000:
	case 11025:
	case 8000:
		snd_soc_update_bits(codec, RT5631_DIG_MIC_CTRL,
			RT5631_DMIC_CLK_CTRL_MASK,
			RT5631_DMIC_CLK_CTRL_TO_128FS);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new rt5631_recmixl_mixer_controls[] = {
	SOC_DAPM_SINGLE("OUTMIXL Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_OUTMIXL_RECMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC1_BST1 Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_MIC1_RECMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXILVOL Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_AXIL_RECMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MONOIN_RX Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_MONO_IN_RECMIXL_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_recmixr_mixer_controls[] = {
	SOC_DAPM_SINGLE("MONOIN_RX Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_MONO_IN_RECMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXIRVOL Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_AXIR_RECMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC2_BST2 Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_MIC2_RECMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIXR Capture Switch", RT5631_ADC_REC_MIXER,
			RT5631_M_OUTMIXR_RECMIXR_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_spkmixl_mixer_controls[] = {
	SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_RECMIXL_SPKMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC1_P Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_MIC1P_SPKMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("DACL Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_DACL_SPKMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIXL Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_OUTMIXL_SPKMIXL_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_spkmixr_mixer_controls[] = {
	SOC_DAPM_SINGLE("OUTMIXR Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_OUTMIXR_SPKMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("DACR Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_DACR_SPKMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC2_P Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_MIC2P_SPKMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_SPK_MIXER_CTRL,
			RT5631_M_RECMIXR_SPKMIXR_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_outmixl_mixer_controls[] = {
	SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_RECMIXL_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_RECMIXR_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("DACL Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_DACL_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_MIC1_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_MIC2_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("MONOIN_RXP Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_MONO_INP_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXILVOL Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_AXIL_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXIRVOL Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_AXIR_OUTMIXL_BIT, 1, 1),
	SOC_DAPM_SINGLE("VDAC Playback Switch", RT5631_OUTMIXER_L_CTRL,
				RT5631_M_VDAC_OUTMIXL_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_outmixr_mixer_controls[] = {
	SOC_DAPM_SINGLE("VDAC Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_VDAC_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXIRVOL Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_AXIR_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("AXILVOL Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_AXIL_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("MONOIN_RXN Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_MONO_INN_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_MIC2_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_MIC1_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("DACR Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_DACR_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_RECMIXR_OUTMIXR_BIT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_OUTMIXER_R_CTRL,
				RT5631_M_RECMIXL_OUTMIXR_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_AXO1MIX_mixer_controls[] = {
	SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_AXO1MIXER_CTRL,
				RT5631_M_MIC1_AXO1MIX_BIT , 1, 1),
	SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_AXO1MIXER_CTRL,
				RT5631_M_MIC2_AXO1MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_AXO1MIXER_CTRL,
				RT5631_M_OUTMIXL_AXO1MIX_BIT , 1 , 1),
	SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_AXO1MIXER_CTRL,
				RT5631_M_OUTMIXR_AXO1MIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_AXO2MIX_mixer_controls[] = {
	SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_AXO2MIXER_CTRL,
				RT5631_M_MIC1_AXO2MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_AXO2MIXER_CTRL,
				RT5631_M_MIC2_AXO2MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_AXO2MIXER_CTRL,
				RT5631_M_OUTMIXL_AXO2MIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_AXO2MIXER_CTRL,
				RT5631_M_OUTMIXR_AXO2MIX_BIT, 1 , 1),
};

static const struct snd_kcontrol_new rt5631_spolmix_mixer_controls[] = {
	SOC_DAPM_SINGLE("SPKVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_SPKVOLL_SPOLMIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_SPKVOLR_SPOLMIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_spormix_mixer_controls[] = {
	SOC_DAPM_SINGLE("SPKVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_SPKVOLL_SPORMIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_SPKVOLR_SPORMIX_BIT, 1, 1),
};

static const struct snd_kcontrol_new rt5631_monomix_mixer_controls[] = {
	SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_OUTVOLL_MONOMIX_BIT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL,
				RT5631_M_OUTVOLR_MONOMIX_BIT, 1, 1),
};

/* Left SPK Volume Input */
static const char *rt5631_spkvoll_sel[] = {"Vmid", "SPKMIXL"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_spkvoll_enum, RT5631_SPK_OUT_VOL,
	RT5631_L_EN_SHIFT, rt5631_spkvoll_sel);

static const struct snd_kcontrol_new rt5631_spkvoll_mux_control =
	SOC_DAPM_ENUM("Left SPKVOL SRC", rt5631_spkvoll_enum);

/* Left HP Volume Input */
static const char *rt5631_hpvoll_sel[] = {"Vmid", "OUTMIXL"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_hpvoll_enum, RT5631_HP_OUT_VOL,
	RT5631_L_EN_SHIFT, rt5631_hpvoll_sel);

static const struct snd_kcontrol_new rt5631_hpvoll_mux_control =
	SOC_DAPM_ENUM("Left HPVOL SRC", rt5631_hpvoll_enum);

/* Left Out Volume Input */
static const char *rt5631_outvoll_sel[] = {"Vmid", "OUTMIXL"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_outvoll_enum, RT5631_MONO_AXO_1_2_VOL,
	RT5631_L_EN_SHIFT, rt5631_outvoll_sel);

static const struct snd_kcontrol_new rt5631_outvoll_mux_control =
	SOC_DAPM_ENUM("Left OUTVOL SRC", rt5631_outvoll_enum);

/* Right Out Volume Input */
static const char *rt5631_outvolr_sel[] = {"Vmid", "OUTMIXR"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_outvolr_enum, RT5631_MONO_AXO_1_2_VOL,
	RT5631_R_EN_SHIFT, rt5631_outvolr_sel);

static const struct snd_kcontrol_new rt5631_outvolr_mux_control =
	SOC_DAPM_ENUM("Right OUTVOL SRC", rt5631_outvolr_enum);

/* Right HP Volume Input */
static const char *rt5631_hpvolr_sel[] = {"Vmid", "OUTMIXR"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_hpvolr_enum, RT5631_HP_OUT_VOL,
	RT5631_R_EN_SHIFT, rt5631_hpvolr_sel);

static const struct snd_kcontrol_new rt5631_hpvolr_mux_control =
	SOC_DAPM_ENUM("Right HPVOL SRC", rt5631_hpvolr_enum);

/* Right SPK Volume Input */
static const char *rt5631_spkvolr_sel[] = {"Vmid", "SPKMIXR"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_spkvolr_enum, RT5631_SPK_OUT_VOL,
	RT5631_R_EN_SHIFT, rt5631_spkvolr_sel);

static const struct snd_kcontrol_new rt5631_spkvolr_mux_control =
	SOC_DAPM_ENUM("Right SPKVOL SRC", rt5631_spkvolr_enum);

/* SPO Left Channel Input */
static const char *rt5631_spol_src_sel[] = {
	"SPOLMIX", "MONOIN_RX", "VDAC", "DACL"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_spol_src_enum, RT5631_SPK_MONO_HP_OUT_CTRL,
	RT5631_SPK_L_MUX_SEL_SHIFT, rt5631_spol_src_sel);

static const struct snd_kcontrol_new rt5631_spol_mux_control =
	SOC_DAPM_ENUM("SPOL SRC", rt5631_spol_src_enum);

/* SPO Right Channel Input */
static const char *rt5631_spor_src_sel[] = {
	"SPORMIX", "MONOIN_RX", "VDAC", "DACR"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_spor_src_enum, RT5631_SPK_MONO_HP_OUT_CTRL,
	RT5631_SPK_R_MUX_SEL_SHIFT, rt5631_spor_src_sel);

static const struct snd_kcontrol_new rt5631_spor_mux_control =
	SOC_DAPM_ENUM("SPOR SRC", rt5631_spor_src_enum);

/* MONO Input */
static const char *rt5631_mono_src_sel[] = {"MONOMIX", "MONOIN_RX", "VDAC"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_mono_src_enum, RT5631_SPK_MONO_HP_OUT_CTRL,
	RT5631_MONO_MUX_SEL_SHIFT, rt5631_mono_src_sel);

static const struct snd_kcontrol_new rt5631_mono_mux_control =
	SOC_DAPM_ENUM("MONO SRC", rt5631_mono_src_enum);

/* Left HPO Input */
static const char *rt5631_hpl_src_sel[] = {"Left HPVOL", "Left DAC"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_hpl_src_enum, RT5631_SPK_MONO_HP_OUT_CTRL,
	RT5631_HP_L_MUX_SEL_SHIFT, rt5631_hpl_src_sel);

static const struct snd_kcontrol_new rt5631_hpl_mux_control =
	SOC_DAPM_ENUM("HPL SRC", rt5631_hpl_src_enum);

/* Right HPO Input */
static const char *rt5631_hpr_src_sel[] = {"Right HPVOL", "Right DAC"};

static const SOC_ENUM_SINGLE_DECL(
	rt5631_hpr_src_enum, RT5631_SPK_MONO_HP_OUT_CTRL,
	RT5631_HP_R_MUX_SEL_SHIFT, rt5631_hpr_src_sel);

static const struct snd_kcontrol_new rt5631_hpr_mux_control =
	SOC_DAPM_ENUM("HPR SRC", rt5631_hpr_src_enum);

static const struct snd_soc_dapm_widget rt5631_dapm_widgets[] = {
	/* Vmid */
	SND_SOC_DAPM_VMID("Vmid"),
	/* PLL */
	SND_SOC_DAPM_SUPPLY("PLL1", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_PLL1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL2", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_PLL2_BIT, 0, NULL, 0),

	/* Input Side */
	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("AXIL"),
	SND_SOC_DAPM_INPUT("AXIR"),
	SND_SOC_DAPM_INPUT("MONOIN_RXN"),
	SND_SOC_DAPM_INPUT("MONOIN_RXP"),
	SND_SOC_DAPM_INPUT("DMIC"),

	/* MICBIAS */
	SND_SOC_DAPM_MICBIAS("MIC Bias1", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS1_VOL_BIT, 0),
	SND_SOC_DAPM_MICBIAS("MIC Bias2", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS2_VOL_BIT, 0),

	/* Boost */
	SND_SOC_DAPM_PGA("MIC1 Boost", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MIC1_BOOT_GAIN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 Boost", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MIC2_BOOT_GAIN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MONOIN_RXP Boost", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_MONO_IN_P_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MONOIN_RXN Boost", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_MONO_IN_N_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AXIL Boost", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_AXIL_IN_VOL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AXIR Boost", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_AXIR_IN_VOL_BIT, 0, NULL, 0),

	/* MONO In */
	SND_SOC_DAPM_MIXER("MONO_IN", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL Mixer", RT5631_PWR_MANAG_ADD2,
		RT5631_PWR_RECMIXER_L_BIT, 0,
		&rt5631_recmixl_mixer_controls[0],
		ARRAY_SIZE(rt5631_recmixl_mixer_controls)),
	SND_SOC_DAPM_MIXER("RECMIXR Mixer", RT5631_PWR_MANAG_ADD2,
		RT5631_PWR_RECMIXER_R_BIT, 0,
		&rt5631_recmixr_mixer_controls[0],
		ARRAY_SIZE(rt5631_recmixr_mixer_controls)),
	/* Because of record duplication for L/R channel,
	 * L/R ADCs need power up at the same time */
	SND_SOC_DAPM_MIXER("ADC Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DMIC */
	SND_SOC_DAPM_SUPPLY("DMIC Supply", RT5631_DIG_MIC_CTRL,
		RT5631_DMIC_ENA_SHIFT, 0,
		set_dmic_params, SND_SOC_DAPM_PRE_PMU),
	/* ADC Data Srouce */
	SND_SOC_DAPM_SUPPLY("Left ADC Select", RT5631_INT_ST_IRQ_CTRL_2,
			RT5631_ADC_DATA_SEL_MIC1_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Right ADC Select", RT5631_INT_ST_IRQ_CTRL_2,
			RT5631_ADC_DATA_SEL_MIC2_SHIFT, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("Left ADC", "HIFI Capture",
		RT5631_PWR_MANAG_ADD1, RT5631_PWR_ADC_L_CLK_BIT, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Voice HIFI Capture",
		RT5631_PWR_MANAG_ADD1, RT5631_PWR_ADC_R_CLK_BIT, 0),

	/* DAC and ADC supply power */
	SND_SOC_DAPM_SUPPLY("I2S", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_MAIN_I2S_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VI2S", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_VOICE_I2S_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC REF", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_DAC_REF_BIT, 0, NULL, 0),

	/* Output Side */
	/* DACs */
	SND_SOC_DAPM_DAC("Left DAC", "HIFI Playback",
		RT5631_PWR_MANAG_ADD1, RT5631_PWR_DAC_L_CLK_BIT, 0),
	SND_SOC_DAPM_DAC("Right DAC", "HIFI Playback",
		RT5631_PWR_MANAG_ADD1, RT5631_PWR_DAC_R_CLK_BIT, 0),
	SND_SOC_DAPM_DAC("Voice DAC", "Voice DAC Mono Playback",
		RT5631_PWR_MANAG_ADD1, RT5631_PWR_VDAC_CLK_BIT, 0),
	SND_SOC_DAPM_PGA("Voice DAC Boost", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* DAC supply power */
	SND_SOC_DAPM_SUPPLY("Left DAC To Mixer", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_DAC_L_TO_MIXER_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Right DAC To Mixer", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_DAC_R_TO_MIXER_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Voice DAC To Mixer", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_VDAC_TO_MIXER_BIT, 0, NULL, 0),

	/* Left SPK Mixer */
	SND_SOC_DAPM_MIXER("SPKMIXL Mixer", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_SPKMIXER_L_BIT, 0,
			&rt5631_spkmixl_mixer_controls[0],
			ARRAY_SIZE(rt5631_spkmixl_mixer_controls)),
	/* Left Out Mixer */
	SND_SOC_DAPM_MIXER("OUTMIXL Mixer", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_OUTMIXER_L_BIT, 0,
			&rt5631_outmixl_mixer_controls[0],
			ARRAY_SIZE(rt5631_outmixl_mixer_controls)),
	/* Right Out Mixer */
	SND_SOC_DAPM_MIXER("OUTMIXR Mixer", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_OUTMIXER_R_BIT, 0,
			&rt5631_outmixr_mixer_controls[0],
			ARRAY_SIZE(rt5631_outmixr_mixer_controls)),
	/* Right SPK Mixer */
	SND_SOC_DAPM_MIXER("SPKMIXR Mixer", RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_SPKMIXER_R_BIT, 0,
			&rt5631_spkmixr_mixer_controls[0],
			ARRAY_SIZE(rt5631_spkmixr_mixer_controls)),

	/* Volume Mux */
	SND_SOC_DAPM_MUX("Left SPKVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_SPK_L_VOL_BIT, 0,
			&rt5631_spkvoll_mux_control),
	SND_SOC_DAPM_MUX("Left HPVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_HP_L_OUT_VOL_BIT, 0,
			&rt5631_hpvoll_mux_control),
	SND_SOC_DAPM_MUX("Left OUTVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_LOUT_VOL_BIT, 0,
			&rt5631_outvoll_mux_control),
	SND_SOC_DAPM_MUX("Right OUTVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_ROUT_VOL_BIT, 0,
			&rt5631_outvolr_mux_control),
	SND_SOC_DAPM_MUX("Right HPVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_HP_R_OUT_VOL_BIT, 0,
			&rt5631_hpvolr_mux_control),
	SND_SOC_DAPM_MUX("Right SPKVOL Mux", RT5631_PWR_MANAG_ADD4,
			RT5631_PWR_SPK_R_VOL_BIT, 0,
			&rt5631_spkvolr_mux_control),

	/* DAC To HP */
	SND_SOC_DAPM_PGA_S("Left DAC_HP", 0, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("Right DAC_HP", 0, SND_SOC_NOPM, 0, 0, NULL, 0),

	/* HP Depop */
	SND_SOC_DAPM_PGA_S("HP Depop", 1, SND_SOC_NOPM, 0, 0,
		hp_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
		
	/* AUXO Depop */
	SND_SOC_DAPM_PGA_S("AUXO Depop", 1, SND_SOC_NOPM, 0, 0,
		auxo_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),	
		
//bard 7-5 s
	/* SPK */
	SND_SOC_DAPM_PGA_S("SPOL PGA", 2,RT5631_SPK_OUT_VOL,
			RT5631_L_MUTE_SHIFT, 1,
			spk_event,SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("SPOR PGA", 2,RT5631_SPK_OUT_VOL,
			RT5631_R_MUTE_SHIFT, 1,
			spk_event,SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
//bard 7-5 e
	/* AXO1 Mixer */
	SND_SOC_DAPM_MIXER("AXO1MIX Mixer", RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_AXO1MIXER_BIT, 0,
			&rt5631_AXO1MIX_mixer_controls[0],
			ARRAY_SIZE(rt5631_AXO1MIX_mixer_controls)),
	/* SPOL Mixer */
	SND_SOC_DAPM_MIXER("SPOLMIX Mixer", SND_SOC_NOPM, 0, 0,
			&rt5631_spolmix_mixer_controls[0],
			ARRAY_SIZE(rt5631_spolmix_mixer_controls)),
	/* MONO Mixer */
	SND_SOC_DAPM_MIXER("MONOMIX Mixer", RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_MONOMIXER_BIT, 0,
			&rt5631_monomix_mixer_controls[0],
			ARRAY_SIZE(rt5631_monomix_mixer_controls)),
	/* SPOR Mixer */
	SND_SOC_DAPM_MIXER("SPORMIX Mixer", SND_SOC_NOPM, 0, 0,
			&rt5631_spormix_mixer_controls[0],
			ARRAY_SIZE(rt5631_spormix_mixer_controls)),
	/* AXO2 Mixer */
	SND_SOC_DAPM_MIXER("AXO2MIX Mixer", RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_AXO2MIXER_BIT, 0,
			&rt5631_AXO2MIX_mixer_controls[0],
			ARRAY_SIZE(rt5631_AXO2MIX_mixer_controls)),

	/* Mux */
	SND_SOC_DAPM_MUX("SPOL Mux", SND_SOC_NOPM, 0, 0,
			&rt5631_spol_mux_control),
	SND_SOC_DAPM_MUX("SPOR Mux", SND_SOC_NOPM, 0, 0,
			&rt5631_spor_mux_control),
	SND_SOC_DAPM_MUX("MONO Mux", SND_SOC_NOPM, 0, 0,
			&rt5631_mono_mux_control),
	SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0,
			&rt5631_hpl_mux_control),
	SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0,
			&rt5631_hpr_mux_control),

	/* AMP supply */
	SND_SOC_DAPM_SUPPLY("MONO Depop", RT5631_PWR_MANAG_ADD3,
			RT5631_PWR_MONO_DEPOP_DIS_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D", RT5631_PWR_MANAG_ADD1,
			RT5631_PWR_CLASS_D_BIT, 0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("AUXO1"),
	SND_SOC_DAPM_OUTPUT("AUXO2"),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("MONO"),
};

static const struct snd_soc_dapm_route rt5631_dapm_routes[] = {
	{"MIC1 Boost", NULL, "MIC1"},
	{"MIC2 Boost", NULL, "MIC2"},
	{"MONOIN_RXP Boost", NULL, "MONOIN_RXP"},
	{"MONOIN_RXN Boost", NULL, "MONOIN_RXN"},
	{"AXIL Boost", NULL, "AXIL"},
	{"AXIR Boost", NULL, "AXIR"},

	{"MONO_IN", NULL, "MONOIN_RXP Boost"},
	{"MONO_IN", NULL, "MONOIN_RXN Boost"},

	{"RECMIXL Mixer", "OUTMIXL Capture Switch", "OUTMIXL Mixer"},
	{"RECMIXL Mixer", "MIC1_BST1 Capture Switch", "MIC1 Boost"},
	{"RECMIXL Mixer", "AXILVOL Capture Switch", "AXIL Boost"},
	{"RECMIXL Mixer", "MONOIN_RX Capture Switch", "MONO_IN"},

	{"RECMIXR Mixer", "OUTMIXR Capture Switch", "OUTMIXR Mixer"},
	{"RECMIXR Mixer", "MIC2_BST2 Capture Switch", "MIC2 Boost"},
	{"RECMIXR Mixer", "AXIRVOL Capture Switch", "AXIR Boost"},
	{"RECMIXR Mixer", "MONOIN_RX Capture Switch", "MONO_IN"},

	{"ADC Mixer", NULL, "RECMIXL Mixer"},
	{"ADC Mixer", NULL, "RECMIXR Mixer"},

	{"Left ADC", NULL, "ADC Mixer"},
	{"Left ADC", NULL, "Left ADC Select", check_adcl_select},
	{"Left ADC", NULL, "PLL1", check_sysclk1_source},
	{"Left ADC", NULL, "I2S", check_adcr_stereo},
	{"Left ADC", NULL, "VI2S", check_adcr_voice},
	{"Left ADC", NULL, "DAC REF"},

	{"Right ADC", NULL, "ADC Mixer"},
	{"Right ADC", NULL, "Right ADC Select", check_adcr_select},
	{"Right ADC", NULL, "PLL1", check_adcr_pll1},
	{"Right ADC", NULL, "PLL2", check_adcr_pll2},
	{"Right ADC", NULL, "I2S", check_adcr_stereo},
	{"Right ADC", NULL, "VI2S", check_adcr_voice},
	{"Right ADC", NULL, "DAC REF"},

	{"DMIC", NULL, "DMIC Supply", check_dmic_used},
	{"Left ADC", NULL, "DMIC"},
	{"Right ADC", NULL, "DMIC"},

	{"Left DAC", NULL, "PLL1", check_sysclk1_source},
	{"Left DAC", NULL, "I2S"},
	{"Left DAC", NULL, "DAC REF"},
	{"Left DAC", NULL, "Left DAC To Mixer"},
	{"Right DAC", NULL, "PLL1", check_sysclk1_source},
	{"Right DAC", NULL, "I2S"},
	{"Right DAC", NULL, "DAC REF"},
	{"Right DAC", NULL, "Right DAC To Mixer"},

	{"Voice DAC", NULL, "PLL1", check_vdac_pll1},
	{"Voice DAC", NULL, "PLL2", check_vdac_pll2},
	{"Voice DAC", NULL, "VI2S"},
	{"Voice DAC", NULL, "DAC REF"},
	{"Voice DAC", NULL, "Voice DAC To Mixer"},

	{"Voice DAC Boost", NULL, "Voice DAC"},

	{"SPKMIXL Mixer", "RECMIXL Playback Switch", "RECMIXL Mixer"},
	{"SPKMIXL Mixer", "MIC1_P Playback Switch", "MIC1"},
	{"SPKMIXL Mixer", "DACL Playback Switch", "Left DAC"},
	{"SPKMIXL Mixer", "OUTMIXL Playback Switch", "OUTMIXL Mixer"},

	{"SPKMIXR Mixer", "OUTMIXR Playback Switch", "OUTMIXR Mixer"},
	{"SPKMIXR Mixer", "DACR Playback Switch", "Right DAC"},
	{"SPKMIXR Mixer", "MIC2_P Playback Switch", "MIC2"},
	{"SPKMIXR Mixer", "RECMIXR Playback Switch", "RECMIXR Mixer"},

	{"OUTMIXL Mixer", "RECMIXL Playback Switch", "RECMIXL Mixer"},
	{"OUTMIXL Mixer", "RECMIXR Playback Switch", "RECMIXR Mixer"},
	{"OUTMIXL Mixer", "DACL Playback Switch", "Left DAC"},
	{"OUTMIXL Mixer", "MIC1_BST1 Playback Switch", "MIC1 Boost"},
	{"OUTMIXL Mixer", "MIC2_BST2 Playback Switch", "MIC2 Boost"},
	{"OUTMIXL Mixer", "MONOIN_RXP Playback Switch", "MONOIN_RXP Boost"},
	{"OUTMIXL Mixer", "AXILVOL Playback Switch", "AXIL Boost"},
	{"OUTMIXL Mixer", "AXIRVOL Playback Switch", "AXIR Boost"},
	{"OUTMIXL Mixer", "VDAC Playback Switch", "Voice DAC Boost"},

	{"OUTMIXR Mixer", "RECMIXL Playback Switch", "RECMIXL Mixer"},
	{"OUTMIXR Mixer", "RECMIXR Playback Switch", "RECMIXR Mixer"},
	{"OUTMIXR Mixer", "DACR Playback Switch", "Right DAC"},
	{"OUTMIXR Mixer", "MIC1_BST1 Playback Switch", "MIC1 Boost"},
	{"OUTMIXR Mixer", "MIC2_BST2 Playback Switch", "MIC2 Boost"},
	{"OUTMIXR Mixer", "MONOIN_RXN Playback Switch", "MONOIN_RXN Boost"},
	{"OUTMIXR Mixer", "AXILVOL Playback Switch", "AXIL Boost"},
	{"OUTMIXR Mixer", "AXIRVOL Playback Switch", "AXIR Boost"},
	{"OUTMIXR Mixer", "VDAC Playback Switch", "Voice DAC Boost"},

	{"Left SPKVOL Mux",  "SPKMIXL", "SPKMIXL Mixer"},
	{"Left SPKVOL Mux",  "Vmid", "Vmid"},
	{"Left HPVOL Mux",  "OUTMIXL", "OUTMIXL Mixer"},
	{"Left HPVOL Mux",  "Vmid", "Vmid"},
	{"Left OUTVOL Mux",  "OUTMIXL", "OUTMIXL Mixer"},
	{"Left OUTVOL Mux",  "Vmid", "Vmid"},
	{"Right OUTVOL Mux",  "OUTMIXR", "OUTMIXR Mixer"},
	{"Right OUTVOL Mux",  "Vmid", "Vmid"},
	{"Right HPVOL Mux",  "OUTMIXR", "OUTMIXR Mixer"},
	{"Right HPVOL Mux",  "Vmid", "Vmid"},
	{"Right SPKVOL Mux",  "SPKMIXR", "SPKMIXR Mixer"},
	{"Right SPKVOL Mux",  "Vmid", "Vmid"},

	{"AXO1MIX Mixer", "MIC1_BST1 Playback Switch", "MIC1 Boost"},
	{"AXO1MIX Mixer", "OUTVOLL Playback Switch", "Left OUTVOL Mux"},
	{"AXO1MIX Mixer", "OUTVOLR Playback Switch", "Right OUTVOL Mux"},
	{"AXO1MIX Mixer", "MIC2_BST2 Playback Switch", "MIC2 Boost"},

	{"AXO2MIX Mixer", "MIC1_BST1 Playback Switch", "MIC1 Boost"},
	{"AXO2MIX Mixer", "OUTVOLL Playback Switch", "Left OUTVOL Mux"},
	{"AXO2MIX Mixer", "OUTVOLR Playback Switch", "Right OUTVOL Mux"},
	{"AXO2MIX Mixer", "MIC2_BST2 Playback Switch", "MIC2 Boost"},

	{"SPOLMIX Mixer", "SPKVOLL Playback Switch", "Left SPKVOL Mux"},
	{"SPOLMIX Mixer", "SPKVOLR Playback Switch", "Right SPKVOL Mux"},

	{"SPORMIX Mixer", "SPKVOLL Playback Switch", "Left SPKVOL Mux"},
	{"SPORMIX Mixer", "SPKVOLR Playback Switch", "Right SPKVOL Mux"},

	{"MONOMIX Mixer", "OUTVOLL Playback Switch", "Left OUTVOL Mux"},
	{"MONOMIX Mixer", "OUTVOLR Playback Switch", "Right OUTVOL Mux"},

	{"SPOL Mux", "SPOLMIX", "SPOLMIX Mixer"},
	{"SPOL Mux", "MONOIN_RX", "MONO_IN"},
	{"SPOL Mux", "VDAC", "Voice DAC Boost"},
	{"SPOL Mux", "DACL", "Left DAC"},

	{"SPOR Mux", "SPORMIX", "SPORMIX Mixer"},
	{"SPOR Mux", "MONOIN_RX", "MONO_IN"},
	{"SPOR Mux", "VDAC", "Voice DAC Boost"},
	{"SPOR Mux", "DACR", "Right DAC"},

	{"MONO Mux", "MONOMIX", "MONOMIX Mixer"},
	{"MONO Mux", "MONOIN_RX", "MONO_IN"},
	{"MONO Mux", "VDAC", "Voice DAC Boost"},

	{"Right DAC_HP", NULL, "Right DAC"},
	{"Left DAC_HP", NULL, "Left DAC"},

	{"HPL Mux", "Left HPVOL", "Left HPVOL Mux"},
	{"HPL Mux", "Left DAC", "Left DAC_HP"},
	{"HPR Mux", "Right HPVOL", "Right HPVOL Mux"},
	{"HPR Mux", "Right DAC", "Right DAC_HP"},

	{"HP Depop", NULL, "HPL Mux"},
	{"HP Depop", NULL, "HPR Mux"},

	{"AUXO1", NULL, "AXO1MIX Mixer"},
	{"AUXO2", NULL, "AXO2MIX Mixer"},

	{"AUXO Depop", NULL, "AXO1MIX Mixer"},
	{"AUXO Depop", NULL, "AXO2MIX Mixer"},
	
	{"AUXO1", NULL, "AUXO Depop"},
	{"AUXO2", NULL, "AUXO Depop"},
	
#if 1 //bard 7-5
	{"SPOL PGA", NULL, "Class D"},
	{"SPOL PGA", NULL, "SPOL Mux"},
	{"SPOR PGA", NULL, "Class D"},
	{"SPOR PGA", NULL, "SPOR Mux"},
	{"SPOL", NULL, "SPOL PGA"},
	{"SPOR", NULL, "SPOR PGA"},
#else //org
	{"SPOL", NULL, "Class D"},
	{"SPOL", NULL, "SPOL Mux"},
	{"SPOR", NULL, "Class D"},
	{"SPOR", NULL, "SPOR Mux"},
#endif
	{"HPOL", NULL, "HP Depop"},
	{"HPOR", NULL, "HP Depop"},

	{"MONO", NULL, "MONO Depop"},
	{"MONO", NULL, "MONO Mux"},
};

struct coeff_clk_div {
	u32 mclk;
	u32 bclk;
	u32 rate;
	u16 reg_val;
};

/* PLL divisors */
struct pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 reg_val;
};

static const struct pll_div codec_master_pll_div[] = {
	{2048000,  8192000,  0x0ea0},
	{3686400,  8192000,  0x4e27},
	{12000000,  8192000,  0x456b},
	{13000000,  8192000,  0x495f},
	{13100000,  8192000,  0x0320},
	{2048000,  11289600,  0xf637},
	{3686400,  11289600,  0x2f22},
	{12000000,  11289600,  0x3e2f},
	{13000000,  11289600,  0x4d5b},
	{13100000,  11289600,  0x363b},
	{2048000,  16384000,  0x1ea0},
	{3686400,  16384000,  0x9e27},
	{12000000,  16384000,  0x452b},
	{13000000,  16384000,  0x542f},
	{13100000,  16384000,  0x03a0},
	{2048000,  16934400,  0xe625},
	{3686400,  16934400,  0x9126},
	{12000000,  16934400,  0x4d2c},
	{13000000,  16934400,  0x742f},
	{13100000,  16934400,  0x3c27},
	{2048000,  22579200,  0x2aa0},
	{3686400,  22579200,  0x2f20},
	{12000000,  22579200,  0x7e2f},
	{13000000,  22579200,  0x742f},
	{13100000,  22579200,  0x3c27},
	{2048000,  24576000,  0x2ea0},
	{3686400,  24576000,  0xee27},
	{12000000,  24576000,  0x2915},
	{13000000,  24576000,  0x772e},
	{13100000,  24576000,  0x0d20},
	{26000000,  24576000,  0x2027},
	{26000000,  22579200,  0x392f},
	{24576000,  22579200,  0x0921},
	{24576000,  24576000,  0x02a0},
};

static const struct pll_div codec_slave_pll_div[] = {
	{256000,  2048000,  0x46f0},
	{256000,  4096000,  0x3ea0},
	{352800,  5644800,  0x3ea0},
	{512000,  8192000,  0x3ea0},
	{1024000,  8192000,  0x46f0},
	{705600,  11289600,  0x3ea0},
	{1024000,  16384000,  0x3ea0},
	{1411200,  22579200,  0x3ea0},
	{1536000,  24576000,  0x3ea0},
	{2048000,  16384000,  0x1ea0},
	{2822400,  22579200,  0x1ea0},
	{2822400,  45158400,  0x5ec0},
	{5644800,  45158400,  0x46f0},
	{3072000,  24576000,  0x1ea0},
	{3072000,  49152000,  0x5ec0},
	{6144000,  49152000,  0x46f0},
	{705600,  11289600,  0x3ea0},
	{705600,  8467200,  0x3ab0},
	{24576000,  24576000,  0x02a0},
	{1411200,  11289600,  0x1690},
	{2822400,  11289600,  0x0a90},
	{1536000,  12288000,  0x1690},
	{3072000,  12288000,  0x0a90},
};

struct coeff_clk_div coeff_div[] = {
	/* sysclk is 256fs */
	{2048000,  8000 * 32,  8000, 0x1000},
	{2048000,  8000 * 64,  8000, 0x0000},
	{2822400,  11025 * 32,  11025,  0x1000},
	{2822400,  11025 * 64,  11025,  0x0000},
	{4096000,  16000 * 32,  16000,  0x1000},
	{4096000,  16000 * 64,  16000,  0x0000},
	{5644800,  22050 * 32,  22050,  0x1000},
	{5644800,  22050 * 64,  22050,  0x0000},
	{8192000,  32000 * 32,  32000,  0x1000},
	{8192000,  32000 * 64,  32000,  0x0000},
	{11289600,  44100 * 32,  44100,  0x1000},
	{11289600,  44100 * 64,  44100,  0x0000},
	{12288000,  48000 * 32,  48000,  0x1000},
	{12288000,  48000 * 64,  48000,  0x0000},
	{22579200,  88200 * 32,  88200,  0x1000},
	{22579200,  88200 * 64,  88200,  0x0000},
	{24576000,  96000 * 32,  96000,  0x1000},
	{24576000,  96000 * 64,  96000,  0x0000},
	/* sysclk is 512fs */
	{4096000,  8000 * 32,  8000, 0x3000},
	{4096000,  8000 * 64,  8000, 0x2000},
	{5644800,  11025 * 32,  11025, 0x3000},
	{5644800,  11025 * 64,  11025, 0x2000},
	{8192000,  16000 * 32,  16000, 0x3000},
	{8192000,  16000 * 64,  16000, 0x2000},
	{11289600,  22050 * 32,  22050, 0x3000},
	{11289600,  22050 * 64,  22050, 0x2000},
	{16384000,  32000 * 32,  32000, 0x3000},
	{16384000,  32000 * 64,  32000, 0x2000},
	{22579200,  44100 * 32,  44100, 0x3000},
	{22579200,  44100 * 64,  44100, 0x2000},
	{24576000,  48000 * 32,  48000, 0x3000},
	{24576000,  48000 * 64,  48000, 0x2000},
	{45158400,  88200 * 32,  88200, 0x3000},
	{45158400,  88200 * 64,  88200, 0x2000},
	{49152000,  96000 * 32,  96000, 0x3000},
	{49152000,  96000 * 64,  96000, 0x2000},
	/* sysclk is 24.576Mhz or 22.5792Mhz */
	{24576000,  8000 * 32,  8000,  0x7080},
	{24576000,  8000 * 64,  8000,  0x6080},
	{24576000,  16000 * 32,  16000,  0x5080},
	{24576000,  16000 * 64,  16000,  0x4080},
	{24576000,  24000 * 32,  24000,  0x5000},
	{24576000,  24000 * 64,  24000,  0x4000},
	{24576000,  32000 * 32,  32000,  0x3080},
	{24576000,  32000 * 64,  32000,  0x2080},
	{22579200,  11025 * 32,  11025,  0x7000},
	{22579200,  11025 * 64,  11025,  0x6000},
	{22579200,  22050 * 32,  22050,  0x5000},
	{22579200,  22050 * 64,  22050,  0x4000},
};

struct coeff_clk_div coeff_div_voice[] = {
	/* sysclk is 256fs */
	{2048000,  8000 * 32,  8000, 0x0008},
	{2048000,  8000 * 64,  8000, 0x0000},
	{2822400,  11025 * 32,  11025,  0x0008},
	{2822400,  11025 * 64,  11025,  0x0000},
	{4096000,  16000 * 32,  16000,  0x0008},
	{4096000,  16000 * 64,  16000,  0x0000},
	/* sysclk is 512fs */
	{4096000,  8000 * 32,  8000, 0x0018},
	{4096000,  8000 * 64,  8000, 0x0010},
	{5644800,  11025 * 32,  11025, 0x0018},
	{5644800,  11025 * 64,  11025, 0x0010},
	{8192000,  16000 * 32,  16000, 0x0018},
	{8192000,  16000 * 64,  16000, 0x0010},
	/* sysclk is 24.576Mhz or 22.5792Mhz */
	{24576000,  8000 * 32,  8000,  0x003c},
	{24576000,  8000 * 64,  8000,  0x0034},
	{24576000,  16000 * 32,  16000,  0x002c},
	{24576000,  16000 * 64,  16000,  0x0024},
	{22579200,  11025 * 32,  11025,  0x0038},
	{22579200,  11025 * 64,  11025,  0x0030},
};

static int get_coeff(int dai_id, int mclk, int rate, int timesofbclk)
{
	struct coeff_clk_div *tabp;
	int i, tab_num;

	if (dai_id == RT5631_AIF1) {
		tabp = coeff_div;
		tab_num = ARRAY_SIZE(coeff_div);
	} else if (dai_id == RT5631_AIF2) {
		tabp = coeff_div_voice;
		tab_num = ARRAY_SIZE(coeff_div_voice);
	} else
		return -EINVAL;

	for (i = 0; i < tab_num; i++) {
		if (tabp[i].mclk == mclk && tabp[i].rate == rate &&
			(tabp[i].bclk / tabp[i].rate) == timesofbclk)
			return i;
	}
	return -EINVAL;
}

static int rt5631_hifi_pcm_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);
	int timesofbclk = 32, coeff, sysclk=0;
	unsigned int iface = 0, val;

	dev_dbg(codec->dev, "enter %s\n", __func__);

	if (RT5631_AIF1 == dai->id) {
		snd_soc_update_bits(codec, RT5631_GLOBAL_CLK_CTRL,
			RT5631_ADCR_FUN_MASK, RT5631_ADCR_FUN_ADC);
		sysclk = rt5631->sysclk[RT5631_SCLK1];
	} else if (RT5631_AIF2 == dai->id) {
		snd_soc_update_bits(codec, RT5631_GLOBAL_CLK_CTRL,
			RT5631_ADCR_FUN_MASK, RT5631_ADCR_FUN_VADC);
		val = snd_soc_read(codec, RT5631_GLOBAL_CLK_CTRL);
		if (val & RT5631_VDAC_CLK_SOUR_SCLK2)
			sysclk = rt5631->sysclk[RT5631_SCLK2];
		else
			sysclk = rt5631->sysclk[RT5631_SCLK1];
	}

	rt5631->bclk_rate = snd_soc_params_to_bclk(params);
	if (rt5631->bclk_rate < 0) {
		dev_err(codec->dev, "Fail to get BCLK rate\n");
		return rt5631->bclk_rate;
	}
	rt5631->rate = params_rate(params);
	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream)
		rt5631->rx_rate = rt5631->rate;

	if (rt5631->master[dai->id])
		coeff = get_coeff(dai->id, sysclk, rt5631->rate,
			rt5631->bclk_rate / rt5631->rate);
	else
		coeff = get_coeff(dai->id, sysclk, rt5631->rate, timesofbclk);
	if (coeff < 0) {
		dev_err(codec->dev, "Fail to get coeff\n");
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= RT5631_SDP_I2S_DL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= RT5631_SDP_I2S_DL_24;
		break;
	case SNDRV_PCM_FORMAT_S8:
		iface |= RT5631_SDP_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	if (RT5631_AIF1 == dai->id) {
		snd_soc_update_bits(codec, RT5631_SDP_CTRL,
			RT5631_SDP_I2S_DL_MASK, iface);
		snd_soc_update_bits(codec, RT5631_STEREO_AD_DA_CLK_CTRL,
			RT5631_I2S_PRE_DIV_MASK |
			RT5631_I2S_LRCK_SEL_N_BCLK_MASK |
			RT5631_ADDA_FILTER_CLK_SEL_MASK,
			coeff_div[coeff].reg_val);
	} else if (RT5631_AIF2 == dai->id) {
		snd_soc_update_bits(codec, RT5631_MONO_SDP_CTRL,
			RT5631_SDP_I2S_DL_MASK, iface);
		snd_soc_update_bits(codec, RT5631_STEREO_AD_DA_CLK_CTRL,
			RT5631_I2S_PRE_DIV2_MASK |
			RT5631_I2S_LRCK_SEL_N_BCLK2_MASK |
			RT5631_ADDA_FILTER_CLK2_SEL_MASK,
			coeff_div_voice[coeff].reg_val);
	}

	return 0;
}

static int rt5631_hifi_codec_set_dai_fmt(
	struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);
	unsigned int iface = 0;

	dev_dbg(codec->dev, "enter %s\n", __func__);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5631->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface |= RT5631_SDP_MODE_SEL_SLAVE;
		rt5631->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= RT5631_SDP_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= RT5631_SDP_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface  |= RT5631_SDP_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= RT5631_SDP_I2S_BCLK_POL_CTRL;
		break;
	default:
		return -EINVAL;
	}

	if (RT5631_AIF1 == dai->id)
		snd_soc_write(codec, RT5631_SDP_CTRL, iface);
	else if (RT5631_AIF2 == dai->id)
		snd_soc_write(codec, RT5631_MONO_SDP_CTRL, iface);

	return 0;
}

static int rt5631_hifi_codec_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "syclk[%d]: %d\n", clk_id, freq);

	if (clk_id != RT5631_SCLK1 && clk_id != RT5631_SCLK2)
		return -EINVAL;

	if ((freq >= (256 * 8000)) && (freq <= (512 * 96000))) {
		rt5631->sysclk[clk_id] = freq;
		return 0;
	}

	return -EINVAL;
}

static int rt5631_codec_set_dai_pll(struct snd_soc_dai *dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pll_div *pll_tab;
	unsigned int pll_reg, clk_val, clk_mask;
	int i, tab_num, ret = -EINVAL;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");
		if (RT5631_PLL1 == pll_id)
			snd_soc_update_bits(codec, RT5631_GLOBAL_CLK_CTRL,
				RT5631_SYSCLK_SOUR_SEL_MASK,
				RT5631_SYSCLK_SOUR_SEL_MCLK);
		else if (RT5631_PLL2 == pll_id)
			snd_soc_update_bits(codec, RT5631_GLOBAL_CLK_CTRL,
				RT5631_SYSCLK2_SOUR_SEL_MASK,
				RT5631_SYSCLK2_SOUR_SEL_MCLK);
		return 0;
	}

	switch (pll_id) {
	case RT5631_PLL1:
		pll_reg = RT5631_PLL_CTRL;
		clk_mask = RT5631_SYSCLK_SOUR_SEL_MASK |
			RT5631_PLLCLK_SOUR_SEL_MASK;
		clk_val = RT5631_SYSCLK_SOUR_SEL_PLL;
		break;

	case RT5631_PLL2:
		pll_reg = RT5631_PLL2_CTRL;
		clk_mask = RT5631_SYSCLK2_SOUR_SEL_MASK |
			RT5631_PLLCLK2_SOUR_SEL_MASK;
		clk_val = RT5631_SYSCLK2_SOUR_SEL_PLL2;
		break;

	default:
		return -EINVAL;
	}

	switch (source) {
	case RT5631_PLL_S_MCLK:
		pll_tab = (struct pll_div *)codec_master_pll_div;
		tab_num = ARRAY_SIZE(codec_master_pll_div);
		clk_val |= (pll_id ? RT5631_PLLCLK2_SOUR_SEL_MCLK :
			RT5631_PLLCLK_SOUR_SEL_MCLK);
		break;

	case RT5631_PLL_S_BCLK:
		pll_tab = (struct pll_div *)codec_slave_pll_div;
		tab_num = ARRAY_SIZE(codec_slave_pll_div);
		clk_val |= (pll_id ? RT5631_PLLCLK2_SOUR_SEL_BCLK :
			RT5631_PLLCLK_SOUR_SEL_BCLK);
		break;

	case RT5631_PLL_S_VBCLK:
		pll_tab = (struct pll_div *)codec_slave_pll_div;
		tab_num = ARRAY_SIZE(codec_slave_pll_div);
		clk_val |= (pll_id ? RT5631_PLLCLK2_SOUR_SEL_VBCLK :
			RT5631_PLLCLK_SOUR_SEL_VBCLK);
		break;

	default:
		return -EINVAL;
	}

	for (i = 0; i < tab_num; i++)
		if (freq_in == pll_tab[i].pll_in &&
			freq_out == pll_tab[i].pll_out) {
			snd_soc_write(codec, pll_reg, pll_tab[i].reg_val);
			mdelay(20);
			snd_soc_update_bits(codec, RT5631_GLOBAL_CLK_CTRL,
						clk_mask, clk_val);
			ret = 0;
			break;
		}

	return ret;
}

static int rt5631_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
/* bard 7-5 remove
		snd_soc_update_bits(codec, RT5631_SPK_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,0);
		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,0);
*/
		break;

	case SND_SOC_BIAS_PREPARE:
//bard 7-5 s
		snd_soc_update_bits(codec, RT5631_SPK_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,
			RT5631_L_MUTE | RT5631_R_MUTE);
		snd_soc_update_bits(codec, RT5631_HP_OUT_VOL,
			RT5631_L_MUTE | RT5631_R_MUTE,
			RT5631_L_MUTE | RT5631_R_MUTE);
//bard 7-5 e
		snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD2,
			RT5631_PWR_MICBIAS1_VOL | RT5631_PWR_MICBIAS2_VOL,
			RT5631_PWR_MICBIAS1_VOL | RT5631_PWR_MICBIAS2_VOL);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
				RT5631_PWR_VREF | RT5631_PWR_MAIN_BIAS,
				RT5631_PWR_VREF | RT5631_PWR_MAIN_BIAS);
			msleep(80);
			snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
				RT5631_PWR_FAST_VREF_CTRL,
				RT5631_PWR_FAST_VREF_CTRL);
			codec->cache_only = false;
			codec->cache_sync = 1;
			snd_soc_cache_sync(codec);
			rt5631_index_sync(codec);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, RT5631_PWR_MANAG_ADD1, 0x0000);
		snd_soc_write(codec, RT5631_PWR_MANAG_ADD2, 0x0000);
		snd_soc_write(codec, RT5631_PWR_MANAG_ADD3, 0x0000);
		snd_soc_write(codec, RT5631_PWR_MANAG_ADD4, 0x0000);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

/**
 * rt5631_index_show - Dump private registers.
 * @dev: codec device.
 * @attr: device attribute.
 * @buf: buffer for display.
 *
 * To show non-zero values of all private registers.
 *
 * Returns buffer length.
 */
static ssize_t rt5631_index_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt5631_priv *rt5631 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5631->codec;
	unsigned int val;
	int cnt = 0, i;

	cnt += sprintf(buf, "RT5631 index register\n");
	for (i = 0; i <= 0x23; i++) {
		if (cnt + 9 >= PAGE_SIZE - 1)
			break;
		val = rt5631_index_read(codec, i);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, 10, "%02x: %04x\n", i, val);
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	return cnt;
}
static DEVICE_ATTR(index_reg, 0444, rt5631_index_show, NULL);

static int rt5631_probe(struct snd_soc_codec *codec)
{
	struct rt5631_priv *rt5631 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;
	int ret;

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	val = rt5631_index_read(codec, RT5631_ADDA_MIXER_INTL_REG3);
	if (val & 0x0002)
		rt5631->codec_version = 1;
	else
		rt5631->codec_version = 0;

	rt5631_reset(codec);
	snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
		RT5631_PWR_VREF | RT5631_PWR_MAIN_BIAS,
		RT5631_PWR_VREF | RT5631_PWR_MAIN_BIAS);
	msleep(80);
	snd_soc_update_bits(codec, RT5631_PWR_MANAG_ADD3,
		RT5631_PWR_FAST_VREF_CTRL, RT5631_PWR_FAST_VREF_CTRL);
	/* enable HP zero cross */
	snd_soc_write(codec, RT5631_INT_ST_IRQ_CTRL_2, 0x0f18);
	/* power off ClassD auto Recovery */
	if (rt5631->codec_version)
		snd_soc_update_bits(codec, RT5631_INT_ST_IRQ_CTRL_2,
					0x2000, 0x2000);
	else
		snd_soc_update_bits(codec, RT5631_INT_ST_IRQ_CTRL_2,
					0x2000, 0);
	/* DMIC */
	if (rt5631->dmic_used_flag) {
		snd_soc_update_bits(codec, RT5631_GPIO_CTRL,
			RT5631_GPIO_PIN_FUN_SEL_MASK |
			RT5631_GPIO_DMIC_FUN_SEL_MASK,
			RT5631_GPIO_PIN_FUN_SEL_GPIO_DIMC |
			RT5631_GPIO_DMIC_FUN_SEL_DIMC);
		snd_soc_update_bits(codec, RT5631_DIG_MIC_CTRL,
			RT5631_DMIC_L_CH_LATCH_MASK |
			RT5631_DMIC_R_CH_LATCH_MASK,
			RT5631_DMIC_L_CH_LATCH_FALLING |
			RT5631_DMIC_R_CH_LATCH_RISING);
	}
	rt5631_reg_init(codec);

	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
	rt5631->codec = codec;
	rt5631_codec = codec;
	snd_soc_add_controls(codec, rt5631_snd_controls,
			ARRAY_SIZE(rt5631_snd_controls));
	snd_soc_dapm_new_controls(&codec->dapm, rt5631_dapm_widgets,
			ARRAY_SIZE(rt5631_dapm_widgets));
	snd_soc_dapm_add_routes(&codec->dapm, rt5631_dapm_routes,
			ARRAY_SIZE(rt5631_dapm_routes));

	ret = device_create_file(codec->dev, &dev_attr_index_reg);
 	if (ret < 0) {
 		dev_err(codec->dev,
			"Failed to create index_reg sysfs files: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rt5631_remove(struct snd_soc_codec *codec)
{
	rt5631_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM
static int rt5631_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	rt5631_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5631_resume(struct snd_soc_codec *codec)
{
	rt5631_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define rt5631_suspend NULL
#define rt5631_resume NULL
#endif

#define RT5631_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5631_VOICE_RATES SNDRV_PCM_RATE_8000_96000//(SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_8000)
#define RT5631_FORMAT	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt5631_ops = {
	.hw_params = rt5631_hifi_pcm_params,
	.set_fmt = rt5631_hifi_codec_set_dai_fmt,
	.set_sysclk = rt5631_hifi_codec_set_dai_sysclk,
	.set_pll = rt5631_codec_set_dai_pll,
};

struct snd_soc_dai_driver rt5631_dai[] = {
	{
		.name = "RT5631 HiFi",
		.id = RT5631_AIF1,
		.playback = {
			.stream_name = "HIFI Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_STEREO_RATES,
			.formats = RT5631_FORMAT,
		},
		.capture = {
			.stream_name = "HIFI Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_STEREO_RATES,
			.formats = RT5631_FORMAT,
		},
		.ops = &rt5631_ops,
	},
	{
		.name = "rt5631-voice",
		.id = RT5631_AIF2,
		.playback = {
			.stream_name = "Mono Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_VOICE_RATES,
			.formats = RT5631_FORMAT,
		},
		.capture = {
			.stream_name = "Voice HIFI Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_VOICE_RATES,
			.formats = RT5631_FORMAT,
		},
		.ops = &rt5631_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5631 = {
	.probe = rt5631_probe,
	.remove = rt5631_remove,
	.suspend = rt5631_suspend,
	.resume = rt5631_resume,
	.set_bias_level = rt5631_set_bias_level,
	.reg_cache_size = RT5631_VENDOR_ID2 + 1,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = rt5631_reg,
	.volatile_register = rt5631_volatile_register,
	.readable_register = rt5631_readable_register,
	.reg_cache_step = 1,
	/*.controls = rt5631_snd_controls,
	.num_controls = ARRAY_SIZE(rt5631_snd_controls),
	.dapm_widgets = rt5631_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5631_dapm_widgets),
	.dapm_routes = rt5631_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5631_dapm_routes),*/
};

static const struct i2c_device_id rt5631_i2c_id[] = {
	{ "rt5631", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5631_i2c_id);

static int rt5631_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5631_priv *rt5631;
	int ret;

	rt5631 = kzalloc(sizeof(struct rt5631_priv), GFP_KERNEL);
	if (NULL == rt5631)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5631);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5631,
			rt5631_dai, ARRAY_SIZE(rt5631_dai));
	if (ret < 0)
		kfree(rt5631);

	return ret;
}

static __devexit int rt5631_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static void rt5631_i2c_shutdown(struct i2c_client *client)
{
	struct rt5631_priv *rt5631 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5631->codec;

	if (codec != NULL)
		rt5631_set_bias_level(codec, SND_SOC_BIAS_OFF);

//	return 0;
}

struct i2c_driver rt5631_i2c_driver = {
	.driver = {
		.name = "RT5631",
		.owner = THIS_MODULE,
	},
	.probe = rt5631_i2c_probe,
	.remove = __devexit_p(rt5631_i2c_remove),
	.shutdown = rt5631_i2c_shutdown,
	.id_table = rt5631_i2c_id,
};

static int __init rt5631_modinit(void)
{
	return i2c_add_driver(&rt5631_i2c_driver);
}
module_init(rt5631_modinit);

static void __exit rt5631_modexit(void)
{
	i2c_del_driver(&rt5631_i2c_driver);
}
module_exit(rt5631_modexit);

MODULE_DESCRIPTION("ASoC RT5631 driver");
MODULE_AUTHOR("flove <flove@realtek.com>");
MODULE_LICENSE("GPL");
