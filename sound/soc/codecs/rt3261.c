/*
 * rt3261.c  --  RT3261 ALSA SoC audio codec driver
 *
 * Copyright 2011 Realtek Semiconductor Corp.
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
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
#include <mach/board.h>
#include <linux/clk.h>
#include <mach/iomux.h>

#define RT3261_PROC
#ifdef RT3261_PROC
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#endif

//#define USE_INT_CLK
#define DIFFERENTIAL 1
#define SINGLE_END 0
#define TWO_SPK 2
#define ONE_SPK 1

static struct snd_soc_codec *rt3261_codec;

#if 0
#define DBG(x...)	printk(KERN_DEBUG x)
#else
#define DBG(x...)
#endif

//#define RTK_IOCTL
#ifdef RTK_IOCTL
#if defined(CONFIG_SND_HWDEP) || defined(CONFIG_SND_HWDEP_MODULE)
#include "rt_codec_ioctl.h"
#include "rt3261_ioctl.h"
#endif
#endif

#include "rt3261.h"
#if defined (CONFIG_SND_SOC_RT3261)
#include "rt3261-dsp.h"
#endif

#define RT3261_REG_RW 1 /* for debug */
#define RT3261_DET_EXT_MIC 0

#define VERSION "RT3261_V1.3.0"

#if defined (CONFIG_SND_SOC_RT5623)
extern void rt5623_on(void);
extern void rt5623_off(void);
#endif

struct rt3261_init_reg {
	u8 reg;
	u16 val;
};

static struct rt3261_init_reg init_list[] = {
	{RT3261_GEN_CTRL1	, 0x3f01},//fa[12:13] = 1'b; fa[8~10]=1; fa[0]=1
	{RT3261_ADDA_CLK1	, 0x1114},//73[2] = 1'b
	{RT3261_MICBIAS		, 0x3030},//93[5:4] = 11'b
	{RT3261_CLS_D_OUT	, 0xa000},//8d[11] = 0'b
	{RT3261_CLS_D_OVCD	, 0x0334},//8c[8] = 1'b
	{RT3261_PRIV_INDEX	, 0x001d},//PR1d[8] = 1'b;
	{RT3261_PRIV_DATA	, 0x0347},
	{RT3261_PRIV_INDEX	, 0x003d},//PR3d[12] = 0'b; PR3d[9] = 1'b
	{RT3261_PRIV_DATA	, 0x3600},
	{RT3261_PRIV_INDEX	, 0x0012},//PR12 = 0aa8'h
	{RT3261_PRIV_DATA	, 0x0aa8},
	{RT3261_PRIV_INDEX	, 0x0014},//PR14 = 8aaa'h
	{RT3261_PRIV_DATA	, 0x8aaa},
	{RT3261_PRIV_INDEX	, 0x0020},//PR20 = 6115'h
	{RT3261_PRIV_DATA	, 0x6115},
	{RT3261_PRIV_INDEX	, 0x0023},//PR23 = 0804'h
	{RT3261_PRIV_DATA	, 0x0804},
	{RT3261_SPK_VOL     	, 0x8888},//SPKMIX -> SPKVOL
	{RT3261_HP_VOL      	, 0x8888},
	{RT3261_OUTPUT      	, 0x8888},//unmute OUTVOLL/R
	{RT3261_SPO_CLSD_RATIO 	, 0x0003},
	{RT3261_I2S1_SDP	, 0xd000},
};
#define RT3261_INIT_REG_LEN ARRAY_SIZE(init_list)

static int rt3261_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT3261_INIT_REG_LEN; i++)
		snd_soc_write(codec, init_list[i].reg, init_list[i].val);

	return 0;
}

static int rt3261_customer_redefine(struct snd_soc_codec *codec, struct rt3261_priv *rt3261)
{
	if(rt3261->spk_num==TWO_SPK)
	{
		snd_soc_update_bits(codec, RT3261_SPO_L_MIXER,
			RT3261_M_SV_R_SPM_L | RT3261_M_SV_L_SPM_L,
			1 << RT3261_M_SV_R_SPM_L_SFT | 0 << RT3261_M_SV_L_SPM_L_SFT);
		snd_soc_update_bits(codec, RT3261_SPO_R_MIXER,
			RT3261_M_SV_R_SPM_R, 0 << RT3261_M_SV_R_SPM_R_SFT);
	}
	else
	{
		snd_soc_update_bits(codec, RT3261_SPO_L_MIXER,
			RT3261_M_SV_R_SPM_L | RT3261_M_SV_L_SPM_L,
			0 << RT3261_M_SV_R_SPM_L_SFT | 0 << RT3261_M_SV_L_SPM_L_SFT);
		snd_soc_update_bits(codec, RT3261_SPO_R_MIXER,
			RT3261_M_SV_R_SPM_R, 1 << RT3261_M_SV_R_SPM_R_SFT);
	}

	
	snd_soc_update_bits(codec, RT3261_IN3_IN4,
		RT3261_IN_DF2, rt3261->modem_input_mode << RT3261_IN_SFT2);
	snd_soc_update_bits(codec, RT3261_GEN_CTRL1,
		RT3261_LOUT_DF_MASK, rt3261->lout_to_modem_mode << RT3261_LOUT_DF);
	snd_soc_update_bits(codec, RT3261_SPO_CLSD_RATIO,
		RT3261_SPO_CLSD_RATIO_MASK, rt3261->spk_amplify);
	snd_soc_update_bits(codec, RT3261_DIG_INF_DATA,
		RT3261_IF1_DAC_SEL_MASK	| RT3261_IF2_DAC_SEL_MASK, 
		(rt3261->playback_if1_data_control<<RT3261_IF1_DAC_SEL_SFT) | (rt3261->playback_if2_data_control<<RT3261_IF2_DAC_SEL_SFT));

	return 0;
}


static int rt3261_index_sync(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT3261_INIT_REG_LEN; i++)
		if (RT3261_PRIV_INDEX == init_list[i].reg ||
			RT3261_PRIV_DATA == init_list[i].reg)
			snd_soc_write(codec, init_list[i].reg,
					init_list[i].val);
	return 0;
}

static const u16 rt3261_reg[RT3261_VENDOR_ID2 + 1] = {
	[RT3261_RESET] = 0x000c,
	[RT3261_SPK_VOL] = 0xc8c8,
	[RT3261_HP_VOL] = 0xc8c8,
	[RT3261_OUTPUT] = 0xc8c8,
	[RT3261_MONO_OUT] = 0x8000,
	[RT3261_INL_INR_VOL] = 0x0808,
	[RT3261_DAC1_DIG_VOL] = 0xafaf,
	[RT3261_DAC2_DIG_VOL] = 0xafaf,
	[RT3261_ADC_DIG_VOL] = 0x2f2f,
	[RT3261_ADC_DATA] = 0x2f2f,
	[RT3261_STO_ADC_MIXER] = 0x7060,
	[RT3261_MONO_ADC_MIXER] = 0x7070,
	[RT3261_AD_DA_MIXER] = 0x8080,
	[RT3261_STO_DAC_MIXER] = 0x5454,
	[RT3261_MONO_DAC_MIXER] = 0x5454,
	[RT3261_DIG_MIXER] = 0xaa00,
	[RT3261_DSP_PATH2] = 0xa000,
	[RT3261_REC_L2_MIXER] = 0x007f,
	[RT3261_REC_R2_MIXER] = 0x007f,
	[RT3261_HPO_MIXER] = 0xe000,
	[RT3261_SPK_L_MIXER] = 0x003e,
	[RT3261_SPK_R_MIXER] = 0x003e,
	[RT3261_SPO_L_MIXER] = 0xf800,
	[RT3261_SPO_R_MIXER] = 0x3800,
	[RT3261_SPO_CLSD_RATIO] = 0x0004,
	[RT3261_MONO_MIXER] = 0xfc00,
	[RT3261_OUT_L3_MIXER] = 0x01ff,
 	[RT3261_OUT_R3_MIXER] = 0x01ff,
	[RT3261_LOUT_MIXER] = 0xf000,
	[RT3261_PWR_ANLG1] = 0x00c0,
	[RT3261_I2S1_SDP] = 0x8000,
	[RT3261_I2S2_SDP] = 0x8000,
	[RT3261_I2S3_SDP] = 0x8000,
	[RT3261_ADDA_CLK1] = 0x1110,
	[RT3261_ADDA_CLK2] = 0x0c00,
	[RT3261_DMIC] = 0x1d00,
	[RT3261_ASRC_3] = 0x0008,
	[RT3261_HP_OVCD] = 0x0600,
	[RT3261_CLS_D_OVCD] = 0x0228,
	[RT3261_CLS_D_OUT] = 0xa800,
	[RT3261_DEPOP_M1] = 0x0004,
	[RT3261_DEPOP_M2] = 0x1100,
	[RT3261_DEPOP_M3] = 0x0646,
	[RT3261_CHARGE_PUMP] = 0x0c00,
	[RT3261_MICBIAS] = 0x3000,
	[RT3261_EQ_CTRL1] = 0x2080,
	[RT3261_DRC_AGC_1] = 0x2206,
	[RT3261_DRC_AGC_2] = 0x1f00,
	[RT3261_ANC_CTRL1] = 0x034b,
	[RT3261_ANC_CTRL2] = 0x0066,
	[RT3261_ANC_CTRL3] = 0x000b,
	[RT3261_GPIO_CTRL1] = 0x0400,
	[RT3261_DSP_CTRL3] = 0x2000,
	[RT3261_BASE_BACK] = 0x0013,
	[RT3261_MP3_PLUS1] = 0x0680,
	[RT3261_MP3_PLUS2] = 0x1c17,
	[RT3261_3D_HP] = 0x8c00,
	[RT3261_ADJ_HPF] = 0x2a20,
	[RT3261_HP_CALIB_AMP_DET] = 0x0400,
	[RT3261_SV_ZCD1] = 0x0809,
	[RT3261_VENDOR_ID1] = 0x10ec,
	[RT3261_VENDOR_ID2] = 0x6231,
};

static int rt3261_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT3261_RESET, 0);
}

static unsigned int rt3261_read(struct snd_soc_codec *codec,
		unsigned int reg)
{
	unsigned int val;

	val = codec->hw_read(codec, reg);
	return val;
}

static int do_hw_write(struct snd_soc_codec *codec, unsigned int reg,
		unsigned int value, const void *data, int len)
{
	int ret;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
			reg < codec->driver->reg_cache_size &&
			!codec->cache_bypass) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	ret = i2c_master_normal_send(codec->control_data, data, len,400*1000);
	if (ret == len)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rt3261_write(struct snd_soc_codec *codec, unsigned int reg,
		unsigned int value)
{
	u8 data[3];

	data[0] = reg;
	data[1] = (value >> 8) & 0xff;
	data[2] = value & 0xff;

	DBG("rt3261_write 0x%x = 0x%x\n",reg,value);
	return do_hw_write(codec, reg, value, data, 3);
}

/**
 * rt3261_index_write - Write private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 * @value: Private register Data.
 *
 * Modify private register for advanced setting. It can be written through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_index_write(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int ret;

	ret = snd_soc_write(codec, RT3261_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_PRIV_DATA, value);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private value: %d\n", ret);
		goto err;
	}
	return 0;

err:
	return ret;
}

/**
 * rt3261_index_read - Read private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 *
 * Read advanced setting from private register. It can be read through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns private register value or negative error code.
 */
static unsigned int rt3261_index_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;

	ret = snd_soc_write(codec, RT3261_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		return ret;
	}
	return snd_soc_read(codec, RT3261_PRIV_DATA);
}

