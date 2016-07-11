/*
 * rt5670.c  --  RT5670 ALSA SoC audio codec driver
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/spi/spi.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5670.h>

#include "rl6231.h"
#include "rt5670.h"
#include "rt5670-dsp.h"

#define RT5670_DEVICE_ID 0x6271

#define RT5670_PR_RANGE_BASE (0xff + 1)
#define RT5670_PR_SPACING 0x100

#define RT5670_PR_BASE (RT5670_PR_RANGE_BASE + (0 * RT5670_PR_SPACING))

static const struct regmap_range_cfg rt5670_ranges[] = {
	{ .name = "PR", .range_min = RT5670_PR_BASE,
	  .range_max = RT5670_PR_BASE + 0xf8,
	  .selector_reg = RT5670_PRIV_INDEX,
	  .selector_mask = 0xff,
	  .selector_shift = 0x0,
	  .window_start = RT5670_PRIV_DATA,
	  .window_len = 0x1, },
};

static const struct reg_sequence init_list[] = {
	{ RT5670_PR_BASE + 0x14, 0x9a8a },
	{ RT5670_PR_BASE + 0x38, 0x3ba1 },
	{ RT5670_PR_BASE + 0x3d, 0x3640 },
};

static const struct reg_default rt5670_reg[] = {
	{ 0x00, 0x0000 },
	{ 0x02, 0x8888 },
	{ 0x03, 0x8888 },
	{ 0x0a, 0x0001 },
	{ 0x0b, 0x0827 },
	{ 0x0c, 0x0000 },
	{ 0x0d, 0x0008 },
	{ 0x0e, 0x0000 },
	{ 0x0f, 0x0808 },
	{ 0x19, 0xafaf },
	{ 0x1a, 0xafaf },
	{ 0x1b, 0x0011 },
	{ 0x1c, 0x2f2f },
	{ 0x1d, 0x2f2f },
	{ 0x1e, 0x0000 },
	{ 0x1f, 0x2f2f },
	{ 0x20, 0x0000 },
	{ 0x26, 0x7860 },
	{ 0x27, 0x7860 },
	{ 0x28, 0x7871 },
	{ 0x29, 0x8080 },
	{ 0x2a, 0x5656 },
	{ 0x2b, 0x5454 },
	{ 0x2c, 0xaaa0 },
	{ 0x2d, 0x0000 },
	{ 0x2e, 0x2f2f },
	{ 0x2f, 0x1002 },
	{ 0x30, 0x0000 },
	{ 0x31, 0x5f00 },
	{ 0x32, 0x0000 },
	{ 0x33, 0x0000 },
	{ 0x34, 0x0000 },
	{ 0x35, 0x0000 },
	{ 0x36, 0x0000 },
	{ 0x37, 0x0000 },
	{ 0x38, 0x0000 },
	{ 0x3b, 0x0000 },
	{ 0x3c, 0x007f },
	{ 0x3d, 0x0000 },
	{ 0x3e, 0x007f },
	{ 0x45, 0xe00f },
	{ 0x4c, 0x5380 },
	{ 0x4f, 0x0073 },
	{ 0x52, 0x00d3 },
	{ 0x53, 0xf000 },
	{ 0x61, 0x0000 },
	{ 0x62, 0x0001 },
	{ 0x63, 0x00c3 },
	{ 0x64, 0x0000 },
	{ 0x65, 0x0001 },
	{ 0x66, 0x0000 },
	{ 0x6f, 0x8000 },
	{ 0x70, 0x8000 },
	{ 0x71, 0x8000 },
	{ 0x72, 0x8000 },
	{ 0x73, 0x7770 },
	{ 0x74, 0x0e00 },
	{ 0x75, 0x1505 },
	{ 0x76, 0x0015 },
	{ 0x77, 0x0c00 },
	{ 0x78, 0x4000 },
	{ 0x79, 0x0123 },
	{ 0x7f, 0x1100 },
	{ 0x80, 0x0000 },
	{ 0x81, 0x0000 },
	{ 0x82, 0x0000 },
	{ 0x83, 0x0000 },
	{ 0x84, 0x0000 },
	{ 0x85, 0x0000 },
	{ 0x86, 0x0004 },
	{ 0x87, 0x0000 },
	{ 0x88, 0x0000 },
	{ 0x89, 0x0000 },
	{ 0x8a, 0x0000 },
	{ 0x8b, 0x0000 },
	{ 0x8c, 0x0003 },
	{ 0x8d, 0x0000 },
	{ 0x8e, 0x0004 },
	{ 0x8f, 0x1100 },
	{ 0x90, 0x0646 },
	{ 0x91, 0x0c06 },
	{ 0x93, 0x0000 },
	{ 0x94, 0x1270 },
	{ 0x95, 0x1000 },
	{ 0x97, 0x0000 },
	{ 0x98, 0x0000 },
	{ 0x99, 0x0000 },
	{ 0x9a, 0x2184 },
	{ 0x9b, 0x010a },
	{ 0x9c, 0x0aea },
	{ 0x9d, 0x000c },
	{ 0x9e, 0x0400 },
	{ 0xae, 0x7000 },
	{ 0xaf, 0x0000 },
	{ 0xb0, 0x7000 },
	{ 0xb1, 0x0000 },
	{ 0xb2, 0x0000 },
	{ 0xb3, 0x001f },
	{ 0xb4, 0x220c },
	{ 0xb5, 0x1f00 },
	{ 0xb6, 0x0000 },
	{ 0xb7, 0x0000 },
	{ 0xbb, 0x0000 },
	{ 0xbc, 0x0000 },
	{ 0xbd, 0x0000 },
	{ 0xbe, 0x0000 },
	{ 0xbf, 0x0000 },
	{ 0xc0, 0x0000 },
	{ 0xc1, 0x0000 },
	{ 0xc2, 0x0000 },
	{ 0xcd, 0x0000 },
	{ 0xce, 0x0000 },
	{ 0xcf, 0x1813 },
	{ 0xd0, 0x0690 },
	{ 0xd1, 0x1c17 },
	{ 0xd3, 0xa220 },
	{ 0xd4, 0x0000 },
	{ 0xd6, 0x0400 },
	{ 0xd9, 0x0809 },
	{ 0xda, 0x0000 },
	{ 0xdb, 0x0001 },
	{ 0xdc, 0x0049 },
	{ 0xdd, 0x0024 },
	{ 0xe6, 0x8000 },
	{ 0xe7, 0x0000 },
	{ 0xec, 0xa200 },
	{ 0xed, 0x0000 },
	{ 0xee, 0xa200 },
	{ 0xef, 0x0000 },
	{ 0xf8, 0x0000 },
	{ 0xf9, 0x0000 },
	{ 0xfa, 0x8010 },
	{ 0xfb, 0x0033 },
	{ 0xfc, 0x0100 },
};

static bool rt5670_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5670_ranges); i++) {
		if ((reg >= rt5670_ranges[i].window_start &&
		     reg <= rt5670_ranges[i].window_start +
		     rt5670_ranges[i].window_len) ||
		    (reg >= rt5670_ranges[i].range_min &&
		     reg <= rt5670_ranges[i].range_max)) {
			return true;
		}
	}

	switch (reg) {
	case RT5670_RESET:
	case RT5670_PDM_DATA_CTRL1:
	case RT5670_PDM1_DATA_CTRL4:
	case RT5670_PDM2_DATA_CTRL4:
	case RT5670_PRIV_DATA:
	case RT5670_ASRC_5:
	case RT5670_CJ_CTRL1:
	case RT5670_CJ_CTRL2:
	case RT5670_CJ_CTRL3:
	case RT5670_A_JD_CTRL1:
	case RT5670_A_JD_CTRL2:
	case RT5670_VAD_CTRL5:
	case RT5670_ADC_EQ_CTRL1:
	case RT5670_EQ_CTRL1:
	case RT5670_ALC_CTRL_1:
	case RT5670_IRQ_CTRL2:
	case RT5670_INT_IRQ_ST:
	case RT5670_IL_CMD:
	case RT5670_DSP_CTRL1:
	case RT5670_DSP_CTRL2:
	case RT5670_DSP_CTRL3:
	case RT5670_DSP_CTRL4:
	case RT5670_DSP_CTRL5:
	case RT5670_VENDOR_ID:
	case RT5670_VENDOR_ID1:
	case RT5670_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool rt5670_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5670_ranges); i++) {
		if ((reg >= rt5670_ranges[i].window_start &&
		     reg <= rt5670_ranges[i].window_start +
		     rt5670_ranges[i].window_len) ||
		    (reg >= rt5670_ranges[i].range_min &&
		     reg <= rt5670_ranges[i].range_max)) {
			return true;
		}
	}

	switch (reg) {
	case RT5670_RESET:
	case RT5670_HP_VOL:
	case RT5670_LOUT1:
	case RT5670_CJ_CTRL1:
	case RT5670_CJ_CTRL2:
	case RT5670_CJ_CTRL3:
	case RT5670_IN2:
	case RT5670_INL1_INR1_VOL:
	case RT5670_DAC1_DIG_VOL:
	case RT5670_DAC2_DIG_VOL:
	case RT5670_DAC_CTRL:
	case RT5670_STO1_ADC_DIG_VOL:
	case RT5670_MONO_ADC_DIG_VOL:
	case RT5670_STO2_ADC_DIG_VOL:
	case RT5670_ADC_BST_VOL1:
	case RT5670_ADC_BST_VOL2:
	case RT5670_STO2_ADC_MIXER:
	case RT5670_STO1_ADC_MIXER:
	case RT5670_MONO_ADC_MIXER:
	case RT5670_AD_DA_MIXER:
	case RT5670_STO_DAC_MIXER:
	case RT5670_DD_MIXER:
	case RT5670_DIG_MIXER:
	case RT5670_DSP_PATH1:
	case RT5670_DSP_PATH2:
	case RT5670_DIG_INF1_DATA:
	case RT5670_DIG_INF2_DATA:
	case RT5670_PDM_OUT_CTRL:
	case RT5670_PDM_DATA_CTRL1:
	case RT5670_PDM1_DATA_CTRL2:
	case RT5670_PDM1_DATA_CTRL3:
	case RT5670_PDM1_DATA_CTRL4:
	case RT5670_PDM2_DATA_CTRL2:
	case RT5670_PDM2_DATA_CTRL3:
	case RT5670_PDM2_DATA_CTRL4:
	case RT5670_REC_L1_MIXER:
	case RT5670_REC_L2_MIXER:
	case RT5670_REC_R1_MIXER:
	case RT5670_REC_R2_MIXER:
	case RT5670_HPO_MIXER:
	case RT5670_MONO_MIXER:
	case RT5670_OUT_L1_MIXER:
	case RT5670_OUT_R1_MIXER:
	case RT5670_LOUT_MIXER:
	case RT5670_PWR_DIG1:
	case RT5670_PWR_DIG2:
	case RT5670_PWR_ANLG1:
	case RT5670_PWR_ANLG2:
	case RT5670_PWR_MIXER:
	case RT5670_PWR_VOL:
	case RT5670_PRIV_INDEX:
	case RT5670_PRIV_DATA:
	case RT5670_I2S4_SDP:
	case RT5670_I2S1_SDP:
	case RT5670_I2S2_SDP:
	case RT5670_I2S3_SDP:
	case RT5670_ADDA_CLK1:
	case RT5670_ADDA_CLK2:
	case RT5670_DMIC_CTRL1:
	case RT5670_DMIC_CTRL2:
	case RT5670_TDM_CTRL_1:
	case RT5670_TDM_CTRL_2:
	case RT5670_TDM_CTRL_3:
	case RT5670_DSP_CLK:
	case RT5670_GLB_CLK:
	case RT5670_PLL_CTRL1:
	case RT5670_PLL_CTRL2:
	case RT5670_ASRC_1:
	case RT5670_ASRC_2:
	case RT5670_ASRC_3:
	case RT5670_ASRC_4:
	case RT5670_ASRC_5:
	case RT5670_ASRC_7:
	case RT5670_ASRC_8:
	case RT5670_ASRC_9:
	case RT5670_ASRC_10:
	case RT5670_ASRC_11:
	case RT5670_ASRC_12:
	case RT5670_ASRC_13:
	case RT5670_ASRC_14:
	case RT5670_DEPOP_M1:
	case RT5670_DEPOP_M2:
	case RT5670_DEPOP_M3:
	case RT5670_CHARGE_PUMP:
	case RT5670_MICBIAS:
	case RT5670_A_JD_CTRL1:
	case RT5670_A_JD_CTRL2:
	case RT5670_VAD_CTRL1:
	case RT5670_VAD_CTRL2:
	case RT5670_VAD_CTRL3:
	case RT5670_VAD_CTRL4:
	case RT5670_VAD_CTRL5:
	case RT5670_ADC_EQ_CTRL1:
	case RT5670_ADC_EQ_CTRL2:
	case RT5670_EQ_CTRL1:
	case RT5670_EQ_CTRL2:
	case RT5670_ALC_DRC_CTRL1:
	case RT5670_ALC_DRC_CTRL2:
	case RT5670_ALC_CTRL_1:
	case RT5670_ALC_CTRL_2:
	case RT5670_ALC_CTRL_3:
	case RT5670_JD_CTRL:
	case RT5670_IRQ_CTRL1:
	case RT5670_IRQ_CTRL2:
	case RT5670_INT_IRQ_ST:
	case RT5670_GPIO_CTRL1:
	case RT5670_GPIO_CTRL2:
	case RT5670_GPIO_CTRL3:
	case RT5670_SCRABBLE_FUN:
	case RT5670_SCRABBLE_CTRL:
	case RT5670_BASE_BACK:
	case RT5670_MP3_PLUS1:
	case RT5670_MP3_PLUS2:
	case RT5670_ADJ_HPF1:
	case RT5670_ADJ_HPF2:
	case RT5670_HP_CALIB_AMP_DET:
	case RT5670_SV_ZCD1:
	case RT5670_SV_ZCD2:
	case RT5670_IL_CMD:
	case RT5670_IL_CMD2:
	case RT5670_IL_CMD3:
	case RT5670_DRC_HL_CTRL1:
	case RT5670_DRC_HL_CTRL2:
	case RT5670_ADC_MONO_HP_CTRL1:
	case RT5670_ADC_MONO_HP_CTRL2:
	case RT5670_ADC_STO2_HP_CTRL1:
	case RT5670_ADC_STO2_HP_CTRL2:
	case RT5670_JD_CTRL3:
	case RT5670_JD_CTRL4:
	case RT5670_DIG_MISC:
	case RT5670_DSP_CTRL1:
	case RT5670_DSP_CTRL2:
	case RT5670_DSP_CTRL3:
	case RT5670_DSP_CTRL4:
	case RT5670_DSP_CTRL5:
	case RT5670_GEN_CTRL2:
	case RT5670_GEN_CTRL3:
	case RT5670_VENDOR_ID:
	case RT5670_VENDOR_ID1:
	case RT5670_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

/**
 * rt5670_headset_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */

