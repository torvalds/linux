// SPDX-License-Identifier: GPL-2.0
//
// Audio driver for AK4458 DAC
//
// Copyright (C) 2016 Asahi Kasei Microdevices Corporation
// Copyright 2018 NXP

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "ak4458.h"

#define AK4458_NUM_SUPPLIES 2
static const char *ak4458_supply_names[AK4458_NUM_SUPPLIES] = {
	"DVDD",
	"AVDD",
};

enum ak4458_type {
	AK4458 = 0,
	AK4497 = 1,
};

struct ak4458_drvdata {
	struct snd_soc_dai_driver *dai_drv;
	const struct snd_soc_component_driver *comp_drv;
	enum ak4458_type type;
};

/* AK4458 Codec Private Data */
struct ak4458_priv {
	struct regulator_bulk_data supplies[AK4458_NUM_SUPPLIES];
	const struct ak4458_drvdata *drvdata;
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpiod;
	struct gpio_desc *mute_gpiod;
	int digfil;	/* SSLOW, SD, SLOW bits */
	int fs;		/* sampling rate */
	int fmt;
	int slots;
	int slot_width;
	u32 dsd_path;    /* For ak4497 */
};

static const struct reg_default ak4458_reg_defaults[] = {
	{ 0x00, 0x0C },	/*	0x00	AK4458_00_CONTROL1	*/
	{ 0x01, 0x22 },	/*	0x01	AK4458_01_CONTROL2	*/
	{ 0x02, 0x00 },	/*	0x02	AK4458_02_CONTROL3	*/
	{ 0x03, 0xFF },	/*	0x03	AK4458_03_LCHATT	*/
	{ 0x04, 0xFF },	/*	0x04	AK4458_04_RCHATT	*/
	{ 0x05, 0x00 },	/*	0x05	AK4458_05_CONTROL4	*/
	{ 0x06, 0x00 },	/*	0x06	AK4458_06_DSD1		*/
	{ 0x07, 0x03 },	/*	0x07	AK4458_07_CONTROL5	*/
	{ 0x08, 0x00 },	/*	0x08	AK4458_08_SOUND_CONTROL	*/
	{ 0x09, 0x00 },	/*	0x09	AK4458_09_DSD2		*/
	{ 0x0A, 0x0D },	/*	0x0A	AK4458_0A_CONTROL6	*/
	{ 0x0B, 0x0C },	/*	0x0B	AK4458_0B_CONTROL7	*/
	{ 0x0C, 0x00 },	/*	0x0C	AK4458_0C_CONTROL8	*/
	{ 0x0D, 0x00 },	/*	0x0D	AK4458_0D_CONTROL9	*/
	{ 0x0E, 0x50 },	/*	0x0E	AK4458_0E_CONTROL10	*/
	{ 0x0F, 0xFF },	/*	0x0F	AK4458_0F_L2CHATT	*/
	{ 0x10, 0xFF },	/*	0x10	AK4458_10_R2CHATT	*/
	{ 0x11, 0xFF },	/*	0x11	AK4458_11_L3CHATT	*/
	{ 0x12, 0xFF },	/*	0x12	AK4458_12_R3CHATT	*/
	{ 0x13, 0xFF },	/*	0x13	AK4458_13_L4CHATT	*/
	{ 0x14, 0xFF },	/*	0x14	AK4458_14_R4CHATT	*/
};

/*
 * Volume control:
 * from -127 to 0 dB in 0.5 dB steps (mute instead of -127.5 dB)
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);

/*
 * DEM1 bit DEM0 bit Mode
 * 0 0 44.1kHz
 * 0 1 OFF (default)
 * 1 0 48kHz
 * 1 1 32kHz
 */
static const char * const ak4458_dem_select_texts[] = {
	"44.1kHz", "OFF", "48kHz", "32kHz"
};

/*
 * SSLOW, SD, SLOW bits Digital Filter Setting
 * 0, 0, 0 : Sharp Roll-Off Filter
 * 0, 0, 1 : Slow Roll-Off Filter
 * 0, 1, 0 : Short delay Sharp Roll-Off Filter
 * 0, 1, 1 : Short delay Slow Roll-Off Filter
 * 1, *, * : Super Slow Roll-Off Filter
 */