/**
 * rt3261_index_update_bits - update private register bits
 * @codec: audio codec
 * @reg: Private register index.
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
static int rt3261_index_update_bits(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int old, new;
	int change, ret;

	ret = rt3261_index_read(codec, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read private reg: %d\n", ret);
		goto err;
	}

	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		ret = rt3261_index_write(codec, reg, new);
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

static int rt3261_volatile_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT3261_RESET:
	case RT3261_PRIV_DATA:
	case RT3261_ASRC_5:
	case RT3261_EQ_CTRL1:
	case RT3261_DRC_AGC_1:
	case RT3261_ANC_CTRL1:
	case RT3261_IRQ_CTRL2:
	case RT3261_INT_IRQ_ST:
	case RT3261_DSP_CTRL2:
	case RT3261_DSP_CTRL3:
	case RT3261_PGM_REG_ARR1:
	case RT3261_PGM_REG_ARR3:
	case RT3261_VENDOR_ID:
	case RT3261_VENDOR_ID1:
	case RT3261_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

static int rt3261_readable_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT3261_RESET:
	case RT3261_SPK_VOL:
	case RT3261_HP_VOL:
	case RT3261_OUTPUT:
	case RT3261_MONO_OUT:
	case RT3261_IN1_IN2:
	case RT3261_IN3_IN4:
	case RT3261_INL_INR_VOL:
	case RT3261_DAC1_DIG_VOL:
	case RT3261_DAC2_DIG_VOL:
	case RT3261_DAC2_CTRL:
	case RT3261_ADC_DIG_VOL:
	case RT3261_ADC_DATA:
	case RT3261_ADC_BST_VOL:
	case RT3261_STO_ADC_MIXER:
	case RT3261_MONO_ADC_MIXER:
	case RT3261_AD_DA_MIXER:
	case RT3261_STO_DAC_MIXER:
	case RT3261_MONO_DAC_MIXER:
	case RT3261_DIG_MIXER:
	case RT3261_DSP_PATH1:
	case RT3261_DSP_PATH2:
	case RT3261_DIG_INF_DATA:
	case RT3261_REC_L1_MIXER:
	case RT3261_REC_L2_MIXER:
	case RT3261_REC_R1_MIXER:
	case RT3261_REC_R2_MIXER:
	case RT3261_HPO_MIXER:
	case RT3261_SPK_L_MIXER:
	case RT3261_SPK_R_MIXER:
	case RT3261_SPO_L_MIXER:
	case RT3261_SPO_R_MIXER:
	case RT3261_SPO_CLSD_RATIO:
	case RT3261_MONO_MIXER:
	case RT3261_OUT_L1_MIXER:
	case RT3261_OUT_L2_MIXER:
	case RT3261_OUT_L3_MIXER:
	case RT3261_OUT_R1_MIXER:
	case RT3261_OUT_R2_MIXER:
	case RT3261_OUT_R3_MIXER:
	case RT3261_LOUT_MIXER:
	case RT3261_PWR_DIG1:
	case RT3261_PWR_DIG2:
	case RT3261_PWR_ANLG1:
	case RT3261_PWR_ANLG2:
	case RT3261_PWR_MIXER:
	case RT3261_PWR_VOL:
	case RT3261_PRIV_INDEX:
	case RT3261_PRIV_DATA:
	case RT3261_I2S1_SDP:
	case RT3261_I2S2_SDP:
	case RT3261_I2S3_SDP:
	case RT3261_ADDA_CLK1:
	case RT3261_ADDA_CLK2:
	case RT3261_DMIC:
	case RT3261_GLB_CLK:
	case RT3261_PLL_CTRL1:
	case RT3261_PLL_CTRL2:
	case RT3261_ASRC_1:
	case RT3261_ASRC_2:
	case RT3261_ASRC_3:
	case RT3261_ASRC_4:
	case RT3261_ASRC_5:
	case RT3261_HP_OVCD:
	case RT3261_CLS_D_OVCD:
	case RT3261_CLS_D_OUT:
	case RT3261_DEPOP_M1:
	case RT3261_DEPOP_M2:
	case RT3261_DEPOP_M3:
	case RT3261_CHARGE_PUMP:
	case RT3261_PV_DET_SPK_G:
	case RT3261_MICBIAS:
	case RT3261_EQ_CTRL1:
	case RT3261_EQ_CTRL2:
	case RT3261_WIND_FILTER:
	case RT3261_DRC_AGC_1:
	case RT3261_DRC_AGC_2:
	case RT3261_DRC_AGC_3:
	case RT3261_SVOL_ZC:
	case RT3261_ANC_CTRL1:
	case RT3261_ANC_CTRL2:
	case RT3261_ANC_CTRL3:
	case RT3261_JD_CTRL:
	case RT3261_ANC_JD:
	case RT3261_IRQ_CTRL1:
	case RT3261_IRQ_CTRL2:
	case RT3261_INT_IRQ_ST:
	case RT3261_GPIO_CTRL1:
	case RT3261_GPIO_CTRL2:
	case RT3261_GPIO_CTRL3:
	case RT3261_DSP_CTRL1:
	case RT3261_DSP_CTRL2:
	case RT3261_DSP_CTRL3:
	case RT3261_DSP_CTRL4:
	case RT3261_PGM_REG_ARR1:
	case RT3261_PGM_REG_ARR2:
	case RT3261_PGM_REG_ARR3:
	case RT3261_PGM_REG_ARR4:
	case RT3261_PGM_REG_ARR5:
	case RT3261_SCB_FUNC:
	case RT3261_SCB_CTRL:
	case RT3261_BASE_BACK:
	case RT3261_MP3_PLUS1:
	case RT3261_MP3_PLUS2:
	case RT3261_3D_HP:
	case RT3261_ADJ_HPF:
	case RT3261_HP_CALIB_AMP_DET:
	case RT3261_HP_CALIB2:
	case RT3261_SV_ZCD1:
	case RT3261_SV_ZCD2:
	case RT3261_GEN_CTRL1:
	case RT3261_GEN_CTRL2:
	case RT3261_GEN_CTRL3:
	case RT3261_VENDOR_ID:
	case RT3261_VENDOR_ID1:
	case RT3261_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}

void codec_set_spk(bool on)
{

	struct snd_soc_codec *codec = rt3261_codec;
	DBG("%s: %d\n", __func__, on);

	if(!codec)
		return;

	mutex_lock(&codec->mutex);
	if(on){
		DBG("snd_soc_dapm_enable_pin\n");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_enable_pin(&codec->dapm, "Ext Spk");
	}else{
		DBG("snd_soc_dapm_disable_pin\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphone Jack");
		snd_soc_dapm_disable_pin(&codec->dapm, "Ext Spk");
	}
	snd_soc_dapm_sync(&codec->dapm);
	mutex_unlock(&codec->mutex);
}



/**
 * rt3261_headset_mic_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
int rt3261_headset_mic_detect(int jack_insert)
{
	int jack_type;
#ifdef USE_INT_CLK
	int sclk_src;
#endif

	if(jack_insert) {
		if (SND_SOC_BIAS_OFF == rt3261_codec->dapm.bias_level) {
			snd_soc_write(rt3261_codec, RT3261_PWR_ANLG1, 0x2004);
			snd_soc_write(rt3261_codec, RT3261_MICBIAS, 0x3830);
			snd_soc_write(rt3261_codec, RT3261_GEN_CTRL1 , 0x3701);
		}
#ifdef USE_INT_CLK
		sclk_src = snd_soc_read(rt3261_codec, RT3261_GLB_CLK) &
			RT3261_SCLK_SRC_MASK;
		snd_soc_update_bits(rt3261_codec, RT3261_GLB_CLK,
			RT3261_SCLK_SRC_MASK, 0x3 << RT3261_SCLK_SRC_SFT);
#endif
		snd_soc_update_bits(rt3261_codec, RT3261_PWR_ANLG1,
			RT3261_PWR_LDO2, RT3261_PWR_LDO2);
		snd_soc_update_bits(rt3261_codec, RT3261_PWR_ANLG2,
			RT3261_PWR_MB1, RT3261_PWR_MB1);
		mdelay(400);
		snd_soc_update_bits(rt3261_codec, RT3261_MICBIAS,
			RT3261_MIC1_OVCD_MASK | RT3261_MIC1_OVTH_MASK |
			RT3261_PWR_CLK25M_MASK | RT3261_PWR_MB_MASK,
			RT3261_MIC1_OVCD_EN | RT3261_MIC1_OVTH_600UA |
			RT3261_PWR_MB_PU | RT3261_PWR_CLK25M_PU);
		snd_soc_update_bits(rt3261_codec, RT3261_GEN_CTRL1,
			0x1, 0x1);
		msleep(100);
		if (snd_soc_read(rt3261_codec, RT3261_IRQ_CTRL2) & 0x8)
			jack_type = RT3261_HEADPHO_DET;
		else
			jack_type = RT3261_HEADSET_DET;
		snd_soc_update_bits(rt3261_codec, RT3261_IRQ_CTRL2,
			RT3261_MB1_OC_CLR, 0);
#ifdef USE_INT_CLK
		snd_soc_update_bits(rt3261_codec, RT3261_GLB_CLK,
			RT3261_SCLK_SRC_MASK, sclk_src);
#endif
	} else {
		snd_soc_update_bits(rt3261_codec, RT3261_MICBIAS,
			RT3261_MIC1_OVCD_MASK,
			RT3261_MIC1_OVCD_DIS);
		
		jack_type = RT3261_NO_JACK;
	}

	return jack_type;
}
EXPORT_SYMBOL(rt3261_headset_mic_detect);

static const char *rt3261_dacr2_src[] = { "TxDC_R", "TxDP_R" };

static const SOC_ENUM_SINGLE_DECL(rt3261_dacr2_enum,RT3261_DUMMY_PR3F,
	14, rt3261_dacr2_src);
static const struct snd_kcontrol_new rt3261_dacr2_mux =
	SOC_DAPM_ENUM("Mono dacr source", rt3261_dacr2_enum);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static unsigned int bst_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
};

static int rt3261_dmic_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt3261->dmic_en;

	return 0;
}

static int rt3261_dmic_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	if (rt3261->dmic_en == ucontrol->value.integer.value[0])
		return 0;

	rt3261->dmic_en = ucontrol->value.integer.value[0];
	switch (rt3261->dmic_en) {
	case RT3261_DMIC_DIS:
		snd_soc_update_bits(codec, RT3261_GPIO_CTRL1,
			RT3261_GP2_PIN_MASK | RT3261_GP3_PIN_MASK |
			RT3261_GP4_PIN_MASK,
			RT3261_GP2_PIN_GPIO2 | RT3261_GP3_PIN_GPIO3 |
			RT3261_GP4_PIN_GPIO4);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_1_DP_MASK | RT3261_DMIC_2_DP_MASK,
			RT3261_DMIC_1_DP_GPIO3 | RT3261_DMIC_2_DP_GPIO4);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_1_EN_MASK | RT3261_DMIC_2_EN_MASK,
			RT3261_DMIC_1_DIS | RT3261_DMIC_2_DIS);
		break;

	case RT3261_DMIC1:
		snd_soc_update_bits(codec, RT3261_GPIO_CTRL1,
			RT3261_GP2_PIN_MASK | RT3261_GP3_PIN_MASK,
			RT3261_GP2_PIN_DMIC1_SCL | RT3261_GP3_PIN_DMIC1_SDA);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_1L_LH_MASK | RT3261_DMIC_1R_LH_MASK |
			RT3261_DMIC_1_DP_MASK,
			RT3261_DMIC_1L_LH_FALLING | RT3261_DMIC_1R_LH_RISING |
			RT3261_DMIC_1_DP_IN1P);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_1_EN_MASK, RT3261_DMIC_1_EN);
		break;

	case RT3261_DMIC2:
		snd_soc_update_bits(codec, RT3261_GPIO_CTRL1,
			RT3261_GP2_PIN_MASK | RT3261_GP4_PIN_MASK,
			RT3261_GP2_PIN_DMIC1_SCL | RT3261_GP4_PIN_DMIC2_SDA);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_2L_LH_MASK | RT3261_DMIC_2R_LH_MASK |
			RT3261_DMIC_2_DP_MASK,
			RT3261_DMIC_2L_LH_FALLING | RT3261_DMIC_2R_LH_RISING |
			RT3261_DMIC_2_DP_IN1N);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_2_EN_MASK, RT3261_DMIC_2_EN);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

//bard 8-9 s
#if 0
static int rt3261_mic1_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = 
		(snd_soc_read(codec, RT3261_REC_L2_MIXER) & RT3261_M_BST1_RM_L) >> RT3261_M_BST1_RM_L_SFT;

	return 0;
}

static int rt3261_mic1_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0]) {
		snd_soc_update_bits(codec, RT3261_REC_L2_MIXER,
			RT3261_M_BST1_RM_L, 0);
		snd_soc_update_bits(codec, RT3261_REC_R2_MIXER,
			RT3261_M_BST1_RM_R, 0);
	}else {
		snd_soc_update_bits(codec, RT3261_REC_L2_MIXER,
			RT3261_M_BST1_RM_L, RT3261_M_BST1_RM_L);
		snd_soc_update_bits(codec, RT3261_REC_R2_MIXER,
			RT3261_M_BST1_RM_R, RT3261_M_BST1_RM_R);
	}

	return 0;
}

static int rt3261_mic2_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = 
		(snd_soc_read(codec, RT3261_REC_L2_MIXER) & RT3261_M_BST1_RM_L) >> RT3261_M_BST1_RM_L_SFT;

	return 0;
}

static int rt3261_mic2_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0]) {
		snd_soc_update_bits(codec, RT3261_REC_L2_MIXER,
			RT3261_M_BST4_RM_L, 0);
		snd_soc_update_bits(codec, RT3261_REC_R2_MIXER,
			RT3261_M_BST4_RM_R, 0);
	}else {
		snd_soc_update_bits(codec, RT3261_REC_L2_MIXER,
			RT3261_M_BST4_RM_L, RT3261_M_BST4_RM_L);
		snd_soc_update_bits(codec, RT3261_REC_R2_MIXER,
			RT3261_M_BST4_RM_R, RT3261_M_BST4_RM_R);
	}

	return 0;
}
#endif
//bard 8-9 e

void hp_amp_power(struct snd_soc_codec *codec, int on)
{
	static int hp_amp_power_count;
	printk("hp_amp_power on=%d hp_amp_power_count=%d\n",on,hp_amp_power_count);
//	dump_reg(codec);
	if(on) {
		if(hp_amp_power_count <= 0) {
			snd_soc_update_bits(codec, RT3261_PWR_DIG1,
				RT3261_PWR_I2S1, RT3261_PWR_I2S1);
			/* depop parameters */
			snd_soc_update_bits(codec, RT3261_DEPOP_M2,
				RT3261_DEPOP_MASK, RT3261_DEPOP_MAN);
			snd_soc_update_bits(codec, RT3261_DEPOP_M1,
				RT3261_HP_CP_MASK | RT3261_HP_SG_MASK | RT3261_HP_CB_MASK,
				RT3261_HP_CP_PU | RT3261_HP_SG_DIS | RT3261_HP_CB_PU);
			rt3261_index_write(codec, RT3261_HP_DCC_INT1, 0x9f00);
			/* headphone amp power on */
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_FV1 | RT3261_PWR_FV2 , 0);
			snd_soc_update_bits(codec, RT3261_PWR_VOL,
				RT3261_PWR_HV_L | RT3261_PWR_HV_R,
				RT3261_PWR_HV_L | RT3261_PWR_HV_R);
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA , //bard 10-18
				RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA); //bard 10-18
			msleep(50);
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_FV1 | RT3261_PWR_FV2,
				RT3261_PWR_FV1 | RT3261_PWR_FV2);
				
			snd_soc_update_bits(codec, RT3261_CHARGE_PUMP,
				RT3261_PM_HP_MASK, RT3261_PM_HP_HV);
			rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0200);
			snd_soc_update_bits(codec, RT3261_DEPOP_M1,
				RT3261_HP_CO_MASK | RT3261_HP_SG_MASK,
				RT3261_HP_CO_EN | RT3261_HP_SG_EN);
		}
		hp_amp_power_count++;
	} else {
		hp_amp_power_count--;
		if(hp_amp_power_count <= 0) {
			//rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0);
			snd_soc_update_bits(codec, RT3261_DEPOP_M1,
				RT3261_HP_SG_MASK | RT3261_HP_L_SMT_MASK |
				RT3261_HP_R_SMT_MASK, RT3261_HP_SG_DIS |
				RT3261_HP_L_SMT_DIS | RT3261_HP_R_SMT_DIS);
			/* headphone amp power down */
			snd_soc_update_bits(codec, RT3261_DEPOP_M1,
				RT3261_SMT_TRIG_MASK | RT3261_HP_CD_PD_MASK |
				RT3261_HP_CO_MASK | RT3261_HP_CP_MASK |
				RT3261_HP_SG_MASK | RT3261_HP_CB_MASK,
				RT3261_SMT_TRIG_DIS | RT3261_HP_CD_PD_EN |
				RT3261_HP_CO_DIS | RT3261_HP_CP_PD |
				RT3261_HP_SG_EN | RT3261_HP_CB_PD);
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA , //bard 10-18
				0);
		}
	}
}

static int rt3261_hp_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	 
	ucontrol->value.integer.value[0] = 
		!((snd_soc_read(codec, RT3261_HP_VOL) & RT3261_L_MUTE) >> RT3261_L_MUTE_SFT);

	return 0;
}

static int rt3261_hp_mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0]) {
		/* headphone unmute sequence */
		snd_soc_update_bits(codec, RT3261_DEPOP_M3,
			RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
			(RT3261_CP_FQ_192_KHZ << RT3261_CP_FQ1_SFT) |
			(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
			(RT3261_CP_FQ_192_KHZ << RT3261_CP_FQ3_SFT));
		rt3261_index_write(codec, RT3261_MAMP_INT_REG2, 0xfc00);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_SMT_TRIG_MASK, RT3261_SMT_TRIG_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTN_MASK, RT3261_RSTN_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTN_MASK | RT3261_HP_L_SMT_MASK | RT3261_HP_R_SMT_MASK,
			RT3261_RSTN_DIS | RT3261_HP_L_SMT_EN | RT3261_HP_R_SMT_EN);
		snd_soc_update_bits(codec, RT3261_HP_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE, 0);
		msleep(100);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_HP_SG_MASK | RT3261_HP_L_SMT_MASK |
			RT3261_HP_R_SMT_MASK, RT3261_HP_SG_DIS |
			RT3261_HP_L_SMT_DIS | RT3261_HP_R_SMT_DIS);
		/*bard 10-18 r
		msleep(20);	
		snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
			RT3261_HPD_PS_MASK, RT3261_HPD_PS_EN);
		*/
	}else {
		/* headphone mute sequence */
		snd_soc_update_bits(codec, RT3261_DEPOP_M3,
			RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
			(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ1_SFT) |
			(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
			(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ3_SFT));
		rt3261_index_write(codec, RT3261_MAMP_INT_REG2, 0xfc00);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_HP_SG_MASK, RT3261_HP_SG_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTP_MASK, RT3261_RSTP_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTP_MASK | RT3261_HP_L_SMT_MASK |
			RT3261_HP_R_SMT_MASK, RT3261_RSTP_DIS |
			RT3261_HP_L_SMT_EN | RT3261_HP_R_SMT_EN);
		/*bard 10-18 r
		snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
			RT3261_HPD_PS_MASK, RT3261_HPD_PS_DIS);
		msleep(90);
		*/
		snd_soc_update_bits(codec, RT3261_HP_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE, RT3261_L_MUTE | RT3261_R_MUTE);
		msleep(30);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_HP_R_SMT_MASK | RT3261_HP_L_SMT_MASK,
			RT3261_HP_L_SMT_DIS | RT3261_HP_R_SMT_DIS);
		} 
	return 0;
}

