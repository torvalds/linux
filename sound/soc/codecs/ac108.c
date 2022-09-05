/*
 * ac10x.c  --  ac10x ALSA SoC Audio driver
 *
 *
 * Author: Baozhu Zuo<zuobaozhu@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* #undef DEBUG
 * use 'make DEBUG=1' to enable debugging
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "ac108.h"
#include "ac10x.h"

#define _USE_CAPTURE	1
#define _MASTER_INDEX	0

/**
 * TODO:
 * 1, add PM API:  ac108_suspend,ac108_resume
 * 2,0x65-0x6a
 * 3,0x76-0x79 high 4bit
 */
struct pll_div {
	unsigned int freq_in;
	unsigned int freq_out;
	unsigned int m1;
	unsigned int m2;
	unsigned int n;
	unsigned int k1;
	unsigned int k2;
};

static struct ac10x_priv *ac10x;

struct real_val_to_reg_val {
	unsigned int real_val;
	unsigned int reg_val;
};

static const struct real_val_to_reg_val ac108_sample_rate[] = {
	{ 8000,  0 },
	{ 11025, 1 },
	{ 12000, 2 },
	{ 16000, 3 },
	{ 22050, 4 },
	{ 24000, 5 },
	{ 32000, 6 },
	{ 44100, 7 },
	{ 48000, 8 },
	{ 96000, 9 },
};

/* Sample resolution */
static const struct real_val_to_reg_val ac108_samp_res[] = {
	{ 8,  1 },
	{ 12, 2 },
	{ 16, 3 },
	{ 20, 4 },
	{ 24, 5 },
	{ 28, 6 },
	{ 32, 7 },
};

static const unsigned int ac108_bclkdivs[] = {
	 0,   1,   2,   4,
	 6,   8,  12,  16,
	24,  32,  48,  64,
	96, 128, 176, 192,
};

/* FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] ;	M1[0,31],  M2[0,1],  N[0,1023],  K1[0,31],  K2[0,1] */
static const struct pll_div ac108_pll_div_list[] = {
	{ 400000,   _FREQ_24_576K, 0,  0, 614, 4, 1 },
	{ 512000,   _FREQ_24_576K, 0,  0, 960, 9, 1 }, /* _FREQ_24_576K/48 */
	{ 768000,   _FREQ_24_576K, 0,  0, 640, 9, 1 }, /* _FREQ_24_576K/32 */
	{ 800000,   _FREQ_24_576K, 0,  0, 614, 9, 1 },
	{ 1024000,  _FREQ_24_576K, 0,  0, 480, 9, 1 }, /* _FREQ_24_576K/24 */
	{ 1600000,  _FREQ_24_576K, 0,  0, 307, 9, 1 },
	{ 2048000,  _FREQ_24_576K, 0,  0, 240, 9, 1 }, /* accurate,  8000 * 256 */
	{ 3072000,  _FREQ_24_576K, 0,  0, 160, 9, 1 }, /* accurate, 12000 * 256 */
	{ 4096000,  _FREQ_24_576K, 2,  0, 360, 9, 1 }, /* accurate, 16000 * 256 */
	{ 6000000,  _FREQ_24_576K, 4,  0, 410, 9, 1 },
	{ 12000000, _FREQ_24_576K, 9,  0, 410, 9, 1 },
	{ 13000000, _FREQ_24_576K, 8,  0, 340, 9, 1 },
	{ 15360000, _FREQ_24_576K, 12, 0, 415, 9, 1 },
	{ 16000000, _FREQ_24_576K, 12, 0, 400, 9, 1 },
	{ 19200000, _FREQ_24_576K, 15, 0, 410, 9, 1 },
	{ 19680000, _FREQ_24_576K, 15, 0, 400, 9, 1 },
	{ 24000000, _FREQ_24_576K, 9,  0, 256,24, 0 }, /* accurate, 24M -> 24.576M */

	{ 400000,   _FREQ_22_579K, 0,  0, 566, 4, 1 },
	{ 512000,   _FREQ_22_579K, 0,  0, 880, 9, 1 },
	{ 768000,   _FREQ_22_579K, 0,  0, 587, 9, 1 },
	{ 800000,   _FREQ_22_579K, 0,  0, 567, 9, 1 },
	{ 1024000,  _FREQ_22_579K, 0,  0, 440, 9, 1 },
	{ 1600000,  _FREQ_22_579K, 1,  0, 567, 9, 1 },
	{ 2048000,  _FREQ_22_579K, 0,  0, 220, 9, 1 },
	{ 3072000,  _FREQ_22_579K, 0,  0, 148, 9, 1 },
	{ 4096000,  _FREQ_22_579K, 2,  0, 330, 9, 1 },
	{ 6000000,  _FREQ_22_579K, 2,  0, 227, 9, 1 },
	{ 12000000, _FREQ_22_579K, 8,  0, 340, 9, 1 },
	{ 13000000, _FREQ_22_579K, 9,  0, 350, 9, 1 },
	{ 15360000, _FREQ_22_579K, 10, 0, 325, 9, 1 },
	{ 16000000, _FREQ_22_579K, 11, 0, 340, 9, 1 },
	{ 19200000, _FREQ_22_579K, 13, 0, 330, 9, 1 },
	{ 19680000, _FREQ_22_579K, 14, 0, 345, 9, 1 },
	{ 24000000, _FREQ_22_579K, 24, 0, 588,24, 0 }, /* accurate, 24M -> 22.5792M */


	{ _FREQ_24_576K / 1,   _FREQ_24_576K, 9,  0, 200, 9, 1 }, /* _FREQ_24_576K */
	{ _FREQ_24_576K / 2,   _FREQ_24_576K, 9,  0, 400, 9, 1 }, /* 12288000,accurate, 48000 * 256 */
	{ _FREQ_24_576K / 4,   _FREQ_24_576K, 4,  0, 400, 9, 1 }, /* 6144000, accurate, 24000 * 256 */
	{ _FREQ_24_576K / 16,  _FREQ_24_576K, 0,  0, 320, 9, 1 }, /* 1536000 */
	{ _FREQ_24_576K / 64,  _FREQ_24_576K, 0,  0, 640, 4, 1 }, /* 384000 */
	{ _FREQ_24_576K / 96,  _FREQ_24_576K, 0,  0, 960, 4, 1 }, /* 256000 */
	{ _FREQ_24_576K / 128, _FREQ_24_576K, 0,  0, 512, 1, 1 }, /* 192000 */
	{ _FREQ_24_576K / 176, _FREQ_24_576K, 0,  0, 880, 4, 0 }, /* 140000 */
	{ _FREQ_24_576K / 192, _FREQ_24_576K, 0,  0, 960, 4, 0 }, /* 128000 */

	{ _FREQ_22_579K / 1,   _FREQ_22_579K, 9,  0, 200, 9, 1 }, /* _FREQ_22_579K */
	{ _FREQ_22_579K / 2,   _FREQ_22_579K, 9,  0, 400, 9, 1 }, /* 11289600,accurate, 44100 * 256 */
	{ _FREQ_22_579K / 4,   _FREQ_22_579K, 4,  0, 400, 9, 1 }, /* 5644800, accurate, 22050 * 256 */
	{ _FREQ_22_579K / 16,  _FREQ_22_579K, 0,  0, 320, 9, 1 }, /* 1411200 */
	{ _FREQ_22_579K / 64,  _FREQ_22_579K, 0,  0, 640, 4, 1 }, /* 352800 */
	{ _FREQ_22_579K / 96,  _FREQ_22_579K, 0,  0, 960, 4, 1 }, /* 235200 */
	{ _FREQ_22_579K / 128, _FREQ_22_579K, 0,  0, 512, 1, 1 }, /* 176400 */
	{ _FREQ_22_579K / 176, _FREQ_22_579K, 0,  0, 880, 4, 0 }, /* 128290 */
	{ _FREQ_22_579K / 192, _FREQ_22_579K, 0,  0, 960, 4, 0 }, /* 117600 */

	{ _FREQ_22_579K / 6,   _FREQ_22_579K, 2,  0, 360, 9, 1 }, /* 3763200 */
	{ _FREQ_22_579K / 8,   _FREQ_22_579K, 0,  0, 160, 9, 1 }, /* 2822400, accurate, 11025 * 256 */
	{ _FREQ_22_579K / 12,  _FREQ_22_579K, 0,  0, 240, 9, 1 }, /* 1881600 */
	{ _FREQ_22_579K / 24,  _FREQ_22_579K, 0,  0, 480, 9, 1 }, /* 940800 */
	{ _FREQ_22_579K / 32,  _FREQ_22_579K, 0,  0, 640, 9, 1 }, /* 705600 */
	{ _FREQ_22_579K / 48,  _FREQ_22_579K, 0,  0, 960, 9, 1 }, /* 470400 */
};