static const char * const ak4458_digfil_select_texts[] = {
	"Sharp Roll-Off Filter",
	"Slow Roll-Off Filter",
	"Short delay Sharp Roll-Off Filter",
	"Short delay Slow Roll-Off Filter",
	"Super Slow Roll-Off Filter"
};

/*
 * DZFB: Inverting Enable of DZF
 * 0: DZF goes H at Zero Detection
 * 1: DZF goes L at Zero Detection
 */
static const char * const ak4458_dzfb_select_texts[] = {"H", "L"};

/*
 * SC1-0 bits: Sound Mode Setting
 * 0 0 : Sound Mode 0
 * 0 1 : Sound Mode 1
 * 1 0 : Sound Mode 2
 * 1 1 : Reserved
 */
static const char * const ak4458_sc_select_texts[] = {
	"Sound Mode 0", "Sound Mode 1", "Sound Mode 2"
};

/* FIR2-0 bits: FIR Filter Mode Setting */
static const char * const ak4458_fir_select_texts[] = {
	"Mode 0", "Mode 1", "Mode 2", "Mode 3",
	"Mode 4", "Mode 5", "Mode 6", "Mode 7",
};

/* ATS1-0 bits Attenuation Speed */
static const char * const ak4458_ats_select_texts[] = {
	"4080/fs", "2040/fs", "510/fs", "255/fs",
};

/* DIF2 bit Audio Interface Format Setting(BICK fs) */
static const char * const ak4458_dif_select_texts[] = {"32fs,48fs", "64fs",};

static const struct soc_enum ak4458_dac1_dem_enum =
	SOC_ENUM_SINGLE(AK4458_01_CONTROL2, 1,
			ARRAY_SIZE(ak4458_dem_select_texts),
			ak4458_dem_select_texts);
static const struct soc_enum ak4458_dac2_dem_enum =
	SOC_ENUM_SINGLE(AK4458_0A_CONTROL6, 0,
			ARRAY_SIZE(ak4458_dem_select_texts),
			ak4458_dem_select_texts);
static const struct soc_enum ak4458_dac3_dem_enum =
	SOC_ENUM_SINGLE(AK4458_0E_CONTROL10, 4,
			ARRAY_SIZE(ak4458_dem_select_texts),
			ak4458_dem_select_texts);
static const struct soc_enum ak4458_dac4_dem_enum =
	SOC_ENUM_SINGLE(AK4458_0E_CONTROL10, 6,
			ARRAY_SIZE(ak4458_dem_select_texts),
			ak4458_dem_select_texts);
static const struct soc_enum ak4458_digfil_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ak4458_digfil_select_texts),
			    ak4458_digfil_select_texts);
static const struct soc_enum ak4458_dzfb_enum =
	SOC_ENUM_SINGLE(AK4458_02_CONTROL3, 2,
			ARRAY_SIZE(ak4458_dzfb_select_texts),
			ak4458_dzfb_select_texts);
static const struct soc_enum ak4458_sm_enum =
	SOC_ENUM_SINGLE(AK4458_08_SOUND_CONTROL, 0,
			ARRAY_SIZE(ak4458_sc_select_texts),
			ak4458_sc_select_texts);
static const struct soc_enum ak4458_fir_enum =
	SOC_ENUM_SINGLE(AK4458_0C_CONTROL8, 0,
			ARRAY_SIZE(ak4458_fir_select_texts),
			ak4458_fir_select_texts);
static const struct soc_enum ak4458_ats_enum =
	SOC_ENUM_SINGLE(AK4458_0B_CONTROL7, 6,
			ARRAY_SIZE(ak4458_ats_select_texts),
			ak4458_ats_select_texts);
static const struct soc_enum ak4458_dif_enum =
	SOC_ENUM_SINGLE(AK4458_00_CONTROL1, 3,
			ARRAY_SIZE(ak4458_dif_select_texts),
			ak4458_dif_select_texts);