static int rt5670_headset_detect(struct snd_soc_codec *codec, int jack_insert)
{
	int val;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	if (jack_insert) {
		snd_soc_dapm_force_enable_pin(dapm, "Mic Det Power");
		snd_soc_dapm_sync(dapm);
		snd_soc_update_bits(codec, RT5670_GEN_CTRL3, 0x4, 0x0);
		snd_soc_update_bits(codec, RT5670_CJ_CTRL2,
			RT5670_CBJ_DET_MODE | RT5670_CBJ_MN_JD,
			RT5670_CBJ_MN_JD);
		snd_soc_write(codec, RT5670_GPIO_CTRL2, 0x0004);
		snd_soc_update_bits(codec, RT5670_GPIO_CTRL1,
			RT5670_GP1_PIN_MASK, RT5670_GP1_PIN_IRQ);
		snd_soc_update_bits(codec, RT5670_CJ_CTRL1,
			RT5670_CBJ_BST1_EN, RT5670_CBJ_BST1_EN);
		snd_soc_write(codec, RT5670_JD_CTRL3, 0x00f0);
		snd_soc_update_bits(codec, RT5670_CJ_CTRL2,
			RT5670_CBJ_MN_JD, RT5670_CBJ_MN_JD);
		snd_soc_update_bits(codec, RT5670_CJ_CTRL2,
			RT5670_CBJ_MN_JD, 0);
		msleep(300);
		val = snd_soc_read(codec, RT5670_CJ_CTRL3) & 0x7;
		if (val == 0x1 || val == 0x2) {
			rt5670->jack_type = SND_JACK_HEADSET;
			/* for push button */
			snd_soc_update_bits(codec, RT5670_INT_IRQ_ST, 0x8, 0x8);
			snd_soc_update_bits(codec, RT5670_IL_CMD, 0x40, 0x40);
			snd_soc_read(codec, RT5670_IL_CMD);
		} else {
			snd_soc_update_bits(codec, RT5670_GEN_CTRL3, 0x4, 0x4);
			rt5670->jack_type = SND_JACK_HEADPHONE;
			snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
			snd_soc_dapm_sync(dapm);
		}
	} else {
		snd_soc_update_bits(codec, RT5670_INT_IRQ_ST, 0x8, 0x0);
		snd_soc_update_bits(codec, RT5670_GEN_CTRL3, 0x4, 0x4);
		rt5670->jack_type = 0;
		snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
		snd_soc_dapm_sync(dapm);
	}

	return rt5670->jack_type;
}

void rt5670_jack_suspend(struct snd_soc_codec *codec)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	rt5670->jack_type_saved = rt5670->jack_type;
	rt5670_headset_detect(codec, 0);
}
EXPORT_SYMBOL_GPL(rt5670_jack_suspend);

void rt5670_jack_resume(struct snd_soc_codec *codec)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	if (rt5670->jack_type_saved)
		rt5670_headset_detect(codec, 1);
}
EXPORT_SYMBOL_GPL(rt5670_jack_resume);

static int rt5670_button_detect(struct snd_soc_codec *codec)
{
	int btn_type, val;

	val = snd_soc_read(codec, RT5670_IL_CMD);
	btn_type = val & 0xff80;
	snd_soc_write(codec, RT5670_IL_CMD, val);
	if (btn_type != 0) {
		msleep(20);
		val = snd_soc_read(codec, RT5670_IL_CMD);
		snd_soc_write(codec, RT5670_IL_CMD, val);
	}

	return btn_type;
}