/* AC108 definition */
#define AC108_CHANNELS_MAX		8		/* range[1, 16] */
#define AC108_RATES			(SNDRV_PCM_RATE_8000_96000 &		\
					~(SNDRV_PCM_RATE_64000 | \
					SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000))
#define AC108_FORMATS			(SNDRV_PCM_FMTBIT_S16_LE | \
					/*SNDRV_PCM_FMTBIT_S20_3LE |   \
					SNDRV_PCM_FMTBIT_S24_LE |*/  \
					SNDRV_PCM_FMTBIT_S32_LE)

static const DECLARE_TLV_DB_SCALE(tlv_adc_pga_gain, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(tlv_ch_digital_vol, -11925, 75, 0);

int ac10x_read(u8 reg, u8* rt_val, struct regmap* i2cm)
{
	int r, v = 0;

	if ((r = regmap_read(i2cm, reg, &v)) < 0)
		pr_info("ac10x_read info->[REG-0x%02x]\n", reg);
	else
		*rt_val = v;
	return r;
}

int ac10x_write(u8 reg, u8 val, struct regmap* i2cm)
{
	int r;

	if ((r = regmap_write(i2cm, reg, val)) < 0)
		pr_info("ac10x_write info->[REG-0x%02x,val-0x%02x]\n", reg, val);
	return r;
}

int ac10x_update_bits(u8 reg, u8 mask, u8 val, struct regmap* i2cm)
{
	int r;

	if ((r = regmap_update_bits(i2cm, reg, mask, val)) < 0)
		pr_info("%s() info->[REG-0x%02x,val-0x%02x]\n", __func__, reg, val);
	return r;
}

/**
 * snd_ac108_get_volsw - single mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
static int snd_ac108_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int mask = (1 << fls(mc->max)) - 1;
	unsigned int invert = mc->invert;
	int ret, chip = mc->autodisable;
	u8 val;

	if ((ret = ac10x_read(mc->reg, &val, ac10x->i2cmap[chip])) < 0)
		return ret;

	val = ((val >> mc->shift) & mask) - mc->min;
	if (invert) {
		val = mc->max - val;
	}
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/**
 * snd_ac108_put_volsw - single mixer put callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a single mixer control, or a double mixer
 * control that spans 2 registers.
 *
 * Returns 0 for success.
 */
static int snd_ac108_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int sign_bit = mc->sign_bit;
	unsigned int val, mask = (1 << fls(mc->max)) - 1;
	unsigned int invert = mc->invert;
	int ret, chip = mc->autodisable;

	if (sign_bit)
		mask = BIT(sign_bit + 1) - 1;

	val = ((ucontrol->value.integer.value[0] + mc->min) & mask);
	if (invert) {
		val = mc->max - val;
	}

	mask = mask << mc->shift;
	val = val << mc->shift;

	ret = ac10x_update_bits(mc->reg, mask, val, ac10x->i2cmap[chip]);
	return ret;
}

#define SOC_AC108_SINGLE_TLV(xname, reg, shift, max, invert, chip, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |\
		 SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, .get = snd_ac108_get_volsw,\
	.put = snd_ac108_put_volsw, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert, chip) }