#if defined (CONFIG_SND_SOC_RT5623)
static int rt3261_modem_input_switch_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt3261->modem_is_open;
	return 0;
}

static int rt3261_modem_input_switch_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	if(ucontrol->value.integer.value[0]) {
		rt5623_on( );
		rt3261->modem_is_open = 1;	
	}else {
		rt5623_off( );
		rt3261->modem_is_open = 0;
	} 

	return 0;
}
#else
static int rt3261_modem_input_switch_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int rt3261_modem_input_switch_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}
#endif

static int rt3261_dacr_sel_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = (rt3261_index_read(codec, RT3261_MIXER_INT_REG) & 0x4000) >> 14;

	return 0;
}

static int rt3261_dacr_sel_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0])
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x4000, 0x4000);
	else
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x4000, 0x0);
	

	return 0;
}

static int rt3261_rxdp_sel_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = (rt3261_index_read(codec, RT3261_MIXER_INT_REG) & 0x0400) >> 10;

	return 0;
}

static int rt3261_rxdp_sel_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0])
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x0400, 0x0400);
	else
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x0400, 0x0);
	

	return 0;
}

static int rt3261_rxdp1_sel_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = (rt3261_index_read(codec, RT3261_MIXER_INT_REG) & 0x0200) >> 9;

	return 0;
}

static int rt3261_rxdp1_sel_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if(ucontrol->value.integer.value[0])
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x0200, 0x0200);
	else
		rt3261_index_update_bits(codec, RT3261_MIXER_INT_REG, 0x0200, 0x0);
	

	return 0;
}

/* IN1/IN2 Input Type */
static const char *rt3261_input_mode[] = {
	"Single ended", "Differential"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_in1_mode_enum, RT3261_IN1_IN2,
	RT3261_IN_SFT1, rt3261_input_mode);

static const SOC_ENUM_SINGLE_DECL(
	rt3261_in2_mode_enum, RT3261_IN3_IN4,
	RT3261_IN_SFT2, rt3261_input_mode);

static const SOC_ENUM_SINGLE_DECL(
	rt3261_in3_mode_enum, RT3261_IN1_IN2,
	RT3261_IN_SFT2, rt3261_input_mode);

//output type
static const char *rt3261_output_mode[] = {
	"Single ended", "Differential"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_lout_mode_enum, RT3261_GEN_CTRL1,
	RT3261_LOUT_DF, rt3261_output_mode);


/* Interface data select */
static const char *rt3261_data_select[] = {
	"Normal", "Swap", "left copy to right", "right copy to left"};

static const SOC_ENUM_SINGLE_DECL(rt3261_if1_dac_enum, RT3261_DIG_INF_DATA,
				RT3261_IF1_DAC_SEL_SFT, rt3261_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_if1_adc_enum, RT3261_DIG_INF_DATA,
				RT3261_IF1_ADC_SEL_SFT, rt3261_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_if2_dac_enum, RT3261_DIG_INF_DATA,
				RT3261_IF2_DAC_SEL_SFT, rt3261_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_if2_adc_enum, RT3261_DIG_INF_DATA,
				RT3261_IF2_ADC_SEL_SFT, rt3261_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_if3_dac_enum, RT3261_DIG_INF_DATA,
				RT3261_IF3_DAC_SEL_SFT, rt3261_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_if3_adc_enum, RT3261_DIG_INF_DATA,
				RT3261_IF3_ADC_SEL_SFT, rt3261_data_select);

/* Class D speaker gain ratio */
static const char *rt3261_clsd_spk_ratio[] = {"1.66x", "1.83x", "1.94x", "2x",
	"2.11x", "2.22x", "2.33x", "2.44x", "2.55x", "2.66x", "2.77x"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_clsd_spk_ratio_enum, RT3261_CLS_D_OUT,
	RT3261_CLSD_RATIO_SFT, rt3261_clsd_spk_ratio);

/* DMIC */
static const char *rt3261_dmic_mode[] = {"Disable", "DMIC1", "DMIC2"};

static const SOC_ENUM_SINGLE_DECL(rt3261_dmic_enum, 0, 0, rt3261_dmic_mode);

/* PR-3F */
static const char *rt3261_dacr_sel_mode[] = {"IF2_DAC", "IF2_ADC"};

static const SOC_ENUM_SINGLE_DECL(rt3261_dacr_sel_enum, 0, 0, rt3261_dacr_sel_mode);

static const char *rt3261_rxdp_sel_mode[] = {"RxDP2", "RxDP1"};

static const SOC_ENUM_SINGLE_DECL(rt3261_rxdp_sel_enum, 0, 0, rt3261_rxdp_sel_mode);

static const char *rt3261_rxdp1_sel_mode[] = {"DAC1", "IF1_DAC"};

static const SOC_ENUM_SINGLE_DECL(rt3261_rxdp1_sel_enum, 0, 0, rt3261_rxdp1_sel_mode);


//bard 8-9 s
#if 0
static const char *rt3261_mic_mode[] = {"off", "on",};

static const SOC_ENUM_SINGLE_DECL(rt3261_mic_enum, 0, 0, rt3261_mic_mode);
#endif
//bard 8-9 e

static const char *rt3261_hp_mute_mode[] = {"off", "on",};

static const SOC_ENUM_SINGLE_DECL(rt3261_hp_mute_enum, 0, 0, rt3261_hp_mute_mode);

static const char *rt3261_modem_input_switch_mode[] = {"off", "on",};

static const SOC_ENUM_SINGLE_DECL(rt3261_modem_input_switch_enum, 0, 0, rt3261_modem_input_switch_mode);

#ifdef RT3261_REG_RW
#define REGVAL_MAX 0xffff
static unsigned int regctl_addr;
static int rt3261_regctl_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = REGVAL_MAX;
	return 0;
}

static int rt3261_regctl_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = regctl_addr;
	ucontrol->value.integer.value[1] = snd_soc_read(codec, regctl_addr);
	return 0;
}

static int rt3261_regctl_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	regctl_addr = ucontrol->value.integer.value[0];
	if(ucontrol->value.integer.value[1] <= REGVAL_MAX)
		snd_soc_write(codec, regctl_addr, ucontrol->value.integer.value[1]);
	return 0;
}
#endif


static int rt3261_vol_rescale_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int val = snd_soc_read(codec, mc->reg);

	ucontrol->value.integer.value[0] = RT3261_VOL_RSCL_MAX -
		((val & RT3261_L_VOL_MASK) >> mc->shift);
	ucontrol->value.integer.value[1] = RT3261_VOL_RSCL_MAX -
		(val & RT3261_R_VOL_MASK);

	return 0;
}

static int rt3261_vol_rescale_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int val, val2;

	val = RT3261_VOL_RSCL_MAX - ucontrol->value.integer.value[0];
	val2 = RT3261_VOL_RSCL_MAX - ucontrol->value.integer.value[1];
	return snd_soc_update_bits_locked(codec, mc->reg, RT3261_L_VOL_MASK |
			RT3261_R_VOL_MASK, val << mc->shift | val2);
}