static int get_digfil(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4458->digfil;

	return 0;
}

static int set_digfil(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int num;

	num = ucontrol->value.enumerated.item[0];
	if (num > 4)
		return -EINVAL;

	ak4458->digfil = num;

	/* write SD bit */
	snd_soc_component_update_bits(component, AK4458_01_CONTROL2,
			    AK4458_SD_MASK,
			    ((ak4458->digfil & 0x02) << 4));

	/* write SLOW bit */
	snd_soc_component_update_bits(component, AK4458_02_CONTROL3,
			    AK4458_SLOW_MASK,
			    (ak4458->digfil & 0x01));

	/* write SSLOW bit */
	snd_soc_component_update_bits(component, AK4458_05_CONTROL4,
			    AK4458_SSLOW_MASK,
			    ((ak4458->digfil & 0x04) >> 2));

	return 0;
}

static const struct snd_kcontrol_new ak4458_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC1 Playback Volume", AK4458_03_LCHATT,
			 AK4458_04_RCHATT, 0, 0xFF, 0, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Playback Volume", AK4458_0F_L2CHATT,
			 AK4458_10_R2CHATT, 0, 0xFF, 0, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC3 Playback Volume", AK4458_11_L3CHATT,
			 AK4458_12_R3CHATT, 0, 0xFF, 0, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC4 Playback Volume", AK4458_13_L4CHATT,
			 AK4458_14_R4CHATT, 0, 0xFF, 0, dac_tlv),
	SOC_ENUM("AK4458 De-emphasis Response DAC1", ak4458_dac1_dem_enum),
	SOC_ENUM("AK4458 De-emphasis Response DAC2", ak4458_dac2_dem_enum),
	SOC_ENUM("AK4458 De-emphasis Response DAC3", ak4458_dac3_dem_enum),
	SOC_ENUM("AK4458 De-emphasis Response DAC4", ak4458_dac4_dem_enum),
	SOC_ENUM_EXT("AK4458 Digital Filter Setting", ak4458_digfil_enum,
		     get_digfil, set_digfil),
	SOC_ENUM("AK4458 Inverting Enable of DZFB", ak4458_dzfb_enum),
	SOC_ENUM("AK4458 Sound Mode", ak4458_sm_enum),
	SOC_ENUM("AK4458 FIR Filter Mode Setting", ak4458_fir_enum),
	SOC_ENUM("AK4458 Attenuation transition Time Setting",
		 ak4458_ats_enum),
	SOC_ENUM("AK4458 BICK fs Setting", ak4458_dif_enum),
};

/* ak4458 dapm widgets */
static const struct snd_soc_dapm_widget ak4458_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("AK4458 DAC1", NULL, AK4458_0A_CONTROL6, 2, 0),/*pw*/
	SND_SOC_DAPM_AIF_IN("AK4458 SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AK4458 AOUTA"),

	SND_SOC_DAPM_DAC("AK4458 DAC2", NULL, AK4458_0A_CONTROL6, 3, 0),/*pw*/
	SND_SOC_DAPM_OUTPUT("AK4458 AOUTB"),

	SND_SOC_DAPM_DAC("AK4458 DAC3", NULL, AK4458_0B_CONTROL7, 2, 0),/*pw*/
	SND_SOC_DAPM_OUTPUT("AK4458 AOUTC"),

	SND_SOC_DAPM_DAC("AK4458 DAC4", NULL, AK4458_0B_CONTROL7, 3, 0),/*pw*/
	SND_SOC_DAPM_OUTPUT("AK4458 AOUTD"),
};

static const struct snd_soc_dapm_route ak4458_intercon[] = {
	{"AK4458 DAC1",		NULL,	"AK4458 SDTI"},
	{"AK4458 AOUTA",	NULL,	"AK4458 DAC1"},

	{"AK4458 DAC2",		NULL,	"AK4458 SDTI"},
	{"AK4458 AOUTB",	NULL,	"AK4458 DAC2"},

	{"AK4458 DAC3",		NULL,	"AK4458 SDTI"},
	{"AK4458 AOUTC",	NULL,	"AK4458 DAC3"},

	{"AK4458 DAC4",		NULL,	"AK4458 SDTI"},
	{"AK4458 AOUTD",	NULL,	"AK4458 DAC4"},
};