/* single ac108 */
static const struct snd_kcontrol_new ac108_snd_controls[] = {
	/* ### chip 0 ### */
	/*0x70: ADC1 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH1 digital volume", ADC1_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x71: ADC2 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH2 digital volume", ADC2_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x72: ADC3 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH3 digital volume", ADC3_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x73: ADC4 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH4 digital volume", ADC4_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),

	/*0x90: Analog PGA1 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC1 PGA gain", ANA_PGA1_CTRL, ADC1_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x91: Analog PGA2 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC2 PGA gain", ANA_PGA2_CTRL, ADC2_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x92: Analog PGA3 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC3 PGA gain", ANA_PGA3_CTRL, ADC3_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x93: Analog PGA4 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC4 PGA gain", ANA_PGA4_CTRL, ADC4_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
};
/* multiple ac108s */
static const struct snd_kcontrol_new ac108tdm_snd_controls[] = {
	/* ### chip 1 ### */
	/*0x70: ADC1 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH1 digital volume", ADC1_DVOL_CTRL, 0, 0xff, 0, 1, tlv_ch_digital_vol),
	/*0x71: ADC2 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH2 digital volume", ADC2_DVOL_CTRL, 0, 0xff, 0, 1, tlv_ch_digital_vol),
	/*0x72: ADC3 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH3 digital volume", ADC3_DVOL_CTRL, 0, 0xff, 0, 1, tlv_ch_digital_vol),
	/*0x73: ADC4 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH4 digital volume", ADC4_DVOL_CTRL, 0, 0xff, 0, 1, tlv_ch_digital_vol),

	/*0x90: Analog PGA1 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC1 PGA gain", ANA_PGA1_CTRL, ADC1_ANALOG_PGA, 0x1f, 0, 1, tlv_adc_pga_gain),
	/*0x91: Analog PGA2 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC2 PGA gain", ANA_PGA2_CTRL, ADC2_ANALOG_PGA, 0x1f, 0, 1, tlv_adc_pga_gain),
	/*0x92: Analog PGA3 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC3 PGA gain", ANA_PGA3_CTRL, ADC3_ANALOG_PGA, 0x1f, 0, 1, tlv_adc_pga_gain),
	/*0x93: Analog PGA4 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC4 PGA gain", ANA_PGA4_CTRL, ADC4_ANALOG_PGA, 0x1f, 0, 1, tlv_adc_pga_gain),

	/* ### chip 0 ### */
	/*0x70: ADC1 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH5 digital volume", ADC1_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x71: ADC2 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH6 digital volume", ADC2_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x72: ADC3 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH7 digital volume", ADC3_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),
	/*0x73: ADC4 Digital Channel Volume Control Register*/
	SOC_AC108_SINGLE_TLV("CH8 digital volume", ADC4_DVOL_CTRL, 0, 0xff, 0, 0, tlv_ch_digital_vol),

	/*0x90: Analog PGA1 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC5 PGA gain", ANA_PGA1_CTRL, ADC1_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x91: Analog PGA2 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC6 PGA gain", ANA_PGA2_CTRL, ADC2_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x92: Analog PGA3 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC7 PGA gain", ANA_PGA3_CTRL, ADC3_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
	/*0x93: Analog PGA4 Control Register*/
	SOC_AC108_SINGLE_TLV("ADC8 PGA gain", ANA_PGA4_CTRL, ADC4_ANALOG_PGA, 0x1f, 0, 0, tlv_adc_pga_gain),
};


static const struct snd_soc_dapm_widget ac108_dapm_widgets[] = {
	/* input widgets */
	SND_SOC_DAPM_INPUT("MIC1P"),
	SND_SOC_DAPM_INPUT("MIC1N"),

	SND_SOC_DAPM_INPUT("MIC2P"),
	SND_SOC_DAPM_INPUT("MIC2N"),

	SND_SOC_DAPM_INPUT("MIC3P"),
	SND_SOC_DAPM_INPUT("MIC3N"),

	SND_SOC_DAPM_INPUT("MIC4P"),
	SND_SOC_DAPM_INPUT("MIC4N"),

	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),

	/*0xa0: ADC1 Analog Control 1 Register*/
	/*0xa1-0xa6:use the defualt value*/
	SND_SOC_DAPM_AIF_IN("Channel 1 AAF", "Capture", 0, ANA_ADC1_CTRL1, ADC1_DSM_ENABLE, 1),
	SND_SOC_DAPM_SUPPLY("Channel 1 EN", ANA_ADC1_CTRL1, ADC1_PGA_ENABLE, 1, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MIC1BIAS", ANA_ADC1_CTRL1, ADC1_MICBIAS_EN, 1),

	/*0xa7: ADC2 Analog Control 1 Register*/
	/*0xa8-0xad:use the defualt value*/
	SND_SOC_DAPM_AIF_IN("Channel 2 AAF", "Capture", 0, ANA_ADC2_CTRL1, ADC2_DSM_ENABLE, 1),
	SND_SOC_DAPM_SUPPLY("Channel 2 EN", ANA_ADC2_CTRL1, ADC2_PGA_ENABLE, 1, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MIC2BIAS", ANA_ADC2_CTRL1, ADC2_MICBIAS_EN, 1),

	/*0xae: ADC3 Analog Control 1 Register*/
	/*0xaf-0xb4:use the defualt value*/
	SND_SOC_DAPM_AIF_IN("Channel 3 AAF", "Capture", 0, ANA_ADC3_CTRL1, ADC3_DSM_ENABLE, 1),
	SND_SOC_DAPM_SUPPLY("Channel 3 EN", ANA_ADC3_CTRL1, ADC3_PGA_ENABLE, 1, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MIC3BIAS", ANA_ADC3_CTRL1, ADC3_MICBIAS_EN, 1),

	/*0xb5: ADC4 Analog Control 1 Register*/
	/*0xb6-0xbb:use the defualt value*/
	SND_SOC_DAPM_AIF_IN("Channel 4 AAF", "Capture", 0, ANA_ADC4_CTRL1, ADC4_DSM_ENABLE, 1),
	SND_SOC_DAPM_SUPPLY("Channel 4 EN", ANA_ADC4_CTRL1, ADC4_PGA_ENABLE, 1, NULL, 0),
	SND_SOC_DAPM_MICBIAS("MIC4BIAS", ANA_ADC4_CTRL1, ADC4_MICBIAS_EN, 1),


	/*0x61: ADC Digital Part Enable Register*/
	SND_SOC_DAPM_SUPPLY("ADC EN", ADC_DIG_EN, 4,  1, NULL, 0),
	SND_SOC_DAPM_ADC("ADC1", "Capture", ADC_DIG_EN, 0,  1),
	SND_SOC_DAPM_ADC("ADC2", "Capture", ADC_DIG_EN, 1,  1),
	SND_SOC_DAPM_ADC("ADC3", "Capture", ADC_DIG_EN, 2,  1),
	SND_SOC_DAPM_ADC("ADC4", "Capture", ADC_DIG_EN, 3,  1),

	SND_SOC_DAPM_SUPPLY("ADC1 CLK", ANA_ADC4_CTRL7, ADC1_CLK_GATING, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 CLK", ANA_ADC4_CTRL7, ADC2_CLK_GATING, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC3 CLK", ANA_ADC4_CTRL7, ADC3_CLK_GATING, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC4 CLK", ANA_ADC4_CTRL7, ADC4_CLK_GATING, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DSM EN", ANA_ADC4_CTRL6, DSM_DEMOFF, 1, NULL, 0),

	/*0x62:Digital MIC Enable Register*/
	SND_SOC_DAPM_MICBIAS("DMIC1 enable", DMIC_EN, 0, 0),
	SND_SOC_DAPM_MICBIAS("DMIC2 enable", DMIC_EN, 1, 0),
};

static const struct snd_soc_dapm_route ac108_dapm_routes[] = {

	{ "ADC1", NULL, "Channel 1 AAF" },
	{ "ADC2", NULL, "Channel 2 AAF" },
	{ "ADC3", NULL, "Channel 3 AAF" },
	{ "ADC4", NULL, "Channel 4 AAF" },

	{ "Channel 1 AAF", NULL, "MIC1BIAS" },
	{ "Channel 2 AAF", NULL, "MIC2BIAS" },
	{ "Channel 3 AAF", NULL, "MIC3BIAS" },
	{ "Channel 4 AAF", NULL, "MIC4BIAS" },

	{ "MIC1BIAS", NULL, "ADC1 CLK" },
	{ "MIC2BIAS", NULL, "ADC2 CLK" },
	{ "MIC3BIAS", NULL, "ADC3 CLK" },
	{ "MIC4BIAS", NULL, "ADC4 CLK" },


	{ "ADC1 CLK", NULL, "DSM EN" },
	{ "ADC2 CLK", NULL, "DSM EN" },
	{ "ADC3 CLK", NULL, "DSM EN" },
	{ "ADC4 CLK", NULL, "DSM EN" },


	{ "DSM EN", NULL, "ADC EN" },

	{ "Channel 1 EN", NULL, "DSM EN" },
	{ "Channel 2 EN", NULL, "DSM EN" },
	{ "Channel 3 EN", NULL, "DSM EN" },
	{ "Channel 4 EN", NULL, "DSM EN" },


	{ "MIC1P", NULL, "Channel 1 EN" },
	{ "MIC1N", NULL, "Channel 1 EN" },

	{ "MIC2P", NULL, "Channel 2 EN" },
	{ "MIC2N", NULL, "Channel 2 EN" },

	{ "MIC3P", NULL, "Channel 3 EN" },
	{ "MIC3N", NULL, "Channel 3 EN" },

	{ "MIC4P", NULL, "Channel 4 EN" },
	{ "MIC4N", NULL, "Channel 4 EN" },

};

static int ac108_multi_write(u8 reg, u8 val, struct ac10x_priv *ac10x)
{
	u8 i;
	for (i = 0; i < ac10x->codec_cnt; i++)
		ac10x_write(reg, val, ac10x->i2cmap[i]);
	return 0;
}

static int ac108_multi_update_bits(u8 reg, u8 mask, u8 val, struct ac10x_priv *ac10x)
{
	int r = 0;
	u8 i;
	for (i = 0; i < ac10x->codec_cnt; i++)
		r |= ac10x_update_bits(reg, mask, val, ac10x->i2cmap[i]);
	return r;
}

static unsigned int ac108_codec_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned char val_r;
	struct ac10x_priv *ac10x = dev_get_drvdata(codec->dev);
	/*read one chip is fine*/
	ac10x_read(reg, &val_r, ac10x->i2cmap[_MASTER_INDEX]);
	return val_r;
}

static int ac108_codec_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int val)
{
	struct ac10x_priv *ac10x = dev_get_drvdata(codec->dev);
	ac108_multi_write(reg, val, ac10x);
	return 0;
}

/**
 * The Power management related registers are Reg01h~Reg09h
 * 0x01-0x05,0x08,use the default value
 * @author baozhu (17-6-21)
 *
 * @param ac10x
 */
static void ac108_configure_power(struct ac10x_priv *ac10x)
{
	/**
	 * 0x06:Enable Analog LDO
	 */
	ac108_multi_update_bits(PWR_CTRL6, 0x01 << LDO33ANA_ENABLE, 0x01 << LDO33ANA_ENABLE, ac10x);
	/**
	 * 0x07: 
	 * Control VREF output and micbias voltage ?
	 * REF faststart disable, enable Enable VREF (needed for Analog
	 * LDO and MICBIAS)
	 */
	ac108_multi_update_bits(PWR_CTRL7, 0x1f << VREF_SEL | 0x01 << VREF_FASTSTART_ENABLE | 0x01 << VREF_ENABLE,
					   0x13 << VREF_SEL | 0x00 << VREF_FASTSTART_ENABLE | 0x01 << VREF_ENABLE, ac10x);
	/**
	 * 0x09:
	 * Disable fast-start circuit on VREFP
	 * VREFP_RESCTRL=00=1 MOhm
	 * IGEN_TRIM=100=+25%
	 * Enable VREFP (needed by all audio input channels)
	 */
	ac108_multi_update_bits(PWR_CTRL9, 0x01 << VREFP_FASTSTART_ENABLE | 0x03 << VREFP_RESCTRL | 0x07 << IGEN_TRIM | 0x01 << VREFP_ENABLE,
					   0x00 << VREFP_FASTSTART_ENABLE | 0x00 << VREFP_RESCTRL | 0x04 << IGEN_TRIM | 0x01 << VREFP_ENABLE,
					   ac10x);
}

/**
 * The clock management related registers are Reg20h~Reg25h
 * The PLL management related registers are Reg10h~Reg18h.
 * @author baozhu (17-6-20)
 *
 * @param ac10x
 * @param rate : sample rate
 *
 * @return int : fail or success
 */
static int ac108_config_pll(struct ac10x_priv *ac10x, unsigned rate, unsigned lrck_ratio)
{
	unsigned int i = 0;
	struct pll_div ac108_pll_div = { 0 };

	if (ac10x->clk_id == SYSCLK_SRC_PLL) {
		unsigned pll_src, pll_freq_in;

		if (lrck_ratio == 0) {
			/* PLL clock source from MCLK */
			pll_freq_in = ac10x->sysclk;
			pll_src = 0x0;
		} else {
			/* PLL clock source from BCLK */
			pll_freq_in = rate * lrck_ratio;
			pll_src = 0x1;
		}

		/* FOUT =(FIN * N) / [(M1+1) * (M2+1)*(K1+1)*(K2+1)] */
		for (i = 0; i < ARRAY_SIZE(ac108_pll_div_list); i++) {
			if (ac108_pll_div_list[i].freq_in == pll_freq_in && ac108_pll_div_list[i].freq_out % rate == 0) {
				ac108_pll_div = ac108_pll_div_list[i];
				dev_info(&ac10x->i2c[_MASTER_INDEX]->dev, "AC108 PLL freq_in match:%u, freq_out:%u\n\n",
								ac108_pll_div.freq_in, ac108_pll_div.freq_out);
				break;
			}
		}
		/* 0x11,0x12,0x13,0x14: Config PLL DIV param M1/M2/N/K1/K2 */
		ac108_multi_update_bits(PLL_CTRL5, 0x1f << PLL_POSTDIV1 | 0x01 << PLL_POSTDIV2,
						   ac108_pll_div.k1 << PLL_POSTDIV1 | ac108_pll_div.k2 << PLL_POSTDIV2, ac10x);
		ac108_multi_update_bits(PLL_CTRL4, 0xff << PLL_LOOPDIV_LSB, (unsigned char)ac108_pll_div.n << PLL_LOOPDIV_LSB, ac10x);
		ac108_multi_update_bits(PLL_CTRL3, 0x03 << PLL_LOOPDIV_MSB, (ac108_pll_div.n >> 8) << PLL_LOOPDIV_MSB, ac10x);
		ac108_multi_update_bits(PLL_CTRL2, 0x1f << PLL_PREDIV1 | 0x01 << PLL_PREDIV2,
						    ac108_pll_div.m1 << PLL_PREDIV1 | ac108_pll_div.m2 << PLL_PREDIV2, ac10x);

		/*0x18: PLL clk lock enable*/
		ac108_multi_update_bits(PLL_LOCK_CTRL, 0x1 << PLL_LOCK_EN, 0x1 << PLL_LOCK_EN, ac10x);

		/**
		 * 0x20: enable pll, pll source from mclk/bclk, sysclk source from pll, enable sysclk
		 */
		ac108_multi_update_bits(SYSCLK_CTRL, 0x01 << PLLCLK_EN | 0x03  << PLLCLK_SRC | 0x01 << SYSCLK_SRC | 0x01 << SYSCLK_EN,
						     0x01 << PLLCLK_EN | pll_src << PLLCLK_SRC | 0x01 << SYSCLK_SRC | 0x01 << SYSCLK_EN, ac10x);
		ac10x->mclk = ac108_pll_div.freq_out;
	}
	if (ac10x->clk_id == SYSCLK_SRC_MCLK) {
		/**
		 *0x20: sysclk source from mclk, enable sysclk
		 */
		ac108_multi_update_bits(SYSCLK_CTRL, 0x01 << PLLCLK_EN | 0x01 << SYSCLK_SRC | 0x01 << SYSCLK_EN,
						     0x00 << PLLCLK_EN | 0x00 << SYSCLK_SRC | 0x01 << SYSCLK_EN, ac10x);
		ac10x->mclk = ac10x->sysclk;
	}

	return 0;
}

/*
 * support no more than 16 slots.
 */
static int ac108_multi_chips_slots(struct ac10x_priv *ac, int slots)
{
	int i;

	/*
	 * codec0 enable slots 2,3,0,1 when 1 codec
	 *
	 * codec0 enable slots 6,7,0,1 when 2 codec
	 * codec1 enable slots 2,3,4,5
	 *
	 * ...
	 */
	for (i = 0; i < ac->codec_cnt; i++) {
		/* rotate map, due to channels rotated by CPU_DAI */
		const unsigned vec_mask[] = {
			0x3 << 6 | 0x3,	// slots 6,7,0,1
			0xF << 2,	// slots 2,3,4,5
			0,
			0,
		};
		const unsigned vec_maps[] = {
			/*
			 * chip 0,
			 * mic 0 sample -> slot 6
			 * mic 1 sample -> slot 7
			 * mic 2 sample -> slot 0
			 * mic 3 sample -> slot 1
			 */
			0x0 << 12 | 0x1 << 14 | 0x2 << 0 | 0x3 << 2,
			/*
			 * chip 1,
			 * mic 0 sample -> slot 2
			 * mic 1 sample -> slot 3
			 * mic 2 sample -> slot 4
			 * mic 3 sample -> slot 5
			 */
			0x0 << 4  | 0x1 << 6  | 0x2 << 8 | 0x3 << 10,
			0,
			0,
		};
		unsigned vec;

		/* 0x38-0x3A I2S_TX1_CTRLx */
		if (ac->codec_cnt == 1) {
			vec = 0xFUL;
		} else {
			vec = vec_mask[i];
		}
		ac10x_write(I2S_TX1_CTRL1, slots - 1, ac->i2cmap[i]);
		ac10x_write(I2S_TX1_CTRL2, (vec >> 0) & 0xFF, ac->i2cmap[i]);
		ac10x_write(I2S_TX1_CTRL3, (vec >> 8) & 0xFF, ac->i2cmap[i]);

		/* 0x3C-0x3F I2S_TX1_CHMP_CTRLx */
		if (ac->codec_cnt == 1) {
			vec = (0x2 << 0 | 0x3 << 2 | 0x0 << 4 | 0x1 << 6);
		} else if (ac->codec_cnt == 2) {
			vec = vec_maps[i];
		}

		ac10x_write(I2S_TX1_CHMP_CTRL1, (vec >>  0) & 0xFF, ac->i2cmap[i]);
		ac10x_write(I2S_TX1_CHMP_CTRL2, (vec >>  8) & 0xFF, ac->i2cmap[i]);
		ac10x_write(I2S_TX1_CHMP_CTRL3, (vec >> 16) & 0xFF, ac->i2cmap[i]);
		ac10x_write(I2S_TX1_CHMP_CTRL4, (vec >> 24) & 0xFF, ac->i2cmap[i]);
	}
	return 0;
}

static int ac108_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	unsigned int i, channels, samp_res, rate;
	struct snd_soc_codec *codec = dai->codec;
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	unsigned bclkdiv;
	int ret = 0;
	u8 v;

	dev_dbg(dai->dev, "%s() stream=%s play:%d capt:%d +++\n", __func__,
			snd_pcm_stream_str(substream),
			dai->stream_active[SNDRV_PCM_STREAM_PLAYBACK], dai->stream_active[SNDRV_PCM_STREAM_CAPTURE]);

	if (ac10x->i2c101) {
		ret = ac101_hw_params(substream, params, dai);
		if (ret > 0) {
			dev_dbg(dai->dev, "%s() L%d returned\n", __func__, __LINE__);
			/* not configure hw_param twice */
			return 0;
		}
	}

	if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE && dai->stream_active[SNDRV_PCM_STREAM_PLAYBACK])
	 || (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && dai->stream_active[SNDRV_PCM_STREAM_CAPTURE])) {
		/* not configure hw_param twice */
		/* return 0; */
	}

	channels = params_channels(params);

	/* Master mode, to clear cpu_dai fifos, output bclk without lrck */
	ac10x_read(I2S_CTRL, &v, ac10x->i2cmap[_MASTER_INDEX]);
	if (v & (0x01 << BCLK_IOEN)) {
		ac10x_update_bits(I2S_CTRL, 0x1 << LRCK_IOEN, 0x0 << LRCK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		samp_res = 0;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		samp_res = 2;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		samp_res = 3;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		samp_res = 4;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		samp_res = 6;
		break;
	default:
		dev_err(dai->dev, "AC108 don't supported the sample resolution: %u\n", params_format(params));
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(ac108_sample_rate); i++) {
		if (ac108_sample_rate[i].real_val == params_rate(params) / (ac10x->data_protocol + 1UL)) {
			rate = i;
			break;
		}
	}
	if (i >= ARRAY_SIZE(ac108_sample_rate)) {
		return -EINVAL;
	}

	if (channels == 8 && ac108_sample_rate[rate].real_val == 96000) {
		/* 24.576M bit clock is not support by ac108 */
		return -EINVAL;
	}

	dev_dbg(dai->dev, "rate: %d , channels: %d , samp_res: %d",
			ac108_sample_rate[rate].real_val,
			channels,
			ac108_samp_res[samp_res].real_val);

	/**
	 * 0x33: 
	 *  The 8-Low bit of LRCK period value. It is used to program
	 *  the number of BCLKs per channel of sample frame. This value
	 *  is interpreted as follow:
	 *  The 8-Low bit of LRCK period value. It is used to program
	 *  the number of BCLKs per channel of sample frame. This value
	 *  is interpreted as follow: PCM mode: Number of BCLKs within
	 *  (Left + Right) channel width I2S / Left-Justified /
	 *  Right-Justified mode: Number of BCLKs within each individual
	 *  channel width (Left or Right) N+1
	 *  For example:
	 *  n = 7: 8 BCLK width
	 *  …
	 *  n = 1023: 1024 BCLKs width
	 *  0X32[0:1]:
	 *  The 2-High bit of LRCK period value. 
	 */
	if (ac10x->i2s_mode != PCM_FORMAT) {
		if (ac10x->data_protocol) {
			ac108_multi_write(I2S_LRCK_CTRL2, ac108_samp_res[samp_res].real_val - 1, ac10x);
			/*encoding mode, the max LRCK period value < 32,so the 2-High bit is zero*/
			ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
		} else {
			/*TDM mode or normal mode*/
			//ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
			ac108_multi_write(I2S_LRCK_CTRL2, ac108_samp_res[samp_res].real_val - 1, ac10x);
			ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, 0x00, ac10x);
		}
	} else {
		unsigned div;

		/*TDM mode or normal mode*/
		div = ac108_samp_res[samp_res].real_val * channels - 1;
		ac108_multi_write(I2S_LRCK_CTRL2, (div & 0xFF), ac10x);
		ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x03 << 0, (div >> 8) << 0, ac10x);
	}

	/**
	 * 0x35: 
	 * TX Encoding mode will add  4bits to mark channel number 
	 * TODO: need a chat to explain this 
	 */
	ac108_multi_update_bits(I2S_FMT_CTRL2, 0x07 << SAMPLE_RESOLUTION | 0x07 << SLOT_WIDTH_SEL,
						ac108_samp_res[samp_res].reg_val << SAMPLE_RESOLUTION
 						  | ac108_samp_res[samp_res].reg_val << SLOT_WIDTH_SEL, ac10x);

	/**
	 * 0x60: 
	 * ADC Sample Rate synchronised with I2S1 clock zone 
	 */
	ac108_multi_update_bits(ADC_SPRC, 0x0f << ADC_FS_I2S1, ac108_sample_rate[rate].reg_val << ADC_FS_I2S1, ac10x);
	ac108_multi_write(HPF_EN, 0x0F, ac10x);

	if (ac10x->i2c101 && _MASTER_MULTI_CODEC == _MASTER_AC101) {
		ac108_config_pll(ac10x, ac108_sample_rate[rate].real_val, ac108_samp_res[samp_res].real_val * channels);
	} else {
		ac108_config_pll(ac10x, ac108_sample_rate[rate].real_val, 0);
	}

	/*
	 * master mode only
	 */
	bclkdiv = ac10x->mclk / (ac108_sample_rate[rate].real_val * channels * ac108_samp_res[samp_res].real_val);
	for (i = 0; i < ARRAY_SIZE(ac108_bclkdivs) - 1; i++) {
		if (ac108_bclkdivs[i] >= bclkdiv) {
			break;
		}
	}
	ac108_multi_update_bits(I2S_BCLK_CTRL, 0x0F << BCLKDIV, i << BCLKDIV, ac10x);

	/*
	 * slots allocation for each chip
	 */
	ac108_multi_chips_slots(ac10x, channels);

	/*0x21: Module clock enable<I2S, ADC digital, MIC offset Calibration, ADC analog>*/
	ac108_multi_write(MOD_CLK_EN, 1 << I2S | 1 << ADC_DIGITAL | 1 << MIC_OFFSET_CALIBRATION | 1 << ADC_ANALOG, ac10x);
	/*0x22: Module reset de-asserted<I2S, ADC digital, MIC offset Calibration, ADC analog>*/
	ac108_multi_write(MOD_RST_CTRL, 1 << I2S | 1 << ADC_DIGITAL | 1 << MIC_OFFSET_CALIBRATION | 1 << ADC_ANALOG, ac10x);
	
	ac108_multi_write(I2S_TX1_CHMP_CTRL1, 0xE4, ac10x);
	ac108_multi_write(I2S_TX1_CHMP_CTRL2, 0xE4, ac10x);
	ac108_multi_write(I2S_TX1_CHMP_CTRL3, 0xE4, ac10x);
	ac108_multi_write(I2S_TX1_CHMP_CTRL4, 0xE4, ac10x);
	
	ac108_multi_write(I2S_TX2_CHMP_CTRL1, 0xE4, ac10x);
	ac108_multi_write(I2S_TX2_CHMP_CTRL2, 0xE4, ac10x);
	ac108_multi_write(I2S_TX2_CHMP_CTRL3, 0xE4, ac10x);
	ac108_multi_write(I2S_TX2_CHMP_CTRL4, 0xE4, ac10x);

	dev_dbg(dai->dev, "%s() stream=%s ---\n", __func__,
			snd_pcm_stream_str(substream));

	return 0;
}

