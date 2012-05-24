/*
 * linux/sound/soc/codecs/tlv320aic326x.c
 *
 * Copyright (C) 2011 Texas Instruments Inc.,
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * The TLV320AIC3262 is a flexible, low-power, low-voltage stereo audio
 * codec with digital microphone inputs and programmable outputs.
 *
 * History:
 *
 * Rev 0.1   ASoC driver support    TI         20-01-2011
 *
 *			 The AIC325x ASoC driver is ported for the codec AIC3262.
 * Rev 0.2   ASoC driver support    TI         21-03-2011
 *			 The AIC326x ASoC driver is updated for linux 2.6.32 Kernel.
 * Rev 0.3   ASoC driver support    TI	    20-04-2011
 *			 The AIC326x ASoC driver is ported to 2.6.35 omap4 kernel
 */

/*
 *****************************************************************************
 * INCLUDES
 *****************************************************************************
 */
//#define DEBUG 1

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <sound/jack.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include <sound/tlv.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/mfd/tlv320aic3262-registers.h>
#include <linux/mfd/tlv320aic3262-core.h>
#include "aic3xxx_cfw.h"
#include "aic3xxx_cfw_ops.h"

#include "tlv320aic326x.h"
#include "aic3262_codec_ops.h"
#include "tlv320aic3262_default_fw.h"


#ifdef AIC3262_TiLoad
extern int aic3262_driver_init(struct snd_soc_codec *codec);
#endif

#define AIC3262_PROC
#ifdef AIC3262_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
char debug_write_read = 0;
#endif

static struct snd_soc_codec *aic3262_codec;

/*
 *******************************************************************************
 * Global Variable							       *	
 *******************************************************************************
 */
static u32 aic3262_reg_ctl;

/* whenever aplay/arecord is run, aic3262_hw_params() function gets called.
 * This function reprograms the clock dividers etc. this flag can be used to
 * disable this when the clock dividers are programmed by pps config file
 */
//static int soc_static_freq_config = 1;

/******************************************************************************
			 Macros
******************************************************************************/


/* ASoC Widget Control definition for a single Register based Control */
#define SOC_SINGLE_AIC3262(xname) \
{\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = __new_control_info, .get = __new_control_get,\
	.put = __new_control_put, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
}
/*
 *****************************************************************************
 * Function Prototype
 *****************************************************************************
 */
static int aic3262_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai);

static int aic3262_mute(struct snd_soc_dai *dai, int mute);

static int aic3262_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir);

static int aic3262_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt);

static int aic3262_dai_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
                          unsigned int Fin, unsigned int Fout);      

static int aic3262_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level);

unsigned int aic3262_codec_read(struct snd_soc_codec *codec, unsigned int reg);


static int __new_control_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo);

static int __new_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);

static int __new_control_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);
int aic3262_change_book(struct snd_soc_codec *codec, u8 new_book);


void aic3262_firmware_load(const struct firmware *fw, void *context);
static int aic3262_test_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);
static int aic3262_test_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);
static int aic3262_set_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);
static int aic3262_set_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol);

static int aic326x_adc_dsp_event(struct snd_soc_dapm_widget *w,struct snd_kcontrol *kcontrol, int event);
/*static int aic326x_adcl_event(struct snd_soc_dapm_widget *w,struct snd_kcontrol *kcontrol, int event);
static int aic326x_adcr_event(struct snd_soc_dapm_widget *w,struct snd_kcontrol *kcontrol, int event);*/
static int __new_control_info(struct snd_kcontrol *kcontrol,struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65535;

	return 0;
}

//static long debug_level = 0;
//module_param(debug_level, int, 0);
//MODULE_PARM_DESC(debug_level, "Debug level for printing");

/*
 *----------------------------------------------------------------------------
 * Function : __new_control_get
 * Purpose  : This function is to read data of new control for
 *            program the AIC3262 registers.
 *
 *----------------------------------------------------------------------------
 */
static int __new_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u32 val;
	val = snd_soc_read(codec, aic3262_reg_ctl);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : __new_control_put
 * Purpose  : new_control_put is called to pass data from user/application to
 *            the driver.
 *
 *----------------------------------------------------------------------------
 */
static int __new_control_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u32 data_from_user = ucontrol->value.integer.value[0];
	u8 val = data_from_user & 0x00ff;
	u32 reg = data_from_user >> 8;//MAKE_REG(book,page,offset)
	snd_soc_write(codec, reg, val);
	aic3262_reg_ctl = reg;

	return 0;
}

/*static ssize_t debug_level_show(struct device *dev, 
		struct device_attribute *attr,
		char *buf, size_t count)
{
	return sprintf(buf, "%ld\n", debug_level);
}

static ssize_t debug_level_set(struct device *dev, 
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret;
	ret = strict_strtol(buf, 10, &debug_level);
	if(ret)
		return ret;
	return count;
}*/  //sxj