/* ak4497 controls */
static const struct snd_kcontrol_new ak4497_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC Playback Volume", AK4458_03_LCHATT,
			 AK4458_04_RCHATT, 0, 0xFF, 0, dac_tlv),
	SOC_ENUM("AK4497 De-emphasis Response DAC", ak4458_dac1_dem_enum),
	SOC_ENUM_EXT("AK4497 Digital Filter Setting", ak4458_digfil_enum,
		     get_digfil, set_digfil),
	SOC_ENUM("AK4497 Inverting Enable of DZFB", ak4458_dzfb_enum),
	SOC_ENUM("AK4497 Sound Mode", ak4458_sm_enum),
	SOC_ENUM("AK4497 Attenuation transition Time Setting",
		 ak4458_ats_enum),
};

/* ak4497 dapm widgets */
static const struct snd_soc_dapm_widget ak4497_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("AK4497 DAC", NULL, AK4458_0A_CONTROL6, 2, 0),
	SND_SOC_DAPM_AIF_IN("AK4497 SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AK4497 AOUT"),
};

/* ak4497 dapm routes */
static const struct snd_soc_dapm_route ak4497_intercon[] = {
	{"AK4497 DAC",		NULL,	"AK4497 SDTI"},
	{"AK4497 AOUT",		NULL,	"AK4497 DAC"},

};

static int ak4458_get_tdm_mode(struct ak4458_priv *ak4458)
{
	switch (ak4458->slots * ak4458->slot_width) {
	case 128:
		return 1;
	case 256:
		return 2;
	case 512:
		return 3;
	default:
		return 0;
	}
}