static int ac108_set_sysclk(struct snd_soc_dai *dai, int clk_id, unsigned int freq, int dir)
{

	struct ac10x_priv *ac10x = snd_soc_dai_get_drvdata(dai);

	freq = 24000000;
	clk_id = SYSCLK_SRC_PLL;

	switch (clk_id) {
	case SYSCLK_SRC_MCLK:
		ac108_multi_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_SRC, SYSCLK_SRC_MCLK << SYSCLK_SRC, ac10x);
		break;
	case SYSCLK_SRC_PLL:
		ac108_multi_update_bits(SYSCLK_CTRL, 0x1 << SYSCLK_SRC, SYSCLK_SRC_PLL << SYSCLK_SRC, ac10x);
		break;
	default:
		return -EINVAL;
	}
	ac10x->sysclk = freq;
	ac10x->clk_id = clk_id;

	return 0;
}

/**
 *  The i2s format management related registers are Reg
 *  30h~Reg36h
 *  33h,35h will be set in ac108_hw_params, It's BCLK width and
 *  Sample Resolution.
 * @author baozhu (17-6-20)
 * 
 * @param dai 
 * @param fmt 
 * 
 * @return int 
 */
static int ac108_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	unsigned char tx_offset, lrck_polarity, brck_polarity;
	struct ac10x_priv *ac10x = dev_get_drvdata(dai->dev);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:    /* AC108 Master */
		if (! ac10x->i2c101 || _MASTER_MULTI_CODEC == _MASTER_AC108) {
			/**
			 * 0x30:chip is master mode ,BCLK & LRCK output
			 */
			ac108_multi_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN | 0x03 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN,
							  0x03 << LRCK_IOEN | 0x01 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN, ac10x);
			/* multi_chips: only one chip set as Master, and the others also need to set as Slave */
			ac10x_update_bits(I2S_CTRL, 0x3 << LRCK_IOEN, 0x01 << BCLK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
		} else {
			/* TODO: Both cpu_dai and codec_dai(AC108) be set as slave in DTS */
			dev_err(dai->dev, "used as slave when AC101 is master\n");
		}
		break;
	case SND_SOC_DAIFMT_CBS_CFS:    /* AC108 Slave */
		/**
		 * 0x30:chip is slave mode, BCLK & LRCK input,enable SDO1_EN and 
		 *  SDO2_EN, Transmitter Block Enable, Globe Enable
		 */
		ac108_multi_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN | 0x03 << SDO1_EN | 0x1 << TXEN | 0x1 << GEN,
						  0x00 << LRCK_IOEN | 0x03 << SDO1_EN | 0x0 << TXEN | 0x0 << GEN, ac10x);
		break;
	default:
		dev_err(dai->dev, "AC108 Master/Slave mode config error:%u\n\n", (fmt & SND_SOC_DAIFMT_MASTER_MASK) >> 12);
		return -EINVAL;
	}

	/*AC108 config I2S/LJ/RJ/PCM format*/
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		ac10x->i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 1;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		ac10x->i2s_mode = RIGHT_JUSTIFIED_FORMAT;
		tx_offset = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ac10x->i2s_mode = LEFT_JUSTIFIED_FORMAT;
		tx_offset = 0;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		ac10x->i2s_mode = PCM_FORMAT;
		tx_offset = 1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		ac10x->i2s_mode = PCM_FORMAT;
		tx_offset = 0;
		break;
	default:
		dev_err(dai->dev, "AC108 I2S format config error:%u\n\n", fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	/*AC108 config BCLK&LRCK polarity*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		brck_polarity = BCLK_NORMAL_DRIVE_N_SAMPLE_P;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_HIGH_RIGHT_LOW;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		brck_polarity = BCLK_INVERT_DRIVE_P_SAMPLE_N;
		lrck_polarity = LRCK_LEFT_LOW_RIGHT_HIGH;
		break;
	default:
		dev_err(dai->dev, "AC108 config BCLK/LRCLK polarity error:%u\n\n", (fmt & SND_SOC_DAIFMT_INV_MASK) >> 8);
		return -EINVAL;
	}

	ac108_configure_power(ac10x);

	/**
	 *0x31: 0: normal mode, negative edge drive and positive edge sample
		1: invert mode, positive edge drive and negative edge sample
	 */
	ac108_multi_update_bits(I2S_BCLK_CTRL,  0x01 << BCLK_POLARITY, brck_polarity << BCLK_POLARITY, ac10x);
	/**
	 * 0x32: same as 0x31
	 */
	ac108_multi_update_bits(I2S_LRCK_CTRL1, 0x01 << LRCK_POLARITY, lrck_polarity << LRCK_POLARITY, ac10x);
	/**
	 * 0x34:Encoding Mode Selection,Mode 
	 * Selection,data is offset by 1 BCLKs to LRCK 
	 * normal mode for the last half cycle of BCLK in the slot ?
	 * turn to hi-z state (TDM) when not transferring slot ?
	 */
	ac108_multi_update_bits(I2S_FMT_CTRL1,	0x01 << ENCD_SEL | 0x03 << MODE_SEL | 0x01 << TX2_OFFSET |
						0x01 << TX1_OFFSET | 0x01 << TX_SLOT_HIZ | 0x01 << TX_STATE,
								  ac10x->data_protocol << ENCD_SEL 	|
								  ac10x->i2s_mode << MODE_SEL 		|
								  tx_offset << TX2_OFFSET 			|
								  tx_offset << TX1_OFFSET 			|
								  0x00 << TX_SLOT_HIZ 				|
								  0x01 << TX_STATE, ac10x);

	/**
	 * 0x60: 
	 * MSB / LSB First Select: This driver only support MSB First Select . 
	 * OUT2_MUTE,OUT1_MUTE shoule be set in widget. 
	 * LRCK = 1 BCLK width 
	 * Linear PCM 
	 *  
	 * TODO:pcm mode, bit[0:1] and bit[2] is special
	 */
	ac108_multi_update_bits(I2S_FMT_CTRL3,	0x01 << TX_MLS | 0x03 << SEXT  | 0x01 << LRCK_WIDTH | 0x03 << TX_PDM,
						0x00 << TX_MLS | 0x03 << SEXT  | 0x00 << LRCK_WIDTH | 0x00 << TX_PDM, ac10x);

	ac108_multi_write(HPF_EN, 0x00, ac10x);

	if (ac10x->i2c101) {
		return ac101_set_dai_fmt(dai, fmt);
	}
	return 0;
}