//static DEVICE_ATTR(debug_level,0644, debug_level_show, debug_level_set);

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -6350, 50, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1200, 50, 0);
static const DECLARE_TLV_DB_SCALE(spk_gain_tlv, 600, 600, 0);
static const DECLARE_TLV_DB_SCALE(output_gain_tlv, -600, 100, 1);
static const DECLARE_TLV_DB_SCALE(micpga_gain_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_SCALE(adc_fine_gain_tlv, -40, 10, 0);
static const DECLARE_TLV_DB_SCALE(beep_gen_volume_tlv, -6300, 100, 0);

/* Chip-level Input and Output CM Mode Controls */
static const char *input_common_mode_text[] = {
	"0.9v", "0.75v" };

static const char *output_common_mode_text[] = {
	"Input CM", "1.25v", "1.5v", "1.65v" };

static const struct soc_enum input_cm_mode =
	SOC_ENUM_SINGLE(AIC3262_CM_REG, 2, 2, input_common_mode_text);	
	
static const struct soc_enum output_cm_mode =
	SOC_ENUM_SINGLE(AIC3262_CM_REG, 0, 4, output_common_mode_text);		
/*
 *****************************************************************************
 * Structure Initialization
 *****************************************************************************
 */
static const struct snd_kcontrol_new aic3262_snd_controls[] = {
	/* Output */
#ifndef DAC_INDEPENDENT_VOL
	/* sound new kcontrol for PCM Playback volume control */

	SOC_DOUBLE_R_SX_TLV("PCM Playback Volume", 
			AIC3262_DAC_LVOL, AIC3262_DAC_RVOL, 8,0xffffff81, 0x30, dac_vol_tlv),
#endif
	/*HP Driver Gain Control*/
	SOC_DOUBLE_R_SX_TLV("HeadPhone Driver Amplifier Volume", AIC3262_HPL_VOL, AIC3262_HPR_VOL, 6, 0xffffffb9, 0xffffffce, output_gain_tlv),
	/*LO Driver Gain Control*/
	SOC_DOUBLE_TLV("Speaker Amplifier Volume", AIC3262_SPK_AMP_CNTL_R4, 4, 0, 5, 0, spk_gain_tlv),

	SOC_DOUBLE_R_SX_TLV("Receiver Amplifier Volume", AIC3262_REC_AMP_CNTL_R5, AIC3262_RAMPR_VOL, 6, 0xffffffb9, 0xffffffd6, output_gain_tlv),

	SOC_DOUBLE_R_SX_TLV("PCM Capture Volume", 
			AIC3262_LADC_VOL, AIC3262_RADC_VOL, 7,0xffffff68, 0xffffffa8, adc_vol_tlv),


	SOC_DOUBLE_R_TLV ("MicPGA Volume Control",                                                                                                           
			AIC3262_MICL_PGA, AIC3262_MICR_PGA, 0, 0x5F, 0, micpga_gain_tlv),        
	SOC_DOUBLE_TLV("PCM Capture Fine Gain Volume", AIC3262_ADC_FINE_GAIN, 4, 0, 5, 1, adc_fine_gain_tlv),
	SOC_DOUBLE("ADC channel mute", AIC3262_ADC_FINE_GAIN, 7, 3, 1, 0),

	SOC_DOUBLE("DAC MUTE", AIC3262_DAC_MVOL_CONF, 2, 3, 1, 1),

	/* sound new kcontrol for Programming the registers from user space */
	SOC_SINGLE_AIC3262("Program Registers"),

	SOC_SINGLE("RESET", AIC3262_RESET_REG, 0,1,0),

	SOC_SINGLE("DAC VOL SOFT STEPPING", AIC3262_DAC_MVOL_CONF, 0, 2, 0),


	SOC_SINGLE("DAC AUTO MUTE CONTROL", AIC3262_DAC_MVOL_CONF, 4, 7, 0),
	SOC_SINGLE("RIGHT MODULATOR SETUP", AIC3262_DAC_MVOL_CONF, 7, 1, 0),

	SOC_SINGLE("ADC Volume soft stepping", AIC3262_ADC_CHANNEL_POW, 0, 3, 0),

	SOC_SINGLE("Mic Bias ext independent enable", AIC3262_MIC_BIAS_CNTL, 7, 1, 0),
	//	SOC_SINGLE("MICBIAS_EXT ON", MIC_BIAS_CNTL, 6, 1, 0),
	SOC_SINGLE("MICBIAS EXT Power Level", AIC3262_MIC_BIAS_CNTL, 4, 3, 0),

	//	SOC_SINGLE("MICBIAS_INT ON", MIC_BIAS_CNTL, 2, 1, 0),
	SOC_SINGLE("MICBIAS INT Power Level", AIC3262_MIC_BIAS_CNTL, 0, 3, 0),

	SOC_DOUBLE("DRC_EN_CTL", AIC3262_DRC_CNTL_R1, 6, 5, 1, 0),
	SOC_SINGLE("DRC_THRESHOLD_LEVEL", AIC3262_DRC_CNTL_R1, 2, 7, 1),
	SOC_SINGLE("DRC_HYSTERISIS_LEVEL", AIC3262_DRC_CNTL_R1, 0, 7, 0),

	SOC_SINGLE("DRC_HOLD_LEVEL", AIC3262_DRC_CNTL_R2, 3, 0x0F, 0),
	SOC_SINGLE("DRC_GAIN_RATE", AIC3262_DRC_CNTL_R2, 0, 4, 0),
	SOC_SINGLE("DRC_ATTACK_RATE", AIC3262_DRC_CNTL_R3, 4, 0x0F, 1),
	SOC_SINGLE("DRC_DECAY_RATE", AIC3262_DRC_CNTL_R3, 0, 0x0F, 1),

	SOC_SINGLE("BEEP_GEN_EN", AIC3262_BEEP_CNTL_R1, 7, 1, 0),
	SOC_DOUBLE_R("BEEP_VOL_CNTL", AIC3262_BEEP_CNTL_R1, AIC3262_BEEP_CNTL_R2, 0, 0x0F, 1),
	SOC_SINGLE("BEEP_MAS_VOL", AIC3262_BEEP_CNTL_R2, 6, 3, 0),

	SOC_DOUBLE_R("AGC_EN", AIC3262_LAGC_CNTL, AIC3262_RAGC_CNTL, 7, 1, 0),
	SOC_DOUBLE_R("AGC_TARGET_LEVEL", AIC3262_LAGC_CNTL, AIC3262_RAGC_CNTL, 4, 7, 1),

	SOC_DOUBLE_R("AGC_GAIN_HYSTERESIS", AIC3262_LAGC_CNTL, AIC3262_RAGC_CNTL, 0, 3, 0),
	SOC_DOUBLE_R("AGC_HYSTERESIS", AIC3262_LAGC_CNTL_R2, AIC3262_RAGC_CNTL_R2, 6, 3, 0),
	SOC_DOUBLE_R("AGC_NOISE_THRESHOLD", AIC3262_LAGC_CNTL_R2, AIC3262_RAGC_CNTL_R2, 1, 31, 1),

	SOC_DOUBLE_R("AGC_MAX_GAIN", AIC3262_LAGC_CNTL_R3, AIC3262_RAGC_CNTL_R3, 0, 116, 0),
	SOC_DOUBLE_R("AGC_ATCK_TIME", AIC3262_LAGC_CNTL_R4, AIC3262_RAGC_CNTL_R4, 3, 31, 0),
	SOC_DOUBLE_R("AGC_ATCK_SCALE_FACTOR", AIC3262_LAGC_CNTL_R4, AIC3262_RAGC_CNTL_R4, 0, 7, 0),

	SOC_DOUBLE_R("AGC_DECAY_TIME", AIC3262_LAGC_CNTL_R5, AIC3262_RAGC_CNTL_R5, 3, 31, 0),
	SOC_DOUBLE_R("AGC_DECAY_SCALE_FACTOR", AIC3262_LAGC_CNTL_R5, AIC3262_RAGC_CNTL_R5, 0, 7, 0),
	SOC_DOUBLE_R("AGC_NOISE_DEB_TIME", AIC3262_LAGC_CNTL_R6, AIC3262_RAGC_CNTL_R6, 0, 31, 0),

	SOC_DOUBLE_R("AGC_SGL_DEB_TIME", AIC3262_LAGC_CNTL_R7, AIC3262_RAGC_CNTL_R7, 0, 0x0F, 0),

	SOC_SINGLE("DAC PRB Selection",AIC3262_DAC_PRB, 0, 26, 0),
	SOC_SINGLE("ADC PRB Selection",AIC3262_ADC_PRB, 0, 18, 0),
	SOC_ENUM("Input CM mode", input_cm_mode),
	SOC_ENUM("Output CM mode", output_cm_mode),

	SOC_SINGLE_EXT("FIRMWARE LOAD",SND_SOC_NOPM,0,0,0,aic3262_test_get,aic3262_test_put),
	SOC_SINGLE_EXT("FIRMWARE SET MODE",SND_SOC_NOPM,0,0xffff,0,aic3262_set_mode_get,aic3262_set_mode_put), 


};
/*
 *----------------------------------------------------------------------------
 * @struct  snd_soc_codec_dai |
 *          It is SoC Codec DAI structure which has DAI capabilities viz.,
 *          playback and capture, DAI runtime information viz. state of DAI
 *			and pop wait state, and DAI private data.
 *          The AIC3262 rates ranges from 8k to 192k
 *          The PCM bit format supported are 16, 20, 24 and 32 bits
 *----------------------------------------------------------------------------
 */
struct snd_soc_dai_ops aic3262_asi1_dai_ops = {
	.hw_params = aic3262_hw_params,
	.digital_mute = aic3262_mute,
	.set_sysclk = aic3262_set_dai_sysclk,
	.set_fmt = aic3262_set_dai_fmt,
	.set_pll = aic3262_dai_set_pll,
};
struct snd_soc_dai_ops aic3262_asi2_dai_ops = {
	.hw_params = aic3262_hw_params,
	.digital_mute = aic3262_mute,
	.set_sysclk = aic3262_set_dai_sysclk,
	.set_fmt = aic3262_set_dai_fmt,
	.set_pll = aic3262_dai_set_pll,
};
struct snd_soc_dai_ops aic3262_asi3_dai_ops = {
	.hw_params = aic3262_hw_params,
	.digital_mute = aic3262_mute,
	.set_sysclk = aic3262_set_dai_sysclk,
	.set_fmt = aic3262_set_dai_fmt,
	.set_pll = aic3262_dai_set_pll,
};

struct snd_soc_dai_driver aic326x_dai_driver[] = {
	{
		.name = "aic326x-asi1",
		.playback = {
			.stream_name = "ASI1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.capture = {
			.stream_name = "ASI1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.ops = &aic3262_asi1_dai_ops,
	},
	{
		.name = "aic326x-asi2",
		.playback = {
			.stream_name = "ASI2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.capture = {
			.stream_name = "ASI2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.ops = &aic3262_asi2_dai_ops,
	},
	{
		.name = "aic326x-asi3",
		.playback = {
			.stream_name = "ASI3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.capture = {
			.stream_name = "ASI3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AIC3262_RATES,
			.formats = AIC3262_FORMATS,
		},
		.ops = &aic3262_asi3_dai_ops,
	},

};

/*
 *****************************************************************************
 * Initializations
 *****************************************************************************
 */
/*
 * AIC3262 register cache
 * We are caching the registers here.
 * There is no point in caching the reset register.
 *
 * NOTE: In AIC3262, there are 127 registers supported in both page0 and page1
 *       The following table contains the page0 and page 1 and page 3
 *       registers values.
 */

static const u8 aic3262_reg[AIC3262_CACHEREGNUM] = {
	0x00, 0x00, 0x10, 0x00,	/* 0 */
	0x03, 0x40, 0x11, 0x08,	/* 4 */
	0x00, 0x00, 0x00, 0x82,	/* 8 */
	0x88, 0x00, 0x80, 0x02,	/* 12 */
	0x00, 0x08, 0x01, 0x01,	/* 16 */
	0x80, 0x01, 0x00, 0x04,	/* 20 */
	0x00, 0x00, 0x01, 0x00,	/* 24 */
	0x00, 0x00, 0x01, 0x00,	/* 28 */
	0x00, 0x00, 0x00, 0x00,	/* 32 */
	0x00, 0x00, 0x00, 0x00,	/* 36 */
	0x00, 0x00, 0x00, 0x00,	/* 40 */
	0x00, 0x00, 0x00, 0x00,	/* 44 */
	0x00, 0x00, 0x00, 0x00,	/* 48 */
	0x00, 0x42, 0x02, 0x02,	/* 52 */
	0x42, 0x02, 0x02, 0x02,	/* 56 */
	0x00, 0x00, 0x00, 0x01,	/* 60 */
	0x01, 0x00, 0x14, 0x00,	/* 64 */
	0x0C, 0x00, 0x00, 0x00,	/* 68 */
	0x00, 0x00, 0x00, 0xEE,	/* 72 */
	0x10, 0xD8, 0x10, 0xD8,	/* 76 */
	0x00, 0x00, 0x88, 0x00,	/* 80 */
	0x00, 0x00, 0x00, 0x00,	/* 84 */
	0x7F, 0x00, 0x00, 0x00,	/* 88 */
	0x00, 0x00, 0x00, 0x00,	/* 92 */
	0x7F, 0x00, 0x00, 0x00,	/* 96 */
	0x00, 0x00, 0x00, 0x00,	/* 100 */
	0x00, 0x00, 0x00, 0x00,	/* 104 */
	0x00, 0x00, 0x00, 0x00,	/* 108 */
	0x00, 0x00, 0x00, 0x00,	/* 112 */
	0x00, 0x00, 0x00, 0x00,	/* 116 */
	0x00, 0x00, 0x00, 0x00,	/* 120 */
	0x00, 0x00, 0x00, 0x00,	/* 124 - PAGE0 Registers(127) ends here */
	0x01, 0x00, 0x08, 0x00,	/* 128, PAGE1-0 */
	0x00, 0x00, 0x00, 0x00,	/* 132, PAGE1-4 */
	0x00, 0x00, 0x00, 0x10,	/* 136, PAGE1-8 */
	0x00, 0x00, 0x00, 0x00,	/* 140, PAGE1-12 */
	0x40, 0x40, 0x40, 0x40,	/* 144, PAGE1-16 */
	0x00, 0x00, 0x00, 0x00,	/* 148, PAGE1-20 */
	0x00, 0x00, 0x00, 0x00,	/* 152, PAGE1-24 */
	0x00, 0x00, 0x00, 0x00,	/* 156, PAGE1-28 */
	0x00, 0x00, 0x00, 0x00,	/* 160, PAGE1-32 */
	0x00, 0x00, 0x00, 0x00,	/* 164, PAGE1-36 */
	0x00, 0x00, 0x00, 0x00,	/* 168, PAGE1-40 */
	0x00, 0x00, 0x00, 0x00,	/* 172, PAGE1-44 */
	0x00, 0x00, 0x00, 0x00,	/* 176, PAGE1-48 */
	0x00, 0x00, 0x00, 0x00,	/* 180, PAGE1-52 */
	0x00, 0x00, 0x00, 0x80,	/* 184, PAGE1-56 */
	0x80, 0x00, 0x00, 0x00,	/* 188, PAGE1-60 */
	0x00, 0x00, 0x00, 0x00,	/* 192, PAGE1-64 */
	0x00, 0x00, 0x00, 0x00,	/* 196, PAGE1-68 */
	0x00, 0x00, 0x00, 0x00,	/* 200, PAGE1-72 */
	0x00, 0x00, 0x00, 0x00,	/* 204, PAGE1-76 */
	0x00, 0x00, 0x00, 0x00,	/* 208, PAGE1-80 */
	0x00, 0x00, 0x00, 0x00,	/* 212, PAGE1-84 */
	0x00, 0x00, 0x00, 0x00,	/* 216, PAGE1-88 */
	0x00, 0x00, 0x00, 0x00,	/* 220, PAGE1-92 */
	0x00, 0x00, 0x00, 0x00,	/* 224, PAGE1-96 */
	0x00, 0x00, 0x00, 0x00,	/* 228, PAGE1-100 */
	0x00, 0x00, 0x00, 0x00,	/* 232, PAGE1-104 */
	0x00, 0x00, 0x00, 0x00,	/* 236, PAGE1-108 */
	0x00, 0x00, 0x00, 0x00,	/* 240, PAGE1-112 */
	0x00, 0x00, 0x00, 0x00,	/* 244, PAGE1-116 */
	0x00, 0x00, 0x00, 0x00,	/* 248, PAGE1-120 */
	0x00, 0x00, 0x00, 0x00,	/* 252, PAGE1-124 Page 1 Registers Ends Here */
	0x00, 0x00, 0x00, 0x00,	/* 256, PAGE2-0  */
	0x00, 0x00, 0x00, 0x00,	/* 260, PAGE2-4  */
	0x00, 0x00, 0x00, 0x00,	/* 264, PAGE2-8  */
	0x00, 0x00, 0x00, 0x00,	/* 268, PAGE2-12 */
	0x00, 0x00, 0x00, 0x00,	/* 272, PAGE2-16 */
	0x00, 0x00, 0x00, 0x00,	/* 276, PAGE2-20 */
	0x00, 0x00, 0x00, 0x00,	/* 280, PAGE2-24 */
	0x00, 0x00, 0x00, 0x00,	/* 284, PAGE2-28 */
	0x00, 0x00, 0x00, 0x00,	/* 288, PAGE2-32 */
	0x00, 0x00, 0x00, 0x00,	/* 292, PAGE2-36 */
	0x00, 0x00, 0x00, 0x00,	/* 296, PAGE2-40 */
	0x00, 0x00, 0x00, 0x00,	/* 300, PAGE2-44 */
	0x00, 0x00, 0x00, 0x00,	/* 304, PAGE2-48 */
	0x00, 0x00, 0x00, 0x00,	/* 308, PAGE2-52 */
	0x00, 0x00, 0x00, 0x00,	/* 312, PAGE2-56 */
	0x00, 0x00, 0x00, 0x00,	/* 316, PAGE2-60 */
	0x00, 0x00, 0x00, 0x00,	/* 320, PAGE2-64 */
	0x00, 0x00, 0x00, 0x00,	/* 324, PAGE2-68 */
	0x00, 0x00, 0x00, 0x00,	/* 328, PAGE2-72 */
	0x00, 0x00, 0x00, 0x00,	/* 332, PAGE2-76 */
	0x00, 0x00, 0x00, 0x00,	/* 336, PAGE2-80 */
	0x00, 0x00, 0x00, 0x00,	/* 340, PAGE2-84 */
	0x00, 0x00, 0x00, 0x00,	/* 344, PAGE2-88 */
	0x00, 0x00, 0x00, 0x00,	/* 348, PAGE2-92 */
	0x00, 0x00, 0x00, 0x00,	/* 352, PAGE2-96 */
	0x00, 0x00, 0x00, 0x00,	/* 356, PAGE2-100 */
	0x00, 0x00, 0x00, 0x00,	/* 360, PAGE2-104 */
	0x00, 0x00, 0x00, 0x00,	/* 364, PAGE2-108 */
	0x00, 0x00, 0x00, 0x00,	/* 368, PAGE2-112*/
	0x00, 0x00, 0x00, 0x00,	/* 372, PAGE2-116*/
	0x00, 0x00, 0x00, 0x00,	/* 376, PAGE2-120*/
	0x00, 0x00, 0x00, 0x00,	/* 380, PAGE2-124 Page 2 Registers Ends Here */
	0x00, 0x00, 0x00, 0x00,	/* 384, PAGE3-0  */
	0x00, 0x00, 0x00, 0x00,	/* 388, PAGE3-4  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-8  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-12  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-16  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-20  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-24  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-28  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-32  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-36  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-40  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-44  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-48  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-52  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-56  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-60  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-64  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE3-68  */
	0x00, 0x00, 0x00, 0x00,	/* 328, PAGE3-72 */
	0x00, 0x00, 0x00, 0x00,	/* 332, PAGE3-76 */
	0x00, 0x00, 0x00, 0x00,	/* 336, PAGE3-80 */
	0x00, 0x00, 0x00, 0x00,	/* 340, PAGE3-84 */
	0x00, 0x00, 0x00, 0x00,	/* 344, PAGE3-88 */
	0x00, 0x00, 0x00, 0x00,	/* 348, PAGE3-92 */
	0x00, 0x00, 0x00, 0x00,	/* 352, PAGE3-96 */
	0x00, 0x00, 0x00, 0x00,	/* 356, PAGE3-100 */
	0x00, 0x00, 0x00, 0x00,	/* 360, PAGE3-104 */
	0x00, 0x00, 0x00, 0x00,	/* 364, PAGE3-108 */
	0x00, 0x00, 0x00, 0x00,	/* 368, PAGE3-112*/
	0x00, 0x00, 0x00, 0x00,	/* 372, PAGE3-116*/
	0x00, 0x00, 0x00, 0x00,	/* 376, PAGE3-120*/
	0x00, 0x00, 0x00, 0x00,	/* 380, PAGE3-124 Page 3 Registers Ends Here */
	0x00, 0x00, 0x00, 0x00,	/* 384, PAGE4-0  */
	0x00, 0x00, 0x00, 0x00,	/* 388, PAGE4-4  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-8  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-12  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-16  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-20  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-24  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-28  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-32  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-36  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-40  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-44  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-48  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-52  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-56  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-60  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-64  */
	0x00, 0x00, 0x00, 0x00,	/* 392, PAGE4-68  */
	0x00, 0x00, 0x00, 0x00,	/* 328, PAGE4-72 */
	0x00, 0x00, 0x00, 0x00,	/* 332, PAGE4-76 */
	0x00, 0x00, 0x00, 0x00,	/* 336, PAGE4-80 */
	0x00, 0x00, 0x00, 0x00,	/* 340, PAGE4-84 */
	0x00, 0x00, 0x00, 0x00,	/* 344, PAGE4-88 */
	0x00, 0x00, 0x00, 0x00,	/* 348, PAGE4-92 */
	0x00, 0x00, 0x00, 0x00,	/* 352, PAGE4-96 */
	0x00, 0x00, 0x00, 0x00,	/* 356, PAGE4-100 */
	0x00, 0x00, 0x00, 0x00,	/* 360, PAGE4-104 */
	0x00, 0x00, 0x00, 0x00,	/* 364, PAGE4-108 */
	0x00, 0x00, 0x00, 0x00,	/* 368, PAGE4-112*/
	0x00, 0x00, 0x00, 0x00,	/* 372, PAGE4-116*/
	0x00, 0x00, 0x00, 0x00,	/* 376, PAGE4-120*/
	0x00, 0x00, 0x00, 0x00,	/* 380, PAGE4-124 Page 2 Registers Ends Here */
};

static const unsigned int adc_ma_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 29, TLV_DB_SCALE_ITEM(-1450, 500, 0),
	30, 35, TLV_DB_SCALE_ITEM(-2060, 1000, 0),
	36, 38, TLV_DB_SCALE_ITEM(-2660, 2000, 0),
	39, 40, TLV_DB_SCALE_ITEM(-3610, 5000, 0),
};
static const DECLARE_TLV_DB_SCALE(lo_hp_tlv, -7830, 50, 0);
static const struct snd_kcontrol_new mal_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN1L Switch", AIC3262_MA_CNTL, 5, 1, 0),
	SOC_DAPM_SINGLE_TLV("Left MicPGA Volume", AIC3262_LADC_PGA_MAL_VOL, 0, 0x3f, 1, adc_ma_tlv),

};

static const struct snd_kcontrol_new mar_pga_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN1R Switch", AIC3262_MA_CNTL, 4, 1, 0),
	SOC_DAPM_SINGLE_TLV("Right MicPGA Volume", AIC3262_RADC_PGA_MAR_VOL, 0, 0x3f, 1, adc_ma_tlv),
};

/* Left HPL Mixer */
static const struct snd_kcontrol_new hpl_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("MAL Switch", AIC3262_HP_AMP_CNTL_R1, 7, 1, 0),
	SOC_DAPM_SINGLE("LDAC Switch", AIC3262_HP_AMP_CNTL_R1, 5, 1, 0),
	SOC_DAPM_SINGLE_TLV("LOL-B1 Volume", AIC3262_HP_AMP_CNTL_R2, 0, 0x7f, 1, lo_hp_tlv),
};

/* Right HPR Mixer */
static const struct snd_kcontrol_new hpr_output_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("LOR-B1 Volume", AIC3262_HP_AMP_CNTL_R3, 0, 0x7f, 1, lo_hp_tlv),
	SOC_DAPM_SINGLE("LDAC Switch", AIC3262_HP_AMP_CNTL_R1,	 2, 1, 0),
	SOC_DAPM_SINGLE("RDAC Switch", AIC3262_HP_AMP_CNTL_R1, 4, 1, 0),
	SOC_DAPM_SINGLE("MAR Switch", AIC3262_HP_AMP_CNTL_R1, 6, 1, 0),
};