static const struct snd_kcontrol_new rt3261_snd_controls[] = {
	/* Speaker Output Volume */
	SOC_DOUBLE("Speaker Playback Switch", RT3261_SPK_VOL,
		RT3261_L_MUTE_SFT, RT3261_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_EXT_TLV("Speaker Playback Volume", RT3261_SPK_VOL,
		RT3261_L_VOL_SFT, RT3261_R_VOL_SFT, RT3261_VOL_RSCL_RANGE, 0,
		rt3261_vol_rescale_get, rt3261_vol_rescale_put, out_vol_tlv),
	SOC_DOUBLE_EXT_TLV("Earpiece Playback Volume", RT3261_SPK_VOL,
		RT3261_L_VOL_SFT, RT3261_R_VOL_SFT, RT3261_VOL_RSCL_RANGE, 0,
		rt3261_vol_rescale_get, rt3261_vol_rescale_put, out_vol_tlv),
	/* Headphone Output Volume */
	SOC_DOUBLE("HP Playback Switch", RT3261_HP_VOL,
		RT3261_L_MUTE_SFT, RT3261_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_EXT_TLV("Headphone Playback Volume", RT3261_HP_VOL,
		RT3261_L_VOL_SFT, RT3261_R_VOL_SFT, RT3261_VOL_RSCL_RANGE, 0,
		rt3261_vol_rescale_get, rt3261_vol_rescale_put, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE("OUT Playback Switch", RT3261_OUTPUT,
		RT3261_L_MUTE_SFT, RT3261_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("OUT Channel Switch", RT3261_OUTPUT,
		RT3261_VOL_L_SFT, RT3261_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT3261_OUTPUT,
		RT3261_L_VOL_SFT, RT3261_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* MONO Output Control */
	SOC_SINGLE("Mono Playback Switch", RT3261_MONO_OUT,
				RT3261_L_MUTE_SFT, 1, 1),
	/* DAC Digital Volume */
	SOC_DOUBLE("DAC2 Playback Switch", RT3261_DAC2_CTRL,
		RT3261_M_DAC_L2_VOL_SFT, RT3261_M_DAC_R2_VOL_SFT, 1, 1),
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT3261_DAC1_DIG_VOL,
			RT3261_L_VOL_SFT, RT3261_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("Mono DAC Playback Volume", RT3261_DAC2_DIG_VOL,
			RT3261_L_VOL_SFT, RT3261_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	/* IN1/IN2 Control */
	SOC_ENUM("IN1 Mode Control",  rt3261_in1_mode_enum),
	SOC_SINGLE_TLV("IN1 Boost", RT3261_IN1_IN2,
		RT3261_BST_SFT1, 8, 0, bst_tlv),
	SOC_ENUM("IN2 Mode Control", rt3261_in2_mode_enum),
	SOC_SINGLE_TLV("IN2 Boost", RT3261_IN3_IN4,
		RT3261_BST_SFT2, 8, 0, bst_tlv),
	SOC_ENUM("IN3 Mode Control",  rt3261_in3_mode_enum),
	SOC_SINGLE_TLV("IN3 Boost", RT3261_IN1_IN2,
		RT3261_BST_SFT2, 8, 0, bst_tlv),

	SOC_ENUM("LOUT Mode Control",  rt3261_lout_mode_enum),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT3261_INL_INR_VOL,
			RT3261_INL_VOL_SFT, RT3261_INR_VOL_SFT,
			31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT3261_ADC_DIG_VOL,
		RT3261_L_MUTE_SFT, RT3261_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT3261_ADC_DIG_VOL,
			RT3261_L_VOL_SFT, RT3261_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT3261_ADC_DATA,
			RT3261_L_VOL_SFT, RT3261_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("ADC Boost Gain", RT3261_ADC_BST_VOL,
			RT3261_ADC_L_BST_SFT, RT3261_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),
	/* Class D speaker gain ratio */
	SOC_ENUM("Class D SPK Ratio Control", rt3261_clsd_spk_ratio_enum),
	/* DMIC */
	SOC_ENUM_EXT("DMIC Switch", rt3261_dmic_enum,
		rt3261_dmic_get, rt3261_dmic_put),

	/* PR-3F */
	SOC_ENUM_EXT("DACR Select", rt3261_dacr_sel_enum,
		rt3261_dacr_sel_get, rt3261_dacr_sel_put),
	SOC_ENUM_EXT("RxDP Select", rt3261_rxdp_sel_enum,
		rt3261_rxdp_sel_get, rt3261_rxdp_sel_put),
	SOC_ENUM_EXT("RxDP1 Select", rt3261_rxdp1_sel_enum,
		rt3261_rxdp1_sel_get, rt3261_rxdp1_sel_put),
#ifdef RT3261_REG_RW
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Register Control",
		.info = rt3261_regctl_info,
		.get = rt3261_regctl_get,
		.put = rt3261_regctl_put,
	},
#endif
//bard 8-9 s
#if 0
	SOC_SINGLE_TLV("Main Mic Capture Volume", RT3261_IN1_IN2,
		RT3261_BST_SFT1,  8, 0, bst_tlv), 
	SOC_SINGLE_TLV("Headset Mic Capture Volume", RT3261_IN3_IN4,
		RT3261_BST_SFT2, 8, 0, bst_tlv),
	SOC_ENUM_EXT("Main Mic Capture Switch", rt3261_mic_enum,
		rt3261_mic1_get, rt3261_mic1_put),
	SOC_ENUM_EXT("Headset Mic Capture Switch", rt3261_mic_enum,
		rt3261_mic2_get, rt3261_mic2_put),
#endif
//bard 8-9 e

	SOC_ENUM_EXT("HP mute Switch", rt3261_hp_mute_enum,
		rt3261_hp_mute_get, rt3261_hp_mute_put),

	SOC_ENUM_EXT("Modem Input Switch", rt3261_modem_input_switch_enum,
		rt3261_modem_input_switch_get, rt3261_modem_input_switch_put),

	SOC_ENUM("ADC IF1 Data Switch", rt3261_if1_adc_enum), 
	SOC_ENUM("DAC IF1 Data Switch", rt3261_if1_dac_enum), 
	SOC_ENUM("ADC IF2 Data Switch", rt3261_if2_adc_enum), 
	SOC_ENUM("DAC IF2 Data Switch", rt3261_if2_dac_enum), 
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
	struct snd_soc_codec *codec = w->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	int div[] = {2, 3, 4, 6, 12}, idx = -EINVAL, i, rate, red, bound, temp;

	rate = rt3261->lrck[rt3261->aif_pu] << 8;
	red = 3000000 * 12;
	for (i = 0; i < ARRAY_SIZE(div); i++) {
		bound = div[i] * 3000000;
		if (rate > bound)
			continue;
		temp = bound - rate;
		if (temp < red) {
			red = temp;
			idx = i;
		}
	}
	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_update_bits(codec, RT3261_DMIC, RT3261_DMIC_CLK_MASK,
					idx << RT3261_DMIC_CLK_SFT);
	return idx;
}

static int check_sysclk1_source(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int val;

	val = snd_soc_read(source->codec, RT3261_GLB_CLK);
	val &= RT3261_SCLK_SRC_MASK;
	if (val == RT3261_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt3261_sto_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT3261_STO_ADC_MIXER,
			RT3261_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT3261_STO_ADC_MIXER,
			RT3261_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_sto_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT3261_STO_ADC_MIXER,
			RT3261_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT3261_STO_ADC_MIXER,
			RT3261_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT3261_MONO_ADC_MIXER,
			RT3261_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT3261_MONO_ADC_MIXER,
			RT3261_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT3261_MONO_ADC_MIXER,
			RT3261_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT3261_MONO_ADC_MIXER,
			RT3261_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT3261_AD_DA_MIXER,
			RT3261_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT3261_AD_DA_MIXER,
			RT3261_M_IF1_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT3261_AD_DA_MIXER,
			RT3261_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT3261_AD_DA_MIXER,
			RT3261_M_IF1_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_DAC_L2_SFT, 1, 1),
	SOC_DAPM_SINGLE("ANC Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_ANC_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_DAC_R2_SFT, 1, 1),
	SOC_DAPM_SINGLE("ANC Switch", RT3261_STO_DAC_MIXER,
			RT3261_M_ANC_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_R2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_MONO_DAC_MIXER,
			RT3261_M_DAC_L2_MONO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_dig_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_DIG_MIXER,
			RT3261_M_STO_L_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_DIG_MIXER,
			RT3261_M_DAC_L2_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_dig_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_DIG_MIXER,
			RT3261_M_STO_R_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_DIG_MIXER,
			RT3261_M_DAC_R2_DAC_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt3261_rec_l_mix[] = {
	SOC_DAPM_SINGLE("HPOL Switch", RT3261_REC_L2_MIXER,
			RT3261_M_HP_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT3261_REC_L2_MIXER,
			RT3261_M_IN_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT3261_REC_L2_MIXER,
			RT3261_M_BST2_RM_L, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT3261_REC_L2_MIXER,
			RT3261_M_BST4_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_REC_L2_MIXER,
			RT3261_M_BST1_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT3261_REC_L2_MIXER,
			RT3261_M_OM_L_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_rec_r_mix[] = {
	SOC_DAPM_SINGLE("HPOR Switch", RT3261_REC_R2_MIXER,
			RT3261_M_HP_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT3261_REC_R2_MIXER,
			RT3261_M_IN_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT3261_REC_R2_MIXER,
			RT3261_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT3261_REC_R2_MIXER,
			RT3261_M_BST4_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_REC_R2_MIXER,
			RT3261_M_BST1_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT3261_REC_R2_MIXER,
			RT3261_M_OM_R_RM_R_SFT, 1, 1),
};

/* Analog Output Mixer */
static const struct snd_kcontrol_new rt3261_spk_l_mix[] = {
	SOC_DAPM_SINGLE("REC MIXL Switch", RT3261_SPK_L_MIXER,
			RT3261_M_RM_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT3261_SPK_L_MIXER,
			RT3261_M_IN_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_SPK_L_MIXER,
			RT3261_M_DAC_L1_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_SPK_L_MIXER,
			RT3261_M_DAC_L2_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT3261_SPK_L_MIXER,
			RT3261_M_OM_L_SM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_spk_r_mix[] = {
	SOC_DAPM_SINGLE("REC MIXR Switch", RT3261_SPK_R_MIXER,
			RT3261_M_RM_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT3261_SPK_R_MIXER,
			RT3261_M_IN_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_SPK_R_MIXER,
			RT3261_M_DAC_R1_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_SPK_R_MIXER,
			RT3261_M_DAC_R2_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT3261_SPK_R_MIXER,
			RT3261_M_OM_R_SM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_out_l_mix[] = {
	SOC_DAPM_SINGLE("SPK MIXL Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_SM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXL Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_DAC_R2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_OUT_L3_MIXER,
			RT3261_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_out_r_mix[] = {
	SOC_DAPM_SINGLE("SPK MIXR Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_SM_L_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_BST4_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXR Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_DAC_L2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_OUT_R3_MIXER,
			RT3261_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_spo_l_mix[] = {
#if 0 //org
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_SPO_L_MIXER,
			RT3261_M_DAC_R1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_SPO_L_MIXER,
			RT3261_M_DAC_L1_SPM_L_SFT, 1, 1),
#else //bard 8-27
	SOC_DAPM_SINGLE("DAC Switch", RT3261_DUMMY_SPKMIXER,
			RT3261_M_DAC_R1_SPM_L_SFT, 1, 1),
#endif
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT3261_SPO_L_MIXER,
			RT3261_M_SV_R_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL L Switch", RT3261_SPO_L_MIXER,
			RT3261_M_SV_L_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_SPO_L_MIXER,
			RT3261_M_BST1_SPM_L_SFT, 1, 1),
};
//bard 8-27 s
static const struct snd_kcontrol_new rt3261_spo_dac_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_SPO_L_MIXER,
			RT3261_M_DAC_R1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_SPO_L_MIXER,
			RT3261_M_DAC_L1_SPM_L_SFT, 1, 1),

};
//bard 8-27 e
static const struct snd_kcontrol_new rt3261_spo_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_SPO_R_MIXER,
			RT3261_M_DAC_R1_SPM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT3261_SPO_R_MIXER,
			RT3261_M_SV_R_SPM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_SPO_R_MIXER,
			RT3261_M_BST1_SPM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_hpo_mix[] = {
	SOC_DAPM_SINGLE("DAC2 Switch", RT3261_HPO_MIXER,
			RT3261_M_DAC2_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT3261_HPO_MIXER,
			RT3261_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPVOL Switch", RT3261_HPO_MIXER,
			RT3261_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT3261_LOUT_MIXER,
			RT3261_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT3261_LOUT_MIXER,
			RT3261_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT3261_LOUT_MIXER,
			RT3261_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT3261_LOUT_MIXER,
			RT3261_M_OV_R_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt3261_mono_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT3261_MONO_MIXER,
			RT3261_M_DAC_R2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT3261_MONO_MIXER,
			RT3261_M_DAC_L2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT3261_MONO_MIXER,
			RT3261_M_OV_R_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT3261_MONO_MIXER,
			RT3261_M_OV_L_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT3261_MONO_MIXER,
			RT3261_M_BST1_MM_SFT, 1, 1),
};

/* INL/R source */
static const char *rt3261_inl_src[] = {"IN2P", "MonoP"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_inl_enum, RT3261_INL_INR_VOL,
	RT3261_INL_SEL_SFT, rt3261_inl_src);

static const struct snd_kcontrol_new rt3261_inl_mux =
	SOC_DAPM_ENUM("INL source", rt3261_inl_enum);

static const char *rt3261_inr_src[] = {"IN2N", "MonoN"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_inr_enum, RT3261_INL_INR_VOL,
	RT3261_INR_SEL_SFT, rt3261_inr_src);

static const struct snd_kcontrol_new rt3261_inr_mux =
	SOC_DAPM_ENUM("INR source", rt3261_inr_enum);

/* Stereo ADC source */
static const char *rt3261_stereo_adc1_src[] = {"DIG MIX", "ADC"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_stereo_adc1_enum, RT3261_STO_ADC_MIXER,
	RT3261_ADC_1_SRC_SFT, rt3261_stereo_adc1_src);

static const struct snd_kcontrol_new rt3261_sto_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo ADC L1 source", rt3261_stereo_adc1_enum);

static const struct snd_kcontrol_new rt3261_sto_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo ADC R1 source", rt3261_stereo_adc1_enum);

static const char *rt3261_stereo_adc2_src[] = {"DMIC1", "DMIC2", "DIG MIX"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_stereo_adc2_enum, RT3261_STO_ADC_MIXER,
	RT3261_ADC_2_SRC_SFT, rt3261_stereo_adc2_src);

static const struct snd_kcontrol_new rt3261_sto_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo ADC L2 source", rt3261_stereo_adc2_enum);

static const struct snd_kcontrol_new rt3261_sto_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo ADC R2 source", rt3261_stereo_adc2_enum);

/* Mono ADC source */
static const char *rt3261_mono_adc_l1_src[] = {"Mono DAC MIXL", "ADCL"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_mono_adc_l1_enum, RT3261_MONO_ADC_MIXER,
	RT3261_MONO_ADC_L1_SRC_SFT, rt3261_mono_adc_l1_src);

static const struct snd_kcontrol_new rt3261_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC1 left source", rt3261_mono_adc_l1_enum);

static const char *rt3261_mono_adc_l2_src[] =
	{"DMIC L1", "DMIC L2", "Mono DAC MIXL"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_mono_adc_l2_enum, RT3261_MONO_ADC_MIXER,
	RT3261_MONO_ADC_L2_SRC_SFT, rt3261_mono_adc_l2_src);

static const struct snd_kcontrol_new rt3261_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC2 left source", rt3261_mono_adc_l2_enum);

static const char *rt3261_mono_adc_r1_src[] = {"Mono DAC MIXR", "ADCR"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_mono_adc_r1_enum, RT3261_MONO_ADC_MIXER,
	RT3261_MONO_ADC_R1_SRC_SFT, rt3261_mono_adc_r1_src);

static const struct snd_kcontrol_new rt3261_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC1 right source", rt3261_mono_adc_r1_enum);

static const char *rt3261_mono_adc_r2_src[] =
	{"DMIC R1", "DMIC R2", "Mono DAC MIXR"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_mono_adc_r2_enum, RT3261_MONO_ADC_MIXER,
	RT3261_MONO_ADC_R2_SRC_SFT, rt3261_mono_adc_r2_src);

static const struct snd_kcontrol_new rt3261_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC2 right source", rt3261_mono_adc_r2_enum);

/* DAC2 channel source */
static const char *rt3261_dac_l2_src[] = {"IF2", "IF3", "TxDC", "Base L/R"};

static const SOC_ENUM_SINGLE_DECL(rt3261_dac_l2_enum, RT3261_DSP_PATH2,
				RT3261_DAC_L2_SEL_SFT, rt3261_dac_l2_src);

static const struct snd_kcontrol_new rt3261_dac_l2_mux =
	SOC_DAPM_ENUM("DAC2 left channel source", rt3261_dac_l2_enum);

static const char *rt3261_dac_r2_src[] = {"IF2", "IF3", "TxDC"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_dac_r2_enum, RT3261_DSP_PATH2,
	RT3261_DAC_R2_SEL_SFT, rt3261_dac_r2_src);

static const struct snd_kcontrol_new rt3261_dac_r2_mux =
	SOC_DAPM_ENUM("DAC2 right channel source", rt3261_dac_r2_enum);

/* Interface 2  ADC channel source */
static const char *rt3261_if2_adc_l_src[] = {"TxDP", "Mono ADC MIXL"};

static const SOC_ENUM_SINGLE_DECL(rt3261_if2_adc_l_enum, RT3261_DSP_PATH2,
			RT3261_IF2_ADC_L_SEL_SFT, rt3261_if2_adc_l_src);

static const struct snd_kcontrol_new rt3261_if2_adc_l_mux =
	SOC_DAPM_ENUM("IF2 ADC left channel source", rt3261_if2_adc_l_enum);

static const char *rt3261_if2_adc_r_src[] = {"TxDP", "Mono ADC MIXR"};

static const SOC_ENUM_SINGLE_DECL(rt3261_if2_adc_r_enum, RT3261_DSP_PATH2,
			RT3261_IF2_ADC_R_SEL_SFT, rt3261_if2_adc_r_src);

static const struct snd_kcontrol_new rt3261_if2_adc_r_mux =
	SOC_DAPM_ENUM("IF2 ADC right channel source", rt3261_if2_adc_r_enum);

/* digital interface and iis interface map */
static const char *rt3261_dai_iis_map[] = {"1:1|2:2|3:3", "1:1|2:3|3:2",
	"1:3|2:1|3:2", "1:3|2:2|3:1", "1:2|2:3|3:1",
	"1:2|2:1|3:3", "1:1|2:1|3:3", "1:2|2:2|3:3"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_dai_iis_map_enum, RT3261_I2S1_SDP,
	RT3261_I2S_IF_SFT, rt3261_dai_iis_map);

static const struct snd_kcontrol_new rt3261_dai_mux =
	SOC_DAPM_ENUM("DAI select", rt3261_dai_iis_map_enum);

/* SDI select */
static const char *rt3261_sdi_sel[] = {"IF1", "IF2"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_sdi_sel_enum, RT3261_I2S2_SDP,
	RT3261_I2S2_SDI_SFT, rt3261_sdi_sel);

static const struct snd_kcontrol_new rt3261_sdi_mux =
	SOC_DAPM_ENUM("SDI select", rt3261_sdi_sel_enum);

static int rt3261_adc_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int val, mask;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		//rt3261_index_update_bits(codec,
		//	RT3261_CHOP_DAC_ADC, 0x1000, 0x1000);
		val = snd_soc_read(codec, RT3261_MONO_ADC_MIXER);
		mask = RT3261_M_MONO_ADC_L1 | RT3261_M_MONO_ADC_L2 |
			RT3261_M_MONO_ADC_R1 | RT3261_M_MONO_ADC_R2;
		if ((val & mask) ^ mask)
			snd_soc_update_bits(codec, RT3261_GEN_CTRL1,
				RT3261_M_MAMIX_L | RT3261_M_MAMIX_R, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RT3261_GEN_CTRL1,
			RT3261_M_MAMIX_L | RT3261_M_MAMIX_R,
			RT3261_M_MAMIX_L | RT3261_M_MAMIX_R);
		//rt3261_index_update_bits(codec,
		//	RT3261_CHOP_DAC_ADC, 0x1000, 0x0000);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt3261_spk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int val;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
//bard 8-26 s
		val = snd_soc_read(codec, RT3261_PWR_DIG1);
		if(val & (RT3261_PWR_DAC_L1 | RT3261_PWR_DAC_R1)) {
			snd_soc_update_bits(codec, RT3261_PWR_DIG1,
				RT3261_PWR_DAC_L1 | RT3261_PWR_DAC_R1,
				RT3261_PWR_DAC_L1 | RT3261_PWR_DAC_R1);
		}
//bard 8-26 e
		snd_soc_update_bits(codec, RT3261_PWR_DIG1,
			RT3261_PWR_CLS_D, RT3261_PWR_CLS_D);
		rt3261_index_update_bits(codec,
			RT3261_CLSD_INT_REG1, 0xf000, 0xf000);
		snd_soc_update_bits(codec, RT3261_SPK_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT3261_SPK_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE,
			RT3261_L_MUTE | RT3261_R_MUTE);
		rt3261_index_update_bits(codec,
			RT3261_CLSD_INT_REG1, 0xf000, 0x0000);
		snd_soc_update_bits(codec, RT3261_PWR_DIG1,
			RT3261_PWR_CLS_D, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

#if 1 //seq
static void rt3261_pmu_depop(struct snd_soc_codec *codec)
{
#if 0
	/* depop parameters */
	snd_soc_update_bits(codec, RT3261_DEPOP_M2,
		RT3261_DEPOP_MASK, RT3261_DEPOP_MAN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_CP_MASK | RT3261_HP_SG_MASK | RT3261_HP_CB_MASK,
		RT3261_HP_CP_PU | RT3261_HP_SG_DIS | RT3261_HP_CB_PU);
	rt3261_index_write(codec, RT3261_HP_DCC_INT1, 0x9f00);
	/* headphone amp power on */
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_FV1 | RT3261_PWR_FV2, 0);
	snd_soc_update_bits(codec, RT3261_PWR_VOL,
		RT3261_PWR_HV_L | RT3261_PWR_HV_R,
		RT3261_PWR_HV_L | RT3261_PWR_HV_R);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA);
	msleep(50);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_FV1 | RT3261_PWR_FV2 | RT3261_PWR_HP_L |
		RT3261_PWR_HP_R | RT3261_PWR_HA,
		RT3261_PWR_FV1 | RT3261_PWR_FV2 | RT3261_PWR_HP_L |
		RT3261_PWR_HP_R | RT3261_PWR_HA);
	snd_soc_update_bits(codec, RT3261_CHARGE_PUMP,
		RT3261_PM_HP_MASK, RT3261_PM_HP_HV);
	rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0200);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_CO_MASK | RT3261_HP_SG_MASK,
		RT3261_HP_CO_EN | RT3261_HP_SG_EN);
#else
	hp_amp_power(codec, 1);
#endif
	/* headphone unmute sequence */
	snd_soc_update_bits(codec, RT3261_DEPOP_M3,
		RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
		(RT3261_CP_FQ_192_KHZ << RT3261_CP_FQ1_SFT) |
		(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
		(RT3261_CP_FQ_192_KHZ << RT3261_CP_FQ3_SFT));
	rt3261_index_write(codec, RT3261_MAMP_INT_REG2, 0xfc00);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_SMT_TRIG_MASK, RT3261_SMT_TRIG_EN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_RSTN_MASK, RT3261_RSTN_EN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_RSTN_MASK | RT3261_HP_L_SMT_MASK | RT3261_HP_R_SMT_MASK,
		RT3261_RSTN_DIS | RT3261_HP_L_SMT_EN | RT3261_HP_R_SMT_EN);
	snd_soc_update_bits(codec, RT3261_HP_VOL,
		RT3261_L_MUTE | RT3261_R_MUTE, 0);
	msleep(100);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_SG_MASK | RT3261_HP_L_SMT_MASK |
		RT3261_HP_R_SMT_MASK, RT3261_HP_SG_DIS |
		RT3261_HP_L_SMT_DIS | RT3261_HP_R_SMT_DIS);
	/*bard 10-18 r
	msleep(20);	
	snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
		RT3261_HPD_PS_MASK, RT3261_HPD_PS_EN);
	*/
}

static void rt3261_pmd_depop(struct snd_soc_codec *codec)
{
	/* headphone mute sequence */
	snd_soc_update_bits(codec, RT3261_DEPOP_M3,
		RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
		(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ1_SFT) |
		(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
		(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ3_SFT));
	rt3261_index_write(codec, RT3261_MAMP_INT_REG2, 0xfc00);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_SG_MASK, RT3261_HP_SG_EN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_RSTP_MASK, RT3261_RSTP_EN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_RSTP_MASK | RT3261_HP_L_SMT_MASK |
		RT3261_HP_R_SMT_MASK, RT3261_RSTP_DIS |
		RT3261_HP_L_SMT_EN | RT3261_HP_R_SMT_EN);
	/*bard 10-18 r
	snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
		RT3261_HPD_PS_MASK, RT3261_HPD_PS_DIS);
	msleep(90);
	*/
	snd_soc_update_bits(codec, RT3261_HP_VOL,
		RT3261_L_MUTE | RT3261_R_MUTE, RT3261_L_MUTE | RT3261_R_MUTE);
	msleep(30);
#if 0
	//rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_SG_MASK | RT3261_HP_L_SMT_MASK |
		RT3261_HP_R_SMT_MASK, RT3261_HP_SG_DIS |
		RT3261_HP_L_SMT_DIS | RT3261_HP_R_SMT_DIS);
	/* headphone amp power down */
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_SMT_TRIG_MASK | RT3261_HP_CD_PD_MASK |
		RT3261_HP_CO_MASK | RT3261_HP_CP_MASK |
		RT3261_HP_SG_MASK | RT3261_HP_CB_MASK,
		RT3261_SMT_TRIG_DIS | RT3261_HP_CD_PD_EN |
		RT3261_HP_CO_DIS | RT3261_HP_CP_PD |
		RT3261_HP_SG_EN | RT3261_HP_CB_PD);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA,
		0);
#else
	hp_amp_power(codec, 0);
#endif
}
#else //one bit
static void rt3261_pmu_depop(struct snd_soc_codec *codec)
{
	/* depop parameters */
	snd_soc_update_bits(codec, RT3261_DEPOP_M2,
		RT3261_DEPOP_MASK, RT3261_DEPOP_MAN);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_CP_MASK | RT3261_HP_SG_MASK | RT3261_HP_CB_MASK,
		RT3261_HP_CP_PU | RT3261_HP_SG_DIS | RT3261_HP_CB_PU);
	rt3261_index_write(codec, RT3261_HP_DCC_INT1, 0x9f00);
	/* headphone amp power on */
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_FV1 | RT3261_PWR_FV2, 0);
	snd_soc_update_bits(codec, RT3261_PWR_VOL,
		RT3261_PWR_HV_L | RT3261_PWR_HV_R,
		RT3261_PWR_HV_L | RT3261_PWR_HV_R);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA);
	msleep(50);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_FV1 | RT3261_PWR_FV2 ,
		RT3261_PWR_FV1 | RT3261_PWR_FV2 );
	rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0200);
	/* headphone unmute sequence */
	snd_soc_update_bits(codec, RT3261_DEPOP_M2,
		RT3261_DEPOP_MASK | RT3261_DIG_DP_MASK,
		RT3261_DEPOP_AUTO | RT3261_DIG_DP_EN);
	snd_soc_update_bits(codec, RT3261_CHARGE_PUMP,
		RT3261_PM_HP_MASK, RT3261_PM_HP_HV);
	snd_soc_update_bits(codec, RT3261_DEPOP_M3,
		RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
		(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ1_SFT) |
		(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
		(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ3_SFT));
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_CP_MASK | RT3261_HP_SG_MASK,
		RT3261_HP_CP_PD | RT3261_HP_SG_EN);
	msleep(10);
	snd_soc_update_bits(codec, RT3261_HP_VOL,
		RT3261_L_MUTE | RT3261_R_MUTE, 0);
	msleep(70); //bard 10-18
	/*bard 10-18 r
	snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
		RT3261_HPD_PS_MASK, RT3261_HPD_PS_EN);
	*/
}

static void rt3261_pmd_depop(struct snd_soc_codec *codec)
{
	/*bard 10-18 r
	snd_soc_update_bits(codec, RT3261_HP_CALIB_AMP_DET,
		RT3261_HPD_PS_MASK, RT3261_HPD_PS_DIS);
	*/
	snd_soc_update_bits(codec, RT3261_HP_VOL,
		RT3261_L_MUTE | RT3261_R_MUTE,
		RT3261_L_MUTE | RT3261_R_MUTE);
	msleep(90);
	snd_soc_update_bits(codec, RT3261_DEPOP_M1,
		RT3261_HP_CB_MASK, RT3261_HP_CB_PD);
	msleep(30);
	//rt3261_index_update_bits(codec, RT3261_CHOP_DAC_ADC, 0x0200, 0x0);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_HP_L | RT3261_PWR_HP_R | RT3261_PWR_HA,
		0);
}
#endif