static int ak4458_rstn_control(struct snd_soc_component *component, int bit)
{
	int ret;

	if (bit)
		ret = snd_soc_component_update_bits(component,
					  AK4458_00_CONTROL1,
					  AK4458_RSTN_MASK,
					  0x1);
	else
		ret = snd_soc_component_update_bits(component,
					  AK4458_00_CONTROL1,
					  AK4458_RSTN_MASK,
					  0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4458_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int pcm_width = max(params_physical_width(params), ak4458->slot_width);
	u8 format, dsdsel0, dsdsel1, dchn;
	int nfs1, dsd_bclk, ret, channels, channels_max;

	nfs1 = params_rate(params);
	ak4458->fs = nfs1;

	/* calculate bit clock */
	channels = params_channels(params);
	channels_max = dai->driver->playback.channels_max;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_DSD_U8:
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
	case SNDRV_PCM_FORMAT_DSD_U16_BE:
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
	case SNDRV_PCM_FORMAT_DSD_U32_BE:
		dsd_bclk = nfs1 * params_physical_width(params);
		switch (dsd_bclk) {
		case 2822400:
			dsdsel0 = 0;
			dsdsel1 = 0;
			break;
		case 5644800:
			dsdsel0 = 1;
			dsdsel1 = 0;
			break;
		case 11289600:
			dsdsel0 = 0;
			dsdsel1 = 1;
			break;
		case 22579200:
			if (ak4458->drvdata->type == AK4497) {
				dsdsel0 = 1;
				dsdsel1 = 1;
			} else {
				dev_err(dai->dev, "DSD512 not supported.\n");
				return -EINVAL;
			}
			break;
		default:
			dev_err(dai->dev, "Unsupported dsd bclk.\n");
			return -EINVAL;
		}

		snd_soc_component_update_bits(component, AK4458_06_DSD1,
					      AK4458_DSDSEL_MASK, dsdsel0);
		snd_soc_component_update_bits(component, AK4458_09_DSD2,
					      AK4458_DSDSEL_MASK, dsdsel1);
		break;
	}

	/* Master Clock Frequency Auto Setting Mode Enable */
	snd_soc_component_update_bits(component, AK4458_00_CONTROL1, 0x80, 0x80);

	switch (pcm_width) {
	case 16:
		if (ak4458->fmt == SND_SOC_DAIFMT_I2S)
			format = AK4458_DIF_24BIT_I2S;
		else
			format = AK4458_DIF_16BIT_LSB;
		break;
	case 32:
		switch (ak4458->fmt) {
		case SND_SOC_DAIFMT_I2S:
			format = AK4458_DIF_32BIT_I2S;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			format = AK4458_DIF_32BIT_MSB;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			format = AK4458_DIF_32BIT_LSB;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			format = AK4458_DIF_32BIT_MSB;
			break;
		case SND_SOC_DAIFMT_PDM:
			format = AK4458_DIF_32BIT_MSB;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AK4458_00_CONTROL1,
			    AK4458_DIF_MASK, format);

	/*
	 * Enable/disable Daisy Chain if in TDM mode and the number of played
	 * channels is bigger than the maximum supported number of channels
	 */
	dchn = ak4458_get_tdm_mode(ak4458) &&
		(ak4458->fmt == SND_SOC_DAIFMT_DSP_B) &&
		(channels > channels_max) ? AK4458_DCHAIN_MASK : 0;

	snd_soc_component_update_bits(component, AK4458_0B_CONTROL7,
				      AK4458_DCHAIN_MASK, dchn);

	ret = ak4458_rstn_control(component, 0);
	if (ret)
		return ret;

	ret = ak4458_rstn_control(component, 1);
	if (ret)
		return ret;

	return 0;
}

static int ak4458_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC: /* Consumer Mode */
		break;
	case SND_SOC_DAIFMT_CBP_CFP: /* Provider Mode is not supported */
	case SND_SOC_DAIFMT_CBC_CFP:
	case SND_SOC_DAIFMT_CBP_CFC:
	default:
		dev_err(component->dev, "Clock provider mode unsupported\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_PDM:
		ak4458->fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		dev_err(component->dev, "Audio format 0x%02X unsupported\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	/* DSD mode */
	snd_soc_component_update_bits(component, AK4458_02_CONTROL3,
				      AK4458_DP_MASK,
				      ak4458->fmt == SND_SOC_DAIFMT_PDM ?
				      AK4458_DP_MASK : 0);

	ret = ak4458_rstn_control(component, 0);
	if (ret)
		return ret;

	ret = ak4458_rstn_control(component, 1);
	if (ret)
		return ret;

	return 0;
}

static const int att_speed[] = { 4080, 2040, 510, 255 };

static int ak4458_set_dai_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int nfs, ndt, reg;
	int ats;

	nfs = ak4458->fs;

	reg = snd_soc_component_read(component, AK4458_0B_CONTROL7);
	ats = (reg & AK4458_ATS_MASK) >> AK4458_ATS_SHIFT;

	ndt = att_speed[ats] / (nfs / 1000);

	if (mute) {
		snd_soc_component_update_bits(component, AK4458_01_CONTROL2,  0x01, 1);
		mdelay(ndt);
		if (ak4458->mute_gpiod)
			gpiod_set_value_cansleep(ak4458->mute_gpiod, 1);
	} else {
		if (ak4458->mute_gpiod)
			gpiod_set_value_cansleep(ak4458->mute_gpiod, 0);
		snd_soc_component_update_bits(component, AK4458_01_CONTROL2, 0x01, 0);
		mdelay(ndt);
	}

	return 0;
}

static int ak4458_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int mode;

	ak4458->slots = slots;
	ak4458->slot_width = slot_width;

	mode = ak4458_get_tdm_mode(ak4458) << AK4458_MODE_SHIFT;

	snd_soc_component_update_bits(component, AK4458_0A_CONTROL6,
			    AK4458_MODE_MASK,
			    mode);

	return 0;
}

#define AK4458_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE |\
			 SNDRV_PCM_FMTBIT_DSD_U8 |\
			 SNDRV_PCM_FMTBIT_DSD_U16_LE |\
			 SNDRV_PCM_FMTBIT_DSD_U32_LE)