static int rt5670_irq_detection(void *data)
{
	struct rt5670_priv *rt5670 = (struct rt5670_priv *)data;
	struct snd_soc_jack_gpio *gpio = &rt5670->hp_gpio;
	struct snd_soc_jack *jack = rt5670->jack;
	int val, btn_type, report = jack->status;

	if (rt5670->pdata.jd_mode == 1) /* 2 port */
		val = snd_soc_read(rt5670->codec, RT5670_A_JD_CTRL1) & 0x0070;
	else
		val = snd_soc_read(rt5670->codec, RT5670_A_JD_CTRL1) & 0x0020;

	switch (val) {
	/* jack in */
	case 0x30: /* 2 port */
	case 0x0: /* 1 port or 2 port */
		if (rt5670->jack_type == 0) {
			report = rt5670_headset_detect(rt5670->codec, 1);
			/* for push button and jack out */
			gpio->debounce_time = 25;
			break;
		}
		btn_type = 0;
		if (snd_soc_read(rt5670->codec, RT5670_INT_IRQ_ST) & 0x4) {
			/* button pressed */
			report = SND_JACK_HEADSET;
			btn_type = rt5670_button_detect(rt5670->codec);
			switch (btn_type) {
			case 0x2000: /* up */
				report |= SND_JACK_BTN_1;
				break;
			case 0x0400: /* center */
				report |= SND_JACK_BTN_0;
				break;
			case 0x0080: /* down */
				report |= SND_JACK_BTN_2;
				break;
			default:
				dev_err(rt5670->codec->dev,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}
		}
		if (btn_type == 0)/* button release */
			report =  rt5670->jack_type;

		break;
	/* jack out */
	case 0x70: /* 2 port */
	case 0x10: /* 2 port */
	case 0x20: /* 1 port */
		report = 0;
		snd_soc_update_bits(rt5670->codec, RT5670_INT_IRQ_ST, 0x1, 0x0);
		rt5670_headset_detect(rt5670->codec, 0);
		gpio->debounce_time = 150; /* for jack in */
		break;
	default:
		break;
	}

	return report;
}

int rt5670_set_jack_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *jack)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	int ret;

	rt5670->jack = jack;
	rt5670->hp_gpio.gpiod_dev = codec->dev;
	rt5670->hp_gpio.name = "headphone detect";
	rt5670->hp_gpio.report = SND_JACK_HEADSET |
		SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2;
	rt5670->hp_gpio.debounce_time = 150;
	rt5670->hp_gpio.wake = true;
	rt5670->hp_gpio.data = (struct rt5670_priv *)rt5670;
	rt5670->hp_gpio.jack_status_check = rt5670_irq_detection;

	ret = snd_soc_jack_add_gpios(rt5670->jack, 1,
			&rt5670->hp_gpio);
	if (ret) {
		dev_err(codec->dev, "Adding jack GPIO failed\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt5670_set_jack_detect);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static const DECLARE_TLV_DB_RANGE(bst_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0)
);

/* Interface data select */
static const char * const rt5670_data_select[] = {
	"Normal", "Swap", "left copy to right", "right copy to left"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if2_dac_enum, RT5670_DIG_INF1_DATA,
				RT5670_IF2_DAC_SEL_SFT, rt5670_data_select);

static SOC_ENUM_SINGLE_DECL(rt5670_if2_adc_enum, RT5670_DIG_INF1_DATA,
				RT5670_IF2_ADC_SEL_SFT, rt5670_data_select);

static const struct snd_kcontrol_new rt5670_snd_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE("HP Playback Switch", RT5670_HP_VOL,
		RT5670_L_MUTE_SFT, RT5670_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("HP Playback Volume", RT5670_HP_VOL,
		RT5670_L_VOL_SFT, RT5670_R_VOL_SFT,
		39, 1, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE("OUT Channel Switch", RT5670_LOUT1,
		RT5670_VOL_L_SFT, RT5670_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5670_LOUT1,
		RT5670_L_VOL_SFT, RT5670_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* DAC Digital Volume */
	SOC_DOUBLE("DAC2 Playback Switch", RT5670_DAC_CTRL,
		RT5670_M_DAC_L2_VOL_SFT, RT5670_M_DAC_R2_VOL_SFT, 1, 1),
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5670_DAC1_DIG_VOL,
			RT5670_L_VOL_SFT, RT5670_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("Mono DAC Playback Volume", RT5670_DAC2_DIG_VOL,
			RT5670_L_VOL_SFT, RT5670_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	/* IN1/IN2 Control */
	SOC_SINGLE_TLV("IN1 Boost Volume", RT5670_CJ_CTRL1,
		RT5670_BST_SFT1, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost Volume", RT5670_IN2,
		RT5670_BST_SFT1, 8, 0, bst_tlv),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5670_INL1_INR1_VOL,
			RT5670_INL_VOL_SFT, RT5670_INR_VOL_SFT,
			31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5670_STO1_ADC_DIG_VOL,
		RT5670_L_MUTE_SFT, RT5670_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5670_STO1_ADC_DIG_VOL,
			RT5670_L_VOL_SFT, RT5670_R_VOL_SFT,
			127, 0, adc_vol_tlv),

	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5670_MONO_ADC_DIG_VOL,
			RT5670_L_VOL_SFT, RT5670_R_VOL_SFT,
			127, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5670_ADC_BST_VOL1,
			RT5670_STO1_ADC_L_BST_SFT, RT5670_STO1_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("STO2 ADC Boost Gain Volume", RT5670_ADC_BST_VOL1,
			RT5670_STO2_ADC_L_BST_SFT, RT5670_STO2_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),

	SOC_ENUM("ADC IF2 Data Switch", rt5670_if2_adc_enum),
	SOC_ENUM("DAC IF2 Data Switch", rt5670_if2_dac_enum),
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	int idx, rate;

	rate = rt5670->sysclk / rl6231_get_pre_div(rt5670->regmap,
		RT5670_ADDA_CLK1, RT5670_I2S_PD1_SFT);
	idx = rl6231_calc_dmic_clk(rate);
	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_update_bits(codec, RT5670_DMIC_CTRL1,
			RT5670_DMIC_CLK_MASK, idx << RT5670_DMIC_CLK_SFT);
	return idx;
}

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	if (rt5670->sysclk_src == RT5670_SCLK_S_PLL1)
		return 1;
	else
		return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	unsigned int reg, shift, val;

	switch (source->shift) {
	case 0:
		reg = RT5670_ASRC_3;
		shift = 0;
		break;
	case 1:
		reg = RT5670_ASRC_3;
		shift = 4;
		break;
	case 2:
		reg = RT5670_ASRC_5;
		shift = 12;
		break;
	case 3:
		reg = RT5670_ASRC_2;
		shift = 0;
		break;
	case 8:
		reg = RT5670_ASRC_2;
		shift = 4;
		break;
	case 9:
		reg = RT5670_ASRC_2;
		shift = 8;
		break;
	case 10:
		reg = RT5670_ASRC_2;
		shift = 12;
		break;
	default:
		return 0;
	}

	val = (snd_soc_read(codec, reg) >> shift) & 0xf;
	switch (val) {
	case 1:
	case 2:
	case 3:
	case 4:
		return 1;
	default:
		return 0;
	}

}

static int can_use_asrc(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	if (rt5670->sysclk > rt5670->lrck[RT5670_AIF1] * 384)
		return 1;

	return 0;
}


/**
 * rt5670_sel_asrc_clk_src - select ASRC clock source for a set of filters
 * @codec: SoC audio codec device.
 * @filter_mask: mask of filters.
 * @clk_src: clock source
 *
 * The ASRC function is for asynchronous MCLK and LRCK. Also, since RT5670 can
 * only support standard 32fs or 64fs i2s format, ASRC should be enabled to
 * support special i2s clock format such as Intel's 100fs(100 * sampling rate).
 * ASRC function will track i2s clock and generate a corresponding system clock
 * for codec. This function provides an API to select the clock source for a
 * set of filters specified by the mask. And the codec driver will turn on ASRC
 * for these filters if ASRC is selected as their clock source.
 */
int rt5670_sel_asrc_clk_src(struct snd_soc_codec *codec,
			    unsigned int filter_mask, unsigned int clk_src)
{
	unsigned int asrc2_mask = 0, asrc2_value = 0;
	unsigned int asrc3_mask = 0, asrc3_value = 0;

	if (clk_src > RT5670_CLK_SEL_SYS3)
		return -EINVAL;

	if (filter_mask & RT5670_DA_STEREO_FILTER) {
		asrc2_mask |= RT5670_DA_STO_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5670_DA_STO_CLK_SEL_MASK)
				| (clk_src <<  RT5670_DA_STO_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_DA_MONO_L_FILTER) {
		asrc2_mask |= RT5670_DA_MONOL_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5670_DA_MONOL_CLK_SEL_MASK)
				| (clk_src <<  RT5670_DA_MONOL_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_DA_MONO_R_FILTER) {
		asrc2_mask |= RT5670_DA_MONOR_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5670_DA_MONOR_CLK_SEL_MASK)
				| (clk_src <<  RT5670_DA_MONOR_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_AD_STEREO_FILTER) {
		asrc2_mask |= RT5670_AD_STO1_CLK_SEL_MASK;
		asrc2_value = (asrc2_value & ~RT5670_AD_STO1_CLK_SEL_MASK)
				| (clk_src <<  RT5670_AD_STO1_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_AD_MONO_L_FILTER) {
		asrc3_mask |= RT5670_AD_MONOL_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5670_AD_MONOL_CLK_SEL_MASK)
				| (clk_src <<  RT5670_AD_MONOL_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_AD_MONO_R_FILTER)  {
		asrc3_mask |= RT5670_AD_MONOR_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5670_AD_MONOR_CLK_SEL_MASK)
				| (clk_src <<  RT5670_AD_MONOR_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_UP_RATE_FILTER) {
		asrc3_mask |= RT5670_UP_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5670_UP_CLK_SEL_MASK)
				| (clk_src <<  RT5670_UP_CLK_SEL_SFT);
	}

	if (filter_mask & RT5670_DOWN_RATE_FILTER) {
		asrc3_mask |= RT5670_DOWN_CLK_SEL_MASK;
		asrc3_value = (asrc3_value & ~RT5670_DOWN_CLK_SEL_MASK)
				| (clk_src <<  RT5670_DOWN_CLK_SEL_SFT);
	}

	if (asrc2_mask)
		snd_soc_update_bits(codec, RT5670_ASRC_2,
				    asrc2_mask, asrc2_value);

	if (asrc3_mask)
		snd_soc_update_bits(codec, RT5670_ASRC_3,
				    asrc3_mask, asrc3_value);
	return 0;
}
EXPORT_SYMBOL_GPL(rt5670_sel_asrc_clk_src);

/* Digital Mixer */
static const struct snd_kcontrol_new rt5670_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_STO1_ADC_MIXER,
			RT5670_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_STO1_ADC_MIXER,
			RT5670_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_STO1_ADC_MIXER,
			RT5670_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_STO1_ADC_MIXER,
			RT5670_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_sto2_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_STO2_ADC_MIXER,
			RT5670_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_STO2_ADC_MIXER,
			RT5670_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_sto2_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_STO2_ADC_MIXER,
			RT5670_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_STO2_ADC_MIXER,
			RT5670_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_MONO_ADC_MIXER,
			RT5670_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_MONO_ADC_MIXER,
			RT5670_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5670_MONO_ADC_MIXER,
			RT5670_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5670_MONO_ADC_MIXER,
			RT5670_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5670_AD_DA_MIXER,
			RT5670_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5670_AD_DA_MIXER,
			RT5670_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5670_AD_DA_MIXER,
			RT5670_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5670_AD_DA_MIXER,
			RT5670_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_L2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_R1_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_R2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_STO_DAC_MIXER,
			RT5670_M_DAC_L1_STO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_R2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_DD_MIXER,
			RT5670_M_DAC_L2_MONO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_dig_l_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix L Switch", RT5670_DIG_MIXER,
			RT5670_M_STO_L_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_DIG_MIXER,
			RT5670_M_DAC_L2_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_DIG_MIXER,
			RT5670_M_DAC_R2_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_dig_r_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix R Switch", RT5670_DIG_MIXER,
			RT5670_M_STO_R_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_DIG_MIXER,
			RT5670_M_DAC_R2_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_DIG_MIXER,
			RT5670_M_DAC_L2_DAC_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5670_rec_l_mix[] = {
	SOC_DAPM_SINGLE("INL Switch", RT5670_REC_L2_MIXER,
			RT5670_M_IN_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5670_REC_L2_MIXER,
			RT5670_M_BST2_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5670_REC_L2_MIXER,
			RT5670_M_BST1_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_rec_r_mix[] = {
	SOC_DAPM_SINGLE("INR Switch", RT5670_REC_R2_MIXER,
			RT5670_M_IN_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5670_REC_R2_MIXER,
			RT5670_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5670_REC_R2_MIXER,
			RT5670_M_BST1_RM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST1 Switch", RT5670_OUT_L1_MIXER,
			RT5670_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5670_OUT_L1_MIXER,
			RT5670_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5670_OUT_L1_MIXER,
			RT5670_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_OUT_L1_MIXER,
			RT5670_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5670_OUT_R1_MIXER,
			RT5670_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5670_OUT_R1_MIXER,
			RT5670_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5670_OUT_R1_MIXER,
			RT5670_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_OUT_R1_MIXER,
			RT5670_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_hpo_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5670_HPO_MIXER,
			RT5670_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPVOL Switch", RT5670_HPO_MIXER,
			RT5670_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_hpvoll_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5670_HPO_MIXER,
			RT5670_M_DACL1_HML_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5670_HPO_MIXER,
			RT5670_M_INL1_HML_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_hpvolr_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5670_HPO_MIXER,
			RT5670_M_DACR1_HMR_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5670_HPO_MIXER,
			RT5670_M_INR1_HMR_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_LOUT_MIXER,
			RT5670_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_LOUT_MIXER,
			RT5670_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIX L Switch", RT5670_LOUT_MIXER,
			RT5670_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIX R Switch", RT5670_LOUT_MIXER,
			RT5670_M_OV_R_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_hpl_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5670_HPO_MIXER,
			RT5670_M_DACL1_HML_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL1 Switch", RT5670_HPO_MIXER,
			RT5670_M_INL1_HML_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5670_hpr_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5670_HPO_MIXER,
			RT5670_M_DACR1_HMR_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR1 Switch", RT5670_HPO_MIXER,
			RT5670_M_INR1_HMR_SFT, 1, 1),
};

static const struct snd_kcontrol_new lout_l_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5670_LOUT1,
		RT5670_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new lout_r_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5670_LOUT1,
		RT5670_R_MUTE_SFT, 1, 1);

/* DAC1 L/R source */ /* MX-29 [9:8] [11:10] */
static const char * const rt5670_dac1_src[] = {
	"IF1 DAC", "IF2 DAC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_dac1l_enum, RT5670_AD_DA_MIXER,
	RT5670_DAC1_L_SEL_SFT, rt5670_dac1_src);

static const struct snd_kcontrol_new rt5670_dac1l_mux =
	SOC_DAPM_ENUM("DAC1 L source", rt5670_dac1l_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_dac1r_enum, RT5670_AD_DA_MIXER,
	RT5670_DAC1_R_SEL_SFT, rt5670_dac1_src);

static const struct snd_kcontrol_new rt5670_dac1r_mux =
	SOC_DAPM_ENUM("DAC1 R source", rt5670_dac1r_enum);

/*DAC2 L/R source*/ /* MX-1B [6:4] [2:0] */
/* TODO Use SOC_VALUE_ENUM_SINGLE_DECL */
static const char * const rt5670_dac12_src[] = {
	"IF1 DAC", "IF2 DAC", "IF3 DAC", "TxDC DAC",
	"Bass", "VAD_ADC", "IF4 DAC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_dac2l_enum, RT5670_DAC_CTRL,
	RT5670_DAC2_L_SEL_SFT, rt5670_dac12_src);

static const struct snd_kcontrol_new rt5670_dac_l2_mux =
	SOC_DAPM_ENUM("DAC2 L source", rt5670_dac2l_enum);

static const char * const rt5670_dacr2_src[] = {
	"IF1 DAC", "IF2 DAC", "IF3 DAC", "TxDC DAC", "TxDP ADC", "IF4 DAC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_dac2r_enum, RT5670_DAC_CTRL,
	RT5670_DAC2_R_SEL_SFT, rt5670_dacr2_src);

static const struct snd_kcontrol_new rt5670_dac_r2_mux =
	SOC_DAPM_ENUM("DAC2 R source", rt5670_dac2r_enum);

/*RxDP source*/ /* MX-2D [15:13] */
static const char * const rt5670_rxdp_src[] = {
	"IF2 DAC", "IF1 DAC", "STO1 ADC Mixer", "STO2 ADC Mixer",
	"Mono ADC Mixer L", "Mono ADC Mixer R", "DAC1"
};

static SOC_ENUM_SINGLE_DECL(rt5670_rxdp_enum, RT5670_DSP_PATH1,
	RT5670_RXDP_SEL_SFT, rt5670_rxdp_src);

static const struct snd_kcontrol_new rt5670_rxdp_mux =
	SOC_DAPM_ENUM("DAC2 L source", rt5670_rxdp_enum);

/* MX-2D [1] [0] */
static const char * const rt5670_dsp_bypass_src[] = {
	"DSP", "Bypass"
};

static SOC_ENUM_SINGLE_DECL(rt5670_dsp_ul_enum, RT5670_DSP_PATH1,
	RT5670_DSP_UL_SFT, rt5670_dsp_bypass_src);

static const struct snd_kcontrol_new rt5670_dsp_ul_mux =
	SOC_DAPM_ENUM("DSP UL source", rt5670_dsp_ul_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_dsp_dl_enum, RT5670_DSP_PATH1,
	RT5670_DSP_DL_SFT, rt5670_dsp_bypass_src);

static const struct snd_kcontrol_new rt5670_dsp_dl_mux =
	SOC_DAPM_ENUM("DSP DL source", rt5670_dsp_dl_enum);

/* Stereo2 ADC source */
/* MX-26 [15] */
static const char * const rt5670_stereo2_adc_lr_src[] = {
	"L", "LR"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo2_adc_lr_enum, RT5670_STO2_ADC_MIXER,
	RT5670_STO2_ADC_SRC_SFT, rt5670_stereo2_adc_lr_src);

static const struct snd_kcontrol_new rt5670_sto2_adc_lr_mux =
	SOC_DAPM_ENUM("Stereo2 ADC LR source", rt5670_stereo2_adc_lr_enum);

/* Stereo1 ADC source */
/* MX-27 MX-26 [12] */
static const char * const rt5670_stereo_adc1_src[] = {
	"DAC MIX", "ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo1_adc1_enum, RT5670_STO1_ADC_MIXER,
	RT5670_ADC_1_SRC_SFT, rt5670_stereo_adc1_src);

static const struct snd_kcontrol_new rt5670_sto_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L1 source", rt5670_stereo1_adc1_enum);

static const struct snd_kcontrol_new rt5670_sto_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R1 source", rt5670_stereo1_adc1_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_stereo2_adc1_enum, RT5670_STO2_ADC_MIXER,
	RT5670_ADC_1_SRC_SFT, rt5670_stereo_adc1_src);

static const struct snd_kcontrol_new rt5670_sto2_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo2 ADC L1 source", rt5670_stereo2_adc1_enum);

static const struct snd_kcontrol_new rt5670_sto2_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo2 ADC R1 source", rt5670_stereo2_adc1_enum);

/* MX-27 MX-26 [11] */
static const char * const rt5670_stereo_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo1_adc2_enum, RT5670_STO1_ADC_MIXER,
	RT5670_ADC_2_SRC_SFT, rt5670_stereo_adc2_src);

static const struct snd_kcontrol_new rt5670_sto_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L2 source", rt5670_stereo1_adc2_enum);

static const struct snd_kcontrol_new rt5670_sto_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R2 source", rt5670_stereo1_adc2_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_stereo2_adc2_enum, RT5670_STO2_ADC_MIXER,
	RT5670_ADC_2_SRC_SFT, rt5670_stereo_adc2_src);

static const struct snd_kcontrol_new rt5670_sto2_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo2 ADC L2 source", rt5670_stereo2_adc2_enum);

static const struct snd_kcontrol_new rt5670_sto2_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo2 ADC R2 source", rt5670_stereo2_adc2_enum);

/* MX-27 MX26 [10] */
static const char * const rt5670_stereo_adc_src[] = {
	"ADC1L ADC2R", "ADC3"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo1_adc_enum, RT5670_STO1_ADC_MIXER,
	RT5670_ADC_SRC_SFT, rt5670_stereo_adc_src);