/* Left LOL Mixer */
static const struct snd_kcontrol_new lol_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("MAL Switch", AIC3262_LINE_AMP_CNTL_R2, 7, 1, 0),
	SOC_DAPM_SINGLE("IN1L-B Switch", AIC3262_LINE_AMP_CNTL_R2, 3, 1,0),
	SOC_DAPM_SINGLE("LDAC Switch", AIC3262_LINE_AMP_CNTL_R1, 7, 1, 0),
	SOC_DAPM_SINGLE("RDAC Switch", AIC3262_LINE_AMP_CNTL_R1, 5, 1, 0),
};

/* Right LOR Mixer */
static const struct snd_kcontrol_new lor_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("LOL Switch", AIC3262_LINE_AMP_CNTL_R1, 2, 1, 0),
	SOC_DAPM_SINGLE("RDAC Switch", AIC3262_LINE_AMP_CNTL_R1, 6, 1, 0),
	SOC_DAPM_SINGLE("MAR Switch", AIC3262_LINE_AMP_CNTL_R2, 6, 1, 0),
	SOC_DAPM_SINGLE("IN1R-B Switch", AIC3262_LINE_AMP_CNTL_R2, 0, 1,0),
};

/* Left SPKL Mixer */
static const struct snd_kcontrol_new spkl_output_mixer_controls[] = {
	SOC_DAPM_SINGLE("MAL Switch", AIC3262_SPK_AMP_CNTL_R1, 7, 1, 0),
	SOC_DAPM_SINGLE_TLV("LOL Volume", AIC3262_SPK_AMP_CNTL_R2, 0, 0x7f,1, lo_hp_tlv),
	SOC_DAPM_SINGLE("SPR_IN Switch", AIC3262_SPK_AMP_CNTL_R1, 2, 1, 0),
};

/* Right SPKR Mixer */
static const struct snd_kcontrol_new spkr_output_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("LOR Volume", AIC3262_SPK_AMP_CNTL_R3, 0, 0x7f, 1, lo_hp_tlv),
	SOC_DAPM_SINGLE("MAR Switch", AIC3262_SPK_AMP_CNTL_R1, 6, 1, 0),
};

/* REC Mixer */
static const struct snd_kcontrol_new rec_output_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("LOL-B2 Volume", AIC3262_RAMP_CNTL_R1, 0, 0x7f,1, lo_hp_tlv),
	SOC_DAPM_SINGLE_TLV("IN1L Volume", AIC3262_IN1L_SEL_RM, 0, 0x7f, 1, lo_hp_tlv),
	SOC_DAPM_SINGLE_TLV("IN1R Volume", AIC3262_IN1R_SEL_RM, 0, 0x7f, 1, lo_hp_tlv),
	SOC_DAPM_SINGLE_TLV("LOR-B2 Volume", AIC3262_RAMP_CNTL_R2, 0,0x7f, 1,lo_hp_tlv),
};

/* Left Input Mixer */
static const struct snd_kcontrol_new left_input_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN1L Switch", AIC3262_LMIC_PGA_PIN, 6, 3, 0),
	SOC_DAPM_SINGLE("IN2L Switch", AIC3262_LMIC_PGA_PIN, 4, 3, 0),
	SOC_DAPM_SINGLE("IN3L Switch", AIC3262_LMIC_PGA_PIN, 2, 3, 0),
	SOC_DAPM_SINGLE("IN4L Switch", AIC3262_LMIC_PGA_PM_IN4, 5, 1, 0),
	SOC_DAPM_SINGLE("IN1R Switch", AIC3262_LMIC_PGA_PIN, 0, 3, 0),
	SOC_DAPM_SINGLE("IN2R Switch", AIC3262_LMIC_PGA_MIN, 4, 3, 0),
	SOC_DAPM_SINGLE("IN3R Switch", AIC3262_LMIC_PGA_MIN, 2, 3, 0),
	SOC_DAPM_SINGLE("IN4R Switch", AIC3262_LMIC_PGA_PM_IN4, 4, 1, 0),
	SOC_DAPM_SINGLE("CM2L Switch", AIC3262_LMIC_PGA_MIN, 0, 3, 0),  
	SOC_DAPM_SINGLE("CM1L Switch", AIC3262_LMIC_PGA_MIN, 6, 3, 0),
};

/* Right Input Mixer */
static const struct snd_kcontrol_new right_input_mixer_controls[] = {
	SOC_DAPM_SINGLE("IN1R Switch", AIC3262_RMIC_PGA_PIN, 6, 3, 0),
	SOC_DAPM_SINGLE("IN2R Switch", AIC3262_RMIC_PGA_PIN, 4, 3, 0),
	SOC_DAPM_SINGLE("IN3R Switch", AIC3262_RMIC_PGA_PIN, 2, 3, 0),
	SOC_DAPM_SINGLE("IN4R Switch", AIC3262_RMIC_PGA_PM_IN4, 5, 1, 0),
	SOC_DAPM_SINGLE("IN2L Switch", AIC3262_RMIC_PGA_PIN, 0, 3, 0),
	SOC_DAPM_SINGLE("IN1L Switch", AIC3262_RMIC_PGA_MIN, 4, 3, 0),
	SOC_DAPM_SINGLE("IN3L Switch", AIC3262_RMIC_PGA_MIN, 2, 3, 0),
	SOC_DAPM_SINGLE("IN4L Switch", AIC3262_RMIC_PGA_PM_IN4, 4, 1, 0),
	SOC_DAPM_SINGLE("CM1R Switch", AIC3262_RMIC_PGA_MIN, 6, 3, 0),
	SOC_DAPM_SINGLE("CM2R Switch", AIC3262_RMIC_PGA_MIN, 0, 3, 0),
};

static const char *asi1lin_text[] = {                                                                                                                       
	"Off", "ASI1 Left In","ASI1 Right In","ASI1 MonoMix In"
};                                                                                                                                                           


SOC_ENUM_SINGLE_DECL(asi1lin_enum, AIC3262_ASI1_DAC_OUT_CNTL, 6, asi1lin_text);

static const struct snd_kcontrol_new asi1lin_control =                                                                                                       
SOC_DAPM_ENUM("ASI1LIN Route", asi1lin_enum);      