/*
 * due to miss channels order in cpu_dai, we meed defer the clock starting.
 */
static int ac108_set_clock(int y_start_n_stop)
{
	u8 reg;
	int ret = 0;

	dev_dbg(ac10x->codec->dev, "%s() L%d cmd:%d\n", __func__, __LINE__, y_start_n_stop);

	/* spin_lock move to machine trigger */

	if (y_start_n_stop && ac10x->sysclk_en == 0) {
		/* enable lrck clock */
		ac10x_read(I2S_CTRL, &reg, ac10x->i2cmap[_MASTER_INDEX]);
		if (reg & (0x01 << BCLK_IOEN)) {
			ret = ret || ac10x_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN, 0x03 << LRCK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
		}

		/*0x10: PLL Common voltage enable, PLL enable */
		ret = ret || ac108_multi_update_bits(PLL_CTRL1, 0x01 << PLL_EN | 0x01 << PLL_COM_EN,
						   0x01 << PLL_EN | 0x01 << PLL_COM_EN, ac10x);
		/* enable global clock */
		ret = ret || ac108_multi_update_bits(I2S_CTRL, 0x1 << TXEN | 0x1 << GEN, 0x1 << TXEN | 0x1 << GEN, ac10x);

		ac10x->sysclk_en = 1UL;
	} else if (!y_start_n_stop && ac10x->sysclk_en != 0) {
		/* disable global clock */
		ret = ret || ac108_multi_update_bits(I2S_CTRL, 0x1 << TXEN | 0x1 << GEN, 0x0 << TXEN | 0x0 << GEN, ac10x);

		/*0x10: PLL Common voltage disable, PLL disable */
		ret = ret || ac108_multi_update_bits(PLL_CTRL1, 0x01 << PLL_EN | 0x01 << PLL_COM_EN,
						   0x00 << PLL_EN | 0x00 << PLL_COM_EN, ac10x);

		/* disable lrck clock if it's enabled */
		ac10x_read(I2S_CTRL, &reg, ac10x->i2cmap[_MASTER_INDEX]);
		if (reg & (0x01 << LRCK_IOEN)) {
			ret = ret || ac10x_update_bits(I2S_CTRL, 0x03 << LRCK_IOEN, 0x01 << BCLK_IOEN, ac10x->i2cmap[_MASTER_INDEX]);
		}
		if (!ret) {
			ac10x->sysclk_en = 0UL;
		}
	}

	return ret;
}