static const struct snd_kcontrol_new rt5670_sto_adc_mux =
	SOC_DAPM_ENUM("Stereo1 ADC source", rt5670_stereo1_adc_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_stereo2_adc_enum, RT5670_STO2_ADC_MIXER,
	RT5670_ADC_SRC_SFT, rt5670_stereo_adc_src);

static const struct snd_kcontrol_new rt5670_sto2_adc_mux =
	SOC_DAPM_ENUM("Stereo2 ADC source", rt5670_stereo2_adc_enum);

/* MX-27 MX-26 [9:8] */
static const char * const rt5670_stereo_dmic_src[] = {
	"DMIC1", "DMIC2", "DMIC3"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo1_dmic_enum, RT5670_STO1_ADC_MIXER,
	RT5670_DMIC_SRC_SFT, rt5670_stereo_dmic_src);

static const struct snd_kcontrol_new rt5670_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC source", rt5670_stereo1_dmic_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_stereo2_dmic_enum, RT5670_STO2_ADC_MIXER,
	RT5670_DMIC_SRC_SFT, rt5670_stereo_dmic_src);

static const struct snd_kcontrol_new rt5670_sto2_dmic_mux =
	SOC_DAPM_ENUM("Stereo2 DMIC source", rt5670_stereo2_dmic_enum);

/* MX-27 [0] */
static const char * const rt5670_stereo_dmic3_src[] = {
	"DMIC3", "PDM ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_stereo_dmic3_enum, RT5670_STO1_ADC_MIXER,
	RT5670_DMIC3_SRC_SFT, rt5670_stereo_dmic3_src);

static const struct snd_kcontrol_new rt5670_sto_dmic3_mux =
	SOC_DAPM_ENUM("Stereo DMIC3 source", rt5670_stereo_dmic3_enum);

/* Mono ADC source */
/* MX-28 [12] */
static const char * const rt5670_mono_adc_l1_src[] = {
	"Mono DAC MIXL", "ADC1"
};

static SOC_ENUM_SINGLE_DECL(rt5670_mono_adc_l1_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_ADC_L1_SRC_SFT, rt5670_mono_adc_l1_src);

static const struct snd_kcontrol_new rt5670_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC1 left source", rt5670_mono_adc_l1_enum);
/* MX-28 [11] */
static const char * const rt5670_mono_adc_l2_src[] = {
	"Mono DAC MIXL", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_mono_adc_l2_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_ADC_L2_SRC_SFT, rt5670_mono_adc_l2_src);

static const struct snd_kcontrol_new rt5670_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC2 left source", rt5670_mono_adc_l2_enum);

/* MX-28 [9:8] */
static const char * const rt5670_mono_dmic_src[] = {
	"DMIC1", "DMIC2", "DMIC3"
};

static SOC_ENUM_SINGLE_DECL(rt5670_mono_dmic_l_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_DMIC_L_SRC_SFT, rt5670_mono_dmic_src);

static const struct snd_kcontrol_new rt5670_mono_dmic_l_mux =
	SOC_DAPM_ENUM("Mono DMIC left source", rt5670_mono_dmic_l_enum);
/* MX-28 [1:0] */
static SOC_ENUM_SINGLE_DECL(rt5670_mono_dmic_r_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_DMIC_R_SRC_SFT, rt5670_mono_dmic_src);

static const struct snd_kcontrol_new rt5670_mono_dmic_r_mux =
	SOC_DAPM_ENUM("Mono DMIC Right source", rt5670_mono_dmic_r_enum);
/* MX-28 [4] */
static const char * const rt5670_mono_adc_r1_src[] = {
	"Mono DAC MIXR", "ADC2"
};

static SOC_ENUM_SINGLE_DECL(rt5670_mono_adc_r1_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_ADC_R1_SRC_SFT, rt5670_mono_adc_r1_src);

static const struct snd_kcontrol_new rt5670_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC1 right source", rt5670_mono_adc_r1_enum);
/* MX-28 [3] */
static const char * const rt5670_mono_adc_r2_src[] = {
	"Mono DAC MIXR", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_mono_adc_r2_enum, RT5670_MONO_ADC_MIXER,
	RT5670_MONO_ADC_R2_SRC_SFT, rt5670_mono_adc_r2_src);

static const struct snd_kcontrol_new rt5670_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC2 right source", rt5670_mono_adc_r2_enum);

/* MX-2D [3:2] */
static const char * const rt5670_txdp_slot_src[] = {
	"Slot 0-1", "Slot 2-3", "Slot 4-5", "Slot 6-7"
};

static SOC_ENUM_SINGLE_DECL(rt5670_txdp_slot_enum, RT5670_DSP_PATH1,
	RT5670_TXDP_SLOT_SEL_SFT, rt5670_txdp_slot_src);

static const struct snd_kcontrol_new rt5670_txdp_slot_mux =
	SOC_DAPM_ENUM("TxDP Slot source", rt5670_txdp_slot_enum);

/* MX-2F [15] */
static const char * const rt5670_if1_adc2_in_src[] = {
	"IF_ADC2", "VAD_ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if1_adc2_in_enum, RT5670_DIG_INF1_DATA,
	RT5670_IF1_ADC2_IN_SFT, rt5670_if1_adc2_in_src);

static const struct snd_kcontrol_new rt5670_if1_adc2_in_mux =
	SOC_DAPM_ENUM("IF1 ADC2 IN source", rt5670_if1_adc2_in_enum);

/* MX-2F [14:12] */
static const char * const rt5670_if2_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "IF_ADC3", "TxDC_DAC", "TxDP_ADC", "VAD_ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if2_adc_in_enum, RT5670_DIG_INF1_DATA,
	RT5670_IF2_ADC_IN_SFT, rt5670_if2_adc_in_src);

static const struct snd_kcontrol_new rt5670_if2_adc_in_mux =
	SOC_DAPM_ENUM("IF2 ADC IN source", rt5670_if2_adc_in_enum);

/* MX-30 [5:4] */
static const char * const rt5670_if4_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "IF_ADC3"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if4_adc_in_enum, RT5670_DIG_INF2_DATA,
	RT5670_IF4_ADC_IN_SFT, rt5670_if4_adc_in_src);

static const struct snd_kcontrol_new rt5670_if4_adc_in_mux =
	SOC_DAPM_ENUM("IF4 ADC IN source", rt5670_if4_adc_in_enum);

/* MX-31 [15] [13] [11] [9] */
static const char * const rt5670_pdm_src[] = {
	"Mono DAC", "Stereo DAC"
};

static SOC_ENUM_SINGLE_DECL(rt5670_pdm1_l_enum, RT5670_PDM_OUT_CTRL,
	RT5670_PDM1_L_SFT, rt5670_pdm_src);

static const struct snd_kcontrol_new rt5670_pdm1_l_mux =
	SOC_DAPM_ENUM("PDM1 L source", rt5670_pdm1_l_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_pdm1_r_enum, RT5670_PDM_OUT_CTRL,
	RT5670_PDM1_R_SFT, rt5670_pdm_src);

static const struct snd_kcontrol_new rt5670_pdm1_r_mux =
	SOC_DAPM_ENUM("PDM1 R source", rt5670_pdm1_r_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_pdm2_l_enum, RT5670_PDM_OUT_CTRL,
	RT5670_PDM2_L_SFT, rt5670_pdm_src);

static const struct snd_kcontrol_new rt5670_pdm2_l_mux =
	SOC_DAPM_ENUM("PDM2 L source", rt5670_pdm2_l_enum);

static SOC_ENUM_SINGLE_DECL(rt5670_pdm2_r_enum, RT5670_PDM_OUT_CTRL,
	RT5670_PDM2_R_SFT, rt5670_pdm_src);

static const struct snd_kcontrol_new rt5670_pdm2_r_mux =
	SOC_DAPM_ENUM("PDM2 R source", rt5670_pdm2_r_enum);

/* MX-FA [12] */
static const char * const rt5670_if1_adc1_in1_src[] = {
	"IF_ADC1", "IF1_ADC3"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if1_adc1_in1_enum, RT5670_DIG_MISC,
	RT5670_IF1_ADC1_IN1_SFT, rt5670_if1_adc1_in1_src);

static const struct snd_kcontrol_new rt5670_if1_adc1_in1_mux =
	SOC_DAPM_ENUM("IF1 ADC1 IN1 source", rt5670_if1_adc1_in1_enum);

/* MX-FA [11] */
static const char * const rt5670_if1_adc1_in2_src[] = {
	"IF1_ADC1_IN1", "IF1_ADC4"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if1_adc1_in2_enum, RT5670_DIG_MISC,
	RT5670_IF1_ADC1_IN2_SFT, rt5670_if1_adc1_in2_src);

static const struct snd_kcontrol_new rt5670_if1_adc1_in2_mux =
	SOC_DAPM_ENUM("IF1 ADC1 IN2 source", rt5670_if1_adc1_in2_enum);

/* MX-FA [10] */
static const char * const rt5670_if1_adc2_in1_src[] = {
	"IF1_ADC2_IN", "IF1_ADC4"
};

static SOC_ENUM_SINGLE_DECL(rt5670_if1_adc2_in1_enum, RT5670_DIG_MISC,
	RT5670_IF1_ADC2_IN1_SFT, rt5670_if1_adc2_in1_src);

static const struct snd_kcontrol_new rt5670_if1_adc2_in1_mux =
	SOC_DAPM_ENUM("IF1 ADC2 IN1 source", rt5670_if1_adc2_in1_enum);

/* MX-9D [9:8] */
static const char * const rt5670_vad_adc_src[] = {
	"Sto1 ADC L", "Mono ADC L", "Mono ADC R", "Sto2 ADC L"
};

static SOC_ENUM_SINGLE_DECL(rt5670_vad_adc_enum, RT5670_VAD_CTRL4,
	RT5670_VAD_SEL_SFT, rt5670_vad_adc_src);

static const struct snd_kcontrol_new rt5670_vad_adc_mux =
	SOC_DAPM_ENUM("VAD ADC source", rt5670_vad_adc_enum);