static const char *asi1rin_text[] = {                                                                                                                       
	"Off", "ASI1 Right In","ASI1 Left In","ASI1 MonoMix In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(asi1rin_enum, AIC3262_ASI1_DAC_OUT_CNTL, 4, asi1rin_text);

static const struct snd_kcontrol_new asi1rin_control =                                                                                                       
SOC_DAPM_ENUM("ASI1RIN Route", asi1rin_enum);      

static const char *asi2lin_text[] = {                                                                                                                       
	"Off", "ASI2 Left In","ASI2 Right In","ASI2 MonoMix In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(asi2lin_enum, AIC3262_ASI2_DAC_OUT_CNTL, 6, asi2lin_text);

static const struct snd_kcontrol_new asi2lin_control =                                                                                                       
SOC_DAPM_ENUM("ASI2LIN Route", asi2lin_enum);      

static const char *asi2rin_text[] = {                                                                                                                       
	"Off", "ASI2 Right In","ASI2 Left In","ASI2 MonoMix In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(asi2rin_enum, AIC3262_ASI2_DAC_OUT_CNTL, 4, asi2rin_text);

static const struct snd_kcontrol_new asi2rin_control =                                                                                                        
SOC_DAPM_ENUM("ASI2RIN Route", asi2rin_enum);      

static const char *asi3lin_text[] = {                                                                                                                       
	"Off", "ASI3 Left In","ASI3 Right In","ASI3 MonoMix In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(asi3lin_enum, AIC3262_ASI3_DAC_OUT_CNTL, 6, asi3lin_text);

static const struct snd_kcontrol_new asi3lin_control =                                                                                                                SOC_DAPM_ENUM("ASI3LIN Route", asi3lin_enum);      


static const char *asi3rin_text[] = {                                                                                                                       
	"Off", "ASI3 Right In","ASI3 Left In","ASI3 MonoMix In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(asi3rin_enum, AIC3262_ASI3_DAC_OUT_CNTL, 4, asi3rin_text);

static const struct snd_kcontrol_new asi3rin_control =                                                                                                       
SOC_DAPM_ENUM("ASI3RIN Route", asi3rin_enum);      


static const char *dacminidspin1_text[] = {                                                                                                       
	"ASI1 In", "ASI2 In","ASI3 In","ADC MiniDSP Out"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(dacminidspin1_enum, AIC3262_MINIDSP_DATA_PORT_CNTL, 4, dacminidspin1_text); 

static const struct snd_kcontrol_new dacminidspin1_control =                                                                                                
SOC_DAPM_ENUM("DAC MiniDSP IN1 Route", dacminidspin1_enum);      

static const char *dacminidspin2_text[] = {                                                                                                       
	"ASI1 In", "ASI2 In","ASI3 In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(dacminidspin2_enum, AIC3262_MINIDSP_DATA_PORT_CNTL, 2, dacminidspin2_text); 

static const struct snd_kcontrol_new dacminidspin2_control =                                                                                                
SOC_DAPM_ENUM("DAC MiniDSP IN2 Route", dacminidspin2_enum);      

static const char *dacminidspin3_text[] = {                                                                                                       
	"ASI1 In", "ASI2 In","ASI3 In"
};                                                                                                                                                           

SOC_ENUM_SINGLE_DECL(dacminidspin3_enum, AIC3262_MINIDSP_DATA_PORT_CNTL, 0, dacminidspin3_text); 

static const struct snd_kcontrol_new dacminidspin3_control =                                                                                                
SOC_DAPM_ENUM("DAC MiniDSP IN3 Route", dacminidspin3_enum);      


static const char *adcdac_route_text[] = {
	"Off",
	"On",
};

SOC_ENUM_SINGLE_DECL(adcdac_enum, 0, 2, adcdac_route_text);
	
static const struct snd_kcontrol_new adcdacroute_control =
	SOC_DAPM_ENUM_VIRT("ADC DAC Route", adcdac_enum);

static const char *dout1_text[] = {                                                                                                                       
	"ASI1 Out",
	"DIN1 Bypass",
	"DIN2 Bypass",
	"DIN3 Bypass",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(dout1_enum, AIC3262_ASI1_DOUT_CNTL, 0, dout1_text);
static const struct snd_kcontrol_new dout1_control =                                                                                                
SOC_DAPM_ENUM("DOUT1 Route", dout1_enum);      


static const char *dout2_text[] = {                                                                                                                       
	"ASI2 Out",
	"DIN1 Bypass",
	"DIN2 Bypass",
	"DIN3 Bypass",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(dout2_enum, AIC3262_ASI2_DOUT_CNTL, 0, dout2_text);
static const struct snd_kcontrol_new dout2_control =                                                                                                
SOC_DAPM_ENUM("DOUT2 Route", dout2_enum);      


static const char *dout3_text[] = {                                                                                                                       
	"ASI3 Out",
	"DIN1 Bypass",
	"DIN2 Bypass",
	"DIN3 Bypass",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(dout3_enum, AIC3262_ASI3_DOUT_CNTL, 0, dout3_text);
static const struct snd_kcontrol_new dout3_control =                                                                                                
SOC_DAPM_ENUM("DOUT3 Route", dout3_enum);      

static const char *asi1out_text[] = {                                                                                                                       
	"Off",
	"ADC MiniDSP Out1",
	"ASI1In Bypass",
	"ASI2In Bypass",
	"ASI3In Bypass",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(asi1out_enum, AIC3262_ASI1_ADC_INPUT_CNTL, 0, asi1out_text);
static const struct snd_kcontrol_new asi1out_control =                                                                                                
SOC_DAPM_ENUM("ASI1OUT Route", asi1out_enum);      

static const char *asi2out_text[] = {                                                                                                                       
	"Off",
	"ADC MiniDSP Out1",
	"ASI1In Bypass",
	"ASI2In Bypass",
	"ASI3In Bypass",
	"ADC MiniDSP Out2",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(asi2out_enum, AIC3262_ASI2_ADC_INPUT_CNTL, 0, asi2out_text);
static const struct snd_kcontrol_new asi2out_control =                                                                                                
SOC_DAPM_ENUM("ASI2OUT Route", asi2out_enum);      
static const char *asi3out_text[] = {                                                                                                                       
	"Off",
	"ADC MiniDSP Out1",
	"ASI1In Bypass",
	"ASI2In Bypass",
	"ASI3In Bypass",
	"Reserved",
	"ADC MiniDSP Out3",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(asi3out_enum, AIC3262_ASI3_ADC_INPUT_CNTL, 0, asi3out_text);
static const struct snd_kcontrol_new asi3out_control =                                                                                                
SOC_DAPM_ENUM("ASI3OUT Route", asi3out_enum);      
static const char *asibclk_text[] = {                                                                                                                       
	"DAC_CLK",
	"DAC_MOD_CLK",
	"ADC_CLK",
	"ADC_MOD_CLK",
};                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(asi1bclk_enum, AIC3262_ASI1_BCLK_N_CNTL, 0, asibclk_text);
static const struct snd_kcontrol_new asi1bclk_control =                                                                                                
SOC_DAPM_ENUM("ASI1_BCLK Route", asi1bclk_enum);      

/*static const char *asi2bclk_text[] = {                                                                                                                       
	"DAC_CLK",
	"DAC_MOD_CLK",
	"ADC_CLK",
	"ADC_MOD_CLK",
}; */                                                                                                                                                          
SOC_ENUM_SINGLE_DECL(asi2bclk_enum, AIC3262_ASI2_BCLK_N_CNTL, 0, asibclk_text);
static const struct snd_kcontrol_new asi2bclk_control =                                                                                                
SOC_DAPM_ENUM("ASI2_BCLK Route", asi2bclk_enum);      
/*static const char *asi3bclk_text[] = {                                                                                                                       
	"DAC_CLK",
	"DAC_MOD_CLK",
	"ADC_CLK",
	"ADC_MOD_CLK",
};*/                                                                                                                                                           
SOC_ENUM_SINGLE_DECL(asi3bclk_enum, AIC3262_ASI3_BCLK_N_CNTL, 0, asibclk_text);
static const struct snd_kcontrol_new asi3bclk_control =                                                                                                
SOC_DAPM_ENUM("ASI3_BCLK Route", asi3bclk_enum);      

static const char *adc_mux_text[] = {
	"Analog",
	"Digital",
};

SOC_ENUM_SINGLE_DECL(adcl_enum, AIC3262_ADC_CHANNEL_POW, 4, adc_mux_text); 
SOC_ENUM_SINGLE_DECL(adcr_enum, AIC3262_ADC_CHANNEL_POW, 2, adc_mux_text); 

static const struct snd_kcontrol_new adcl_mux =
	SOC_DAPM_ENUM("Left ADC Route", adcl_enum);

static const struct snd_kcontrol_new adcr_mux =
	SOC_DAPM_ENUM("Right ADC Route", adcr_enum);

/*static const char *dmicinput_text[] = {
	"GPI1",
	"GPI2",
	"DIN1",
	"DIN2",
	"GPIO1",
	"GPIO2",
	"MCLK2",
};
SOC_ENUM_SINGLE_DECL(dmicinput_enum, AIC3262_DMIC_INPUT_CNTL, 0, dmicinput_text); 

static const struct snd_kcontrol_new dmicinput_control =
	SOC_DAPM_ENUM("DMICDAT Input Route",  dmicinput_enum);
*/
static int aic326x_hp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int status;
	int count=20;
	int reg_mask = 0;
	if(w->shift == 1) {// Left HPL
		reg_mask = AIC3262_HPL_POWER_MASK;
	}
	if(w->shift == 0) { // Right HPR
		reg_mask = AIC3262_HPR_POWER_MASK;
	}
	switch(event){
	case SND_SOC_DAPM_POST_PMU:
		do
		{
			status = snd_soc_read(w->codec,AIC3262_HP_FLAG);
			count--;

		}while(((status & reg_mask) == 0x00) && count != 0 ); //wait until hp powered up
		break;
	case SND_SOC_DAPM_POST_PMD:
		do
		{
			status = snd_soc_read(w->codec,AIC3262_HP_FLAG);
			count--;

		}while(((status & reg_mask) == reg_mask) && count != 0 ); //wait until hp powered down
		break;
	default:
		BUG();
		return -EINVAL;
	}
	return 0;
}

/***********************************************************************
Arguments     : pointer variable to dapm_widget,
		pointer variable to sound control,
		integer to event
Return value  : 0
Purpose	      : Headset popup reduction and powering up dsps together 
		when they are in sync mode  
************************************************************************/
static int aic326x_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int status;
	int count=20;
	int reg_mask = 0;
	int run_state_mask;
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(w->codec);
	int sync_needed = 0, non_sync_state =0;
	int other_dsp = 0, run_state = 0;


	if(w->shift == 7) {// Left DAC
 		reg_mask = AIC3262_LDAC_POWER_MASK;
		run_state_mask = AIC3262_COPS_MDSP_D_L;
	}
	if (w->shift == 6) { // Right DAC
		reg_mask = AIC3262_RDAC_POWER_MASK;
		run_state_mask = AIC3262_COPS_MDSP_D_R;
	}
	switch(event){
	case SND_SOC_DAPM_POST_PMU:
		do
		{
			status = snd_soc_read(w->codec, AIC3262_DAC_FLAG);
			count--;
		} while(((status & reg_mask) == 0)&& count != 0);

		sync_needed 	= 	SYNC_STATE(aic3262);
		non_sync_state 	= 	DSP_NON_SYNC_MODE(aic3262->dsp_runstate);
		other_dsp	=	aic3262->dsp_runstate & AIC3262_COPS_MDSP_A;

		if( sync_needed && non_sync_state && other_dsp )
		{
			run_state = get_runstate(aic3262->codec->control_data);
			aic3262_dsp_pwrdwn_status(aic3262);  
			aic3262_dsp_pwrup(aic3262,run_state);	 
		}	
		aic3262->dsp_runstate  |= run_state_mask;
		break;
	case SND_SOC_DAPM_POST_PMD:
		do
		{
			status = snd_soc_read(w->codec, AIC3262_DAC_FLAG);
			count--;
		} while(((status & reg_mask) == reg_mask)&& count != 0);
	
		aic3262->dsp_runstate = (aic3262->dsp_runstate & ~run_state_mask);
		break;
	default:
		BUG();
		return -EINVAL;
	}
	return 0;
}



static int aic326x_spk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	int status;
	int count=20;
	int reg_mask;

	if(w->shift == 1) {// Left SPK
		reg_mask = AIC3262_SPKL_POWER_MASK;
	}
	if(w->shift == 0) { // Right SPK
		reg_mask = AIC3262_SPKR_POWER_MASK;
	}
	switch(event){
	case SND_SOC_DAPM_POST_PMU:
		do
		{
			status = snd_soc_read(w->codec,AIC3262_HP_FLAG);
			count--;

		}while(((status & reg_mask) == 0x00) && count != 0 ); //wait until spk powered up
		break;
	case SND_SOC_DAPM_POST_PMD:
		do
		{
			status = snd_soc_read(w->codec,AIC3262_HP_FLAG);
			count--;

		}while(((status & reg_mask) == reg_mask) && count != 0 ); //wait until spk powered up
		break;
	default:
		BUG();
		return -EINVAL;
	}
	return 0;
}
static int pll_power_on_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	if(event == SND_SOC_DAPM_POST_PMU)
	{
		mdelay(10);
	}	
	return 0;
}
static int aic3262_test_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{

	return 0;
}

static int aic3262_test_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			"tlv320aic3262_fw_v1.bin", codec->dev, GFP_KERNEL,
			codec, aic3262_firmware_load);

	return 0;
}

static int aic3262_set_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	//struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	//struct aic3262_priv *priv_ds = snd_soc_codec_get_drvdata(codec);

	return 0;
}

static int aic3262_set_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct aic3262_priv *priv_ds = snd_soc_codec_get_drvdata(codec);

	int next_mode=0,next_cfg=0;

	next_mode = (ucontrol->value.integer.value[0]>>8);
	next_cfg = (ucontrol->value.integer.value[0])&0xFF;
	if (priv_ds==NULL)
	{	
		dev_err(codec->dev,"\nFirmware not loaded, no mode switch can occur\n");
	}
	else
	{
                mutex_lock(&priv_ds->cfw_mutex);
		aic3xxx_cfw_setmode_cfg(priv_ds->cfw_p,next_mode,next_cfg);
                mutex_unlock(&priv_ds->cfw_mutex);
	}

	return 0;
}
static int aic326x_adc_dsp_event(struct snd_soc_dapm_widget *w,struct snd_kcontrol *kcontrol, int event)
{
	int run_state=0;
	int non_sync_state = 0,sync_needed = 0;
	int other_dsp = 0;
	int run_state_mask = 0;
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(w->codec);

	if(w->shift == 7) {// Left ADC
		run_state_mask = AIC3262_COPS_MDSP_A_L;
	}
	if (w->shift == 6) { // Right ADC
		run_state_mask = AIC3262_COPS_MDSP_A_R;
	}
	switch(event){
		case SND_SOC_DAPM_POST_PMU:
			sync_needed 	= SYNC_STATE(aic3262);
			non_sync_state 	= DSP_NON_SYNC_MODE(aic3262->dsp_runstate);
			other_dsp	= aic3262->dsp_runstate & AIC3262_COPS_MDSP_D;
			if( sync_needed && non_sync_state && other_dsp ){
				run_state = get_runstate(aic3262->codec->control_data);
				aic3262_dsp_pwrdwn_status(aic3262);  
				aic3262_dsp_pwrup(aic3262,run_state);	 
			}	
			aic3262->dsp_runstate  |= run_state_mask;
			break;
		case SND_SOC_DAPM_POST_PMD:
			aic3262->dsp_runstate = (aic3262->dsp_runstate & ~run_state_mask) ;
			break;
		default:
			BUG();
			return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dapm_widget aic3262_dapm_widgets[] = {
	/* TODO: Can we switch these off ? */
	SND_SOC_DAPM_AIF_IN("DIN1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DIN2", "ASI2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DIN3", "ASI3 Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC_E("Left DAC", NULL, AIC3262_PASI_DAC_DP_SETUP, 7, 0,
		aic326x_dac_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("Right DAC", NULL, AIC3262_PASI_DAC_DP_SETUP, 6, 0,
		aic326x_dac_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),


	/* dapm widget (path domain) for HPL Output Mixer */
	SND_SOC_DAPM_MIXER("HPL Output Mixer", SND_SOC_NOPM, 0, 0,
			&hpl_output_mixer_controls[0],
			ARRAY_SIZE(hpl_output_mixer_controls)),

	/* dapm widget (path domain) for HPR Output Mixer */
	SND_SOC_DAPM_MIXER("HPR Output Mixer", SND_SOC_NOPM, 0, 0,
			&hpr_output_mixer_controls[0],
			ARRAY_SIZE(hpr_output_mixer_controls)),


	SND_SOC_DAPM_PGA_E("HPL Driver", AIC3262_HP_AMP_CNTL_R1, 1, 0, NULL, 0, aic326x_hp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HPR Driver", AIC3262_HP_AMP_CNTL_R1, 0, 0, NULL, 0, aic326x_hp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),


	/* dapm widget (path domain) for LOL Output Mixer */
	SND_SOC_DAPM_MIXER("LOL Output Mixer", SND_SOC_NOPM, 0, 0,
			&lol_output_mixer_controls[0],
			ARRAY_SIZE(lol_output_mixer_controls)),

	/* dapm widget (path domain) for LOR Output Mixer mixer */
	SND_SOC_DAPM_MIXER("LOR Output Mixer", SND_SOC_NOPM, 0, 0,
			&lor_output_mixer_controls[0],
			ARRAY_SIZE(lor_output_mixer_controls)),

	SND_SOC_DAPM_PGA("LOL Driver", AIC3262_LINE_AMP_CNTL_R1, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LOR Driver", AIC3262_LINE_AMP_CNTL_R1, 0, 0, NULL, 0),


	/* dapm widget (path domain) for SPKL Output Mixer */
	SND_SOC_DAPM_MIXER("SPKL Output Mixer", SND_SOC_NOPM, 0, 0,
			&spkl_output_mixer_controls[0],
			ARRAY_SIZE(spkl_output_mixer_controls)),

	/* dapm widget (path domain) for SPKR Output Mixer */
	SND_SOC_DAPM_MIXER("SPKR Output Mixer", SND_SOC_NOPM, 0, 0,
			&spkr_output_mixer_controls[0],
			ARRAY_SIZE(spkr_output_mixer_controls)),

	SND_SOC_DAPM_PGA_E("SPKL Driver",AIC3262_SPK_AMP_CNTL_R1, 1, 0, NULL, 0, 
		aic326x_spk_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("SPKR Driver", AIC3262_SPK_AMP_CNTL_R1, 0, 0, NULL, 0, 
		aic326x_spk_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),


	/* dapm widget (path domain) for SPKR Output Mixer */
	SND_SOC_DAPM_MIXER("REC Output Mixer", SND_SOC_NOPM, 0, 0,
			&rec_output_mixer_controls[0],
			ARRAY_SIZE(rec_output_mixer_controls)),

	SND_SOC_DAPM_PGA("RECP Driver", AIC3262_REC_AMP_CNTL_R5, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RECM Driver", AIC3262_REC_AMP_CNTL_R5, 6, 0, NULL, 0),


	SND_SOC_DAPM_MUX("ASI1LIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi1lin_control),       
	SND_SOC_DAPM_MUX("ASI1RIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi1rin_control),       
	SND_SOC_DAPM_MUX("ASI2LIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi2lin_control),       
	SND_SOC_DAPM_MUX("ASI2RIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi2rin_control),       
	SND_SOC_DAPM_MUX("ASI3LIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi3lin_control),       
	SND_SOC_DAPM_MUX("ASI3RIN Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi3rin_control),       

	SND_SOC_DAPM_PGA("ASI1LIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI1RIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI2LIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI2RIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI3LIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI3RIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI1MonoMixIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI2MonoMixIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI3MonoMixIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* TODO: Can we switch the ASIxIN off? */
	SND_SOC_DAPM_PGA("ASI1IN Port", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI2IN Port", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI3IN Port", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DAC MiniDSP IN1 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dacminidspin1_control),       
	SND_SOC_DAPM_MUX("DAC MiniDSP IN2 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dacminidspin2_control),       
	SND_SOC_DAPM_MUX("DAC MiniDSP IN3 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dacminidspin3_control),       

	SND_SOC_DAPM_VIRT_MUX("ADC DAC Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &adcdacroute_control),       

	SND_SOC_DAPM_PGA("CM", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("CM1L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("CM2L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("CM1R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("CM2R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* TODO: Can we switch these off ? */
	SND_SOC_DAPM_AIF_OUT("DOUT1","ASI1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DOUT2", "ASI2 Capture",0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DOUT3", "ASI3 Capture",0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX("DOUT1 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dout1_control),       
	SND_SOC_DAPM_MUX("DOUT2 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dout2_control),       
	SND_SOC_DAPM_MUX("DOUT3 Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &dout3_control),       

	SND_SOC_DAPM_PGA("ASI1OUT",  SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI2OUT",  SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ASI3OUT",  SND_SOC_NOPM, 0, 0, NULL, 0),


	SND_SOC_DAPM_MUX("ASI1OUT Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi1out_control),       
	SND_SOC_DAPM_MUX("ASI2OUT Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi2out_control),       
	SND_SOC_DAPM_MUX("ASI3OUT Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi3out_control),       

	/* TODO: Can we switch the ASI1 OUT1 off? */
	/* TODO: Can we switch them off? */
	SND_SOC_DAPM_PGA("ADC MiniDSP OUT1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC MiniDSP OUT2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ADC MiniDSP OUT3", SND_SOC_NOPM, 0, 0, NULL, 0),

	///M SND_SOC_DAPM_PGA("GPI1 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("GPI2 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("DIN1 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("DIN2 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("GPIO1 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("GPIO2 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),
	///M SND_SOC_DAPM_PGA("MCLK2 PIN", SND_SOC_NOPM, 0, 0, NULL, 0),

//	SND_SOC_DAPM_MUX("DMICDAT Input Route",                                                                                                        
//			SND_SOC_NOPM, 0, 0, &dmicinput_control),       

	SND_SOC_DAPM_MUX("Left ADC Route", SND_SOC_NOPM,0, 0, &adcl_mux), 
	SND_SOC_DAPM_MUX("Right ADC Route", SND_SOC_NOPM,0, 0, &adcr_mux), 

	SND_SOC_DAPM_ADC_E("Left ADC", NULL, AIC3262_ADC_CHANNEL_POW, 7, 0, 
		aic326x_adc_dsp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("Right ADC", NULL, AIC3262_ADC_CHANNEL_POW, 6, 0,
		aic326x_adc_dsp_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA("Left MicPGA",AIC3262_MICL_PGA, 7, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right MicPGA",AIC3262_MICR_PGA, 7, 1, NULL, 0),

	SND_SOC_DAPM_PGA("MAL PGA", AIC3262_MA_CNTL, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MAR PGA", AIC3262_MA_CNTL, 2, 0, NULL, 0),

	/* dapm widget for MAL PGA Mixer*/
	SND_SOC_DAPM_MIXER("MAL PGA Mixer", SND_SOC_NOPM, 0, 0,
			&mal_pga_mixer_controls[0],
			ARRAY_SIZE(mal_pga_mixer_controls)),

	/* dapm widget for MAR PGA Mixer*/
	SND_SOC_DAPM_MIXER("MAR PGA Mixer", SND_SOC_NOPM, 0, 0,
			&mar_pga_mixer_controls[0],
			ARRAY_SIZE(mar_pga_mixer_controls)),

	/* dapm widget for Left Input Mixer*/
	SND_SOC_DAPM_MIXER("Left Input Mixer", SND_SOC_NOPM, 0, 0,
			&left_input_mixer_controls[0],
			ARRAY_SIZE(left_input_mixer_controls)),

	/* dapm widget for Right Input Mixer*/
	SND_SOC_DAPM_MIXER("Right Input Mixer", SND_SOC_NOPM, 0, 0,
			&right_input_mixer_controls[0],
			ARRAY_SIZE(right_input_mixer_controls)),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LOL"),
	SND_SOC_DAPM_OUTPUT("LOR"),
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR"),
	SND_SOC_DAPM_OUTPUT("RECP"),
	SND_SOC_DAPM_OUTPUT("RECM"),

	SND_SOC_DAPM_INPUT("IN1L"),
	SND_SOC_DAPM_INPUT("IN2L"),
	SND_SOC_DAPM_INPUT("IN3L"),
	SND_SOC_DAPM_INPUT("IN4L"),
	SND_SOC_DAPM_INPUT("IN1R"),
	SND_SOC_DAPM_INPUT("IN2R"),
	SND_SOC_DAPM_INPUT("IN3R"),
	SND_SOC_DAPM_INPUT("IN4R"),
//	SND_SOC_DAPM_INPUT("DMICDAT"),
	SND_SOC_DAPM_INPUT("Left DMIC"),
	SND_SOC_DAPM_INPUT("Right DMIC"),


	SND_SOC_DAPM_MICBIAS("Mic Bias Ext", AIC3262_MIC_BIAS_CNTL, 6, 0),    
	SND_SOC_DAPM_MICBIAS("Mic Bias Int", AIC3262_MIC_BIAS_CNTL, 2, 0),    


	SND_SOC_DAPM_SUPPLY("PLLCLK",AIC3262_PLL_PR_POW_REG,7,0,pll_power_on_event,SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("DACCLK",AIC3262_NDAC_DIV_POW_REG,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CODEC_CLK_IN",SND_SOC_NOPM,0,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC_MOD_CLK",AIC3262_MDAC_DIV_POW_REG,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADCCLK",AIC3262_NADC_DIV_POW_REG,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC_MOD_CLK",AIC3262_MADC_DIV_POW_REG,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI1_BCLK",AIC3262_ASI1_BCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI1_WCLK",AIC3262_ASI1_WCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI2_BCLK",AIC3262_ASI2_BCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI2_WCLK",AIC3262_ASI2_WCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI3_BCLK",AIC3262_ASI3_BCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ASI3_WCLK",AIC3262_ASI3_WCLK_N,7,0, NULL, 0),
	SND_SOC_DAPM_MUX("ASI1_BCLK Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi1bclk_control),       
	SND_SOC_DAPM_MUX("ASI2_BCLK Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi2bclk_control),       
	SND_SOC_DAPM_MUX("ASI3_BCLK Route",                                                                                                        
			SND_SOC_NOPM, 0, 0, &asi3bclk_control),       
};

static const struct snd_soc_dapm_route aic3262_dapm_routes[] ={
	/* TODO: Do we need only DACCLK for ASIIN's and ADCCLK for ASIOUT??? */
	/* Clock portion */
	{"CODEC_CLK_IN", NULL, "PLLCLK"},
	{"DACCLK", NULL, "CODEC_CLK_IN"},
	{"ADCCLK", NULL, "CODEC_CLK_IN"},
	{"DAC_MOD_CLK", NULL, "DACCLK"},
#ifdef AIC3262_SYNC_MODE
	{"ADC_MOD_CLK", NULL,"DACCLK"},
#else
	{"ADC_MOD_CLK", NULL, "ADCCLK"},
#endif

	{"ASI1_BCLK Route","DAC_CLK","DACCLK"},
	{"ASI1_BCLK Route","DAC_MOD_CLK","DAC_MOD_CLK"},
	{"ASI1_BCLK Route","ADC_CLK","ADCCLK"},
	{"ASI1_BCLK Route","ADC_MOD_CLK","ADC_MOD_CLK"},

	{"ASI2_BCLK Route","DAC_CLK","DACCLK"},
	{"ASI2_BCLK Route","DAC_MOD_CLK","DAC_MOD_CLK"},
	{"ASI2_BCLK Route","ADC_CLK","ADCCLK"},
	{"ASI2_BCLK Route","ADC_MOD_CLK","ADC_MOD_CLK"},

	{"ASI3_BCLK Route","DAC_CLK","DACCLK"},
	{"ASI3_BCLK Route","DAC_MOD_CLK","DAC_MOD_CLK"},
	{"ASI3_BCLK Route","ADC_CLK","ADCCLK"},
	{"ASI3_BCLK Route","ADC_MOD_CLK","ADC_MOD_CLK"},

	{"ASI1_BCLK", NULL, "ASI1_BCLK Route"},
	{"ASI2_BCLK", NULL, "ASI2_BCLK Route"},
	{"ASI3_BCLK", NULL, "ASI3_BCLK Route"},


	{"DIN1", NULL , "PLLCLK"},
	{"DIN1", NULL , "DACCLK"},
	{"DIN1", NULL , "ADCCLK"},
	{"DIN1", NULL , "DAC_MOD_CLK"},
	{"DIN1", NULL , "ADC_MOD_CLK"},

	{"DOUT1", NULL , "PLLCLK"},
	{"DOUT1", NULL , "DACCLK"},
	{"DOUT1", NULL , "ADCCLK"},
	{"DOUT1", NULL , "DAC_MOD_CLK"},
	{"DOUT1", NULL , "ADC_MOD_CLK"},
#ifdef AIC3262_ASI1_MASTER
	{"DIN1", NULL , "ASI1_BCLK"},
	{"DOUT1", NULL , "ASI1_BCLK"},
	{"DIN1", NULL , "ASI1_WCLK"},
	{"DOUT1", NULL , "ASI1_WCLK"},
#else

#endif

	{"DIN2", NULL , "PLLCLK"},
	{"DIN2", NULL , "DACCLK"},
	{"DIN2", NULL , "ADCCLK"},
	{"DIN2", NULL , "DAC_MOD_CLK"},
	{"DIN2", NULL , "ADC_MOD_CLK"},

	{"DOUT2", NULL , "PLLCLK"},
	{"DOUT2", NULL , "DACCLK"},
	{"DOUT2", NULL , "ADCCLK"},
	{"DOUT2", NULL , "DAC_MOD_CLK"},
	{"DOUT2", NULL , "ADC_MOD_CLK"},

#ifdef AIC3262_ASI2_MASTER
	{"DIN2", NULL , "ASI2_BCLK"},
	{"DOUT2", NULL , "ASI2_BCLK"},
	{"DIN2", NULL , "ASI2_WCLK"},
	{"DOUT2", NULL , "ASI2_WCLK"},
#else

#endif
	{"DIN3", NULL , "PLLCLK"},
	{"DIN3", NULL , "DACCLK"},
	{"DIN3", NULL , "ADCCLK"},
	{"DIN3", NULL , "DAC_MOD_CLK"},
	{"DIN3", NULL , "ADC_MOD_CLK"},


	{"DOUT3", NULL , "PLLCLK"},
	{"DOUT3", NULL , "DACCLK"},
	{"DOUT3", NULL , "ADCCLK"},
	{"DOUT3", NULL , "DAC_MOD_CLK"},
	{"DOUT3", NULL , "ADC_MOD_CLK"},

#ifdef AIC3262_ASI3_MASTER
	{"DIN3", NULL , "ASI3_BCLK"},
	{"DOUT3", NULL , "ASI3_BCLK"},
	{"DIN3", NULL , "ASI3_WCLK"},
	{"DOUT3", NULL , "ASI3_WCLK"},
#else

#endif
	/* Playback (DAC) Portion */
	{"HPL Output Mixer","LDAC Switch","Left DAC"},
	{"HPL Output Mixer","MAL Switch","MAL PGA"},
	{"HPL Output Mixer","LOL-B1 Volume","LOL"},

	{"HPR Output Mixer","LOR-B1 Volume","LOR"},
	{"HPR Output Mixer","LDAC Switch","Left DAC"},
	{"HPR Output Mixer","RDAC Switch","Right DAC"},
	{"HPR Output Mixer","MAR Switch","MAR PGA"},

	{"HPL Driver",NULL,"HPL Output Mixer"},
	{"HPR Driver",NULL,"HPR Output Mixer"},

	{"HPL",NULL,"HPL Driver"},
	{"HPR",NULL,"HPR Driver"},

	{"LOL Output Mixer","MAL Switch","MAL PGA"},
	{"LOL Output Mixer","IN1L-B Switch","IN1L"},
	{"LOL Output Mixer","LDAC Switch","Left DAC"},
	{"LOL Output Mixer","RDAC Switch","Right DAC"},

	{"LOR Output Mixer","LOL Switch","LOL"},
	{"LOR Output Mixer","RDAC Switch","Right DAC"},
	{"LOR Output Mixer","MAR Switch","MAR PGA"},
	{"LOR Output Mixer","IN1R-B Switch","IN1R"},

	{"LOL Driver",NULL,"LOL Output Mixer"},
	{"LOR Driver",NULL,"LOR Output Mixer"},

	{"LOL",NULL,"LOL Driver"},
	{"LOR",NULL,"LOR Driver"},

	{"REC Output Mixer","LOL-B2 Volume","LOL"},
	{"REC Output Mixer","IN1L Volume","IN1L"},
	{"REC Output Mixer","IN1R Volume","IN1R"},
	{"REC Output Mixer","LOR-B2 Volume","LOR"},

	{"RECP Driver",NULL,"REC Output Mixer"},
	{"RECM Driver",NULL,"REC Output Mixer"},

	{"RECP",NULL,"RECP Driver"},
	{"RECM",NULL,"RECM Driver"},

	{"SPKL Output Mixer","MAL Switch","MAL PGA"},
	{"SPKL Output Mixer","LOL Volume","LOL"},
	{"SPKL Output Mixer","SPR_IN Switch","SPKR Output Mixer"},

	{"SPKR Output Mixer", "LOR Volume","LOR"},
	{"SPKR Output Mixer", "MAR Switch","MAR PGA"},


	{"SPKL Driver",NULL,"SPKL Output Mixer"},
	{"SPKR Driver",NULL,"SPKR Output Mixer"},

	{"SPKL",NULL,"SPKL Driver"},
	{"SPKR",NULL,"SPKR Driver"},
	/* ASI Input routing */
	{"ASI1LIN", NULL, "DIN1"},
	{"ASI1RIN", NULL, "DIN1"},
	{"ASI1MonoMixIN", NULL, "DIN1"},
	{"ASI2LIN", NULL, "DIN2"},
	{"ASI2RIN", NULL, "DIN2"},
	{"ASI2MonoMixIN", NULL, "DIN2"},
	{"ASI3LIN", NULL, "DIN3"},
	{"ASI3RIN", NULL, "DIN3"},
	{"ASI3MonoMixIN", NULL, "DIN3"},

	{"ASI1LIN Route","ASI1 Left In","ASI1LIN"},
	{"ASI1LIN Route","ASI1 Right In","ASI1RIN"},
	{"ASI1LIN Route","ASI1 MonoMix In","ASI1MonoMixIN"},

	{"ASI1RIN Route","ASI1 Right In","ASI1RIN"},
	{"ASI1RIN Route","ASI1 Left In","ASI1LIN"},
	{"ASI1RIN Route","ASI1 MonoMix In","ASI1MonoMixIN"},


	{"ASI2LIN Route","ASI2 Left In","ASI2LIN"},
	{"ASI2LIN Route","ASI2 Right In","ASI2RIN"},
	{"ASI2LIN Route","ASI2 MonoMix In","ASI2MonoMixIN"},

	{"ASI2RIN Route","ASI2 Right In","ASI2RIN"},
	{"ASI2RIN Route","ASI2 Left In","ASI2LIN"},
	{"ASI2RIN Route","ASI2 MonoMix In","ASI2MonoMixIN"},


	{"ASI3LIN Route","ASI3 Left In","ASI3LIN"},
	{"ASI3LIN Route","ASI3 Right In","ASI3RIN"},
	{"ASI3LIN Route","ASI3 MonoMix In","ASI3MonoMixIN"},

	{"ASI3RIN Route","ASI3 Right In","ASI3RIN"},
	{"ASI3RIN Route","ASI3 Left In","ASI3LIN"},
	{"ASI3RIN Route","ASI3 MonoMix In","ASI3MonoMixIN"},

	{"ASI1IN Port", NULL, "ASI1LIN Route"},
	{"ASI1IN Port", NULL, "ASI1RIN Route"},
	{"ASI2IN Port", NULL, "ASI2LIN Route"},
	{"ASI2IN Port", NULL, "ASI2RIN Route"},
	{"ASI3IN Port", NULL, "ASI3LIN Route"},
	{"ASI3IN Port", NULL, "ASI3RIN Route"},

	{"DAC MiniDSP IN1 Route","ASI1 In","ASI1IN Port"},
	{"DAC MiniDSP IN1 Route","ASI2 In","ASI2IN Port"},
	{"DAC MiniDSP IN1 Route","ASI3 In","ASI3IN Port"},
	{"DAC MiniDSP IN1 Route","ADC MiniDSP Out","ADC MiniDSP OUT1"},

	{"DAC MiniDSP IN2 Route","ASI1 In","ASI1IN Port"},
	{"DAC MiniDSP IN2 Route","ASI2 In","ASI2IN Port"},
	{"DAC MiniDSP IN2 Route","ASI3 In","ASI3IN Port"},

	{"DAC MiniDSP IN3 Route","ASI1 In","ASI1IN Port"},
	{"DAC MiniDSP IN3 Route","ASI2 In","ASI2IN Port"},
	{"DAC MiniDSP IN3 Route","ASI3 In","ASI3IN Port"},


	{"Left DAC", "NULL","DAC MiniDSP IN1 Route"},	
	{"Right DAC", "NULL","DAC MiniDSP IN1 Route"},	
	{"Left DAC", "NULL","DAC MiniDSP IN2 Route"},	
	{"Right DAC", "NULL","DAC MiniDSP IN2 Route"},	
	{"Left DAC", "NULL","DAC MiniDSP IN3 Route"},	
	{"Right DAC", "NULL","DAC MiniDSP IN3 Route"},	




	/* Mixer Amplifier */

	{"MAL PGA Mixer", "IN1L Switch","IN1L"}, 
	{"MAL PGA Mixer", "Left MicPGA Volume","Left MicPGA"}, 

	{"MAL PGA", NULL, "MAL PGA Mixer"},


	{"MAR PGA Mixer", "IN1R Switch","IN1R"}, 
	{"MAR PGA Mixer", "Right MicPGA Volume","Right MicPGA"}, 

	{"MAR PGA", NULL, "MAR PGA Mixer"},


	/* Virtual connection between DAC and ADC for miniDSP IPC */
	{"ADC DAC Route", "On", "Left ADC"},
	{"ADC DAC Route", "On", "Right ADC"},
	
	{"Left DAC", NULL, "ADC DAC Route"},
	{"Right DAC", NULL, "ADC DAC Route"},

	/* Capture (ADC) portions */ 
	/* Left Positive PGA input */
	{"Left Input Mixer","IN1L Switch","IN1L"},
	{"Left Input Mixer","IN2L Switch","IN2L"},
	{"Left Input Mixer","IN3L Switch","IN3L"},
	{"Left Input Mixer","IN4L Switch","IN4L"},
	{"Left Input Mixer","IN1R Switch","IN1R"},
	/* Left Negative PGA input */
	{"Left Input Mixer","IN2R Switch","IN2R"},
	{"Left Input Mixer","IN3R Switch","IN3R"},
	{"Left Input Mixer","IN4R Switch","IN4R"},
	{"Left Input Mixer","CM2L Switch","CM2L"},
	{"Left Input Mixer","CM1L Switch","CM1L"},


	/* Right Positive PGA Input */
	{"Right Input Mixer","IN1R Switch","IN1R"},
	{"Right Input Mixer","IN2R Switch","IN2R"},
	{"Right Input Mixer","IN3R Switch","IN3R"},
	{"Right Input Mixer","IN4R Switch","IN4R"},
	{"Right Input Mixer","IN2L Switch","IN2L"},
	/* Right Negative PGA Input */
	{"Right Input Mixer","IN1L Switch","IN1L"},
	{"Right Input Mixer","IN3L Switch","IN3L"},
	{"Right Input Mixer","IN4L Switch","IN4L"},
	{"Right Input Mixer","CM1R Switch","CM1R"},
	{"Right Input Mixer","CM2R Switch","CM2R"},


	{"CM1L", NULL, "CM"},
	{"CM2L", NULL, "CM"},
	{"CM1R", NULL, "CM"},
	{"CM1R", NULL, "CM"},

	{"Left MicPGA",NULL,"Left Input Mixer"},
	{"Right MicPGA",NULL,"Right Input Mixer"},

/*	{"DMICDAT Input Route","GPI1","GPI1 Pin"},
	{"DMICDAT Input Route","GPI2","GPI2 Pin"},
	{"DMICDAT Input Route","DIN1","DIN1 Pin"},
	{"DMICDAT Input Route","GPIO1","GPIO1 Pin"},
	{"DMICDAT Input Route","GPIO2","GPIO2 Pin"},
	{"DMICDAT Input Route","MCLK2","MCLK2 Pin"},

	{"DMICDAT", NULL, "DMICDAT Input Route"},
	{"DMICDAT", NULL, "ADC_MOD_CLK"},
*/
//	{"Left DMIC", NULL, "DMICDAT"},
//	{"Right DMIC", NULL, "DMICDAT"},

	{"Left ADC Route", "Analog","Left MicPGA"},
	{"Left ADC Route", "Digital", "Left DMIC"},

	{"Right ADC Route", "Analog","Right MicPGA"},
	{"Right ADC Route", "Digital", "Right DMIC"},

	{"Left ADC", NULL, "Left ADC Route"},
	{"Right ADC", NULL, "Right ADC Route"},


	/* ASI Output Routing */
	{"ADC MiniDSP OUT1", NULL, "Left ADC"},
	{"ADC MiniDSP OUT1", NULL, "Right ADC"},
	{"ADC MiniDSP OUT2", NULL, "Left ADC"},
	{"ADC MiniDSP OUT2", NULL, "Right ADC"},
	{"ADC MiniDSP OUT3", NULL, "Left ADC"},
	{"ADC MiniDSP OUT3", NULL, "Right ADC"},


	{"ASI1OUT Route", "ADC MiniDSP Out1","ADC MiniDSP OUT1"},// Port 1
	{"ASI1OUT Route", "ASI1In Bypass","ASI1IN Port"},
	{"ASI1OUT Route", "ASI2In Bypass","ASI2IN Port"},
	{"ASI1OUT Route", "ASI3In Bypass","ASI3IN Port"},

	{"ASI2OUT Route", "ADC MiniDSP Out1","ADC MiniDSP OUT1"},// Port 1
	{"ASI2OUT Route", "ASI1In Bypass","ASI1IN Port"},
	{"ASI2OUT Route", "ASI2In Bypass","ASI2IN Port"},
	{"ASI2OUT Route", "ASI3In Bypass","ASI3IN Port"},
	{"ASI2OUT Route", "ADC MiniDSP Out2","ADC MiniDSP OUT2"},// Port 2

	{"ASI3OUT Route", "ADC MiniDSP Out1","ADC MiniDSP OUT1"},// Port 1
	{"ASI3OUT Route", "ASI1In Bypass","ASI1IN Port"},
	{"ASI3OUT Route", "ASI2In Bypass","ASI2IN Port"},
	{"ASI3OUT Route", "ASI3In Bypass","ASI3IN Port"},
	{"ASI3OUT Route", "ADC MiniDSP Out3","ADC MiniDSP OUT3"},// Port 3

	{"ASI1OUT",NULL,"ASI1OUT Route"},	
	{"ASI2OUT",NULL,"ASI2OUT Route"},	
	{"ASI3OUT",NULL,"ASI3OUT Route"},	


	{"DOUT1 Route", "ASI1 Out", "ASI1OUT"},
	{"DOUT1 Route", "DIN1 Bypass", "DIN1"},
	{"DOUT1 Route", "DIN2 Bypass", "DIN2"},
	{"DOUT1 Route", "DIN3 Bypass", "DIN3"},

	{"DOUT2 Route", "ASI2 Out", "ASI2OUT"},
	{"DOUT2 Route", "DIN1 Bypass", "DIN1"},
	{"DOUT2 Route", "DIN2 Bypass", "DIN2"},
	{"DOUT2 Route", "DIN3 Bypass", "DIN3"},

	{"DOUT3 Route", "ASI3 Out", "ASI3OUT"},
	{"DOUT3 Route", "DIN1 Bypass", "DIN1"},
	{"DOUT3 Route", "DIN2 Bypass", "DIN2"},
	{"DOUT3 Route", "DIN3 Bypass", "DIN3"},

	{"DOUT1", NULL, "DOUT1 Route"},
	{"DOUT2", NULL, "DOUT2 Route"},
	{"DOUT3", NULL, "DOUT3 Route"},


};
#define AIC3262_DAPM_ROUTE_NUM (sizeof(aic3262_dapm_routes)/sizeof(struct snd_soc_dapm_route))


/* aic3262_firmware_load
   This function is called by the request_firmware_nowait function as soon 
   as the firmware has been loaded from the file. The firmware structure
   contains the data and the size of the firmware loaded.
 */

void aic3262_firmware_load(const struct firmware *fw, void *context)
{
	struct snd_soc_codec *codec = context;
	struct aic3262_priv *private_ds = snd_soc_codec_get_drvdata(codec);
	
	mutex_lock(&private_ds->cfw_mutex);
	if(private_ds->cur_fw != NULL) release_firmware(private_ds->cur_fw);
	private_ds->cur_fw = NULL ;  
	if (fw==NULL)
	{
		dev_dbg(codec->dev,"Default firmware load\n");
		/*Request firmware failed due to non availbility of firmware file. Hence,Default firmware is getting loaded */
		if(!private_ds->isdefault_fw) // default firmware is already loaded
		{
			aic3xxx_cfw_reload( private_ds->cfw_p,default_firmware,sizeof(default_firmware) );
			private_ds->isdefault_fw = 1;
	                //init function for transition
	                aic3xxx_cfw_transition(private_ds->cfw_p,"INIT");
		}
	}
	else
	{
		dev_dbg(codec->dev,"Firmware load\n");
		private_ds->cur_fw = fw;
		aic3xxx_cfw_reload(private_ds->cfw_p,(void*)fw->data,fw->size);
		private_ds->isdefault_fw	= 0;
	        //init function for transition
	        aic3xxx_cfw_transition(private_ds->cfw_p,"INIT");
	}
        // when new firmware is loaded, mode is changed to zero and config is changed to zero
        aic3xxx_cfw_setmode_cfg(private_ds->cfw_p,0,0);
        mutex_unlock(&private_ds->cfw_mutex);
}

/*
 *****************************************************************************
 * Function Definitions
 *****************************************************************************
 */

/* headset work and headphone/headset jack interrupt handlers */


static void aic3262_hs_jack_report(struct snd_soc_codec *codec,
		struct snd_soc_jack *jack, int report)
{
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);
	int status, state = 0;

	mutex_lock(&aic3262->mutex);

	// Sync status 
	status = snd_soc_read(codec, AIC3262_DAC_FLAG);
	// We will check only stereo MIC and headphone 
	if(status & AIC3262_JACK_WITH_STEREO_HS)
		state |= SND_JACK_HEADPHONE;
	if(status & AIC3262_JACK_WITH_MIC)
		state |= SND_JACK_MICROPHONE;


	mutex_unlock(&aic3262->mutex);

	snd_soc_jack_report(jack, state, report);
	if (&aic3262->hs_jack.sdev)
		switch_set_state(&aic3262->hs_jack.sdev, !!state);
}

void aic3262_hs_jack_detect(struct snd_soc_codec *codec,
		struct snd_soc_jack *jack, int report)
{
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);
	struct aic3262_jack_data *hs_jack = &aic3262->hs_jack;

	hs_jack->jack = jack;
	hs_jack->report = report;

	aic3262_hs_jack_report(codec, hs_jack->jack, hs_jack->report);
}
EXPORT_SYMBOL_GPL(aic3262_hs_jack_detect);

static void aic3262_accessory_work(struct work_struct *work)
{
	struct aic3262_priv *aic3262 = container_of(work,
			struct aic3262_priv, delayed_work.work);
	struct snd_soc_codec *codec = aic3262->codec;
	struct aic3262_jack_data *hs_jack = &aic3262->hs_jack;

	aic3262_hs_jack_report(codec, hs_jack->jack, hs_jack->report);
}

/* audio interrupt handler */
/*static irqreturn_t aic3262_audio_handler(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);

	queue_delayed_work(aic3262->workqueue, &aic3262->delayed_work,
			msecs_to_jiffies(200));

	return IRQ_HANDLED;
}*/ //sxj

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_write_reg_cache
 * Purpose  : This function is to write aic3262 register cache
 *
 *----------------------------------------------------------------------------
 */
void aic3262_write_reg_cache(struct snd_soc_codec *codec,
		u16 reg, u8 value)
{
	u8 *cache = codec->reg_cache;

	if (reg >= AIC3262_CACHEREGNUM) {
		return;
	}
	cache[reg] = value;
	return;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_codec_read
 * Purpose  : This function is to read the aic3262 register space.
 *
 *----------------------------------------------------------------------------
 */
unsigned int aic3262_codec_read(struct snd_soc_codec *codec, unsigned int reg)
{

	u8 value;
	aic326x_reg_union *aic_reg = (aic326x_reg_union *)&reg; 
	value = aic3262_reg_read(codec->control_data, reg);
	dev_dbg(codec->dev,"p %d ,r 30 %x %x \n",aic_reg->aic326x_register.page,aic_reg->aic326x_register.offset,value);
	return value;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_write
 * Purpose  : This function is to write to the aic3262 register space.
 *
 *----------------------------------------------------------------------------
 */
int aic3262_codec_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	aic326x_reg_union *aic_reg = (aic326x_reg_union *)&reg; 
	dev_dbg(codec->dev,"p %d,w 30 %x %x \n",aic_reg->aic326x_register.page,aic_reg->aic326x_register.offset,value);
	return aic3262_reg_write(codec->control_data, reg, value);
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_reset_cache
 * Purpose  : This function is to reset the cache.
 *----------------------------------------------------------------------------
 */
int aic3262_reset_cache (struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev,KERN_ALERT "codec: %s : started\n", __func__);

#if defined(EN_REG_CACHE)
	if (codec->reg_cache != NULL) {
		memcpy(codec->reg_cache, aic3262_reg, sizeof (aic3262_reg));
		return 0;
	}

	codec->reg_cache = kmemdup (aic3262_reg, sizeof (aic3262_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL) {
		dev_err(codec->dev,"aic32x4: kmemdup failed\n");
		return -ENOMEM;
	}
#endif
	dev_dbg(codec->dev,KERN_ALERT "codec: %s : ended\n", __func__);

	return 0;
}


/*
 *----------------------------------------------------------------------------
 * Function : aic3262_add_widgets
 * Purpose  : This function is to add the dapm widgets
 *            The following are the main widgets supported
 *                # Left DAC to Left Outputs
 *                # Right DAC to Right Outputs
 *		  # Left Inputs to Left ADC
 *		  # Right Inputs to Right ADC
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_add_widgets(struct snd_soc_codec *codec)
{

	snd_soc_dapm_new_controls(&codec->dapm, aic3262_dapm_widgets,
			ARRAY_SIZE(aic3262_dapm_widgets));
	/* set up audio path interconnects */
	dev_dbg(codec->dev,"#Completed adding new dapm widget controls size=%d\n",ARRAY_SIZE(aic3262_dapm_widgets));

	snd_soc_dapm_add_routes(&codec->dapm, aic3262_dapm_routes, ARRAY_SIZE(aic3262_dapm_routes));
	dev_dbg(codec->dev,"#Completed adding DAPM routes\n");
	snd_soc_dapm_new_widgets(&codec->dapm);
	dev_dbg(codec->dev,"#Completed updating dapm\n");

	return 0;
}
 /*----------------------------------------------------------------------------
 * Function : aic3262_hw_params
 * Purpose  : This function is to set the hardware parameters for AIC3262.
 *            The functions set the sample rate and audio serial data word
 *            length.
 *
 *----------------------------------------------------------------------------
 */
int aic3262_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);
	int asi_reg, bclk_reg;
	u8 data = 0;


	if(substream->stream==SNDRV_PCM_STREAM_PLAYBACK)
		aic3262->stream_status=1;
	else
		aic3262->stream_status=0;


	switch(dai->id)
	{
		case 0:
			asi_reg = AIC3262_ASI1_BUS_FMT;
			bclk_reg = AIC3262_ASI1_BCLK_N;
			break;
		case 1:
			asi_reg = AIC3262_ASI2_BUS_FMT;
			bclk_reg = AIC3262_ASI2_BCLK_N;
			break;
		case 2:
			asi_reg = AIC3262_ASI3_BUS_FMT;
			bclk_reg = AIC3262_ASI3_BCLK_N;
			break;
		default:
			return -EINVAL;


	}

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			data = data | 0x00;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			data |= (0x08);
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			data |= (0x10);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			data |= (0x18);
			break;
	}

	/* configure the respective Registers for the above configuration */
	snd_soc_update_bits(codec, asi_reg, AIC3262_ASI_DATA_WORD_LENGTH_MASK, data);
	return 0;
}
/*
 *----------------------------------------------------------------------------
 * Function : aic3262_mute
 * Purpose  : This function is to mute or unmute the left and right DAC
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	//	int mute_reg;

	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "codec : %s : started\n", __FUNCTION__ );
	if(dai->id > 2)
		return -EINVAL;
	if(mute) {
		aic3262->mute_asi &= ~((0x1) << dai->id);
		if(aic3262->mute_asi == 0)// Mute only when all asi's are muted
			snd_soc_update_bits_locked(codec, AIC3262_DAC_MVOL_CONF, AIC3262_DAC_LR_MUTE_MASK,AIC3262_DAC_LR_MUTE);

	} else { // Unmute
		if(aic3262->mute_asi == 0)// Unmute for the first asi that need to unmute. rest unmute will pass 
			snd_soc_update_bits_locked(codec, AIC3262_DAC_MVOL_CONF, AIC3262_DAC_LR_MUTE_MASK, 0x0);
		aic3262->mute_asi |= ((0x1) <<  dai->id);
	}
	dev_dbg(codec->dev, "codec : %s : ended\n", __FUNCTION__ );
	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_set_dai_sysclk
 * Purpose  : This function is to set the DAI system clock
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct aic3262_priv *aic3262; 
	struct snd_soc_codec *codec;
	codec = codec_dai->codec;
	aic3262 = snd_soc_codec_get_drvdata(codec);
	switch (freq) {
		case AIC3262_FREQ_11289600:
			aic3262->sysclk = freq;
			return 0;
		case AIC3262_FREQ_12000000:
			aic3262->sysclk = freq;
			return 0;
		case AIC3262_FREQ_24000000:
			aic3262->sysclk = freq;
			return 0;
			break;
		case AIC3262_FREQ_19200000:
			aic3262->sysclk = freq;
			return 0;
			break;
		case AIC3262_FREQ_38400000:
			aic3262->sysclk = freq;
			dev_dbg(codec->dev,"codec: sysclk = %d\n", aic3262->sysclk);
			return 0;
			break;

	}
	dev_err(codec->dev,"Invalid frequency to set DAI system clock\n");

	return -EINVAL;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_set_dai_fmt
 * Purpose  : This function is to set the DAI format
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	//	struct aic3262_priv *aic3262 = aic3262_priv_data;
	struct aic3262_priv *aic3262; 
	struct snd_soc_codec *codec;
	u8 iface_val, master,dsp_a_val ;
	int aif_bclk_wclk_reg; 
	int aif_interface_reg; 
	int aif_bclk_offset_reg; 
	int iface_reg = 0; 			//must be init, not will lead to error
	codec = codec_dai->codec;
	aic3262 = snd_soc_codec_get_drvdata(codec);
	iface_val = 0x00;
	master = 0x0;
	dsp_a_val = 0x0;
	switch(codec_dai->id)
	{
		case 0:
			aif_bclk_wclk_reg = AIC3262_ASI1_BWCLK_CNTL_REG;
			aif_interface_reg = AIC3262_ASI1_BUS_FMT;
			aif_bclk_offset_reg = AIC3262_ASI1_LCH_OFFSET;	
			break;
		case 1:
			aif_bclk_wclk_reg = AIC3262_ASI2_BWCLK_CNTL_REG;
			aif_interface_reg = AIC3262_ASI2_BUS_FMT;
			aif_bclk_offset_reg = AIC3262_ASI2_LCH_OFFSET;	
			break;
		case 2:
			aif_bclk_wclk_reg = AIC3262_ASI3_BWCLK_CNTL_REG;
			aif_interface_reg = AIC3262_ASI3_BUS_FMT;
			aif_bclk_offset_reg = AIC3262_ASI3_LCH_OFFSET;	
			break;
		default:
			return -EINVAL;

	}
	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:
			dev_dbg(codec->dev, "setdai_fmt : SND_SOC_DAIFMT_CBM_CFM : master=1 \n");
			aic3262->master = 1;
			master |= (AIC3262_WCLK_OUT_MASK | AIC3262_BCLK_OUT_MASK);
			break;
		case SND_SOC_DAIFMT_CBS_CFS:
			dev_dbg(codec->dev, "setdai_fmt : SND_SOC_DAIFMT_CBS_CFS : master=0 \n");

			aic3262->master = 0;
			break;
		case SND_SOC_DAIFMT_CBS_CFM: //new case..just for debugging
			master |= (AIC3262_WCLK_OUT_MASK);
			dev_dbg(codec->dev,"%s: SND_SOC_DAIFMT_CBS_CFM\n", __FUNCTION__);
			aic3262->master = 0;
			break;
		case SND_SOC_DAIFMT_CBM_CFS:	
			master |= (AIC3262_BCLK_OUT_MASK);
			aic3262->master = 0;
			break;

		default:
			dev_err(codec->dev, "Invalid DAI master/slave interface\n");

			return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			iface_val = (iface_reg & 0x1f);
			break;
		case SND_SOC_DAIFMT_DSP_A:
			dsp_a_val = 0x1; /* Intentionally falling back to following case */
		case SND_SOC_DAIFMT_DSP_B:
			iface_val = (iface_reg & 0x1f) | 0x20;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			iface_val = (iface_reg & 0x1f) | 0x40;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			iface_val = (iface_reg & 0x1f) | 0x60;
			break;

			dev_err(codec->dev,"Invalid DAI interface format\n");

			return -EINVAL;
	}
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_DSP_A:	
		case SND_SOC_DAIFMT_DSP_B:	
			switch(fmt & SND_SOC_DAIFMT_INV_MASK) {
				case SND_SOC_DAIFMT_NB_NF:
					break;
				case SND_SOC_DAIFMT_IB_NF:
					master |= AIC3262_BCLK_INV_MASK;
					break;
				default:
					return -EINVAL;
			}
			break;
		case SND_SOC_DAIFMT_I2S:
		case SND_SOC_DAIFMT_RIGHT_J:
		case SND_SOC_DAIFMT_LEFT_J:
			switch(fmt & SND_SOC_DAIFMT_INV_MASK) {
				case SND_SOC_DAIFMT_NB_NF:
					break;
				case SND_SOC_DAIFMT_IB_NF:
					master |= AIC3262_BCLK_INV_MASK; 
					break;
				default:
					return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
	}
	snd_soc_update_bits(codec, aif_bclk_wclk_reg, AIC3262_WCLK_BCLK_MASTER_MASK ,master);
	snd_soc_update_bits(codec, aif_interface_reg, AIC3262_ASI_INTERFACE_MASK ,iface_val);
	snd_soc_update_bits(codec, aif_bclk_offset_reg, AIC3262_BCLK_OFFSET_MASK,dsp_a_val);

	return 0;
}

static int aic3262_dai_set_pll(struct snd_soc_dai *dai, int pll_id, int source,
                          unsigned int Fin, unsigned int Fout)      
{
	struct snd_soc_codec *codec = dai->codec;
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);
	
	dev_dbg(codec->dev,"In aic3262: dai_set_pll\n");
	dev_dbg(codec->dev,"%d,%s,dai->id = %d\n", __LINE__ , __FUNCTION__ ,dai->id);	
	// select the PLL_CLKIN
	snd_soc_update_bits(codec, AIC3262_PLL_CLKIN_REG, AIC3262_PLL_CLKIN_MASK, source << AIC3262_PLL_CLKIN_SHIFT);
	// TODO: How to select low/high clock range?
	
        mutex_lock(&aic3262->cfw_mutex);
	aic3xxx_cfw_set_pll(aic3262->cfw_p,dai->id);
        mutex_unlock(&aic3262->cfw_mutex);

	return 0;


}
/*
 *----------------------------------------------------------------------------
 * Function : aic3262_set_bias_level
 * Purpose  : This function is to get triggered when dapm events occurs.
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{

	switch (level) {
		/* full On */
		case SND_SOC_BIAS_ON:

			dev_dbg(codec->dev, "set_bias_on \n");
			break;

			/* partial On */
		case SND_SOC_BIAS_PREPARE:
			dev_dbg(codec->dev, "set_bias_prepare \n");
			break;


			/* Off, with power */
		case SND_SOC_BIAS_STANDBY:
			/*
			 * all power is driven by DAPM system,
			 * so output power is safe if bypass was set
			 */
			dev_dbg(codec->dev, "set_bias_stby \n");
			if(codec->dapm.bias_level == SND_SOC_BIAS_OFF)
			{
				snd_soc_update_bits(codec, AIC3262_POWER_CONF, (AIC3262_AVDD_TO_DVDD_MASK | AIC3262_EXT_ANALOG_SUPPLY_MASK), 0x0);

			}
			snd_soc_update_bits(codec, AIC3262_REF_PWR_DLY, AIC3262_CHIP_REF_PWR_ON_MASK, AIC3262_CHIP_REF_PWR_ON);

			break;


			/* Off, without power */
		case SND_SOC_BIAS_OFF:
			dev_dbg(codec->dev, "set_bias_off \n");
			/* force all power off */
			snd_soc_update_bits(codec, AIC3262_REF_PWR_DLY, AIC3262_CHIP_REF_PWR_ON_MASK, 0x0);
			snd_soc_update_bits(codec, AIC3262_POWER_CONF, (AIC3262_AVDD_TO_DVDD_MASK | AIC3262_EXT_ANALOG_SUPPLY_MASK),
						 (AIC3262_AVDD_TO_DVDD | AIC3262_EXT_ANALOG_SUPPLY_OFF));
			break;
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_suspend
 * Purpose  : This function is to suspend the AIC3262 driver.
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	aic3262_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_resume
 * Purpose  : This function is to resume the AIC3262 driver
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_resume(struct snd_soc_codec *codec)
{
	aic3262_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_probe
 * Purpose  : This is first driver function called by the SoC core driver.
 *
 *----------------------------------------------------------------------------
 */

#ifdef AIC3262_PROC	
static int aic3262_proc_init(void);
#endif

static int aic3262_codec_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	struct aic3262 *control;
	struct aic3262_priv *aic3262; 
	//struct aic3262_jack_data *jack;
	if(codec == NULL)
		dev_err(codec->dev,"codec pointer is NULL. \n");

	#ifdef AIC3262_PROC	
	aic3262_proc_init();
	#endif

	codec->control_data = dev_get_drvdata(codec->dev->parent);
	control = codec->control_data;

	aic3262 = kzalloc(sizeof(struct aic3262_priv), GFP_KERNEL);

	if(aic3262 == NULL)
		return -ENOMEM;
	snd_soc_codec_set_drvdata( codec, aic3262);

	aic3262->pdata = dev_get_platdata(codec->dev->parent);
	aic3262->codec = codec;

	aic3262->cur_fw = NULL;
	aic3262->isdefault_fw= 0;
        aic3262->cfw_p = &(aic3262->cfw_ps);
	aic3262->cfw_p->ops = &aic3262_cfw_codec_ops; 
	aic3262->cfw_p->ops_obj = aic3262;

	aic3262->workqueue = create_singlethread_workqueue("aic3262-codec");
	if( !aic3262->workqueue) {
		ret = -ENOMEM;
		goto work_err;
	}
	/*ret = device_create_file(codec->dev, &dev_attr_debug_level);	
	if (ret)                                                      	
		dev_info(codec->dev, "Failed to add debug_level sysfs \n");*/	//sxj
	INIT_DELAYED_WORK(&aic3262->delayed_work, aic3262_accessory_work);

	mutex_init(&aic3262->mutex);
	mutex_init(&aic3262->cfw_mutex);	
	pm_runtime_enable(codec->dev);
	pm_runtime_resume(codec->dev);
	aic3262->dsp_runstate = 0;
	/* use switch-class based headset reporting if platform requires it */
	/*jack = &aic3262->hs_jack;			
	jack->sdev.name = "h2w";
	ret = switch_dev_register(&jack->sdev);
	if(ret)	{
		dev_err(codec->dev, "error registering switch device %d\n",ret);
		goto reg_err;
	}
	if(control->irq)
	{
		ret = aic3262_request_irq(codec->control_data, AIC3262_IRQ_HEADSET_DETECT,
			aic3262_audio_handler, IRQF_NO_SUSPEND,"aic3262_irq_headset",
			codec);

		if(ret){
			dev_err(codec->dev, "HEADSET detect irq request failed:%d\n",ret);
			goto irq_err;
		}
	}*/		//sxj

	aic3262_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	aic3262->mute_asi = 0;

	snd_soc_add_controls(codec, aic3262_snd_controls,
			ARRAY_SIZE(aic3262_snd_controls));
	mutex_init(&codec->mutex);

	aic3262_add_widgets(codec);

#ifdef AIC3262_TiLoad
	ret = aic3262_driver_init(codec);
	if (ret < 0)
		dev_err(codec->dev,"\nTiLoad Initialization failed\n");
#endif
        // force loading the default firmware
        aic3262_firmware_load(NULL,codec);
	dev_dbg(codec->dev,"%d,%s,Firmware test\n",__LINE__,__FUNCTION__);
	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,"tlv320aic3262_fw_v1.bin", codec->dev, GFP_KERNEL,codec, aic3262_firmware_load);
	
	aic3262_codec = codec;
	
	return 0;
//irq_err:
//	switch_dev_unregister(&jack->sdev);
//reg_err:			//sxj
work_err:
	kfree(aic3262);
	return 0;	
}

/*
 *----------------------------------------------------------------------------
 * Function : aic3262_remove
 * Purpose  : to remove aic3262 soc device
 *
 *----------------------------------------------------------------------------
 */
static int aic3262_codec_remove(struct snd_soc_codec *codec)
{
	/* power down chip */
	struct aic3262_priv *aic3262 = snd_soc_codec_get_drvdata(codec);
	struct aic3262 *control = codec->control_data;
	//struct aic3262_jack_data *jack = &aic3262->hs_jack;	//sxj

	aic3262_set_bias_level(codec, SND_SOC_BIAS_OFF);

	pm_runtime_disable(codec->dev);
	/* free_irq if any */
	switch(control->type) {
		case TLV320AIC3262:
			aic3262_free_irq(control, AIC3262_IRQ_HEADSET_DETECT, codec);
			break;
	}
	/* release firmware if any */
	if(aic3262->cur_fw != NULL)
	{
		release_firmware(aic3262->cur_fw);
	}
	/* destroy workqueue for jac dev */
	//switch_dev_unregister(&jack->sdev);	//sxj
	destroy_workqueue(aic3262->workqueue);

	kfree(aic3262);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_driver_aic326x = {
	.probe = aic3262_codec_probe,
	.remove = aic3262_codec_remove,
	.suspend = aic3262_suspend,
	.resume = aic3262_resume,
	.read = aic3262_codec_read,
	.write = aic3262_codec_write,
	.set_bias_level = aic3262_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(aic3262_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = aic3262_reg,
};
static int aic326x_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_driver_aic326x, 
			aic326x_dai_driver, ARRAY_SIZE(aic326x_dai_driver));

}

static int aic326x_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver aic326x_codec_driver = {
	.driver = {
		.name = "tlv320aic3262-codec",
		.owner = THIS_MODULE,
	},
	.probe = aic326x_probe,
	.remove = __devexit_p(aic326x_remove),
};
/*
 *----------------------------------------------------------------------------
 * Function : tlv320aic3262_modinit
 * Purpose  : module init function. First function to run.
 *
 *----------------------------------------------------------------------------
 */
static int __init tlv320aic3262_modinit(void)
{
	return platform_driver_register(&aic326x_codec_driver);
}

module_init(tlv320aic3262_modinit);

/*
 *----------------------------------------------------------------------------
 * Function : tlv320aic3262_exit
 * Purpose  : module init function. First function to run.
 *
 *----------------------------------------------------------------------------
 */
static void __exit tlv320aic3262_exit(void)
{
	platform_driver_unregister(&aic326x_codec_driver);

}

module_exit(tlv320aic3262_exit);
MODULE_ALIAS("platform:tlv320aic3262-codec");
MODULE_DESCRIPTION("ASoC TLV320AIC3262 codec driver");
MODULE_AUTHOR("Y Preetam Sashank Reddy ");
MODULE_AUTHOR("Barani Prashanth ");
MODULE_AUTHOR("Mukund Navada K <navada@ti.com>");
MODULE_AUTHOR("Naren Vasanad <naren.vasanad@ti.com>");
MODULE_LICENSE("GPL");


#ifdef AIC3262_PROC


/*static void test_playback(void)
{
	int ret;
	printk("test palyback start\n");

	aic3262_write(aic3262_codec, 0x00, 0x00);
	ret = aic3262_read(aic3262_codec,0x00);
	printk("0x00 = %x\n",ret);
	aic3262_write(aic3262_codec, 0x7f, 0x00);
	ret = aic3262_read(aic3262_codec,0x7f);
	printk("0x7f = %x\n",ret);
	aic3262_write(aic3262_codec, 0x01, 0x01);
	ret = aic3262_read(aic3262_codec,0x01);
	printk("0x01 = %x\n",ret);

	aic3262_write(aic3262_codec, 0x04, 0x00);
	aic3262_write(aic3262_codec, 0x0b, 0x81);
	aic3262_write(aic3262_codec, 0x0c, 0x82);
	aic3262_write(aic3262_codec, 0x0d, 0x00);
	aic3262_write(aic3262_codec, 0x0e, 0x80);
	
	aic3262_write(aic3262_codec, 0x00, 0x01);
	aic3262_write(aic3262_codec, 0x01+1*128, 0x00);
	aic3262_write(aic3262_codec, 0x7a+1*128, 0x01);

	aic3262_write(aic3262_codec, 0x00, 0x04);
	aic3262_write(aic3262_codec, 0x01+4*128, 0x00);
	aic3262_write(aic3262_codec, 0x0a+4*128, 0x00);

	aic3262_write(aic3262_codec, 0x00, 0x00);
	aic3262_write(aic3262_codec, 0x3c, 0x01);

	aic3262_write(aic3262_codec, 0x00, 0x01);
	aic3262_write(aic3262_codec, 0x03+1*128, 0x00);
	aic3262_write(aic3262_codec, 0x04+1*128, 0x00);
	aic3262_write(aic3262_codec, 0x1f+1*128, 0x80);
	aic3262_write(aic3262_codec, 0x20+1*128, 0x00);
	aic3262_write(aic3262_codec, 0x21+1*128, 0x28);
	aic3262_write(aic3262_codec, 0x23+1*128, 0x10);
	aic3262_write(aic3262_codec, 0x1b+1*128, 0x33);
	aic3262_write(aic3262_codec, 0x00, 0x00);
	aic3262_write(aic3262_codec, 0x3f, 0xc0);
	aic3262_write(aic3262_codec, 0x40, 0x00);

	aic3262_write(aic3262_codec, 0x00, 0x01);
	aic3262_write(aic3262_codec, 0x16+1*128, 0xc3);
	ret = aic3262_read(aic3262_codec,0x16);
	printk("0x16 = %x\n",ret);
	aic3262_write(aic3262_codec, 0xae, 0x00);
	aic3262_write(aic3262_codec, 0x2f+1*128, 0x00);
	aic3262_write(aic3262_codec, 0x30+1*128, 0x11);
	aic3262_write(aic3262_codec, 0x52+1*128, 0x75);
	aic3262_write(aic3262_codec, 0x53+1*128, 0x03);
	aic3262_write(aic3262_codec, 0x2d+1*128, 0x03);
	aic3262_write(aic3262_codec, 0x00, 0x00);
	aic3262_write(aic3262_codec, 0x3f, 0xc0);
	aic3262_write(aic3262_codec, 0x40, 0x00);

}


static void AP_to_speaker(void)
{
	printk("AP_to_speaker\n");

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RESET_REG, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_DAC_ADC_CLKIN_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_NDAC_DIV_POW_REG, 0x81);
	aic3262_codec_write(aic3262_codec, AIC3262_MDAC_DIV_POW_REG, 0x82);
	aic3262_codec_write(aic3262_codec, AIC3262_DOSR_MSB_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_DOSR_LSB_REG, 0x80);
	
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_POWER_CONF, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_REF_PWR_DLY, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x04);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BUS_FMT, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BWCLK_CNTL_REG, 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_DAC_PRB, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_LDAC_PTM, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RDAC_PTM, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_LINE_AMP_CNTL_R1, 0xC3);
	aic3262_codec_write(aic3262_codec, AIC3262_SPK_AMP_CNTL_R2, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_SPK_AMP_CNTL_R3, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_SPK_AMP_CNTL_R4, 0x11);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,1, 82), 0x75);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,1, 83), 0x03);
	aic3262_codec_write(aic3262_codec, AIC3262_SPK_AMP_CNTL_R1, 0x03);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_PASI_DAC_DP_SETUP, 0xc0);
	aic3262_codec_write(aic3262_codec, AIC3262_DAC_MVOL_CONF, 0x00);

}


static void AP_to_headphone(void)
{
	printk("AP_to_headphone\n");

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RESET_REG, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_DAC_ADC_CLKIN_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_NDAC_DIV_POW_REG, 0x81);
	aic3262_codec_write(aic3262_codec, AIC3262_MDAC_DIV_POW_REG, 0x82);
	aic3262_codec_write(aic3262_codec, AIC3262_DOSR_MSB_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_DOSR_LSB_REG, 0x80);
	
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_POWER_CONF, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_REF_PWR_DLY, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x04);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BUS_FMT, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BWCLK_CNTL_REG, 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_DAC_PRB, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_LDAC_PTM, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RDAC_PTM, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_HPL_VOL, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_HPR_VOL, 0x80);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,1, 33), 0x28);
	aic3262_codec_write(aic3262_codec, AIC3262_CHARGE_PUMP_CNTL, 0x10);
	aic3262_codec_write(aic3262_codec, AIC3262_HP_AMP_CNTL_R1, 0x33);
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_PASI_DAC_DP_SETUP, 0xc0);
	aic3262_codec_write(aic3262_codec, AIC3262_DAC_MVOL_CONF, 0x00);

}*/

/*static void record_in1lr(void)
{
	printk("record in1lr\n");

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RESET_REG, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_DAC_ADC_CLKIN_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_NADC_DIV_POW_REG, 0x81);
	aic3262_codec_write(aic3262_codec, AIC3262_MADC_DIV_POW_REG, 0x82);
	aic3262_codec_write(aic3262_codec, AIC3262_AOSR_REG, 0x80);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_POWER_CONF, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_REF_PWR_DLY, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_MIC_PWR_DLY, 0x33);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x04);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BUS_FMT, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI1_BWCLK_CNTL_REG, 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_ADC_PRB, 0x01);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_MIC_BIAS_CNTL, 0x40);
	aic3262_codec_write(aic3262_codec, AIC3262_LMIC_PGA_PIN, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_LMIC_PGA_MIN, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_RMIC_PGA_PIN, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_RMIC_PGA_MIN, 0x80);

	aic3262_codec_write(aic3262_codec, AIC3262_MICL_PGA, 0x3c);
	aic3262_codec_write(aic3262_codec, AIC3262_MICR_PGA, 0x3c);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,1, 61), 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_PASI_DAC_DP_SETUP, 0xc0);
	aic3262_codec_write(aic3262_codec, AIC3262_DAC_MVOL_CONF, 0x00);
}

static void record_in2lr(void)
{
	printk("record in2lr\n");

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_RESET_REG, 0x01);

}

static void incall_mic(void)
{
	unsigned char ret;
	//Choose Book 0
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);

	// Clock Configuration
	//ret = aic3262_codec_read(aic3262_codec, AIC3262_DAC_ADC_CLKIN_REG);
	//ret = ret &0xf0;
	//aic3262_codec_write(aic3262_codec, AIC3262_DAC_ADC_CLKIN_REG, ret);
	//aic3262_codec_write(aic3262_codec, AIC3262_NADC_DIV_POW_REG, 0x81);
	//aic3262_codec_write(aic3262_codec, AIC3262_MADC_DIV_POW_REG, 0x8b);
	//aic3262_codec_write(aic3262_codec, AIC3262_AOSR_REG, 0x80);

	// Initialize the Codec
	//aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	//aic3262_codec_write(aic3262_codec, AIC3262_POWER_CONF, 0x00);
	//aic3262_codec_write(aic3262_codec, AIC3262_REF_PWR_DLY, 0x01);
	//aic3262_codec_write(aic3262_codec, AIC3262_MIC_PWR_DLY, 0x33);

	//ASI#3 configuration
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x04);
	//aic3262_codec_write(aic3262_codec, AIC3262_ASI3_BUS_FMT, 0x00);
	//aic3262_codec_write(aic3262_codec, AIC3262_ASI3_ADC_INPUT_CNTL, 0x01);
	//aic3262_codec_write(aic3262_codec, AIC3262_ASI3_BWCLK_CNTL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI3_DOUT_CNTL, 0x00);

	//Signal processing
	//aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	//aic3262_codec_write(aic3262_codec, AIC3262_ADC_PRB, 0x01);

	//ADC configuration
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x01);
	aic3262_codec_write(aic3262_codec, AIC3262_MIC_BIAS_CNTL, 0x55);
	aic3262_codec_write(aic3262_codec, AIC3262_LMIC_PGA_PIN, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_LMIC_PGA_MIN, 0x80);
	aic3262_codec_write(aic3262_codec, AIC3262_RMIC_PGA_PIN, 0x80);
	
	aic3262_codec_write(aic3262_codec, AIC3262_RMIC_PGA_MIN, 0x20);
	aic3262_codec_write(aic3262_codec, AIC3262_MICL_PGA, 0x3c);
	aic3262_codec_write(aic3262_codec, AIC3262_MICR_PGA, 0x3c);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,1, 61), 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_ADC_CHANNEL_POW, 0xc0);
	aic3262_codec_write(aic3262_codec, AIC3262_ADC_FINE_GAIN, 0x00);
}*/

static void test(void)
{
	//Choose Book 0
	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x00);
	aic3262_codec_write(aic3262_codec, MAKE_REG(0,0,127), 0x00);

	aic3262_codec_write(aic3262_codec, AIC3262_PAGE_SEL_REG, 0x04);
	aic3262_codec_write(aic3262_codec, AIC3262_ASI3_BUS_FMT, 0x60);
}

static void test_playback(void)
{
	
	printk("test\n");

	test( );

}


static ssize_t aic3262_proc_write(struct file *file, const char __user *buffer,
			   unsigned long len, void *data)
{
	char *cookie_pot; 
	char *p;
	int reg;
	int value;
	
	cookie_pot = (char *)vmalloc( len );
	if (!cookie_pot) 
	{
		return -ENOMEM;
	} 
	else 
	{
		if (copy_from_user( cookie_pot, buffer, len )) 
			return -EFAULT;
	}

	switch(cookie_pot[0])
	{
	case 'd':
	case 'D':
		debug_write_read ++;
		debug_write_read %= 2;
		if(debug_write_read != 0)
			printk("Debug read and write reg on\n");
		else	
			printk("Debug read and write reg off\n");	
		break;	
	case 'r':
	case 'R':
		printk("Read reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,",")))
			{
				reg = simple_strtol(p,NULL,16);
				value = aic3262_codec_read(aic3262_codec,reg);
				printk("aic3262_codec_read:0x%04x = 0x%04x\n",reg,value);
			}
			debug_write_read = 0;
			printk("\n");
		}
		else
		{
			printk("Error Read reg debug.\n");
			printk("For example: echo r:22,23,24,25>aic3262_ts\n");
		}
		break;
	case 'w':
	case 'W':
		printk("Write reg debug\n");		
		if(cookie_pot[1] ==':')
		{
			debug_write_read = 1;
			strsep(&cookie_pot,":");
			while((p=strsep(&cookie_pot,"=")))
			{
				reg = simple_strtol(p,NULL,16);
				p=strsep(&cookie_pot,",");
				value = simple_strtol(p,NULL,16);
				aic3262_codec_write(aic3262_codec,reg,value);
				printk("aic3262_codec_write:0x%04x = 0x%04x\n",reg,value);
			}
			debug_write_read = 0;
			printk("\n");
		}
		else
		{
			printk("Error Write reg debug.\n");
			printk("For example: w:22=0,23=0,24=0,25=0>aic3262_ts\n");
		}
		break;
	case 'f':
	case 'F':
		test_playback( );
		break;
	case 'a':
		printk("Dump reg \n");		

		for(reg = 0; reg < 0x6e; reg+=2)
		{
			value = aic3262_codec_read(aic3262_codec,reg);
			printk("aic3262_codec_read:0x%04x = 0x%04x\n",reg,value);
		}

		break;		
	default:
		printk("Help for aic3262_ts .\n-->The Cmd list: \n");
		printk("-->'d&&D' Open or Off the debug\n");
		printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>aic3262_ts\n");
		printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>aic3262_ts\n");
		break;
	}

	return len;
}

static const struct file_operations aic3262_proc_fops = {
	.owner		= THIS_MODULE,
};

static int aic3262_proc_init(void)
{
	struct proc_dir_entry *aic3262_proc_entry;
	aic3262_proc_entry = create_proc_entry("driver/aic3262_ts", 0777, NULL);
	if(aic3262_proc_entry != NULL)
	{
		aic3262_proc_entry->write_proc = aic3262_proc_write;
		return 0;
	}
	else
	{
		printk("create proc error !\n");
		return -1;
	}
}
#endif