static int ac108_prepare(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	u8 r;

	dev_dbg(dai->dev, "%s() stream=%s\n",
		__func__,
		snd_pcm_stream_str(substream));
	
	if (ac10x->i2c101 && _MASTER_MULTI_CODEC == _MASTER_AC101) {
		ac101_trigger(substream, SNDRV_PCM_TRIGGER_START, dai);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		       return 0;
		}
	}

	ac10x_read(I2S_CTRL, &r, ac10x->i2cmap[_MASTER_INDEX]);
	if ((r & (0x01 << BCLK_IOEN)) && (r & (0x01 << LRCK_IOEN)) == 0) {
		/* disable global clock */
		ac108_multi_update_bits(I2S_CTRL, 0x1 << TXEN | 0x1 << GEN, 0x0 << TXEN | 0x0 << GEN, ac10x);
	}

	/* delayed clock starting, move to machine trigger() */
	ac108_set_clock(1);

	return 0;
}

int ac108_audio_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	if (ac10x->i2c101) {
		return ac101_audio_startup(substream, dai);
	}
	return 0;
}

void ac108_aif_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	ac108_set_clock(0);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		/*0x21: Module clock disable <I2S, ADC digital, MIC offset Calibration, ADC analog>*/
		ac108_multi_write(MOD_CLK_EN, 0x0, ac10x);
		/*0x22: Module reset asserted <I2S, ADC digital, MIC offset Calibration, ADC analog>*/
		ac108_multi_write(MOD_RST_CTRL, 0x0, ac10x);
	}

	if (ac10x->i2c101) {
		ac101_aif_shutdown(substream, dai);
	}
}