static int rt5670_hp_power_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(rt5670->regmap, RT5670_CHARGE_PUMP,
			RT5670_PM_HP_MASK, RT5670_PM_HP_HV);
		regmap_update_bits(rt5670->regmap, RT5670_GEN_CTRL2,
			0x0400, 0x0400);
		/* headphone amp power on */
		regmap_update_bits(rt5670->regmap, RT5670_PWR_ANLG1,
			RT5670_PWR_HA |	RT5670_PWR_FV1 |
			RT5670_PWR_FV2,	RT5670_PWR_HA |
			RT5670_PWR_FV1 | RT5670_PWR_FV2);
		/* depop parameters */
		regmap_write(rt5670->regmap, RT5670_DEPOP_M2, 0x3100);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x8009);
		regmap_write(rt5670->regmap, RT5670_PR_BASE +
			RT5670_HP_DCC_INT1, 0x9f00);
		mdelay(20);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x8019);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x0004);
		msleep(30);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt5670_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* headphone unmute sequence */
		regmap_write(rt5670->regmap, RT5670_PR_BASE +
				RT5670_MAMP_INT_REG2, 0xb400);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M3, 0x0772);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x805d);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x831d);
		regmap_update_bits(rt5670->regmap, RT5670_GEN_CTRL2,
				0x0300, 0x0300);
		regmap_update_bits(rt5670->regmap, RT5670_HP_VOL,
			RT5670_L_MUTE | RT5670_R_MUTE, 0);
		msleep(80);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x8019);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* headphone mute sequence */
		regmap_write(rt5670->regmap, RT5670_PR_BASE +
				RT5670_MAMP_INT_REG2, 0xb400);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M3, 0x0772);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x803d);
		mdelay(10);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x831d);
		mdelay(10);
		regmap_update_bits(rt5670->regmap, RT5670_HP_VOL,
				   RT5670_L_MUTE | RT5670_R_MUTE,
				   RT5670_L_MUTE | RT5670_R_MUTE);
		msleep(20);
		regmap_update_bits(rt5670->regmap,
				   RT5670_GEN_CTRL2, 0x0300, 0x0);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M1, 0x8019);
		regmap_write(rt5670->regmap, RT5670_DEPOP_M3, 0x0707);
		regmap_write(rt5670->regmap, RT5670_PR_BASE +
				RT5670_MAMP_INT_REG2, 0xfc00);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5670_bst1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5670_PWR_ANLG2,
				    RT5670_PWR_BST1_P, RT5670_PWR_BST1_P);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5670_PWR_ANLG2,
				    RT5670_PWR_BST1_P, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5670_bst2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5670_PWR_ANLG2,
				    RT5670_PWR_BST2_P, RT5670_PWR_BST2_P);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5670_PWR_ANLG2,
				    RT5670_PWR_BST2_P, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5670_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL1", RT5670_PWR_ANLG2,
			    RT5670_PWR_PLL_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S DSP", RT5670_PWR_DIG2,
			    RT5670_PWR_I2S_DSP_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", RT5670_PWR_VOL,
			    RT5670_PWR_MIC_DET_BIT, 0, NULL, 0),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("I2S1 ASRC", 1, RT5670_ASRC_1,
			      11, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2 ASRC", 1, RT5670_ASRC_1,
			      12, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC STO ASRC", 1, RT5670_ASRC_1,
			      10, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO L ASRC", 1, RT5670_ASRC_1,
			      9, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC MONO R ASRC", 1, RT5670_ASRC_1,
			      8, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO1 ASRC", 1, RT5670_ASRC_1,
			      7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC STO2 ASRC", 1, RT5670_ASRC_1,
			      6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO L ASRC", 1, RT5670_ASRC_1,
			      5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DMIC MONO R ASRC", 1, RT5670_ASRC_1,
			      4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5670_ASRC_1,
			      3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO2 ASRC", 1, RT5670_ASRC_1,
			      2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC MONO L ASRC", 1, RT5670_ASRC_1,
			      1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC MONO R ASRC", 1, RT5670_ASRC_1,
			      0, 0, NULL, 0),

	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5670_PWR_ANLG2,
			     RT5670_PWR_MB1_BIT, 0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),
	SND_SOC_DAPM_INPUT("DMIC L3"),
	SND_SOC_DAPM_INPUT("DMIC R3"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC3", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
			    set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5670_DMIC_CTRL1,
			    RT5670_DMIC_1_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC2 Power", RT5670_DMIC_CTRL1,
			    RT5670_DMIC_2_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC3 Power", RT5670_DMIC_CTRL1,
			    RT5670_DMIC_3_EN_SFT, 0, NULL, 0),
	/* Boost */
	SND_SOC_DAPM_PGA_E("BST1", RT5670_PWR_ANLG2, RT5670_PWR_BST1_BIT,
			   0, NULL, 0, rt5670_bst1_event,
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST2", RT5670_PWR_ANLG2, RT5670_PWR_BST2_BIT,
			   0, NULL, 0, rt5670_bst2_event,
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT5670_PWR_VOL,
			 RT5670_PWR_IN_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT5670_PWR_VOL,
			 RT5670_PWR_IN_R_BIT, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5670_PWR_MIXER, RT5670_PWR_RM_L_BIT, 0,
			   rt5670_rec_l_mix, ARRAY_SIZE(rt5670_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5670_PWR_MIXER, RT5670_PWR_RM_R_BIT, 0,
			   rt5670_rec_r_mix, ARRAY_SIZE(rt5670_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC 1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC 2", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA("ADC 1_2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC 1 power", RT5670_PWR_DIG1,
			    RT5670_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC 2 power", RT5670_PWR_DIG1,
			    RT5670_PWR_ADC_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC clock", RT5670_PR_BASE +
			    RT5670_CHOP_DAC_ADC, 12, 0, NULL, 0),
	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto_adc_r2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto_adc_r1_mux),
	SND_SOC_DAPM_MUX("Stereo2 DMIC Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_adc_r2_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_adc_r1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC LR Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_sto2_adc_lr_mux),
	SND_SOC_DAPM_MUX("Mono DMIC L Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_dmic_l_mux),
	SND_SOC_DAPM_MUX("Mono DMIC R Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_dmic_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_mono_adc_r2_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("ADC Stereo1 Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Stereo2 Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_ADC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXL", RT5670_STO1_ADC_DIG_VOL,
			   RT5670_L_MUTE_SFT, 1, rt5670_sto1_adc_l_mix,
			   ARRAY_SIZE(rt5670_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXR", RT5670_STO1_ADC_DIG_VOL,
			   RT5670_R_MUTE_SFT, 1, rt5670_sto1_adc_r_mix,
			   ARRAY_SIZE(rt5670_sto1_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5670_sto2_adc_l_mix,
			   ARRAY_SIZE(rt5670_sto2_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto2 ADC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5670_sto2_adc_r_mix,
			   ARRAY_SIZE(rt5670_sto2_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Left Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", RT5670_MONO_ADC_DIG_VOL,
			   RT5670_L_MUTE_SFT, 1, rt5670_mono_adc_l_mix,
			   ARRAY_SIZE(rt5670_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Right Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", RT5670_MONO_ADC_DIG_VOL,
			   RT5670_R_MUTE_SFT, 1, rt5670_mono_adc_r_mix,
			   ARRAY_SIZE(rt5670_mono_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Sto2 ADC LR MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono ADC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("VAD_ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DSP */
	SND_SOC_DAPM_PGA("TxDP_ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDP_ADC_L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDP_ADC_R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDC_DAC", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("TDM Data Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_txdp_slot_mux),

	SND_SOC_DAPM_MUX("DSP UL Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_dsp_ul_mux),
	SND_SOC_DAPM_MUX("DSP DL Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_dsp_dl_mux),

	SND_SOC_DAPM_MUX("RxDP Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_rxdp_mux),

	/* IF2 Mux */
	SND_SOC_DAPM_MUX("IF2 ADC Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_if2_adc_in_mux),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5670_PWR_DIG1,
			    RT5670_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5670_PWR_DIG1,
			    RT5670_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("IF1 ADC1 IN1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_if1_adc1_in1_mux),
	SND_SOC_DAPM_MUX("IF1 ADC1 IN2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_if1_adc1_in2_mux),
	SND_SOC_DAPM_MUX("IF1 ADC2 IN Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_if1_adc2_in_mux),
	SND_SOC_DAPM_MUX("IF1 ADC2 IN1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_if1_adc2_in1_mux),
	SND_SOC_DAPM_MUX("VAD ADC Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_vad_adc_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0,
			     RT5670_GPIO_CTRL1, RT5670_I2S2_PIN_SFT, 1),

	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
			   rt5670_dac_l_mix, ARRAY_SIZE(rt5670_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
			   rt5670_dac_r_mix, ARRAY_SIZE(rt5670_dac_r_mix)),
	SND_SOC_DAPM_PGA("DAC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5670_dac_r2_mux),
	SND_SOC_DAPM_PGA("DAC L2 Volume", RT5670_PWR_DIG1,
			 RT5670_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R2 Volume", RT5670_PWR_DIG1,
			 RT5670_PWR_DAC_R2_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DAC1 L Mux", SND_SOC_NOPM, 0, 0, &rt5670_dac1l_mux),
	SND_SOC_DAPM_MUX("DAC1 R Mux", SND_SOC_NOPM, 0, 0, &rt5670_dac1r_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("DAC Stereo1 Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Left Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_DAC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Right Filter", RT5670_PWR_DIG2,
			    RT5670_PWR_DAC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5670_sto_dac_l_mix,
			   ARRAY_SIZE(rt5670_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5670_sto_dac_r_mix,
			   ARRAY_SIZE(rt5670_sto_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5670_mono_dac_l_mix,
			   ARRAY_SIZE(rt5670_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5670_mono_dac_r_mix,
			   ARRAY_SIZE(rt5670_mono_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5670_dig_l_mix,
			   ARRAY_SIZE(rt5670_dig_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5670_dig_r_mix,
			   ARRAY_SIZE(rt5670_dig_r_mix)),

	/* DACs */
	SND_SOC_DAPM_SUPPLY("DAC L1 Power", RT5670_PWR_DIG1,
			    RT5670_PWR_DAC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R1 Power", RT5670_PWR_DIG1,
			    RT5670_PWR_DAC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, RT5670_PWR_DIG1,
			 RT5670_PWR_DAC_L2_BIT, 0),

	SND_SOC_DAPM_DAC("DAC R2", NULL, RT5670_PWR_DIG1,
			 RT5670_PWR_DAC_R2_BIT, 0),
	/* OUT Mixer */

	SND_SOC_DAPM_MIXER("OUT MIXL", RT5670_PWR_MIXER, RT5670_PWR_OM_L_BIT,
			   0, rt5670_out_l_mix, ARRAY_SIZE(rt5670_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5670_PWR_MIXER, RT5670_PWR_OM_R_BIT,
			   0, rt5670_out_r_mix, ARRAY_SIZE(rt5670_out_r_mix)),
	/* Ouput Volume */
	SND_SOC_DAPM_MIXER("HPOVOL MIXL", RT5670_PWR_VOL,
			   RT5670_PWR_HV_L_BIT, 0,
			   rt5670_hpvoll_mix, ARRAY_SIZE(rt5670_hpvoll_mix)),
	SND_SOC_DAPM_MIXER("HPOVOL MIXR", RT5670_PWR_VOL,
			   RT5670_PWR_HV_R_BIT, 0,
			   rt5670_hpvolr_mix, ARRAY_SIZE(rt5670_hpvolr_mix)),
	SND_SOC_DAPM_PGA("DAC 1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC 2", SND_SOC_NOPM,	0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("HPO MIX", SND_SOC_NOPM, 0, 0,
			   rt5670_hpo_mix, ARRAY_SIZE(rt5670_hpo_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", RT5670_PWR_ANLG1, RT5670_PWR_LM_BIT,
			   0, rt5670_lout_mix, ARRAY_SIZE(rt5670_lout_mix)),
	SND_SOC_DAPM_SUPPLY_S("Improve HP Amp Drv", 1, SND_SOC_NOPM, 0, 0,
			      rt5670_hp_power_event, SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("HP L Amp", RT5670_PWR_ANLG1,
			    RT5670_PWR_HP_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("HP R Amp", RT5670_PWR_ANLG1,
			    RT5670_PWR_HP_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0,
			   rt5670_hp_event, SND_SOC_DAPM_PRE_PMD |
			   SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SWITCH("LOUT L Playback", SND_SOC_NOPM, 0, 0,
			    &lout_l_enable_control),
	SND_SOC_DAPM_SWITCH("LOUT R Playback", SND_SOC_NOPM, 0, 0,
			    &lout_r_enable_control),
	SND_SOC_DAPM_PGA("LOUT Amp", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* PDM */
	SND_SOC_DAPM_SUPPLY("PDM1 Power", RT5670_PWR_DIG2,
		RT5670_PWR_PDM1_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MUX("PDM1 L Mux", RT5670_PDM_OUT_CTRL,
			 RT5670_M_PDM1_L_SFT, 1, &rt5670_pdm1_l_mux),
	SND_SOC_DAPM_MUX("PDM1 R Mux", RT5670_PDM_OUT_CTRL,
			 RT5670_M_PDM1_R_SFT, 1, &rt5670_pdm1_r_mux),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
};

static const struct snd_soc_dapm_widget rt5670_specific_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PDM2 Power", RT5670_PWR_DIG2,
		RT5670_PWR_PDM2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MUX("PDM2 L Mux", RT5670_PDM_OUT_CTRL,
			 RT5670_M_PDM2_L_SFT, 1, &rt5670_pdm2_l_mux),
	SND_SOC_DAPM_MUX("PDM2 R Mux", RT5670_PDM_OUT_CTRL,
			 RT5670_M_PDM2_R_SFT, 1, &rt5670_pdm2_r_mux),
	SND_SOC_DAPM_OUTPUT("PDM1L"),
	SND_SOC_DAPM_OUTPUT("PDM1R"),
	SND_SOC_DAPM_OUTPUT("PDM2L"),
	SND_SOC_DAPM_OUTPUT("PDM2R"),
};

static const struct snd_soc_dapm_widget rt5672_specific_dapm_widgets[] = {
	SND_SOC_DAPM_PGA("SPO Amp", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("SPOLP"),
	SND_SOC_DAPM_OUTPUT("SPOLN"),
	SND_SOC_DAPM_OUTPUT("SPORP"),
	SND_SOC_DAPM_OUTPUT("SPORN"),
};

static const struct snd_soc_dapm_route rt5670_dapm_routes[] = {
	{ "ADC Stereo1 Filter", NULL, "ADC STO1 ASRC", is_using_asrc },
	{ "ADC Stereo2 Filter", NULL, "ADC STO2 ASRC", is_using_asrc },
	{ "ADC Mono Left Filter", NULL, "ADC MONO L ASRC", is_using_asrc },
	{ "ADC Mono Right Filter", NULL, "ADC MONO R ASRC", is_using_asrc },
	{ "DAC Mono Left Filter", NULL, "DAC MONO L ASRC", is_using_asrc },
	{ "DAC Mono Right Filter", NULL, "DAC MONO R ASRC", is_using_asrc },
	{ "DAC Stereo1 Filter", NULL, "DAC STO ASRC", is_using_asrc },
	{ "Stereo1 DMIC Mux", NULL, "DMIC STO1 ASRC", can_use_asrc },
	{ "Stereo2 DMIC Mux", NULL, "DMIC STO2 ASRC", can_use_asrc },
	{ "Mono DMIC L Mux", NULL, "DMIC MONO L ASRC", can_use_asrc },
	{ "Mono DMIC R Mux", NULL, "DMIC MONO R ASRC", can_use_asrc },

	{ "I2S1", NULL, "I2S1 ASRC", can_use_asrc},
	{ "I2S2", NULL, "I2S2 ASRC", can_use_asrc},

	{ "DMIC1", NULL, "DMIC L1" },
	{ "DMIC1", NULL, "DMIC R1" },
	{ "DMIC2", NULL, "DMIC L2" },
	{ "DMIC2", NULL, "DMIC R2" },
	{ "DMIC3", NULL, "DMIC L3" },
	{ "DMIC3", NULL, "DMIC R3" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST1", NULL, "Mic Det Power" },
	{ "BST2", NULL, "IN2P" },
	{ "BST2", NULL, "IN2N" },

	{ "INL VOL", NULL, "IN2P" },
	{ "INR VOL", NULL, "IN2N" },

	{ "RECMIXL", "INL Switch", "INL VOL" },
	{ "RECMIXL", "BST2 Switch", "BST2" },
	{ "RECMIXL", "BST1 Switch", "BST1" },

	{ "RECMIXR", "INR Switch", "INR VOL" },
	{ "RECMIXR", "BST2 Switch", "BST2" },
	{ "RECMIXR", "BST1 Switch", "BST1" },

	{ "ADC 1", NULL, "RECMIXL" },
	{ "ADC 1", NULL, "ADC 1 power" },
	{ "ADC 1", NULL, "ADC clock" },
	{ "ADC 2", NULL, "RECMIXR" },
	{ "ADC 2", NULL, "ADC 2 power" },
	{ "ADC 2", NULL, "ADC clock" },

	{ "DMIC L1", NULL, "DMIC CLK" },
	{ "DMIC L1", NULL, "DMIC1 Power" },
	{ "DMIC R1", NULL, "DMIC CLK" },
	{ "DMIC R1", NULL, "DMIC1 Power" },
	{ "DMIC L2", NULL, "DMIC CLK" },
	{ "DMIC L2", NULL, "DMIC2 Power" },
	{ "DMIC R2", NULL, "DMIC CLK" },
	{ "DMIC R2", NULL, "DMIC2 Power" },
	{ "DMIC L3", NULL, "DMIC CLK" },
	{ "DMIC L3", NULL, "DMIC3 Power" },
	{ "DMIC R3", NULL, "DMIC CLK" },
	{ "DMIC R3", NULL, "DMIC3 Power" },

	{ "Stereo1 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo1 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo1 DMIC Mux", "DMIC3", "DMIC3" },

	{ "Stereo2 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo2 DMIC Mux", "DMIC2", "DMIC2" },
	{ "Stereo2 DMIC Mux", "DMIC3", "DMIC3" },

	{ "Mono DMIC L Mux", "DMIC1", "DMIC L1" },
	{ "Mono DMIC L Mux", "DMIC2", "DMIC L2" },
	{ "Mono DMIC L Mux", "DMIC3", "DMIC L3" },

	{ "Mono DMIC R Mux", "DMIC1", "DMIC R1" },
	{ "Mono DMIC R Mux", "DMIC2", "DMIC R2" },
	{ "Mono DMIC R Mux", "DMIC3", "DMIC R3" },

	{ "ADC 1_2", NULL, "ADC 1" },
	{ "ADC 1_2", NULL, "ADC 2" },

	{ "Stereo1 ADC L2 Mux", "DMIC", "Stereo1 DMIC Mux" },
	{ "Stereo1 ADC L2 Mux", "DAC MIX", "DAC MIXL" },
	{ "Stereo1 ADC L1 Mux", "ADC", "ADC 1_2" },
	{ "Stereo1 ADC L1 Mux", "DAC MIX", "DAC MIXL" },

	{ "Stereo1 ADC R1 Mux", "ADC", "ADC 1_2" },
	{ "Stereo1 ADC R1 Mux", "DAC MIX", "DAC MIXR" },
	{ "Stereo1 ADC R2 Mux", "DMIC", "Stereo1 DMIC Mux" },
	{ "Stereo1 ADC R2 Mux", "DAC MIX", "DAC MIXR" },

	{ "Mono ADC L2 Mux", "DMIC", "Mono DMIC L Mux" },
	{ "Mono ADC L2 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "ADC1",  "ADC 1" },

	{ "Mono ADC R1 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },
	{ "Mono ADC R1 Mux", "ADC2", "ADC 2" },
	{ "Mono ADC R2 Mux", "DMIC", "Mono DMIC R Mux" },
	{ "Mono ADC R2 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },

	{ "Sto1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux" },
	{ "Sto1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux" },
	{ "Sto1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux" },
	{ "Sto1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux" },

	{ "Stereo1 ADC MIXL", NULL, "Sto1 ADC MIXL" },
	{ "Stereo1 ADC MIXL", NULL, "ADC Stereo1 Filter" },
	{ "ADC Stereo1 Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo1 ADC MIXR", NULL, "Sto1 ADC MIXR" },
	{ "Stereo1 ADC MIXR", NULL, "ADC Stereo1 Filter" },
	{ "ADC Stereo1 Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux" },
	{ "Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux" },
	{ "Mono ADC MIXL", NULL, "ADC Mono Left Filter" },
	{ "ADC Mono Left Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux" },
	{ "Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux" },
	{ "Mono ADC MIXR", NULL, "ADC Mono Right Filter" },
	{ "ADC Mono Right Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo2 ADC L2 Mux", "DMIC", "Stereo2 DMIC Mux" },
	{ "Stereo2 ADC L2 Mux", "DAC MIX", "DAC MIXL" },
	{ "Stereo2 ADC L1 Mux", "ADC", "ADC 1_2" },
	{ "Stereo2 ADC L1 Mux", "DAC MIX", "DAC MIXL" },

	{ "Stereo2 ADC R1 Mux", "ADC", "ADC 1_2" },
	{ "Stereo2 ADC R1 Mux", "DAC MIX", "DAC MIXR" },
	{ "Stereo2 ADC R2 Mux", "DMIC", "Stereo2 DMIC Mux" },
	{ "Stereo2 ADC R2 Mux", "DAC MIX", "DAC MIXR" },

	{ "Sto2 ADC MIXL", "ADC1 Switch", "Stereo2 ADC L1 Mux" },
	{ "Sto2 ADC MIXL", "ADC2 Switch", "Stereo2 ADC L2 Mux" },
	{ "Sto2 ADC MIXR", "ADC1 Switch", "Stereo2 ADC R1 Mux" },
	{ "Sto2 ADC MIXR", "ADC2 Switch", "Stereo2 ADC R2 Mux" },

	{ "Sto2 ADC LR MIX", NULL, "Sto2 ADC MIXL" },
	{ "Sto2 ADC LR MIX", NULL, "Sto2 ADC MIXR" },

	{ "Stereo2 ADC LR Mux", "L", "Sto2 ADC MIXL" },
	{ "Stereo2 ADC LR Mux", "LR", "Sto2 ADC LR MIX" },

	{ "Stereo2 ADC MIXL", NULL, "Stereo2 ADC LR Mux" },
	{ "Stereo2 ADC MIXL", NULL, "ADC Stereo2 Filter" },
	{ "ADC Stereo2 Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "Stereo2 ADC MIXR", NULL, "Sto2 ADC MIXR" },
	{ "Stereo2 ADC MIXR", NULL, "ADC Stereo2 Filter" },
	{ "ADC Stereo2 Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "VAD ADC Mux", "Sto1 ADC L", "Stereo1 ADC MIXL" },
	{ "VAD ADC Mux", "Mono ADC L", "Mono ADC MIXL" },
	{ "VAD ADC Mux", "Mono ADC R", "Mono ADC MIXR" },
	{ "VAD ADC Mux", "Sto2 ADC L", "Sto2 ADC MIXL" },

	{ "VAD_ADC", NULL, "VAD ADC Mux" },

	{ "IF_ADC1", NULL, "Stereo1 ADC MIXL" },
	{ "IF_ADC1", NULL, "Stereo1 ADC MIXR" },
	{ "IF_ADC2", NULL, "Mono ADC MIXL" },
	{ "IF_ADC2", NULL, "Mono ADC MIXR" },
	{ "IF_ADC3", NULL, "Stereo2 ADC MIXL" },
	{ "IF_ADC3", NULL, "Stereo2 ADC MIXR" },

	{ "IF1 ADC1 IN1 Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF1 ADC1 IN1 Mux", "IF1_ADC3", "IF1_ADC3" },

	{ "IF1 ADC1 IN2 Mux", "IF1_ADC1_IN1", "IF1 ADC1 IN1 Mux" },
	{ "IF1 ADC1 IN2 Mux", "IF1_ADC4", "IF1_ADC4" },

	{ "IF1 ADC2 IN Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF1 ADC2 IN Mux", "VAD_ADC", "VAD_ADC" },

	{ "IF1 ADC2 IN1 Mux", "IF1_ADC2_IN", "IF1 ADC2 IN Mux" },
	{ "IF1 ADC2 IN1 Mux", "IF1_ADC4", "IF1_ADC4" },

	{ "IF1_ADC1" , NULL, "IF1 ADC1 IN2 Mux" },
	{ "IF1_ADC2" , NULL, "IF1 ADC2 IN1 Mux" },

	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXL" },
	{ "Stereo1 ADC MIX", NULL, "Stereo1 ADC MIXR" },
	{ "Stereo2 ADC MIX", NULL, "Sto2 ADC MIXL" },
	{ "Stereo2 ADC MIX", NULL, "Sto2 ADC MIXR" },
	{ "Mono ADC MIX", NULL, "Mono ADC MIXL" },
	{ "Mono ADC MIX", NULL, "Mono ADC MIXR" },

	{ "RxDP Mux", "IF2 DAC", "IF2 DAC" },
	{ "RxDP Mux", "IF1 DAC", "IF1 DAC2" },
	{ "RxDP Mux", "STO1 ADC Mixer", "Stereo1 ADC MIX" },
	{ "RxDP Mux", "STO2 ADC Mixer", "Stereo2 ADC MIX" },
	{ "RxDP Mux", "Mono ADC Mixer L", "Mono ADC MIXL" },
	{ "RxDP Mux", "Mono ADC Mixer R", "Mono ADC MIXR" },
	{ "RxDP Mux", "DAC1", "DAC MIX" },

	{ "TDM Data Mux", "Slot 0-1", "Stereo1 ADC MIX" },
	{ "TDM Data Mux", "Slot 2-3", "Mono ADC MIX" },
	{ "TDM Data Mux", "Slot 4-5", "Stereo2 ADC MIX" },
	{ "TDM Data Mux", "Slot 6-7", "IF2 DAC" },

	{ "DSP UL Mux", "Bypass", "TDM Data Mux" },
	{ "DSP UL Mux", NULL, "I2S DSP" },
	{ "DSP DL Mux", "Bypass", "RxDP Mux" },
	{ "DSP DL Mux", NULL, "I2S DSP" },

	{ "TxDP_ADC_L", NULL, "DSP UL Mux" },
	{ "TxDP_ADC_R", NULL, "DSP UL Mux" },
	{ "TxDC_DAC", NULL, "DSP DL Mux" },

	{ "TxDP_ADC", NULL, "TxDP_ADC_L" },
	{ "TxDP_ADC", NULL, "TxDP_ADC_R" },

	{ "IF1 ADC", NULL, "I2S1" },
	{ "IF1 ADC", NULL, "IF1_ADC1" },
	{ "IF1 ADC", NULL, "IF1_ADC2" },
	{ "IF1 ADC", NULL, "IF_ADC3" },
	{ "IF1 ADC", NULL, "TxDP_ADC" },

	{ "IF2 ADC Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF2 ADC Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF2 ADC Mux", "IF_ADC3", "IF_ADC3" },
	{ "IF2 ADC Mux", "TxDC_DAC", "TxDC_DAC" },
	{ "IF2 ADC Mux", "TxDP_ADC", "TxDP_ADC" },
	{ "IF2 ADC Mux", "VAD_ADC", "VAD_ADC" },

	{ "IF2 ADC L", NULL, "IF2 ADC Mux" },
	{ "IF2 ADC R", NULL, "IF2 ADC Mux" },

	{ "IF2 ADC", NULL, "I2S2" },
	{ "IF2 ADC", NULL, "IF2 ADC L" },
	{ "IF2 ADC", NULL, "IF2 ADC R" },

	{ "AIF1TX", NULL, "IF1 ADC" },
	{ "AIF2TX", NULL, "IF2 ADC" },

	{ "IF1 DAC1", NULL, "AIF1RX" },
	{ "IF1 DAC2", NULL, "AIF1RX" },
	{ "IF2 DAC", NULL, "AIF2RX" },

	{ "IF1 DAC1", NULL, "I2S1" },
	{ "IF1 DAC2", NULL, "I2S1" },
	{ "IF2 DAC", NULL, "I2S2" },

	{ "IF1 DAC2 L", NULL, "IF1 DAC2" },
	{ "IF1 DAC2 R", NULL, "IF1 DAC2" },
	{ "IF1 DAC1 L", NULL, "IF1 DAC1" },
	{ "IF1 DAC1 R", NULL, "IF1 DAC1" },
	{ "IF2 DAC L", NULL, "IF2 DAC" },
	{ "IF2 DAC R", NULL, "IF2 DAC" },

	{ "DAC1 L Mux", "IF1 DAC", "IF1 DAC1 L" },
	{ "DAC1 L Mux", "IF2 DAC", "IF2 DAC L" },

	{ "DAC1 R Mux", "IF1 DAC", "IF1 DAC1 R" },
	{ "DAC1 R Mux", "IF2 DAC", "IF2 DAC R" },

	{ "DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL" },
	{ "DAC1 MIXL", "DAC1 Switch", "DAC1 L Mux" },
	{ "DAC1 MIXL", NULL, "DAC Stereo1 Filter" },
	{ "DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR" },
	{ "DAC1 MIXR", "DAC1 Switch", "DAC1 R Mux" },
	{ "DAC1 MIXR", NULL, "DAC Stereo1 Filter" },

	{ "DAC Stereo1 Filter", NULL, "PLL1", is_sys_clk_from_pll },
	{ "DAC Mono Left Filter", NULL, "PLL1", is_sys_clk_from_pll },
	{ "DAC Mono Right Filter", NULL, "PLL1", is_sys_clk_from_pll },

	{ "DAC MIX", NULL, "DAC1 MIXL" },
	{ "DAC MIX", NULL, "DAC1 MIXR" },

	{ "Audio DSP", NULL, "DAC1 MIXL" },
	{ "Audio DSP", NULL, "DAC1 MIXR" },

	{ "DAC L2 Mux", "IF1 DAC", "IF1 DAC2 L" },
	{ "DAC L2 Mux", "IF2 DAC", "IF2 DAC L" },
	{ "DAC L2 Mux", "TxDC DAC", "TxDC_DAC" },
	{ "DAC L2 Mux", "VAD_ADC", "VAD_ADC" },
	{ "DAC L2 Volume", NULL, "DAC L2 Mux" },
	{ "DAC L2 Volume", NULL, "DAC Mono Left Filter" },

	{ "DAC R2 Mux", "IF1 DAC", "IF1 DAC2 R" },
	{ "DAC R2 Mux", "IF2 DAC", "IF2 DAC R" },
	{ "DAC R2 Mux", "TxDC DAC", "TxDC_DAC" },
	{ "DAC R2 Mux", "TxDP ADC", "TxDP_ADC" },
	{ "DAC R2 Volume", NULL, "DAC R2 Mux" },
	{ "DAC R2 Volume", NULL, "DAC Mono Right Filter" },

	{ "Stereo DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXL", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Stereo DAC MIXL", NULL, "DAC Stereo1 Filter" },
	{ "Stereo DAC MIXL", NULL, "DAC L1 Power" },
	{ "Stereo DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXR", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Stereo DAC MIXR", NULL, "DAC Stereo1 Filter" },
	{ "Stereo DAC MIXR", NULL, "DAC R1 Power" },

	{ "Mono DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Mono DAC MIXL", NULL, "DAC Mono Left Filter" },
	{ "Mono DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Mono DAC MIXR", NULL, "DAC Mono Right Filter" },

	{ "DAC MIXL", "Sto DAC Mix L Switch", "Stereo DAC MIXL" },
	{ "DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "DAC MIXL", "DAC R2 Switch", "DAC R2 Volume" },
	{ "DAC MIXR", "Sto DAC Mix R Switch", "Stereo DAC MIXR" },
	{ "DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "DAC MIXR", "DAC L2 Switch", "DAC L2 Volume" },

	{ "DAC L1", NULL, "DAC L1 Power" },
	{ "DAC L1", NULL, "Stereo DAC MIXL" },
	{ "DAC R1", NULL, "DAC R1 Power" },
	{ "DAC R1", NULL, "Stereo DAC MIXR" },
	{ "DAC L2", NULL, "Mono DAC MIXL" },
	{ "DAC R2", NULL, "Mono DAC MIXR" },

	{ "OUT MIXL", "BST1 Switch", "BST1" },
	{ "OUT MIXL", "INL Switch", "INL VOL" },
	{ "OUT MIXL", "DAC L2 Switch", "DAC L2" },
	{ "OUT MIXL", "DAC L1 Switch", "DAC L1" },

	{ "OUT MIXR", "BST2 Switch", "BST2" },
	{ "OUT MIXR", "INR Switch", "INR VOL" },
	{ "OUT MIXR", "DAC R2 Switch", "DAC R2" },
	{ "OUT MIXR", "DAC R1 Switch", "DAC R1" },

	{ "HPOVOL MIXL", "DAC1 Switch", "DAC L1" },
	{ "HPOVOL MIXL", "INL Switch", "INL VOL" },
	{ "HPOVOL MIXR", "DAC1 Switch", "DAC R1" },
	{ "HPOVOL MIXR", "INR Switch", "INR VOL" },

	{ "DAC 2", NULL, "DAC L2" },
	{ "DAC 2", NULL, "DAC R2" },
	{ "DAC 1", NULL, "DAC L1" },
	{ "DAC 1", NULL, "DAC R1" },
	{ "HPOVOL", NULL, "HPOVOL MIXL" },
	{ "HPOVOL", NULL, "HPOVOL MIXR" },
	{ "HPO MIX", "DAC1 Switch", "DAC 1" },
	{ "HPO MIX", "HPVOL Switch", "HPOVOL" },

	{ "LOUT MIX", "DAC L1 Switch", "DAC L1" },
	{ "LOUT MIX", "DAC R1 Switch", "DAC R1" },
	{ "LOUT MIX", "OUTMIX L Switch", "OUT MIXL" },
	{ "LOUT MIX", "OUTMIX R Switch", "OUT MIXR" },

	{ "PDM1 L Mux", "Stereo DAC", "Stereo DAC MIXL" },
	{ "PDM1 L Mux", "Mono DAC", "Mono DAC MIXL" },
	{ "PDM1 L Mux", NULL, "PDM1 Power" },
	{ "PDM1 R Mux", "Stereo DAC", "Stereo DAC MIXR" },
	{ "PDM1 R Mux", "Mono DAC", "Mono DAC MIXR" },
	{ "PDM1 R Mux", NULL, "PDM1 Power" },

	{ "HP Amp", NULL, "HPO MIX" },
	{ "HP Amp", NULL, "Mic Det Power" },
	{ "HPOL", NULL, "HP Amp" },
	{ "HPOL", NULL, "HP L Amp" },
	{ "HPOL", NULL, "Improve HP Amp Drv" },
	{ "HPOR", NULL, "HP Amp" },
	{ "HPOR", NULL, "HP R Amp" },
	{ "HPOR", NULL, "Improve HP Amp Drv" },

	{ "LOUT Amp", NULL, "LOUT MIX" },
	{ "LOUT L Playback", "Switch", "LOUT Amp" },
	{ "LOUT R Playback", "Switch", "LOUT Amp" },
	{ "LOUTL", NULL, "LOUT L Playback" },
	{ "LOUTR", NULL, "LOUT R Playback" },
	{ "LOUTL", NULL, "Improve HP Amp Drv" },
	{ "LOUTR", NULL, "Improve HP Amp Drv" },
};

static const struct snd_soc_dapm_route rt5670_specific_dapm_routes[] = {
	{ "PDM2 L Mux", "Stereo DAC", "Stereo DAC MIXL" },
	{ "PDM2 L Mux", "Mono DAC", "Mono DAC MIXL" },
	{ "PDM2 L Mux", NULL, "PDM2 Power" },
	{ "PDM2 R Mux", "Stereo DAC", "Stereo DAC MIXR" },
	{ "PDM2 R Mux", "Mono DAC", "Mono DAC MIXR" },
	{ "PDM2 R Mux", NULL, "PDM2 Power" },
	{ "PDM1L", NULL, "PDM1 L Mux" },
	{ "PDM1R", NULL, "PDM1 R Mux" },
	{ "PDM2L", NULL, "PDM2 L Mux" },
	{ "PDM2R", NULL, "PDM2 R Mux" },
};

static const struct snd_soc_dapm_route rt5672_specific_dapm_routes[] = {
	{ "SPO Amp", NULL, "PDM1 L Mux" },
	{ "SPO Amp", NULL, "PDM1 R Mux" },
	{ "SPOLP", NULL, "SPO Amp" },
	{ "SPOLN", NULL, "SPO Amp" },
	{ "SPORP", NULL, "SPO Amp" },
	{ "SPORN", NULL, "SPO Amp" },
};

static int rt5670_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt5670->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5670->sysclk, rt5670->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting %d for DAI %d\n",
			rt5670->lrck[dai->id], dai->id);
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32;
	rt5670->bclk[dai->id] = rt5670->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5670->bclk[dai->id], rt5670->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5670_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5670_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5670_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5670_AIF1:
		mask_clk = RT5670_I2S_BCLK_MS1_MASK | RT5670_I2S_PD1_MASK;
		val_clk = bclk_ms << RT5670_I2S_BCLK_MS1_SFT |
			pre_div << RT5670_I2S_PD1_SFT;
		snd_soc_update_bits(codec, RT5670_I2S1_SDP,
			RT5670_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5670_ADDA_CLK1, mask_clk, val_clk);
		break;
	case RT5670_AIF2:
		mask_clk = RT5670_I2S_BCLK_MS2_MASK | RT5670_I2S_PD2_MASK;
		val_clk = bclk_ms << RT5670_I2S_BCLK_MS2_SFT |
			pre_div << RT5670_I2S_PD2_SFT;
		snd_soc_update_bits(codec, RT5670_I2S2_SDP,
			RT5670_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5670_ADDA_CLK1, mask_clk, val_clk);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5670_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5670->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5670_I2S_MS_S;
		rt5670->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5670_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5670_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5670_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5670_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5670_AIF1:
		snd_soc_update_bits(codec, RT5670_I2S1_SDP,
			RT5670_I2S_MS_MASK | RT5670_I2S_BP_MASK |
			RT5670_I2S_DF_MASK, reg_val);
		break;
	case RT5670_AIF2:
		snd_soc_update_bits(codec, RT5670_I2S2_SDP,
			RT5670_I2S_MS_MASK | RT5670_I2S_BP_MASK |
			RT5670_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5670_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (clk_id) {
	case RT5670_SCLK_S_MCLK:
		reg_val |= RT5670_SCLK_SRC_MCLK;
		break;
	case RT5670_SCLK_S_PLL1:
		reg_val |= RT5670_SCLK_SRC_PLL1;
		break;
	case RT5670_SCLK_S_RCCLK:
		reg_val |= RT5670_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5670_GLB_CLK,
		RT5670_SCLK_SRC_MASK, reg_val);
	rt5670->sysclk = freq;
	if (clk_id != RT5670_SCLK_S_RCCLK)
		rt5670->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5670_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5670->pll_src && freq_in == rt5670->pll_in &&
	    freq_out == rt5670->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5670->pll_in = 0;
		rt5670->pll_out = 0;
		snd_soc_update_bits(codec, RT5670_GLB_CLK,
			RT5670_SCLK_SRC_MASK, RT5670_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5670_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT5670_GLB_CLK,
			RT5670_PLL1_SRC_MASK, RT5670_PLL1_SRC_MCLK);
		break;
	case RT5670_PLL1_S_BCLK1:
	case RT5670_PLL1_S_BCLK2:
	case RT5670_PLL1_S_BCLK3:
	case RT5670_PLL1_S_BCLK4:
		switch (dai->id) {
		case RT5670_AIF1:
			snd_soc_update_bits(codec, RT5670_GLB_CLK,
				RT5670_PLL1_SRC_MASK, RT5670_PLL1_SRC_BCLK1);
			break;
		case RT5670_AIF2:
			snd_soc_update_bits(codec, RT5670_GLB_CLK,
				RT5670_PLL1_SRC_MASK, RT5670_PLL1_SRC_BCLK2);
			break;
		default:
			dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
			return -EINVAL;
		}
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_write(codec, RT5670_PLL_CTRL1,
		pll_code.n_code << RT5670_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT5670_PLL_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5670_PLL_M_SFT |
		pll_code.m_bp << RT5670_PLL_M_BP_SFT);

	rt5670->pll_in = freq_in;
	rt5670->pll_out = freq_out;
	rt5670->pll_src = source;

	return 0;
}

static int rt5670_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val = 0;

	if (rx_mask || tx_mask)
		val |= (1 << 14);

	switch (slots) {
	case 4:
		val |= (1 << 12);
		break;
	case 6:
		val |= (2 << 12);
		break;
	case 8:
		val |= (3 << 12);
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width) {
	case 20:
		val |= (1 << 10);
		break;
	case 24:
		val |= (2 << 10);
		break;
	case 32:
		val |= (3 << 10);
		break;
	case 16:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5670_TDM_CTRL_1, 0x7c00, val);

	return 0;
}

static int rt5670_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (SND_SOC_BIAS_STANDBY == snd_soc_codec_get_bias_level(codec)) {
			snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_PWR_VREF1 | RT5670_PWR_MB |
				RT5670_PWR_BG | RT5670_PWR_VREF2,
				RT5670_PWR_VREF1 | RT5670_PWR_MB |
				RT5670_PWR_BG | RT5670_PWR_VREF2);
			mdelay(10);
			snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_PWR_FV1 | RT5670_PWR_FV2,
				RT5670_PWR_FV1 | RT5670_PWR_FV2);
			snd_soc_update_bits(codec, RT5670_CHARGE_PUMP,
				RT5670_OSW_L_MASK | RT5670_OSW_R_MASK,
				RT5670_OSW_L_DIS | RT5670_OSW_R_DIS);
			snd_soc_update_bits(codec, RT5670_DIG_MISC, 0x1, 0x1);
			snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_LDO_SEL_MASK, 0x3);
		}
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_PWR_VREF1 | RT5670_PWR_VREF2 |
				RT5670_PWR_FV1 | RT5670_PWR_FV2, 0);
		snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_LDO_SEL_MASK, 0x1);
		break;
	case SND_SOC_BIAS_OFF:
		if (rt5670->pdata.jd_mode)
			snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_PWR_VREF1 | RT5670_PWR_MB |
				RT5670_PWR_BG | RT5670_PWR_VREF2 |
				RT5670_PWR_FV1 | RT5670_PWR_FV2,
				RT5670_PWR_MB | RT5670_PWR_BG);
		else
			snd_soc_update_bits(codec, RT5670_PWR_ANLG1,
				RT5670_PWR_VREF1 | RT5670_PWR_MB |
				RT5670_PWR_BG | RT5670_PWR_VREF2 |
				RT5670_PWR_FV1 | RT5670_PWR_FV2, 0);

		snd_soc_update_bits(codec, RT5670_DIG_MISC, 0x1, 0x0);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5670_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	switch (snd_soc_read(codec, RT5670_RESET) & RT5670_ID_MASK) {
	case RT5670_ID_5670:
	case RT5670_ID_5671:
		snd_soc_dapm_new_controls(dapm,
			rt5670_specific_dapm_widgets,
			ARRAY_SIZE(rt5670_specific_dapm_widgets));
		snd_soc_dapm_add_routes(dapm,
			rt5670_specific_dapm_routes,
			ARRAY_SIZE(rt5670_specific_dapm_routes));
		break;
	case RT5670_ID_5672:
		snd_soc_dapm_new_controls(dapm,
			rt5672_specific_dapm_widgets,
			ARRAY_SIZE(rt5672_specific_dapm_widgets));
		snd_soc_dapm_add_routes(dapm,
			rt5672_specific_dapm_routes,
			ARRAY_SIZE(rt5672_specific_dapm_routes));
		break;
	default:
		dev_err(codec->dev,
			"The driver is for RT5670 RT5671 or RT5672 only\n");
		return -ENODEV;
	}
	rt5670->codec = codec;

	return 0;
}

static int rt5670_remove(struct snd_soc_codec *codec)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	regmap_write(rt5670->regmap, RT5670_RESET, 0);
	snd_soc_jack_free_gpios(rt5670->jack, 1, &rt5670->hp_gpio);
	return 0;
}

#ifdef CONFIG_PM
static int rt5670_suspend(struct snd_soc_codec *codec)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5670->regmap, true);
	regcache_mark_dirty(rt5670->regmap);
	return 0;
}

static int rt5670_resume(struct snd_soc_codec *codec)
{
	struct rt5670_priv *rt5670 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5670->regmap, false);
	regcache_sync(rt5670->regmap);

	return 0;
}
#else
#define rt5670_suspend NULL
#define rt5670_resume NULL
#endif

#define RT5670_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5670_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5670_aif_dai_ops = {
	.hw_params = rt5670_hw_params,
	.set_fmt = rt5670_set_dai_fmt,
	.set_sysclk = rt5670_set_dai_sysclk,
	.set_tdm_slot = rt5670_set_tdm_slot,
	.set_pll = rt5670_set_dai_pll,
};

static struct snd_soc_dai_driver rt5670_dai[] = {
	{
		.name = "rt5670-aif1",
		.id = RT5670_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5670_STEREO_RATES,
			.formats = RT5670_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5670_STEREO_RATES,
			.formats = RT5670_FORMATS,
		},
		.ops = &rt5670_aif_dai_ops,
	},
	{
		.name = "rt5670-aif2",
		.id = RT5670_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5670_STEREO_RATES,
			.formats = RT5670_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5670_STEREO_RATES,
			.formats = RT5670_FORMATS,
		},
		.ops = &rt5670_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5670 = {
	.probe = rt5670_probe,
	.remove = rt5670_remove,
	.suspend = rt5670_suspend,
	.resume = rt5670_resume,
	.set_bias_level = rt5670_set_bias_level,
	.idle_bias_off = true,
	.controls = rt5670_snd_controls,
	.num_controls = ARRAY_SIZE(rt5670_snd_controls),
	.dapm_widgets = rt5670_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5670_dapm_widgets),
	.dapm_routes = rt5670_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5670_dapm_routes),
};

static const struct regmap_config rt5670_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_rw = true,
	.max_register = RT5670_VENDOR_ID2 + 1 + (ARRAY_SIZE(rt5670_ranges) *
					       RT5670_PR_SPACING),
	.volatile_reg = rt5670_volatile_register,
	.readable_reg = rt5670_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5670_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5670_reg),
	.ranges = rt5670_ranges,
	.num_ranges = ARRAY_SIZE(rt5670_ranges),
};

static const struct i2c_device_id rt5670_i2c_id[] = {
	{ "rt5670", 0 },
	{ "rt5671", 0 },
	{ "rt5672", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5670_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt5670_acpi_match[] = {
	{ "10EC5670", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt5670_acpi_match);
#endif

static const struct dmi_system_id dmi_platform_intel_braswell[] = {
	{
		.ident = "Intel Braswell",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Braswell CRB"),
		},
	},
	{}
};

static int rt5670_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5670_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5670_priv *rt5670;
	int ret;
	unsigned int val;

	rt5670 = devm_kzalloc(&i2c->dev,
				sizeof(struct rt5670_priv),
				GFP_KERNEL);
	if (NULL == rt5670)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5670);

	if (pdata)
		rt5670->pdata = *pdata;

	if (dmi_check_system(dmi_platform_intel_braswell)) {
		rt5670->pdata.dmic_en = true;
		rt5670->pdata.dmic1_data_pin = RT5670_DMIC_DATA_IN2P;
		rt5670->pdata.dev_gpio = true;
		rt5670->pdata.jd_mode = 1;
	}

	rt5670->regmap = devm_regmap_init_i2c(i2c, &rt5670_regmap);
	if (IS_ERR(rt5670->regmap)) {
		ret = PTR_ERR(rt5670->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5670->regmap, RT5670_VENDOR_ID2, &val);
	if (val != RT5670_DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt5670/72\n", val);
		return -ENODEV;
	}

	regmap_write(rt5670->regmap, RT5670_RESET, 0);
	regmap_update_bits(rt5670->regmap, RT5670_PWR_ANLG1,
		RT5670_PWR_HP_L | RT5670_PWR_HP_R |
		RT5670_PWR_VREF2, RT5670_PWR_VREF2);
	msleep(100);

	regmap_write(rt5670->regmap, RT5670_RESET, 0);

	regmap_read(rt5670->regmap, RT5670_VENDOR_ID, &val);
	if (val >= 4)
		regmap_write(rt5670->regmap, RT5670_GPIO_CTRL3, 0x0980);
	else
		regmap_write(rt5670->regmap, RT5670_GPIO_CTRL3, 0x0d00);

	ret = regmap_register_patch(rt5670->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	if (rt5670->pdata.in2_diff)
		regmap_update_bits(rt5670->regmap, RT5670_IN2,
					RT5670_IN_DF2, RT5670_IN_DF2);

	if (rt5670->pdata.dev_gpio) {
		/* for push button */
		regmap_write(rt5670->regmap, RT5670_IL_CMD, 0x0000);
		regmap_write(rt5670->regmap, RT5670_IL_CMD2, 0x0010);
		regmap_write(rt5670->regmap, RT5670_IL_CMD3, 0x0014);
		/* for irq */
		regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
				   RT5670_GP1_PIN_MASK, RT5670_GP1_PIN_IRQ);
		regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL2,
				   RT5670_GP1_PF_MASK, RT5670_GP1_PF_OUT);
		regmap_update_bits(rt5670->regmap, RT5670_DIG_MISC, 0x8, 0x8);
	}

	if (rt5670->pdata.jd_mode) {
		regmap_update_bits(rt5670->regmap, RT5670_GLB_CLK,
				   RT5670_SCLK_SRC_MASK, RT5670_SCLK_SRC_RCCLK);
		rt5670->sysclk = 0;
		rt5670->sysclk_src = RT5670_SCLK_S_RCCLK;
		regmap_update_bits(rt5670->regmap, RT5670_PWR_ANLG1,
				   RT5670_PWR_MB, RT5670_PWR_MB);
		regmap_update_bits(rt5670->regmap, RT5670_PWR_ANLG2,
				   RT5670_PWR_JD1, RT5670_PWR_JD1);
		regmap_update_bits(rt5670->regmap, RT5670_IRQ_CTRL1,
				   RT5670_JD1_1_EN_MASK, RT5670_JD1_1_EN);
		regmap_update_bits(rt5670->regmap, RT5670_JD_CTRL3,
				   RT5670_JD_TRI_CBJ_SEL_MASK |
				   RT5670_JD_TRI_HPO_SEL_MASK,
				   RT5670_JD_CBJ_JD1_1 | RT5670_JD_HPO_JD1_1);
		switch (rt5670->pdata.jd_mode) {
		case 1:
			regmap_update_bits(rt5670->regmap, RT5670_A_JD_CTRL1,
					   RT5670_JD1_MODE_MASK,
					   RT5670_JD1_MODE_0);
			break;
		case 2:
			regmap_update_bits(rt5670->regmap, RT5670_A_JD_CTRL1,
					   RT5670_JD1_MODE_MASK,
					   RT5670_JD1_MODE_1);
			break;
		case 3:
			regmap_update_bits(rt5670->regmap, RT5670_A_JD_CTRL1,
					   RT5670_JD1_MODE_MASK,
					   RT5670_JD1_MODE_2);
			break;
		default:
			break;
		}
	}

	if (rt5670->pdata.dmic_en) {
		regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
				   RT5670_GP2_PIN_MASK,
				   RT5670_GP2_PIN_DMIC1_SCL);

		switch (rt5670->pdata.dmic1_data_pin) {
		case RT5670_DMIC_DATA_IN2P:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL1,
					   RT5670_DMIC_1_DP_MASK,
					   RT5670_DMIC_1_DP_IN2P);
			break;

		case RT5670_DMIC_DATA_GPIO6:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL1,
					   RT5670_DMIC_1_DP_MASK,
					   RT5670_DMIC_1_DP_GPIO6);
			regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
					   RT5670_GP6_PIN_MASK,
					   RT5670_GP6_PIN_DMIC1_SDA);
			break;

		case RT5670_DMIC_DATA_GPIO7:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL1,
					   RT5670_DMIC_1_DP_MASK,
					   RT5670_DMIC_1_DP_GPIO7);
			regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
					   RT5670_GP7_PIN_MASK,
					   RT5670_GP7_PIN_DMIC1_SDA);
			break;

		default:
			break;
		}

		switch (rt5670->pdata.dmic2_data_pin) {
		case RT5670_DMIC_DATA_IN3N:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL1,
					   RT5670_DMIC_2_DP_MASK,
					   RT5670_DMIC_2_DP_IN3N);
			break;

		case RT5670_DMIC_DATA_GPIO8:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL1,
					   RT5670_DMIC_2_DP_MASK,
					   RT5670_DMIC_2_DP_GPIO8);
			regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
					   RT5670_GP8_PIN_MASK,
					   RT5670_GP8_PIN_DMIC2_SDA);
			break;

		default:
			break;
		}

		switch (rt5670->pdata.dmic3_data_pin) {
		case RT5670_DMIC_DATA_GPIO5:
			regmap_update_bits(rt5670->regmap, RT5670_DMIC_CTRL2,
					   RT5670_DMIC_3_DP_MASK,
					   RT5670_DMIC_3_DP_GPIO5);
			regmap_update_bits(rt5670->regmap, RT5670_GPIO_CTRL1,
					   RT5670_GP5_PIN_MASK,
					   RT5670_GP5_PIN_DMIC3_SDA);
			break;

		case RT5670_DMIC_DATA_GPIO9:
		case RT5670_DMIC_DATA_GPIO10:
			dev_err(&i2c->dev,
				"Always use GPIO5 as DMIC3 data pin\n");
			break;

		default:
			break;
		}

	}

	pm_runtime_enable(&i2c->dev);
	pm_request_idle(&i2c->dev);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5670,
			rt5670_dai, ARRAY_SIZE(rt5670_dai));
	if (ret < 0)
		goto err;

	pm_runtime_put(&i2c->dev);

	return 0;
err:
	pm_runtime_disable(&i2c->dev);

	return ret;
}

static int rt5670_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static struct i2c_driver rt5670_i2c_driver = {
	.driver = {
		.name = "rt5670",
		.acpi_match_table = ACPI_PTR(rt5670_acpi_match),
	},
	.probe = rt5670_i2c_probe,
	.remove   = rt5670_i2c_remove,
	.id_table = rt5670_i2c_id,
};

module_i2c_driver(rt5670_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5670 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