static int rt3261_hp_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt3261_pmu_depop(codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt3261_pmd_depop(codec);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt3261_mono_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT3261_MONO_OUT,
				RT3261_L_MUTE, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT3261_MONO_OUT,
			RT3261_L_MUTE, RT3261_L_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt3261_lout_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		hp_amp_power(codec,1);
		snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
			RT3261_PWR_LM, RT3261_PWR_LM); //bard 10-18
		snd_soc_update_bits(codec, RT3261_OUTPUT,
			RT3261_L_MUTE | RT3261_R_MUTE, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT3261_OUTPUT,
			RT3261_L_MUTE | RT3261_R_MUTE,
			RT3261_L_MUTE | RT3261_R_MUTE);
		snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
			RT3261_PWR_LM, 0); //bard 10-18
		hp_amp_power(codec,0);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt3261_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL1", RT3261_PWR_ANLG2,
			RT3261_PWR_PLL_BIT, 0, NULL, 0),
	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_SUPPLY("LDO2", RT3261_PWR_ANLG1,
			RT3261_PWR_LDO2_BIT, 0, NULL, 0),
	#if 0
	SND_SOC_DAPM_MICBIAS("micbias1", RT3261_PWR_ANLG2,
			RT3261_PWR_MB1_BIT, 0),
	#else
	SND_SOC_DAPM_MICBIAS("micbias1", SND_SOC_NOPM,
			0, 0),
	#endif
	SND_SOC_DAPM_MICBIAS("micbias2", RT3261_PWR_ANLG2,
			RT3261_PWR_MB2_BIT, 0),
	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),
	SND_SOC_DAPM_INPUT("IN3P"),
	SND_SOC_DAPM_INPUT("IN3N"),
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),
	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	/* Boost */
	SND_SOC_DAPM_PGA("BST1", RT3261_PWR_ANLG2,
		RT3261_PWR_BST1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST2", RT3261_PWR_ANLG2,
		RT3261_PWR_BST4_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST3", RT3261_PWR_ANLG2,
		RT3261_PWR_BST2_BIT, 0, NULL, 0),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT3261_PWR_VOL,
		RT3261_PWR_IN_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT3261_PWR_VOL,
		RT3261_PWR_IN_R_BIT, 0, NULL, 0),
	/* IN Mux */
	SND_SOC_DAPM_MUX("INL Mux", SND_SOC_NOPM, 0, 0, &rt3261_inl_mux),
	SND_SOC_DAPM_MUX("INR Mux", SND_SOC_NOPM, 0, 0, &rt3261_inr_mux),
	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT3261_PWR_MIXER, RT3261_PWR_RM_L_BIT, 0,
			rt3261_rec_l_mix, ARRAY_SIZE(rt3261_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT3261_PWR_MIXER, RT3261_PWR_RM_R_BIT, 0,
			rt3261_rec_r_mix, ARRAY_SIZE(rt3261_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM,
		0, 0),
	SND_SOC_DAPM_ADC_E("ADC R", NULL, SND_SOC_NOPM,
		0, 0, rt3261_adc_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY("ADC L power",RT3261_PWR_DIG1,
			RT3261_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC R power",RT3261_PWR_DIG1,
			RT3261_PWR_ADC_R_BIT, 0, NULL, 0),
	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_sto_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_sto_adc_r2_mux),
	SND_SOC_DAPM_MUX("Stereo ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_sto_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_sto_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_mono_adc_r2_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("stereo filter", RT3261_PWR_DIG2,
		RT3261_PWR_ADC_SF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_sto_adc_l_mix, ARRAY_SIZE(rt3261_sto_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_sto_adc_r_mix, ARRAY_SIZE(rt3261_sto_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("mono left filter", RT3261_PWR_DIG2,
		RT3261_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_mono_adc_l_mix, ARRAY_SIZE(rt3261_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("mono right filter", RT3261_PWR_DIG2,
		RT3261_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_mono_adc_r_mix, ARRAY_SIZE(rt3261_mono_adc_r_mix)),

	/* IF2 Mux */
	SND_SOC_DAPM_MUX("IF2 ADC L Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_if2_adc_l_mux),
	SND_SOC_DAPM_MUX("IF2 ADC R Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_if2_adc_r_mux),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT3261_PWR_DIG1,
		RT3261_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT3261_PWR_DIG1,
		RT3261_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S3", RT3261_PWR_DIG1,
		RT3261_PWR_I2S3_BIT, 0, NULL, 0),
 	SND_SOC_DAPM_PGA("IF3 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("DAI1 RX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 TX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 IF1 Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 IF2 Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("SDI1 TX Mux", SND_SOC_NOPM, 0, 0, &rt3261_sdi_mux),

	SND_SOC_DAPM_MUX("DAI2 RX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 TX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 IF1 Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 IF2 Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("SDI2 TX Mux", SND_SOC_NOPM, 0, 0, &rt3261_sdi_mux),

	SND_SOC_DAPM_MUX("DAI3 RX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),
	SND_SOC_DAPM_MUX("DAI3 TX Mux", SND_SOC_NOPM, 0, 0, &rt3261_dai_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* ANC */
	SND_SOC_DAPM_PGA("ANC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_dac_l_mix, ARRAY_SIZE(rt3261_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_dac_r_mix, ARRAY_SIZE(rt3261_dac_r_mix)),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt3261_dac_r2_mux),
#if 0 //org
	SND_SOC_DAPM_PGA("DAC L2 Volume", RT3261_PWR_DIG1,
			RT3261_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R2 Volume", RT3261_PWR_DIG1,
			RT3261_PWR_DAC_R2_BIT, 0, NULL, 0),
#else //bard 9-26
	SND_SOC_DAPM_PGA("DAC L2 Volume", SND_SOC_NOPM,
			0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R2 Volume", SND_SOC_NOPM,
			0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC L1 Power", RT3261_PWR_DIG1,
		RT3261_PWR_DAC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R1 Power", RT3261_PWR_DIG1,
		RT3261_PWR_DAC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC L2 Power", RT3261_PWR_DIG1,
		RT3261_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R2 Power", RT3261_PWR_DIG1,
		RT3261_PWR_DAC_R2_BIT, 0, NULL, 0),
#endif

	/* DAC Mixer */
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_sto_dac_l_mix, ARRAY_SIZE(rt3261_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_sto_dac_r_mix, ARRAY_SIZE(rt3261_sto_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_mono_dac_l_mix, ARRAY_SIZE(rt3261_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_mono_dac_r_mix, ARRAY_SIZE(rt3261_mono_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DIG MIXL", SND_SOC_NOPM, 0, 0,
		rt3261_dig_l_mix, ARRAY_SIZE(rt3261_dig_l_mix)),
	SND_SOC_DAPM_MIXER("DIG MIXR", SND_SOC_NOPM, 0, 0,
		rt3261_dig_r_mix, ARRAY_SIZE(rt3261_dig_r_mix)),
	SND_SOC_DAPM_MUX("Mono dacr Mux", SND_SOC_NOPM, 0, 0,
		&rt3261_dacr2_mux),

	/* DACs */
#if 0 //org
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT3261_PWR_DIG1,
			RT3261_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, RT3261_PWR_DIG1,
			RT3261_PWR_DAC_L2_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT3261_PWR_DIG1,
			RT3261_PWR_DAC_R1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R2", NULL, RT3261_PWR_DIG1,
			RT3261_PWR_DAC_R2_BIT, 0),
#else //bard 9-26
	SND_SOC_DAPM_DAC("DAC L1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R2", NULL, SND_SOC_NOPM, 0, 0),
#endif
	SND_SOC_DAPM_PGA("DAC 1", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC 2", SND_SOC_NOPM, 
		0, 0, NULL, 0),
	/* SPK/OUT Mixer */
	SND_SOC_DAPM_MIXER("SPK MIXL", RT3261_PWR_MIXER, RT3261_PWR_SM_L_BIT,
		0, rt3261_spk_l_mix, ARRAY_SIZE(rt3261_spk_l_mix)),
	SND_SOC_DAPM_MIXER("SPK MIXR", RT3261_PWR_MIXER, RT3261_PWR_SM_R_BIT,
		0, rt3261_spk_r_mix, ARRAY_SIZE(rt3261_spk_r_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXL", RT3261_PWR_MIXER, RT3261_PWR_OM_L_BIT,
		0, rt3261_out_l_mix, ARRAY_SIZE(rt3261_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT3261_PWR_MIXER, RT3261_PWR_OM_R_BIT,
		0, rt3261_out_r_mix, ARRAY_SIZE(rt3261_out_r_mix)),
	/* Ouput Volume */
	SND_SOC_DAPM_PGA("SPKVOL L", RT3261_PWR_VOL,
		RT3261_PWR_SV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPKVOL R", RT3261_PWR_VOL,
		RT3261_PWR_SV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OUTVOL L", RT3261_PWR_VOL,
		RT3261_PWR_OV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OUTVOL R", RT3261_PWR_VOL,
		RT3261_PWR_OV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL L", RT3261_PWR_VOL,
		RT3261_PWR_HV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL R", RT3261_PWR_VOL,
		RT3261_PWR_HV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL", SND_SOC_NOPM, 
		0, 0, NULL, 0),
	/* SPO/HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("SPOL MIX", SND_SOC_NOPM, 0,
		0, rt3261_spo_l_mix, ARRAY_SIZE(rt3261_spo_l_mix)),
	SND_SOC_DAPM_MIXER("SPOR MIX", SND_SOC_NOPM, 0,
		0, rt3261_spo_r_mix, ARRAY_SIZE(rt3261_spo_r_mix)),
	SND_SOC_DAPM_MIXER("DAC SPK", SND_SOC_NOPM, 0,
		0, rt3261_spo_dac_mix, ARRAY_SIZE(rt3261_spo_dac_mix)), //bard 8-27
	SND_SOC_DAPM_MIXER("HPO MIX", SND_SOC_NOPM, 0, 0,
		rt3261_hpo_mix, ARRAY_SIZE(rt3261_hpo_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", SND_SOC_NOPM, 0, 0,
		rt3261_lout_mix, ARRAY_SIZE(rt3261_lout_mix)),
	SND_SOC_DAPM_MIXER("Mono MIX", RT3261_PWR_ANLG1, RT3261_PWR_MM_BIT, 0,
		rt3261_mono_mix, ARRAY_SIZE(rt3261_mono_mix)),

	SND_SOC_DAPM_PGA_S("HP amp", 1, SND_SOC_NOPM, 0, 0,
		rt3261_hp_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("SPK amp", 1, SND_SOC_NOPM, 0, 0,
		rt3261_spk_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT amp", 1, SND_SOC_NOPM, 0, 0,
		rt3261_lout_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("Mono amp", 1, RT3261_PWR_ANLG1,
		RT3261_PWR_MA_BIT, 0, rt3261_mono_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("SPOLP"),
	SND_SOC_DAPM_OUTPUT("SPOLN"),
	SND_SOC_DAPM_OUTPUT("SPORP"),
	SND_SOC_DAPM_OUTPUT("SPORN"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("MonoP"),
	SND_SOC_DAPM_OUTPUT("MonoN"),
};

static const struct snd_soc_dapm_route rt3261_dapm_routes[] = {
	{"IN1P", NULL, "LDO2"},
	{"IN2P", NULL, "LDO2"},
	{"IN3P", NULL, "LDO2"},

	{"IN1P", NULL, "MIC1"},
	{"IN1N", NULL, "MIC1"},
	{"IN2P", NULL, "MIC2"},
	{"IN2N", NULL, "MIC2"},
	{"IN3P", NULL, "MIC3"},
	{"IN3N", NULL, "MIC3"},

	{"DMIC L1", NULL, "DMIC1"},
	{"DMIC R1", NULL, "DMIC1"},
	{"DMIC L2", NULL, "DMIC2"},
	{"DMIC R2", NULL, "DMIC2"},

	{"BST1", NULL, "IN1P"},
	{"BST1", NULL, "IN1N"},
	{"BST2", NULL, "IN2P"},
	{"BST2", NULL, "IN2N"},
	{"BST3", NULL, "IN3P"},
	{"BST3", NULL, "IN3N"},

	{"INL VOL", NULL, "IN2P"},
	{"INR VOL", NULL, "IN2N"},
	
	{"RECMIXL", "HPOL Switch", "HPOL"},
	{"RECMIXL", "INL Switch", "INL VOL"},
	{"RECMIXL", "BST3 Switch", "BST3"},
	{"RECMIXL", "BST2 Switch", "BST2"},
	{"RECMIXL", "BST1 Switch", "BST1"},
	{"RECMIXL", "OUT MIXL Switch", "OUT MIXL"},

	{"RECMIXR", "HPOR Switch", "HPOR"},
	{"RECMIXR", "INR Switch", "INR VOL"},
	{"RECMIXR", "BST3 Switch", "BST3"},
	{"RECMIXR", "BST2 Switch", "BST2"},
	{"RECMIXR", "BST1 Switch", "BST1"},
	{"RECMIXR", "OUT MIXR Switch", "OUT MIXR"},

	{"ADC L", NULL, "RECMIXL"},
	{"ADC L", NULL, "ADC L power"},
	{"ADC R", NULL, "RECMIXR"},
	{"ADC R", NULL, "ADC R power"},

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L2", NULL, "DMIC CLK"},

	{"Stereo ADC L2 Mux", "DMIC1", "DMIC L1"},
	{"Stereo ADC L2 Mux", "DMIC2", "DMIC L2"},
	{"Stereo ADC L2 Mux", "DIG MIX", "DIG MIXL"},
	{"Stereo ADC L1 Mux", "ADC", "ADC L"},
	{"Stereo ADC L1 Mux", "DIG MIX", "DIG MIXL"},

	{"Stereo ADC R1 Mux", "ADC", "ADC R"},
	{"Stereo ADC R1 Mux", "DIG MIX", "DIG MIXR"},
	{"Stereo ADC R2 Mux", "DMIC1", "DMIC R1"},
	{"Stereo ADC R2 Mux", "DMIC2", "DMIC R2"},
	{"Stereo ADC R2 Mux", "DIG MIX", "DIG MIXR"},

	{"Mono ADC L2 Mux", "DMIC L1", "DMIC L1"},
	{"Mono ADC L2 Mux", "DMIC L2", "DMIC L2"},
	{"Mono ADC L2 Mux", "Mono DAC MIXL", "Mono DAC MIXL"},
	{"Mono ADC L1 Mux", "Mono DAC MIXL", "Mono DAC MIXL"},
	{"Mono ADC L1 Mux", "ADCL", "ADC L"},

	{"Mono ADC R1 Mux", "Mono DAC MIXR", "Mono DAC MIXR"},
	{"Mono ADC R1 Mux", "ADCR", "ADC R"},
	{"Mono ADC R2 Mux", "DMIC R1", "DMIC R1"},
	{"Mono ADC R2 Mux", "DMIC R2", "DMIC R2"},
	{"Mono ADC R2 Mux", "Mono DAC MIXR", "Mono DAC MIXR"},

	{"Stereo ADC MIXL", "ADC1 Switch", "Stereo ADC L1 Mux"},
	{"Stereo ADC MIXL", "ADC2 Switch", "Stereo ADC L2 Mux"},
	{"Stereo ADC MIXL", NULL, "stereo filter"},
	{"stereo filter", NULL, "PLL1", check_sysclk1_source},

	{"Stereo ADC MIXR", "ADC1 Switch", "Stereo ADC R1 Mux"},
	{"Stereo ADC MIXR", "ADC2 Switch", "Stereo ADC R2 Mux"},
	{"Stereo ADC MIXR", NULL, "stereo filter"},
	{"stereo filter", NULL, "PLL1", check_sysclk1_source},

	{"Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux"},
	{"Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux"},
	{"Mono ADC MIXL", NULL, "mono left filter"},
	{"mono left filter", NULL, "PLL1", check_sysclk1_source},

	{"Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux"},
	{"Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux"},
	{"Mono ADC MIXR", NULL, "mono right filter"},
	{"mono right filter", NULL, "PLL1", check_sysclk1_source},

	{"IF2 ADC L Mux", "Mono ADC MIXL", "Mono ADC MIXL"},
	{"IF2 ADC R Mux", "Mono ADC MIXR", "Mono ADC MIXR"},

	{"IF2 ADC L", NULL, "IF2 ADC L Mux"},
	{"IF2 ADC R", NULL, "IF2 ADC R Mux"},
	{"IF3 ADC L", NULL, "Mono ADC MIXL"},
	{"IF3 ADC R", NULL, "Mono ADC MIXR"},
	{"IF1 ADC L", NULL, "Stereo ADC MIXL"},
	{"IF1 ADC R", NULL, "Stereo ADC MIXR"},

	{"IF1 ADC", NULL, "I2S1"},
	{"IF1 ADC", NULL, "IF1 ADC L"},
	{"IF1 ADC", NULL, "IF1 ADC R"},
	{"IF2 ADC", NULL, "I2S2"},
	{"IF2 ADC", NULL, "IF2 ADC L"},
	{"IF2 ADC", NULL, "IF2 ADC R"},
	{"IF3 ADC", NULL, "I2S3"},
	{"IF3 ADC", NULL, "IF3 ADC L"},
	{"IF3 ADC", NULL, "IF3 ADC R"},

	{"DAI1 TX Mux", "1:1|2:2|3:3", "IF1 ADC"},
	{"DAI1 TX Mux", "1:1|2:3|3:2", "IF1 ADC"},
	{"DAI1 TX Mux", "1:3|2:1|3:2", "IF2 ADC"},
	{"DAI1 TX Mux", "1:2|2:1|3:3", "IF2 ADC"},
	{"DAI1 TX Mux", "1:3|2:2|3:1", "IF3 ADC"},
	{"DAI1 TX Mux", "1:2|2:3|3:1", "IF3 ADC"},
	{"DAI1 IF1 Mux", "1:1|2:1|3:3", "IF1 ADC"},
	{"DAI1 IF2 Mux", "1:1|2:1|3:3", "IF2 ADC"},
	{"SDI1 TX Mux", "IF1", "DAI1 IF1 Mux"},
	{"SDI1 TX Mux", "IF2", "DAI1 IF2 Mux"},

	{"DAI2 TX Mux", "1:2|2:3|3:1", "IF1 ADC"},
	{"DAI2 TX Mux", "1:2|2:1|3:3", "IF1 ADC"},
	{"DAI2 TX Mux", "1:1|2:2|3:3", "IF2 ADC"},
	{"DAI2 TX Mux", "1:3|2:2|3:1", "IF2 ADC"},
	{"DAI2 TX Mux", "1:1|2:3|3:2", "IF3 ADC"},
	{"DAI2 TX Mux", "1:3|2:1|3:2", "IF3 ADC"},
	{"DAI2 IF1 Mux", "1:2|2:2|3:3", "IF1 ADC"},
	{"DAI2 IF2 Mux", "1:2|2:2|3:3", "IF2 ADC"},
	{"SDI2 TX Mux", "IF1", "DAI2 IF1 Mux"},
	{"SDI2 TX Mux", "IF2", "DAI2 IF2 Mux"},

	{"DAI3 TX Mux", "1:3|2:1|3:2", "IF1 ADC"},
	{"DAI3 TX Mux", "1:3|2:2|3:1", "IF1 ADC"},
	{"DAI3 TX Mux", "1:1|2:3|3:2", "IF2 ADC"},
	{"DAI3 TX Mux", "1:2|2:3|3:1", "IF2 ADC"},
	{"DAI3 TX Mux", "1:1|2:2|3:3", "IF3 ADC"},
	{"DAI3 TX Mux", "1:2|2:1|3:3", "IF3 ADC"},
	{"DAI3 TX Mux", "1:1|2:1|3:3", "IF3 ADC"},
	{"DAI3 TX Mux", "1:2|2:2|3:3", "IF3 ADC"},

	{"AIF1TX", NULL, "DAI1 TX Mux"},
	{"AIF1TX", NULL, "SDI1 TX Mux"},
	{"AIF2TX", NULL, "DAI2 TX Mux"},
	{"AIF2TX", NULL, "SDI2 TX Mux"},
	{"AIF3TX", NULL, "DAI3 TX Mux"},

	{"DAI1 RX Mux", "1:1|2:2|3:3", "AIF1RX"},
	{"DAI1 RX Mux", "1:1|2:3|3:2", "AIF1RX"},
	{"DAI1 RX Mux", "1:1|2:1|3:3", "AIF1RX"},
	{"DAI1 RX Mux", "1:2|2:3|3:1", "AIF2RX"},
	{"DAI1 RX Mux", "1:2|2:1|3:3", "AIF2RX"},
	{"DAI1 RX Mux", "1:2|2:2|3:3", "AIF2RX"},
	{"DAI1 RX Mux", "1:3|2:1|3:2", "AIF3RX"},
	{"DAI1 RX Mux", "1:3|2:2|3:1", "AIF3RX"},

	{"DAI2 RX Mux", "1:3|2:1|3:2", "AIF1RX"},
	{"DAI2 RX Mux", "1:2|2:1|3:3", "AIF1RX"},
	{"DAI2 RX Mux", "1:1|2:1|3:3", "AIF1RX"},
	{"DAI2 RX Mux", "1:1|2:2|3:3", "AIF2RX"},
	{"DAI2 RX Mux", "1:3|2:2|3:1", "AIF2RX"},
	{"DAI2 RX Mux", "1:2|2:2|3:3", "AIF2RX"},
	{"DAI2 RX Mux", "1:1|2:3|3:2", "AIF3RX"},
	{"DAI2 RX Mux", "1:2|2:3|3:1", "AIF3RX"},

	{"DAI3 RX Mux", "1:3|2:2|3:1", "AIF1RX"},
	{"DAI3 RX Mux", "1:2|2:3|3:1", "AIF1RX"},
	{"DAI3 RX Mux", "1:1|2:3|3:2", "AIF2RX"},
	{"DAI3 RX Mux", "1:3|2:1|3:2", "AIF2RX"},
	{"DAI3 RX Mux", "1:1|2:2|3:3", "AIF3RX"},
	{"DAI3 RX Mux", "1:2|2:1|3:3", "AIF3RX"},
	{"DAI3 RX Mux", "1:1|2:1|3:3", "AIF3RX"},
	{"DAI3 RX Mux", "1:2|2:2|3:3", "AIF3RX"},

	{"IF1 DAC", NULL, "I2S1"},
	{"IF1 DAC", NULL, "DAI1 RX Mux"},
	{"IF2 DAC", NULL, "I2S2"},
	{"IF2 DAC", NULL, "DAI2 RX Mux"},
	{"IF3 DAC", NULL, "I2S3"},
	{"IF3 DAC", NULL, "DAI3 RX Mux"},

	{"IF1 DAC L", NULL, "IF1 DAC"},
	{"IF1 DAC R", NULL, "IF1 DAC"},
	{"IF2 DAC L", NULL, "IF2 DAC"},
	{"IF2 DAC R", NULL, "IF2 DAC"},
	{"IF3 DAC L", NULL, "IF3 DAC"},
	{"IF3 DAC R", NULL, "IF3 DAC"},

	{"DAC MIXL", "Stereo ADC Switch", "Stereo ADC MIXL"},
	{"DAC MIXL", "INF1 Switch", "IF1 DAC L"},
	{"DAC MIXL", NULL, "DAC L1 Power"}, //bard 9-26
	{"DAC MIXR", "Stereo ADC Switch", "Stereo ADC MIXR"},
	{"DAC MIXR", "INF1 Switch", "IF1 DAC R"},
	{"DAC MIXR", NULL, "DAC R1 Power"}, //bard 9-26

	{"ANC", NULL, "Stereo ADC MIXL"},
	{"ANC", NULL, "Stereo ADC MIXR"},

	{"Audio DSP", NULL, "DAC MIXL"},
	{"Audio DSP", NULL, "DAC MIXR"},

	{"DAC L2 Mux", "IF2", "IF2 DAC L"},
	{"DAC L2 Mux", "IF3", "IF3 DAC L"},
	{"DAC L2 Mux", "Base L/R", "Audio DSP"},
	{"DAC L2 Volume", NULL, "DAC L2 Mux"},
	{"DAC L2 Volume", NULL, "DAC L2 Power"}, //bard 9-26

	{"DAC R2 Mux", "IF2", "IF2 DAC R"},
	{"DAC R2 Mux", "IF3", "IF3 DAC R"},
	{"DAC R2 Volume", NULL, "Mono dacr Mux"},
	{"Mono dacr Mux", "TxDC_R", "DAC R2 Mux"},
	{"Mono dacr Mux", "TxDP_R", "IF2 ADC R Mux"},
	{"DAC R2 Volume", NULL, "DAC R2 Power"}, //bsrd 9-26

	{"Stereo DAC MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Volume"},
	{"Stereo DAC MIXL", "ANC Switch", "ANC"},
	{"Stereo DAC MIXR", "DAC R1 Switch", "DAC MIXR"},
	{"Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Volume"},
	{"Stereo DAC MIXR", "ANC Switch", "ANC"},

	{"Mono DAC MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Volume"},
	{"Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Volume"},
	{"Mono DAC MIXR", "DAC R1 Switch", "DAC MIXR"},
	{"Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Volume"},
	{"Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Volume"},

	{"DIG MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"DIG MIXL", "DAC L2 Switch", "DAC L2 Volume"},
	{"DIG MIXR", "DAC R1 Switch", "DAC MIXR"},
	{"DIG MIXR", "DAC R2 Switch", "DAC R2 Volume"},

	{"DAC L1", NULL, "Stereo DAC MIXL"},
	{"DAC L1", NULL, "PLL1", check_sysclk1_source},
	{"DAC L1", NULL, "DAC L1 Power"}, //bard 9-26
	{"DAC R1", NULL, "Stereo DAC MIXR"},
	{"DAC R1", NULL, "PLL1", check_sysclk1_source},
	{"DAC R1", NULL, "DAC R1 Power"}, //bard 9-26
	{"DAC L2", NULL, "Mono DAC MIXL"},
	{"DAC L2", NULL, "PLL1", check_sysclk1_source},
	{"DAC L2", NULL, "DAC L2 Power"}, //bard 9-26
	{"DAC R2", NULL, "Mono DAC MIXR"},
	{"DAC R2", NULL, "PLL1", check_sysclk1_source},
	{"DAC R2", NULL, "DAC R2 Power"}, //bard 9-26

	{"SPK MIXL", "REC MIXL Switch", "RECMIXL"},
	{"SPK MIXL", "INL Switch", "INL VOL"},
	{"SPK MIXL", "DAC L1 Switch", "DAC L1"},
	{"SPK MIXL", "DAC L2 Switch", "DAC L2"},
	{"SPK MIXL", "OUT MIXL Switch", "OUT MIXL"},
	{"SPK MIXR", "REC MIXR Switch", "RECMIXR"},
	{"SPK MIXR", "INR Switch", "INR VOL"},
	{"SPK MIXR", "DAC R1 Switch", "DAC R1"},
	{"SPK MIXR", "DAC R2 Switch", "DAC R2"},
	{"SPK MIXR", "OUT MIXR Switch", "OUT MIXR"},

	{"OUT MIXL", "BST3 Switch", "BST3"},
	{"OUT MIXL", "BST1 Switch", "BST1"},
	{"OUT MIXL", "INL Switch", "INL VOL"},
	{"OUT MIXL", "REC MIXL Switch", "RECMIXL"},
	{"OUT MIXL", "DAC R2 Switch", "DAC R2"},
	{"OUT MIXL", "DAC L2 Switch", "DAC L2"},
	{"OUT MIXL", "DAC L1 Switch", "DAC L1"},

	{"OUT MIXR", "BST3 Switch", "BST3"},
	{"OUT MIXR", "BST2 Switch", "BST2"},
	{"OUT MIXR", "BST1 Switch", "BST1"},
	{"OUT MIXR", "INR Switch", "INR VOL"},
	{"OUT MIXR", "REC MIXR Switch", "RECMIXR"},
	{"OUT MIXR", "DAC L2 Switch", "DAC L2"},
	{"OUT MIXR", "DAC R2 Switch", "DAC R2"},
	{"OUT MIXR", "DAC R1 Switch", "DAC R1"},

	{"SPKVOL L", NULL, "SPK MIXL"},
	{"SPKVOL R", NULL, "SPK MIXR"},
	{"HPOVOL L", NULL, "OUT MIXL"},
	{"HPOVOL R", NULL, "OUT MIXR"},
	{"OUTVOL L", NULL, "OUT MIXL"},
	{"OUTVOL R", NULL, "OUT MIXR"},
#if 0//org
	{"SPOL MIX", "DAC R1 Switch", "DAC R1"},
	{"SPOL MIX", "DAC L1 Switch", "DAC L1"},
#else //bard 8-27
	{"SPOL MIX", "DAC Switch", "DAC SPK"},
	{"DAC SPK", "DAC L1 Switch", "DAC L1"},
	{"DAC SPK", "DAC R1 Switch", "DAC R1"},
#endif
	{"SPOL MIX", "SPKVOL R Switch", "SPKVOL R"},
	{"SPOL MIX", "SPKVOL L Switch", "SPKVOL L"},
	{"SPOL MIX", "BST1 Switch", "BST1"},
	{"SPOR MIX", "DAC R1 Switch", "DAC R1"},
	{"SPOR MIX", "SPKVOL R Switch", "SPKVOL R"},
	{"SPOR MIX", "BST1 Switch", "BST1"},

	{"DAC 2", NULL, "DAC L2"},
	{"DAC 2", NULL, "DAC R2"},
	{"DAC 1", NULL, "DAC L1"},
	{"DAC 1", NULL, "DAC R1"},
	{"HPOVOL", NULL, "HPOVOL L"},
	{"HPOVOL", NULL, "HPOVOL R"},
	{"HPO MIX", "DAC2 Switch", "DAC 2"},
	{"HPO MIX", "DAC1 Switch", "DAC 1"},
	{"HPO MIX", "HPVOL Switch", "HPOVOL"},

	{"LOUT MIX", "DAC L1 Switch", "DAC L1"},
	{"LOUT MIX", "DAC R1 Switch", "DAC R1"},
	{"LOUT MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"LOUT MIX", "OUTVOL R Switch", "OUTVOL R"},

	{"Mono MIX", "DAC R2 Switch", "DAC R2"},
	{"Mono MIX", "DAC L2 Switch", "DAC L2"},
	{"Mono MIX", "OUTVOL R Switch", "OUTVOL R"},
	{"Mono MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"Mono MIX", "BST1 Switch", "BST1"},

	{"SPK amp", NULL, "SPOL MIX"},
	{"SPK amp", NULL, "SPOR MIX"},
	{"SPOLP", NULL, "SPK amp"},
	{"SPOLN", NULL, "SPK amp"},
	{"SPORP", NULL, "SPK amp"},
	{"SPORN", NULL, "SPK amp"},
	
	{"HP amp", NULL, "HPO MIX"},
	{"HPOL", NULL, "HP amp"},
	{"HPOR", NULL, "HP amp"},

	{"LOUT amp", NULL, "LOUT MIX"},
	{"LOUTL", NULL, "LOUT amp"},
	{"LOUTR", NULL, "LOUT amp"},

	{"Mono amp", NULL, "Mono MIX"},
	{"MonoP", NULL, "Mono amp"},
	{"MonoN", NULL, "Mono amp"},
};

static int get_sdp_info(struct snd_soc_codec *codec, int dai_id)
{
	int ret = 0, val;

	if(codec == NULL)
		return -EINVAL;

	val = snd_soc_read(codec, RT3261_I2S1_SDP);
	val = (val & RT3261_I2S_IF_MASK) >> RT3261_I2S_IF_SFT;
	switch (dai_id) {
	case RT3261_AIF1:
		if (val == RT3261_IF_123 || val == RT3261_IF_132 ||
			val == RT3261_IF_113)
			ret |= RT3261_U_IF1;
		if (val == RT3261_IF_312 || val == RT3261_IF_213 ||
			val == RT3261_IF_113)
			ret |= RT3261_U_IF2;
		if (val == RT3261_IF_321 || val == RT3261_IF_231)
			ret |= RT3261_U_IF3;
		break;

	case RT3261_AIF2:
		if (val == RT3261_IF_231 || val == RT3261_IF_213 ||
			val == RT3261_IF_223)
			ret |= RT3261_U_IF1;
		if (val == RT3261_IF_123 || val == RT3261_IF_321 ||
			val == RT3261_IF_223)
			ret |= RT3261_U_IF2;
		if (val == RT3261_IF_132 || val == RT3261_IF_312)
			ret |= RT3261_U_IF3;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int get_clk_info(int sclk, int rate)
{
	int i, pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt3261_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk, dai_sel;
	int pre_div, bclk_ms, frame_size;

	rt3261->lrck[dai->id] = params_rate(params);
	if(dai->id == 1)
		rt3261->lrck[dai->id] = 8000;
	pre_div = get_clk_info(rt3261->sysclk, rt3261->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32 ? 1 : 0;
	rt3261->bclk[dai->id] = rt3261->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt3261->bclk[dai->id], rt3261->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val_len |= RT3261_I2S_DL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val_len |= RT3261_I2S_DL_24;
		break;
	case SNDRV_PCM_FORMAT_S8:
		val_len |= RT3261_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	dai_sel = get_sdp_info(codec, dai->id);
	dai_sel |= (RT3261_U_IF1 | RT3261_U_IF2);
	if (dai_sel < 0) {
		dev_err(codec->dev, "Failed to get sdp info: %d\n", dai_sel);
		return -EINVAL;
	}
	if (dai_sel & RT3261_U_IF1) {
 		mask_clk = RT3261_I2S_BCLK_MS1_MASK | RT3261_I2S_PD1_MASK;
		val_clk = bclk_ms << RT3261_I2S_BCLK_MS1_SFT |
			pre_div << RT3261_I2S_PD1_SFT;
		snd_soc_update_bits(codec, RT3261_I2S1_SDP,
			RT3261_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT3261_ADDA_CLK1, mask_clk, val_clk);
	}
	if (dai_sel & RT3261_U_IF2) {
		mask_clk = RT3261_I2S_BCLK_MS2_MASK | RT3261_I2S_PD2_MASK;
		val_clk = bclk_ms << RT3261_I2S_BCLK_MS2_SFT |
			pre_div << RT3261_I2S_PD2_SFT;
		snd_soc_update_bits(codec, RT3261_I2S2_SDP,
			RT3261_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT3261_ADDA_CLK1, mask_clk, val_clk);
	}

	return 0;
}

static int rt3261_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	rt3261->aif_pu = dai->id;
	return 0;
}

static int rt3261_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0, dai_sel;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt3261->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT3261_I2S_MS_S;
		rt3261->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT3261_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT3261_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT3261_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT3261_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	dai_sel = get_sdp_info(codec, dai->id);
	if (dai_sel < 0) {
		dev_err(codec->dev, "Failed to get sdp info: %d\n", dai_sel);
		return -EINVAL;
	}
	if (dai_sel & RT3261_U_IF1) {
		snd_soc_update_bits(codec, RT3261_I2S1_SDP,
			RT3261_I2S_MS_MASK | RT3261_I2S_BP_MASK |
			RT3261_I2S_DF_MASK, reg_val);
	}
	if (dai_sel & RT3261_U_IF2) {
		snd_soc_update_bits(codec, RT3261_I2S2_SDP,
			RT3261_I2S_MS_MASK | RT3261_I2S_BP_MASK |
			RT3261_I2S_DF_MASK, reg_val);
	}

	return 0;
}

static int rt3261_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt3261->sysclk && clk_id == rt3261->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT3261_SCLK_S_MCLK:
		reg_val |= RT3261_SCLK_SRC_MCLK;
		break;
	case RT3261_SCLK_S_PLL1:
		reg_val |= RT3261_SCLK_SRC_PLL1;
		break;
	case RT3261_SCLK_S_RCCLK:
		reg_val |= RT3261_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT3261_GLB_CLK,
		RT3261_SCLK_SRC_MASK, reg_val);
	rt3261->sysclk = freq;
	rt3261->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

/**
 * rt3261_pll_calc - Calcualte PLL M/N/K code.
 * @freq_in: external clock provided to codec.
 * @freq_out: target clock which codec works on.
 * @pll_code: Pointer to structure with M, N, K and bypass flag.
 *
 * Calcualte M/N/K code to configure PLL for codec. And K is assigned to 2
 * which make calculation more efficiently.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rt3261_pll_code *pll_code)
{
	int max_n = RT3261_PLL_N_MAX, max_m = RT3261_PLL_M_MAX;
	int n, m, red, n_t, m_t, in_t, out_t, red_t = abs(freq_out - freq_in);
	bool bypass = false;

	if (RT3261_PLL_INP_MAX < freq_in || RT3261_PLL_INP_MIN > freq_in)
		return -EINVAL;

	for (n_t = 0; n_t <= max_n; n_t++) {
		in_t = (freq_in >> 1) + (freq_in >> 2) * n_t;
		if (in_t < 0)
			continue;
		if (in_t == freq_out) {
			bypass = true;
			n = n_t;
			goto code_find;
		}
		for (m_t = 0; m_t <= max_m; m_t++) {
			out_t = in_t / (m_t + 2);
			red = abs(out_t - freq_out);
			if (red < red_t) {
				n = n_t;
				m = m_t;
				if (red == 0)
					goto code_find;
				red_t = red;
			}
		}
	}
	pr_debug("Only get approximation about PLL\n");

code_find:

	pll_code->m_bp = bypass;
	pll_code->m_code = m;
	pll_code->n_code = n;
	pll_code->k_code = 2;
	return 0;
}

static int rt3261_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	struct rt3261_pll_code pll_code;
	int ret, dai_sel;

	if (source == rt3261->pll_src && freq_in == rt3261->pll_in &&
	    freq_out == rt3261->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt3261->pll_in = 0;
		rt3261->pll_out = 0;
		snd_soc_update_bits(codec, RT3261_GLB_CLK,
			RT3261_SCLK_SRC_MASK, RT3261_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT3261_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT3261_GLB_CLK,
			RT3261_PLL1_SRC_MASK, RT3261_PLL1_SRC_MCLK);
		break;
	case RT3261_PLL1_S_BCLK1:
	case RT3261_PLL1_S_BCLK2:
		dai_sel = get_sdp_info(codec, dai->id);
		if (dai_sel < 0) {
			dev_err(codec->dev,
				"Failed to get sdp info: %d\n", dai_sel);
			return -EINVAL;
		}
		if (dai_sel & RT3261_U_IF1) {
			snd_soc_update_bits(codec, RT3261_GLB_CLK,
				RT3261_PLL1_SRC_MASK, RT3261_PLL1_SRC_BCLK1);
		}
		if (dai_sel & RT3261_U_IF2) {
			snd_soc_update_bits(codec, RT3261_GLB_CLK,
				RT3261_PLL1_SRC_MASK, RT3261_PLL1_SRC_BCLK2);
		}
		if (dai_sel & RT3261_U_IF3) {
			snd_soc_update_bits(codec, RT3261_GLB_CLK,
				RT3261_PLL1_SRC_MASK, RT3261_PLL1_SRC_BCLK3);
		}
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rt3261_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=2\n", pll_code.m_bp,
		(pll_code.m_bp ? 0 : pll_code.m_code), pll_code.n_code);

	snd_soc_write(codec, RT3261_PLL_CTRL1,
		pll_code.n_code << RT3261_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT3261_PLL_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT3261_PLL_M_SFT |
		pll_code.m_bp << RT3261_PLL_M_BP_SFT);

	rt3261->pll_in = freq_in;
	rt3261->pll_out = freq_out;
	rt3261->pll_src = source;

	return 0;
}

/**
 * rt3261_index_show - Dump private registers.
 * @dev: codec device.
 * @attr: device attribute.
 * @buf: buffer for display.
 *
 * To show non-zero values of all private registers.
 *
 * Returns buffer length.
 */
static ssize_t rt3261_index_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt3261_priv *rt3261 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt3261->codec;
	unsigned int val;
	int cnt = 0, i;

	cnt += sprintf(buf, "RT3261 index register\n");
	for (i = 0; i < 0xb4; i++) {
		if (cnt + RT3261_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = rt3261_index_read(codec, i);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT3261_REG_DISP_LEN,
				"%02x: %04x\n", i, val);
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	return cnt;
}
static DEVICE_ATTR(index_reg, 0444, rt3261_index_show, NULL);

static int rt3261_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* headphone mute sequence */
		snd_soc_update_bits(codec, RT3261_DEPOP_M3,
			RT3261_CP_FQ1_MASK | RT3261_CP_FQ2_MASK | RT3261_CP_FQ3_MASK,
			(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ1_SFT) |
			(RT3261_CP_FQ_12_KHZ << RT3261_CP_FQ2_SFT) |
			(RT3261_CP_FQ_96_KHZ << RT3261_CP_FQ3_SFT));
		rt3261_index_write(codec, RT3261_MAMP_INT_REG2, 0xfc00);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_HP_SG_MASK, RT3261_HP_SG_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTP_MASK, RT3261_RSTP_EN);
		snd_soc_update_bits(codec, RT3261_DEPOP_M1,
			RT3261_RSTP_MASK | RT3261_HP_L_SMT_MASK |
			RT3261_HP_R_SMT_MASK, RT3261_RSTP_DIS |
			RT3261_HP_L_SMT_EN | RT3261_HP_R_SMT_EN);

		snd_soc_update_bits(codec, RT3261_HP_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE, RT3261_L_MUTE | RT3261_R_MUTE);
		msleep(30);
		snd_soc_write(codec, RT3261_DEPOP_M1, 0x0004);

		snd_soc_update_bits(codec, RT3261_SPK_VOL,
			RT3261_L_MUTE | RT3261_R_MUTE,
			RT3261_L_MUTE | RT3261_R_MUTE);
		snd_soc_update_bits(codec, RT3261_PWR_ANLG2,
			RT3261_PWR_MB1 | RT3261_PWR_MB2,
			RT3261_PWR_MB1 | RT3261_PWR_MB2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (SND_SOC_BIAS_OFF == codec->dapm.bias_level) {
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_VREF1 | RT3261_PWR_MB |
				RT3261_PWR_BG | RT3261_PWR_VREF2,
				RT3261_PWR_VREF1 | RT3261_PWR_MB |
				RT3261_PWR_BG | RT3261_PWR_VREF2);
			msleep(10);
			snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
				RT3261_PWR_FV1 | RT3261_PWR_FV2,
				RT3261_PWR_FV1 | RT3261_PWR_FV2);
			snd_soc_write(codec, RT3261_GEN_CTRL1, 0x3701);
			codec->cache_only = false;
			codec->cache_sync = 1;
			snd_soc_cache_sync(codec);
			rt3261_index_sync(codec);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, RT3261_DEPOP_M1, 0x0004);
		snd_soc_write(codec, RT3261_DEPOP_M2, 0x1100);
		snd_soc_write(codec, RT3261_GEN_CTRL1, 0x3700);
		snd_soc_write(codec, RT3261_PWR_DIG1, 0x0000);
		snd_soc_write(codec, RT3261_PWR_DIG2, 0x0000);
		snd_soc_write(codec, RT3261_PWR_VOL, 0x0000);
		snd_soc_write(codec, RT3261_PWR_MIXER, 0x0000);
		snd_soc_write(codec, RT3261_PWR_ANLG1, 0x0000);
		snd_soc_write(codec, RT3261_PWR_ANLG2, 0x0000);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}
	
static int rt3261_proc_init(void);


static int rt3261_probe(struct snd_soc_codec *codec)
{
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	int ret;
	struct clk *iis_clk;

	#if defined (CONFIG_SND_SOC_RT3224)
	pr_info("Codec driver version %s, in fact you choose rt3224, no dsp!\n", VERSION);
	#else
	pr_info("Codec driver version %s, in fact you choose rt3261 with a dsp!\n", VERSION);
	#endif

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	codec->write = rt3261_write;
	
	#ifdef RT3261_PROC	
	rt3261_proc_init();
	#endif

	#if defined (CONFIG_SND_SOC_RT5623)
	//for rt5623 MCLK use
	iis_clk = clk_get_sys("rk29_i2s.2", "i2s");
	if (IS_ERR(iis_clk)) {
		DBG("failed to get i2s clk\n");
		ret = PTR_ERR(iis_clk);
	}else{
		DBG("I2S2 got i2s clk ok!\n");
		clk_enable(iis_clk);
		clk_set_rate(iis_clk, 11289600);
		rk30_mux_api_set(GPIO0D0_I2S22CHCLK_SMCCSN0_NAME, GPIO0D_I2S2_2CH_CLK);
		clk_put(iis_clk);
	}
	#endif
	
	rt3261_reset(codec);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_VREF1 | RT3261_PWR_MB |
		RT3261_PWR_BG | RT3261_PWR_VREF2,
		RT3261_PWR_VREF1 | RT3261_PWR_MB |
		RT3261_PWR_BG | RT3261_PWR_VREF2);
	msleep(10);
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
		RT3261_PWR_FV1 | RT3261_PWR_FV2,
		RT3261_PWR_FV1 | RT3261_PWR_FV2);
	/* DMIC */
	if (rt3261->dmic_en == RT3261_DMIC1) {
		snd_soc_update_bits(codec, RT3261_GPIO_CTRL1,
			RT3261_GP2_PIN_MASK, RT3261_GP2_PIN_DMIC1_SCL);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_1L_LH_MASK | RT3261_DMIC_1R_LH_MASK,
			RT3261_DMIC_1L_LH_FALLING | RT3261_DMIC_1R_LH_RISING);
	} else if (rt3261->dmic_en == RT3261_DMIC2) {
		snd_soc_update_bits(codec, RT3261_GPIO_CTRL1,
			RT3261_GP2_PIN_MASK, RT3261_GP2_PIN_DMIC1_SCL);
		snd_soc_update_bits(codec, RT3261_DMIC,
			RT3261_DMIC_2L_LH_MASK | RT3261_DMIC_2R_LH_MASK,
			RT3261_DMIC_2L_LH_FALLING | RT3261_DMIC_2R_LH_RISING);
	}
	snd_soc_write(codec, RT3261_GEN_CTRL2, 0x4040);
	ret = snd_soc_read(codec, RT3261_VENDOR_ID);
	printk("read codec chip id is 0x%x\n",ret);
	if(0x5==ret) {
		snd_soc_update_bits(codec, RT3261_JD_CTRL, 
			RT3261_JD1_IN4P_MASK | RT3261_JD2_IN4N_MASK,
			RT3261_JD1_IN4P_EN | RT3261_JD2_IN4N_EN);
	}
	else if(0x3==ret)
	{
		printk("you use an old chip, please use a new one\n");
	}
	snd_soc_update_bits(codec, RT3261_PWR_ANLG1,
			RT3261_PWR_HP_L | RT3261_PWR_HP_R,
			0<<7 | 0<<6 );
	rt3261_reg_init(codec);
	rt3261_customer_redefine(codec, rt3261);

	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
	rt3261->codec = codec;

	snd_soc_add_controls(codec, rt3261_snd_controls,
			ARRAY_SIZE(rt3261_snd_controls));
	snd_soc_dapm_new_controls(&codec->dapm, rt3261_dapm_widgets,
			ARRAY_SIZE(rt3261_dapm_widgets));
	snd_soc_dapm_add_routes(&codec->dapm, rt3261_dapm_routes,
			ARRAY_SIZE(rt3261_dapm_routes));


#if defined (CONFIG_SND_SOC_RT3261)
	rt3261->dsp_sw = RT3261_DSP_AEC_NS_FENS;
	rt3261_dsp_probe(codec);
#endif

#ifdef RTK_IOCTL
#if defined(CONFIG_SND_HWDEP) || defined(CONFIG_SND_HWDEP_MODULE)
	struct rt_codec_ops *ioctl_ops = rt_codec_get_ioctl_ops();
	ioctl_ops->index_write = rt3261_index_write;
	ioctl_ops->index_read = rt3261_index_read;
	ioctl_ops->index_update_bits = rt3261_index_update_bits;
	ioctl_ops->ioctl_common = rt3261_ioctl_common;
	realtek_ce_init_hwdep(codec);
#endif
#endif


	ret = device_create_file(codec->dev, &dev_attr_index_reg);
	if (ret != 0) {
		dev_err(codec->dev,
			"Failed to create index_reg sysfs files: %d\n", ret);
		return ret;
	}
	rt3261_codec = codec;
	return 0;
}

static int rt3261_remove(struct snd_soc_codec *codec)
{
	rt3261_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM
static int rt3261_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
#if defined (CONFIG_SND_SOC_RT3261)
	/* After opening LDO of DSP, then close LDO of codec.
	 * (1) DSP LDO power on
	 * (2) DSP core power off
	 * (3) DSP IIS interface power off
	 * (4) Toggle pin of codec LDO1 to power off
	 */
	rt3261_dsp_suspend(codec, state);
#endif
	rt3261_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt3261_resume(struct snd_soc_codec *codec)
{
	rt3261_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#if defined (CONFIG_SND_SOC_RT3261)
	/* After opening LDO of codec, then close LDO of DSP. */
	rt3261_dsp_resume(codec);
#endif
	return 0;
}
#else
#define rt3261_suspend NULL
#define rt3261_resume NULL
#endif

#define RT3261_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT3261_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt3261_aif_dai_ops = {
	.hw_params = rt3261_hw_params,
	.prepare = rt3261_prepare,
	.set_fmt = rt3261_set_dai_fmt,
	.set_sysclk = rt3261_set_dai_sysclk,
	.set_pll = rt3261_set_dai_pll,
};

struct snd_soc_dai_driver rt3261_dai[] = {
	{
		.name = "rt3261-aif1",
		.id = RT3261_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT3261_STEREO_RATES,
			.formats = RT3261_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT3261_STEREO_RATES,
			.formats = RT3261_FORMATS,
		},
		.ops = &rt3261_aif_dai_ops,
	},
	{
		.name = "rt3261-aif2",
		.id = RT3261_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT3261_STEREO_RATES,
			.formats = RT3261_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT3261_STEREO_RATES,
			.formats = RT3261_FORMATS,
		},
		.ops = &rt3261_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt3261 = {
	.probe = rt3261_probe,
	.remove = rt3261_remove,
	.suspend = rt3261_suspend,
	.resume = rt3261_resume,
	.write = rt3261_write,
	.set_bias_level = rt3261_set_bias_level,
	.reg_cache_size = RT3261_VENDOR_ID2 + 1,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = rt3261_reg,
	.volatile_register = rt3261_volatile_register,
	.readable_register = rt3261_readable_register,
	.reg_cache_step = 1,
};

static const struct i2c_device_id rt3261_i2c_id[] = {
	{ "rt3261", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt3261_i2c_id);

static int __devinit rt3261_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt3261_priv *rt3261;
	int ret;
	struct rt3261_platform_data *pdata = pdata = i2c->dev.platform_data;

	rt3261 = kzalloc(sizeof(struct rt3261_priv), GFP_KERNEL);
	if (NULL == rt3261)
		return -ENOMEM;

	rt3261->codec_en_gpio = pdata->codec_en_gpio;
	rt3261->io_init = pdata->io_init;
	rt3261->spk_num = pdata->spk_num;
	rt3261->modem_input_mode = pdata->modem_input_mode;
	rt3261->lout_to_modem_mode = pdata->lout_to_modem_mode;
	rt3261->spk_amplify = pdata->spk_amplify;
	rt3261->playback_if1_data_control = pdata->playback_if1_data_control;
	rt3261->playback_if2_data_control = pdata->playback_if2_data_control;

	if(rt3261->io_init)
		rt3261->io_init(pdata->codec_en_gpio, pdata->codec_en_gpio_info.iomux_name, pdata->codec_en_gpio_info.iomux_mode);

	#if defined (CONFIG_SND_SOC_RT5623)
	rt3261->modem_is_open = 0;
	#endif

	i2c_set_clientdata(i2c, rt3261);
	DBG("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt3261,
			rt3261_dai, ARRAY_SIZE(rt3261_dai));
	if (ret < 0)
		kfree(rt3261);

	return ret;
}

static int __devexit rt3261_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	kfree(i2c_get_clientdata(i2c));
	return 0;
}

static void rt3261_i2c_shutdown(struct i2c_client *client)
{
	struct rt3261_priv *rt3261 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt3261->codec;

	if (codec != NULL)
		rt3261_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

struct i2c_driver rt3261_i2c_driver = {
	.driver = {
		.name = "rt3261",
		.owner = THIS_MODULE,
	},
	.probe = rt3261_i2c_probe,
	.remove   = __devexit_p(rt3261_i2c_remove),
	.shutdown = rt3261_i2c_shutdown,
	.id_table = rt3261_i2c_id,
};

static int __init rt3261_modinit(void)
{
	return i2c_add_driver(&rt3261_i2c_driver);
}
module_init(rt3261_modinit);

static void __exit rt3261_modexit(void)
{
	i2c_del_driver(&rt3261_i2c_driver);
}
module_exit(rt3261_modexit);

MODULE_DESCRIPTION("ASoC RT3261 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL");


#ifdef RT3261_PROC

static ssize_t rt3261_proc_write(struct file *file, const char __user *buffer,
		unsigned long len, void *data)
{
	char *cookie_pot; 
	char *p;
	int reg;
	int i;
	int value;
	#if defined (CONFIG_SND_SOC_RT3261)
	struct rt3261_dsp_param param;
	#endif

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
		case 'r':
		case 'R':
			printk("Read reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,",")))
				{
					reg = simple_strtol(p,NULL,16);
					value = rt3261_read(rt3261_codec,reg);
					printk("rt3261_read:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			else
			{
				printk("Error Read reg debug.\n");
				printk("For example: echo r:22,23,24,25>rt3261_ts\n");
			}
			break;
		case 'w':
		case 'W':
			printk("Write reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,"=")))
				{
					reg = simple_strtol(p,NULL,16);
					p=strsep(&cookie_pot,",");
					value = simple_strtol(p,NULL,16);
					rt3261_write(rt3261_codec,reg,value);
					printk("rt3261_write:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			else
			{
				printk("Error Write reg debug.\n");
				printk("For example: w:22=0,23=0,24=0,25=0>rt3261_ts\n");
			}
			break;
		case 'a':
			printk("Dump rt3261 index reg \n");		

			for (i = 0; i < 0xb4; i++) 
			{
				value = rt3261_index_read(rt3261_codec, i);
				printk("rt3261_index_read:0x%04x = 0x%04x\n",i,value);
			}
			break;	
		#if defined (CONFIG_SND_SOC_RT3261)
		case 'b':
			param.cmd_fmt =  0x00e0;
			param.cmd = RT3261_DSP_CMD_MW;
			printk("Write dsp reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,"=")))
				{
					param.addr = simple_strtol(p,NULL,16);
					p=strsep(&cookie_pot,",");
					param.data = simple_strtol(p,NULL,16);
					rt3261_dsp_write(rt3261_codec,&param);
					printk("rt3261_dsp_write:0x%04x = 0x%04x\n",param.addr,param.data);
				}
				printk("\n");
			}
			break;
		case 'c':
			printk("Read dsp reg debug\n");		
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,",")))
				{
					reg = simple_strtol(p,NULL,16);
					value = rt3261_dsp_read(rt3261_codec,reg);
					printk("rt3261_dsp_read:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			break;
		#endif
		case 'd':
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,"=")))
				{
					reg = simple_strtol(p,NULL,16);
					p=strsep(&cookie_pot,",");
					value = simple_strtol(p,NULL,16);
					rt3261_index_write(rt3261_codec,reg,value);
					printk("rt3261_index_write:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			break;
		case 'e':	
			if(cookie_pot[1] ==':')
			{
				strsep(&cookie_pot,":");
				while((p=strsep(&cookie_pot,",")))
				{
					reg = simple_strtol(p,NULL,16);
					value = rt3261_index_read(rt3261_codec,reg);
					printk("rt3261_index_read:0x%04x = 0x%04x\n",reg,value);
				}
				printk("\n");
			}
			break;
		default:
			printk("Help for rt3261_ts .\n-->The Cmd list: \n");
			printk("-->'d&&D' Open or Off the debug\n");
			printk("-->'r&&R' Read reg debug,Example: echo 'r:22,23,24,25'>rt3261_ts\n");
			printk("-->'w&&W' Write reg debug,Example: echo 'w:22=0,23=0,24=0,25=0'>rt3261_ts\n");
			break;
	}

	return len;
}

static const struct file_operations rt3261_proc_fops = {
	.owner		= THIS_MODULE,
};

static int rt3261_proc_init(void)
{
	struct proc_dir_entry *rt3261_proc_entry;
	rt3261_proc_entry = create_proc_entry("driver/rt3261_ts", 0777, NULL);
	if(rt3261_proc_entry != NULL)
	{
		rt3261_proc_entry->write_proc = rt3261_proc_write;
		return 0;
	}
	else
	{
		printk("create proc error !\n");
		return -1;
	}
}
#endif