int ac108_aif_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	if (ac10x->i2c101) {
		return ac101_aif_mute(dai, mute);
	}
	return 0;	
}

static const struct snd_soc_dai_ops ac108_dai_ops = {
	.startup	= ac108_audio_startup,
	.shutdown	= ac108_aif_shutdown,

	/*DAI clocking configuration*/
	.set_sysclk	= ac108_set_sysclk,

	/*ALSA PCM audio operations*/
	.hw_params	= ac108_hw_params,
	.prepare	= ac108_prepare,
	.mute_stream	= ac108_aif_mute,

	/*DAI format configuration*/
	.set_fmt	= ac108_set_fmt,
};

static  struct snd_soc_dai_driver ac108_dai0 = {
	.name = "ac10x-codec0",
	#if _USE_CAPTURE
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = AC108_CHANNELS_MAX,
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	#endif
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = AC108_CHANNELS_MAX,
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static  struct snd_soc_dai_driver ac108_dai1 = {
	.name = "ac10x-codec1",
	#if _USE_CAPTURE
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = AC108_CHANNELS_MAX,
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	#endif
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = AC108_CHANNELS_MAX,
		.rates = AC108_RATES,
		.formats = AC108_FORMATS,
	},
	.ops = &ac108_dai_ops,
};

static  struct snd_soc_dai_driver *ac108_dai[] = {
	&ac108_dai0,
	&ac108_dai1,
};

static int ac108_add_widgets(struct snd_soc_codec *codec)
{
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	const struct snd_kcontrol_new* snd_kcntl = ac108_snd_controls;
	int ctrl_cnt = ARRAY_SIZE(ac108_snd_controls);

	/* only register controls correspond to exist chips */
	if (ac10x->tdm_chips_cnt >= 2) {
		snd_kcntl = ac108tdm_snd_controls;
		ctrl_cnt = ARRAY_SIZE(ac108tdm_snd_controls);
	}
	snd_soc_add_codec_controls(codec, snd_kcntl, ctrl_cnt);

	snd_soc_dapm_new_controls(dapm, ac108_dapm_widgets,ARRAY_SIZE(ac108_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, ac108_dapm_routes, ARRAY_SIZE(ac108_dapm_routes));

	return 0;
}

static int ac108_codec_probe(struct snd_soc_codec *codec)
{
	spin_lock_init(&ac10x->lock);

	ac10x->codec = codec;
	dev_set_drvdata(codec->dev, ac10x);
	ac108_add_widgets(codec);

	if (ac10x->i2c101) {
		ac101_codec_probe(codec);
	}

	/* change default volume */
	ac108_multi_update_bits(ADC1_DVOL_CTRL, 0xff, 0xc8, ac10x);
	ac108_multi_update_bits(ADC2_DVOL_CTRL, 0xff, 0xc8, ac10x);
	ac108_multi_update_bits(ADC3_DVOL_CTRL, 0xff, 0xc8, ac10x);
	ac108_multi_update_bits(ADC4_DVOL_CTRL, 0xff, 0xc8, ac10x);

	return 0;
}

static int ac108_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level)
{
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "AC108 level:%d\n", level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		ac108_multi_update_bits(ANA_ADC1_CTRL1, 0x01 << ADC1_MICBIAS_EN,  0x01 << ADC1_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC2_CTRL1, 0x01 << ADC2_MICBIAS_EN,  0x01 << ADC2_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC3_CTRL1, 0x01 << ADC3_MICBIAS_EN,  0x01 << ADC3_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC4_CTRL1, 0x01 << ADC4_MICBIAS_EN,  0x01 << ADC4_MICBIAS_EN, ac10x);
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Put the MICBIASes into regulating mode */
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		ac108_multi_update_bits(ANA_ADC1_CTRL1, 0x01 << ADC1_MICBIAS_EN,  0x00 << ADC1_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC2_CTRL1, 0x01 << ADC2_MICBIAS_EN,  0x00 << ADC2_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC3_CTRL1, 0x01 << ADC3_MICBIAS_EN,  0x00 << ADC3_MICBIAS_EN, ac10x);
		ac108_multi_update_bits(ANA_ADC4_CTRL1, 0x01 << ADC4_MICBIAS_EN,  0x00 << ADC4_MICBIAS_EN, ac10x);
		break;
	}

	if (ac10x->i2c101) {
		ac101_set_bias_level(codec, level);
	}
	return 0;
}

int ac108_codec_remove(struct snd_soc_codec *codec) {
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);

	if (! ac10x->i2c101) {
		return 0;
	}
	return ac101_codec_remove(codec);
}
#if __NO_SND_SOC_CODEC_DRV
void ac108_codec_remove_void(struct snd_soc_codec *codec) {
	ac108_codec_remove(codec);
}
#define ac108_codec_remove ac108_codec_remove_void
#endif

int ac108_codec_suspend(struct snd_soc_codec *codec)
{
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	int i;

	for (i = 0; i < ac10x->codec_cnt; i++) {
		regcache_cache_only(ac10x->i2cmap[i], true);
	}

	if (! ac10x->i2c101) {
		return 0;
	}
	return ac101_codec_suspend(codec);
}

int ac108_codec_resume(struct snd_soc_codec *codec) {
	struct ac10x_priv *ac10x = snd_soc_codec_get_drvdata(codec);
	int i, ret;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ac10x->codec_cnt; i++) {
		regcache_cache_only(ac10x->i2cmap[i], false);
		ret = regcache_sync(ac10x->i2cmap[i]);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to sync i2cmap%d register cache: %d\n", i, ret);
			regcache_cache_only(ac10x->i2cmap[i], true);
		}
	}

	if (! ac10x->i2c101) {
		return 0;
	}
	return ac101_codec_resume(codec);
}