static const unsigned int ak4458_rates[] = {
	8000, 11025,  16000, 22050,
	32000, 44100, 48000, 88200,
	96000, 176400, 192000, 352800,
	384000, 705600, 768000, 1411200,
	2822400,
};

static const struct snd_pcm_hw_constraint_list ak4458_rate_constraints = {
	.count = ARRAY_SIZE(ak4458_rates),
	.list = ak4458_rates,
};

static int ak4458_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	int ret;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &ak4458_rate_constraints);

	return ret;
}

static const struct snd_soc_dai_ops ak4458_dai_ops = {
	.startup        = ak4458_startup,
	.hw_params	= ak4458_hw_params,
	.set_fmt	= ak4458_set_dai_fmt,
	.mute_stream	= ak4458_set_dai_mute,
	.set_tdm_slot	= ak4458_set_tdm_slot,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver ak4458_dai = {
	.name = "ak4458-aif",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = AK4458_FORMATS,
	},
	.ops = &ak4458_dai_ops,
};

static struct snd_soc_dai_driver ak4497_dai = {
	.name = "ak4497-aif",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = AK4458_FORMATS,
	},
	.ops = &ak4458_dai_ops,
};

static void ak4458_reset(struct ak4458_priv *ak4458, bool active)
{
	if (ak4458->reset_gpiod) {
		gpiod_set_value_cansleep(ak4458->reset_gpiod, active);
		usleep_range(1000, 2000);
	}
}

static int ak4458_init(struct snd_soc_component *component)
{
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);
	int ret;

	/* External Mute ON */
	if (ak4458->mute_gpiod)
		gpiod_set_value_cansleep(ak4458->mute_gpiod, 1);

	ak4458_reset(ak4458, false);

	ret = snd_soc_component_update_bits(component, AK4458_00_CONTROL1,
			    0x80, 0x80);   /* ACKS bit = 1; 10000000 */
	if (ret < 0)
		return ret;

	if (ak4458->drvdata->type == AK4497) {
		ret = snd_soc_component_update_bits(component, AK4458_09_DSD2,
						    0x4, (ak4458->dsd_path << 2));
		if (ret < 0)
			return ret;
	}

	return ak4458_rstn_control(component, 1);
}

static int ak4458_probe(struct snd_soc_component *component)
{
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);

	ak4458->fs = 48000;

	return ak4458_init(component);
}

static void ak4458_remove(struct snd_soc_component *component)
{
	struct ak4458_priv *ak4458 = snd_soc_component_get_drvdata(component);

	ak4458_reset(ak4458, true);
}

#ifdef CONFIG_PM
static int __maybe_unused ak4458_runtime_suspend(struct device *dev)
{
	struct ak4458_priv *ak4458 = dev_get_drvdata(dev);

	regcache_cache_only(ak4458->regmap, true);

	ak4458_reset(ak4458, true);

	if (ak4458->mute_gpiod)
		gpiod_set_value_cansleep(ak4458->mute_gpiod, 0);

	regulator_bulk_disable(ARRAY_SIZE(ak4458->supplies),
			       ak4458->supplies);
	return 0;
}

static int __maybe_unused ak4458_runtime_resume(struct device *dev)
{
	struct ak4458_priv *ak4458 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ak4458->supplies),
				    ak4458->supplies);
	if (ret != 0) {
		dev_err(ak4458->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	if (ak4458->mute_gpiod)
		gpiod_set_value_cansleep(ak4458->mute_gpiod, 1);

	ak4458_reset(ak4458, true);
	ak4458_reset(ak4458, false);

	regcache_cache_only(ak4458->regmap, false);
	regcache_mark_dirty(ak4458->regmap);

	return regcache_sync(ak4458->regmap);
}
#endif /* CONFIG_PM */

static const struct snd_soc_component_driver soc_codec_dev_ak4458 = {
	.probe			= ak4458_probe,
	.remove			= ak4458_remove,
	.controls		= ak4458_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4458_snd_controls),
	.dapm_widgets		= ak4458_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4458_dapm_widgets),
	.dapm_routes		= ak4458_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4458_intercon),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_component_driver soc_codec_dev_ak4497 = {
	.probe			= ak4458_probe,
	.remove			= ak4458_remove,
	.controls		= ak4497_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4497_snd_controls),
	.dapm_widgets		= ak4497_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4497_dapm_widgets),
	.dapm_routes		= ak4497_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4497_intercon),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak4458_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AK4458_14_R4CHATT,
	.reg_defaults = ak4458_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ak4458_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static const struct ak4458_drvdata ak4458_drvdata = {
	.dai_drv = &ak4458_dai,
	.comp_drv = &soc_codec_dev_ak4458,
	.type = AK4458,
};