static struct snd_soc_codec_driver ac10x_soc_codec_driver = {
	.probe 		= ac108_codec_probe,
	.remove 	= ac108_codec_remove,
	.suspend 	= ac108_codec_suspend,
	.resume 	= ac108_codec_resume,
	.set_bias_level = ac108_set_bias_level,
	.read		= ac108_codec_read,
	.write		= ac108_codec_write,
};

static ssize_t ac108_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0, flag = 0;
	u8 i = 0, reg, num, value_w, value_r[4];

	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xF;

	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		ac108_multi_write(reg, value_w, ac10x);
		dev_info(dev, "Write 0x%02x to REG:0x%02x\n", value_w, reg);
	} else {
		int k;

		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		dev_info(dev, "\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);

		for (k = 0; k < ac10x->codec_cnt; k++)
			regcache_cache_bypass(ac10x->i2cmap[k], true);

		do {
			memset(value_r, 0, sizeof value_r);

			for (k = 0; k < ac10x->codec_cnt; k++)
				ac10x_read(reg, &value_r[k], ac10x->i2cmap[k]);

			if (ac10x->codec_cnt >= 2)
				dev_info(dev, "REG[0x%02x]: 0x%02x 0x%02x", reg, value_r[0], value_r[1]);
			else
				dev_info(dev, "REG[0x%02x]: 0x%02x", reg, value_r[0]);
			reg++;

			if ((++i == num) || (i % 4 == 0))
				dev_info(dev, "\n");
		} while (i < num);

		for (k = 0; k < ac10x->codec_cnt; k++)
			regcache_cache_bypass(ac10x->i2cmap[k], false);
	}

	return count;
}

static ssize_t ac108_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	dev_info(dev, "echo flag|reg|val > ac108\n");
	dev_info(dev, "eg read star addres=0x06,count 0x10:echo 0610 >ac108\n");
	dev_info(dev, "eg write value:0xfe to address:0x06 :echo 106fe > ac108\n");
	return 0;
}

static DEVICE_ATTR(ac108, 0644, ac108_show, ac108_store);
static struct attribute *ac108_debug_attrs[] = {
	&dev_attr_ac108.attr,
	NULL,
};
static struct attribute_group ac108_debug_attr_group = {
	.name   = "ac108_debug",
	.attrs  = ac108_debug_attrs,
};

static const struct regmap_config ac108_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = 0xDF,
	.cache_type = REGCACHE_FLAT,
};
static int ac108_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *i2c_id)
{
	struct device_node *np = i2c->dev.of_node;
	unsigned int val = 0;
	int ret = 0, index;

	if (ac10x == NULL) {
		ac10x = kzalloc(sizeof(struct ac10x_priv), GFP_KERNEL);
		if (ac10x == NULL) {
			dev_err(&i2c->dev, "Unable to allocate ac10x private data\n");
			return -ENOMEM;
		}
	}

	index = (int)i2c_id->driver_data;
	if (index == AC101_I2C_ID) {
		ac10x->i2c101 = i2c;
		i2c_set_clientdata(i2c, ac10x);
		ret = ac101_probe(i2c, i2c_id);
		if (ret) {
			ac10x->i2c101 = NULL;
			return ret;
		}
		goto __ret;
	}

	ret = of_property_read_u32(np, "data-protocol", &val);
	if (ret) {
		dev_err(&i2c->dev, "Please set data-protocol.\n");
		return -EINVAL;
	}
	ac10x->data_protocol = val;

	if (of_property_read_u32(np, "tdm-chips-count", &val)) val = 1;
	ac10x->tdm_chips_cnt = val;

	dev_info(&i2c->dev, " ac10x i2c_id number: %d\n", index);
	dev_info(&i2c->dev, " ac10x data protocol: %d\n", ac10x->data_protocol);

	ac10x->i2c[index] = i2c;
	ac10x->i2cmap[index] = devm_regmap_init_i2c(i2c, &ac108_regmap);
	if (IS_ERR(ac10x->i2cmap[index])) {
		ret = PTR_ERR(ac10x->i2cmap[index]);
		dev_err(&i2c->dev, "Fail to initialize i2cmap%d I/O: %d\n", index, ret);
		return ret;
	}

	/*
	 * Writing this register with 0x12
	 * will resets all register to their default state.
	 */
	regcache_cache_only(ac10x->i2cmap[index], false);
	
	ret = regmap_write(ac10x->i2cmap[index], CHIP_RST, CHIP_RST_VAL);
	msleep(1);

	/* sync regcache for FLAT type */
	ac10x_fill_regcache(&i2c->dev, ac10x->i2cmap[index]);

	ac10x->codec_cnt++;
	dev_info(&i2c->dev, " ac10x codec count  : %d\n", ac10x->codec_cnt);

	ret = sysfs_create_group(&i2c->dev.kobj, &ac108_debug_attr_group);
	if (ret) {
		dev_err(&i2c->dev, "failed to create attr group\n");
	}

__ret:
	/* It's time to bind codec to i2c[_MASTER_INDEX] when all i2c are ready */
	if ((ac10x->codec_cnt != 0 && ac10x->tdm_chips_cnt < 2)
	|| (ac10x->i2c[0] && ac10x->i2c[1] && ac10x->i2c101)) {
		/* no playback stream */
		if (! ac10x->i2c101) {
			memset(&ac108_dai[_MASTER_INDEX]->playback, '\0', sizeof ac108_dai[_MASTER_INDEX]->playback);
		}
		ret = snd_soc_register_codec(&ac10x->i2c[_MASTER_INDEX]->dev, &ac10x_soc_codec_driver,
						ac108_dai[_MASTER_INDEX], 1);
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to register ac10x codec: %d\n", ret);
		}
	}
	return ret;
}

static int ac108_i2c_remove(struct i2c_client *i2c)
{
	if (ac10x->codec != NULL) {
		snd_soc_unregister_codec(&ac10x->i2c[_MASTER_INDEX]->dev);
		ac10x->codec = NULL;
	}
	if (i2c == ac10x->i2c101) {
		ac101_remove(ac10x->i2c101);
		ac10x->i2c101 = NULL;
		goto __ret;
	}

	if (i2c == ac10x->i2c[0]) {
		ac10x->i2c[0] = NULL;
	}
	if (i2c == ac10x->i2c[1]) {
		ac10x->i2c[1] = NULL;
	}

	sysfs_remove_group(&i2c->dev.kobj, &ac108_debug_attr_group);

__ret:
	if (!ac10x->i2c[0] && !ac10x->i2c[1] && !ac10x->i2c101) {
		kfree(ac10x);
		ac10x = NULL;
	}
	return 0;
}

static const struct i2c_device_id ac108_i2c_id[] = {
	{ "ac108_0", 0 },
	{ "ac108_1", 1 },
	{ "ac108_2", 2 },
	{ "ac108_3", 3 },
	{ "ac101", AC101_I2C_ID },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ac108_i2c_id);

static const struct of_device_id ac108_of_match[] = {
	{ .compatible = "x-power,ac108_0", },
	{ .compatible = "x-power,ac108_1", },
	{ .compatible = "x-power,ac108_2", },
	{ .compatible = "x-power,ac108_3", },
	{ .compatible = "x-power,ac101",   },
	{ }
};
MODULE_DEVICE_TABLE(of, ac108_of_match);

static struct i2c_driver ac108_i2c_driver = {
	.driver = {
		.name = "ac10x-codec",
		.of_match_table = ac108_of_match,
	},
	.probe =    ac108_i2c_probe,
	.remove =   ac108_i2c_remove,
	.id_table = ac108_i2c_id,
};

module_i2c_driver(ac108_i2c_driver);

MODULE_DESCRIPTION("ASoC AC108 driver");
MODULE_AUTHOR("Baozhu Zuo<zuobaozhu@gmail.com>");
MODULE_LICENSE("GPL");