static const struct ak4458_drvdata ak4497_drvdata = {
	.dai_drv = &ak4497_dai,
	.comp_drv = &soc_codec_dev_ak4497,
	.type = AK4497,
};

static const struct dev_pm_ops ak4458_pm = {
	SET_RUNTIME_PM_OPS(ak4458_runtime_suspend, ak4458_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static int ak4458_i2c_probe(struct i2c_client *i2c)
{
	struct ak4458_priv *ak4458;
	int ret, i;

	ak4458 = devm_kzalloc(&i2c->dev, sizeof(*ak4458), GFP_KERNEL);
	if (!ak4458)
		return -ENOMEM;

	ak4458->regmap = devm_regmap_init_i2c(i2c, &ak4458_regmap);
	if (IS_ERR(ak4458->regmap))
		return PTR_ERR(ak4458->regmap);

	i2c_set_clientdata(i2c, ak4458);
	ak4458->dev = &i2c->dev;

	ak4458->drvdata = of_device_get_match_data(&i2c->dev);

	ak4458->reset_gpiod = devm_gpiod_get_optional(ak4458->dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(ak4458->reset_gpiod))
		return PTR_ERR(ak4458->reset_gpiod);

	ak4458->mute_gpiod = devm_gpiod_get_optional(ak4458->dev, "mute",
						     GPIOD_OUT_LOW);
	if (IS_ERR(ak4458->mute_gpiod))
		return PTR_ERR(ak4458->mute_gpiod);

	/* Optional property for ak4497 */
	of_property_read_u32(i2c->dev.of_node, "dsd-path", &ak4458->dsd_path);

	for (i = 0; i < ARRAY_SIZE(ak4458->supplies); i++)
		ak4458->supplies[i].supply = ak4458_supply_names[i];

	ret = devm_regulator_bulk_get(ak4458->dev, ARRAY_SIZE(ak4458->supplies),
				      ak4458->supplies);
	if (ret != 0) {
		dev_err(ak4458->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(ak4458->dev,
					      ak4458->drvdata->comp_drv,
					      ak4458->drvdata->dai_drv, 1);
	if (ret < 0) {
		dev_err(ak4458->dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&i2c->dev);
	regcache_cache_only(ak4458->regmap, true);

	return 0;
}

static int ak4458_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);

	return 0;
}

static const struct of_device_id ak4458_of_match[] = {
	{ .compatible = "asahi-kasei,ak4458", .data = &ak4458_drvdata},
	{ .compatible = "asahi-kasei,ak4497", .data = &ak4497_drvdata},
	{ },
};
MODULE_DEVICE_TABLE(of, ak4458_of_match);

static struct i2c_driver ak4458_i2c_driver = {
	.driver = {
		.name = "ak4458",
		.pm = &ak4458_pm,
		.of_match_table = ak4458_of_match,
		},
	.probe_new = ak4458_i2c_probe,
	.remove = ak4458_i2c_remove,
};

module_i2c_driver(ak4458_i2c_driver);

MODULE_AUTHOR("Junichi Wakasugi <wakasugi.jb@om.asahi-kasei.co.jp>");
MODULE_AUTHOR("Mihai Serban <mihai.serban@nxp.com>");
MODULE_DESCRIPTION("ASoC AK4458 DAC driver");
MODULE_LICENSE("GPL v2");
