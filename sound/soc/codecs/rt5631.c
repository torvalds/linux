/*
 * rt5631.c  --  RT5631 ALSA Soc Audio driver
 *
 * Copyright 2009 Realtek Microelectronics
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

#include "rt5631.h"

#define RT5631_VERSION "0.01 alsa 1.0.21"
#define ALSA_SOC_VERSION "1.0.21"
static const u16 rt5631_reg[0x80];
static int timesofbclk = 32;
module_param(timesofbclk, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(timeofbclk, "relationship between bclk and fs");
	
#define VIRTUAL_POWER_CONTROL 0x90
/*
 * bit0: spkl amp power
 * bit1: spkr amp power
 * bit2: dmic flag  
 *  
*/
struct rt5631_priv {
	int master;
	int sysclk;
	int dmic_used_flag;	
};

static unsigned int reg90;
static int rt5631_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int val);
static unsigned int rt5631_read(struct snd_soc_codec *codec, unsigned int reg);
#define rt5631_reset(c) rt5631_write(c, RT5631_RESET, 0)
#define rt5631_write_mask(c, reg, val, mask) snd_soc_update_bits(c, reg, mask, val)
static int rt5631_reg_init(struct snd_soc_codec *codec);

static struct snd_soc_device *rt5631_socdev;
/*
 * read rt5631 register cache
 */
static inline unsigned int rt5631_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg < 1 || reg > (ARRAY_SIZE(rt5631_reg) + 1))
		return -1;
	return cache[reg];
}


/*
 * write rt5631 register cache
 */

static inline void rt5631_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg < 0 || reg > 0x7e)
		return;
	cache[reg] = value;
}

static int rt5631_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int val)
{
	u8 data[3];
	
	if (reg > 0x7e) {
		if (reg == 0x90) 
			reg90 = val;
	}
	
	data[0] = reg;
	data[1] = (0xff00 & val) >> 8;
	data[2] = (0xff & val);
	
	if (codec->hw_write(codec->control_data, data, 3) == 3) {
		rt5631_write_reg_cache(codec, reg, val);
		printk(KERN_INFO "%s reg=0x%x, val=0x%x\n", __func__, reg, val);
		return 0;	
	} else {
		printk(KERN_ERR "%s failed\n", __func__);
		return -EIO;	
	}
}

static unsigned int rt5631_read(struct snd_soc_codec *codec, unsigned int reg)
{
	u8 data[2] = {0};
	unsigned int value = 0x0;
	
	if (reg > 0x7e) {
		if (reg == 0x90)
		     return reg90;
	}
	
	data[0] = reg;
	
	i2c_master_reg8_recv(codec->control_data,reg,data,2,100 * 1000);	
	   	      
    value = (data[0]<<8) | data[1];         
    
    printk("rt5631_read reg%x=%x\n",reg,value);
       
    return value;	
}

#define rt5631_write_index_reg(c, addr, data) \
{ \
	rt5631_write(c, 0x6a, addr); \
	rt5631_write(c, 0x6c, data); \
}


struct rt5631_init_reg{
	u8 reg;
	u16 val;
};

static struct rt5631_init_reg init_list[] = {

	{RT5631_SPK_OUT_VOL			, 0xc8c8},		//speaker channel volume select SPKMIXER,0DB by default
	{RT5631_HP_OUT_VOL			, 0xc0c0},		//Headphone channel volume select OUTMIXER,0DB by default
	{RT5631_MONO_AXO_1_2_VOL	, 0xf0c0},		//AXO1/AXO2 channel volume select OUTMIXER,0DB by default
	{RT5631_ADC_REC_MIXER		, 0xb0f0},		//Record Mixer source from Mic1 by default
	{RT5631_MIC_CTRL_2			, 0x5500}, 		//Mic1/Mic2 boost 40DB by default
	{RT5631_OUTMIXER_L_CTRL		, 0xdfC0},		//DAC_L-->OutMixer_L by default
	{RT5631_OUTMIXER_R_CTRL		, 0xdfC0},		//DAC_R-->OutMixer_R by default
	{RT5631_AXO1MIXER_CTRL		, 0x8840},		//OutMixer_L-->AXO1Mixer by default
	{RT5631_AXO2MIXER_CTRL		, 0x8880},		//OutMixer_R-->AXO2Mixer by default
	{RT5631_SPK_MIXER_CTRL		, 0xd8d8},		//DAC-->SpeakerMixer
	{RT5631_SPK_MONO_OUT_CTRL	, 0x6c00},		//Speaker volume-->SPOMixer(L-->L,R-->R)	
	{RT5631_GEN_PUR_CTRL_REG	, 0x4e00},		//Speaker AMP ratio gain is 1.44X
	{RT5631_SPK_MONO_HP_OUT_CTRL, 0x0000},		//HP from OutMixer,speaker out from SpeakerOut Mixer
	{RT5631_DEPOP_FUN_CTRL_2	, 0x8000},		//HP depop by register control	
	{RT5631_INT_ST_IRQ_CTRL_2	, 0x0f18},		//enable HP zero cross	
	{RT5631_MIC_CTRL_1			, 0x8000},		//set mic 1 to differnetial mode
//	{RT5631_GPIO_CTRL			, 0x0000},		//set GPIO to input pin	
//	{RT5631_JACK_DET_CTRL		, 0x4e80},		//Jack detect for GPIO,high is HP,low is speaker	
//	{RT5631_JACK_DET_CTRL		, 0x4bc0},		//Jack detect for GPIO,high is speaker,low is hp	
};

#define RT5631_INIT_REG_LEN 	ARRAY_SIZE(init_list)

static int rt5631_reg_init(struct snd_soc_codec *codec)
{
	int i;
	
	for (i = 0; i < RT5631_INIT_REG_LEN; i ++) {
		rt5631_write(codec, init_list[i].reg, init_list[i].val);	
	}	
	
	return 0;
}

static const char *rt5631_spol_source_sel[] = {"SPOLMIX", "MONOIN_RX", "VDAC", "DACL"};
static const char *rt5631_spor_source_sel[] = {"SPORMIX", "MONOIN_RX", "VDAC", "DACR"};
static const char *rt5631_mono_source_sel[] = {"MONOMIX", "MONOIN_RX", "VDAC"};
static const char *rt5631_input_mode_source_sel[] = {"Single-end", "Differential"}; 
static const char *rt5631_mic_boost[] = {"Bypass", "+20db", "+24db", "+30db",	
			"+35db", "+40db", "+44db", "+50db", "+52db"};

static const struct soc_enum rt5631_enum[] = {
SOC_ENUM_SINGLE(RT5631_SPK_MONO_HP_OUT_CTRL, 14, 4, rt5631_spol_source_sel), 	 /*0*/
SOC_ENUM_SINGLE(RT5631_SPK_MONO_HP_OUT_CTRL, 10, 4, rt5631_spor_source_sel),  	/*1*/
SOC_ENUM_SINGLE(RT5631_SPK_MONO_HP_OUT_CTRL, 6, 3, rt5631_mono_source_sel),	/*2*/
SOC_ENUM_SINGLE(RT5631_MIC_CTRL_1, 15, 2,  rt5631_input_mode_source_sel),	/*3*/
SOC_ENUM_SINGLE(RT5631_MIC_CTRL_1, 7, 2,  rt5631_input_mode_source_sel),	/*4*/
SOC_ENUM_SINGLE(RT5631_MONO_INPUT_VOL, 15, 2, rt5631_input_mode_source_sel),	/*5*/
SOC_ENUM_SINGLE(RT5631_MIC_CTRL_2, 12, 9, rt5631_mic_boost),			/*6*/
SOC_ENUM_SINGLE(RT5631_MIC_CTRL_2, 8, 9, rt5631_mic_boost),			/*7*/
};

static int rt5631_dmic_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	val = rt5631_read(codec, VIRTUAL_POWER_CONTROL) & 0x0004;
	val >>= 2;
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static void rt5631_close_dmic(struct snd_soc_codec *codec)	//disable DMic to ADC filter 
{
	rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_L_CH_MUTE | DMIC_R_CH_MUTE, DMIC_L_CH_MUTE_MASK | DMIC_R_CH_MUTE_MASK); 

	rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_DIS, DMIC_ENA_MASK);
}

static int rt5631_dmic_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int old, new;
	struct rt5631_priv *rt5631 = codec->private_data;	

	old = rt5631_read(codec, VIRTUAL_POWER_CONTROL) & 0x0004;
	new = ucontrol->value.integer.value[0] << 2;

	if (old == new)
		return 0;

	rt5631_write_mask(codec, VIRTUAL_POWER_CONTROL, new, 0x0004);

	if (new)
		rt5631->dmic_used_flag = 1;
	else
	{		
		rt5631_close_dmic(codec);
		rt5631->dmic_used_flag=0;
	}

	return 0;
}

static const struct snd_kcontrol_new rt5631_snd_controls[] = {
SOC_ENUM("MIC1 Mode Control",  rt5631_enum[3]),   
SOC_ENUM("MIC1 Boost", rt5631_enum[6]),

SOC_ENUM("MIC2 Mode Control", rt5631_enum[4]),
SOC_ENUM("MIC2 Boost", rt5631_enum[7]),
SOC_ENUM("MONOIN Mode Control", rt5631_enum[5]),

SOC_DOUBLE("PCM Playback Volume", RT5631_STEREO_DAC_VOL_2, 8, 0, 255, 1),
SOC_DOUBLE("PCM Playback Switch", RT5631_STEREO_DAC_VOL_1,15, 7, 1, 1),

SOC_DOUBLE("MONOIN_RX Capture Volume", RT5631_MONO_INPUT_VOL, 8, 0, 31, 1),

SOC_DOUBLE("AXI Capture Volume", RT5631_AUX_IN_VOL, 8, 0, 31, 1),

SOC_SINGLE("AXO1 Playback Switch", RT5631_MONO_AXO_1_2_VOL, 15, 1, 1),
SOC_SINGLE("AXO2 Playback Switch", RT5631_MONO_AXO_1_2_VOL, 7, 1, 1),
SOC_DOUBLE("OUTVOL Playback Volume", RT5631_MONO_AXO_1_2_VOL, 8, 0, 31, 1),

SOC_DOUBLE("Speaker Playback Switch", RT5631_SPK_OUT_VOL,15, 7, 1, 1),
SOC_DOUBLE("Speaker Playback Volume", RT5631_SPK_OUT_VOL, 8, 0, 63, 1),

SOC_SINGLE("MONO Playback Switch", RT5631_MONO_AXO_1_2_VOL, 13, 1, 1),

SOC_DOUBLE("HP Playback Switch", RT5631_HP_OUT_VOL,15, 7, 1, 1),
SOC_DOUBLE("HP Playback Volume", RT5631_HP_OUT_VOL, 8, 0, 63, 1),

//SOC_SINGLE_EXT("HIFI Loopback", ),/*not finished*/
//SOC_SINGLE_EXT("Voice Loopback", ), /*not finished*/
SOC_SINGLE_EXT("DMIC Capture Switch", VIRTUAL_POWER_CONTROL, 2, 1, 0, 
	rt5631_dmic_get, rt5631_dmic_put),
};

static int rt5631_add_controls(struct snd_soc_codec *codec)
{
	int err, i;
	
	for (i = 0; i < ARRAY_SIZE(rt5631_snd_controls); i ++) {

		err = snd_ctl_add(codec->card, 
								snd_soc_cnew(&rt5631_snd_controls[i],
												codec, NULL));
		if (err < 0)
			return err;	
	}	
	
	return 0;
}

static const struct snd_kcontrol_new rt5631_recmixl_mixer_controls[] = {
SOC_DAPM_SINGLE("OUTMIXL Capture Switch", RT5631_ADC_REC_MIXER, 15, 1, 1),
SOC_DAPM_SINGLE("MIC1_BST1 Capture Switch", RT5631_ADC_REC_MIXER, 14, 1, 1),
SOC_DAPM_SINGLE("AXILVOL Capture Switch", RT5631_ADC_REC_MIXER, 13, 1, 1),
SOC_DAPM_SINGLE("MONOIN_RX Capture Switch", RT5631_ADC_REC_MIXER, 12, 1, 1),
};

static const struct snd_kcontrol_new rt5631_recmixr_mixer_controls[] = {
SOC_DAPM_SINGLE("MONOIN_RX Capture Switch", RT5631_ADC_REC_MIXER, 4, 1, 1),
SOC_DAPM_SINGLE("AXIRVOL Capture Switch", RT5631_ADC_REC_MIXER, 5, 1, 1),
SOC_DAPM_SINGLE("MIC2_BST2 Capture Switch", RT5631_ADC_REC_MIXER, 6, 1, 1),
SOC_DAPM_SINGLE("OUTMIXR Capture Switch", RT5631_ADC_REC_MIXER, 7, 1, 1),
};


static const struct snd_kcontrol_new rt5631_spkmixl_mixer_controls[] = {
SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_SPK_MIXER_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC1_P Playback Switch", RT5631_SPK_MIXER_CTRL, 14, 1, 1),	
SOC_DAPM_SINGLE("DACL Playback Switch", RT5631_SPK_MIXER_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("OUTMIXL Playback Switch", RT5631_SPK_MIXER_CTRL, 12, 1, 1)
};

static const struct snd_kcontrol_new rt5631_spkmixr_mixer_controls[] = {
SOC_DAPM_SINGLE("OUTMIXR Playback Switch", RT5631_SPK_MIXER_CTRL, 4, 1, 1),
SOC_DAPM_SINGLE("DACR Playback Switch", RT5631_SPK_MIXER_CTRL, 5, 1, 1),
SOC_DAPM_SINGLE("MIC2_P Playback Switch", RT5631_SPK_MIXER_CTRL, 6, 1, 1),
SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_SPK_MIXER_CTRL, 7, 1, 1),
};

static const struct snd_kcontrol_new rt5631_outmixl_mixer_controls[] = {
SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_OUTMIXER_L_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_OUTMIXER_L_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("DACL Playback Switch", RT5631_OUTMIXER_L_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_OUTMIXER_L_CTRL, 12, 1, 1),
SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_OUTMIXER_L_CTRL, 11, 1, 1),
SOC_DAPM_SINGLE("MONOIN_RXP Playback Switch", RT5631_OUTMIXER_L_CTRL, 10, 1, 1),
SOC_DAPM_SINGLE("AXILVOL Playback Switch", RT5631_OUTMIXER_L_CTRL, 9, 1, 1),
SOC_DAPM_SINGLE("AXIRVOL Playback Switch", RT5631_OUTMIXER_L_CTRL, 8, 1, 1),
SOC_DAPM_SINGLE("VDAC Playback Switch", RT5631_OUTMIXER_L_CTRL, 7, 1, 1)
};

static const struct snd_kcontrol_new rt5631_outmixr_mixer_controls[] = {
SOC_DAPM_SINGLE("VDAC Playback Switch", RT5631_OUTMIXER_R_CTRL, 7, 1, 1),
SOC_DAPM_SINGLE("AXIRVOL Playback Switch", RT5631_OUTMIXER_R_CTRL, 8, 1, 1),
SOC_DAPM_SINGLE("AXILVOL Playback Switch", RT5631_OUTMIXER_R_CTRL, 9, 1, 1),
SOC_DAPM_SINGLE("MONOIN_RXN Playback Switch", RT5631_OUTMIXER_R_CTRL, 10, 1, 1),
SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_OUTMIXER_R_CTRL, 11, 1, 1),
SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_OUTMIXER_R_CTRL, 12, 1, 1),	
SOC_DAPM_SINGLE("DACR Playback Switch", RT5631_OUTMIXER_R_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("RECMIXR Playback Switch", RT5631_OUTMIXER_R_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("RECMIXL Playback Switch", RT5631_OUTMIXER_R_CTRL, 15, 1, 1),
};


static const struct snd_kcontrol_new rt5631_AXO1MIX_mixer_controls[] = {
SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_AXO1MIXER_CTRL, 15 , 1, 1),
SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_AXO1MIXER_CTRL, 11, 1, 1), 	
SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_AXO1MIXER_CTRL, 7 ,1 ,1),
SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_AXO1MIXER_CTRL, 6, 1, 1),
};


static const struct snd_kcontrol_new rt5631_AXO2MIX_mixer_controls[] = {
SOC_DAPM_SINGLE("MIC1_BST1 Playback Switch", RT5631_AXO2MIXER_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC2_BST2 Playback Switch", RT5631_AXO2MIXER_CTRL, 11, 1, 1),	 
SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_AXO2MIXER_CTRL, 7, 1, 1),
SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_AXO2MIXER_CTRL, 6, 1 ,1),		
};

static const struct snd_kcontrol_new rt5631_spolmix_mixer_controls[] = {
SOC_DAPM_SINGLE("SPKVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("SPKVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 14, 1, 1),	
};


static const struct snd_kcontrol_new rt5631_spormix_mixer_controls[] = {
SOC_DAPM_SINGLE("SPKVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("SPKVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 12, 1, 1),
};


static const struct snd_kcontrol_new rt5631_monomix_mixer_controls[] = {
SOC_DAPM_SINGLE("OUTVOLL Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 11, 1, 1),
SOC_DAPM_SINGLE("OUTVOLR Playback Switch", RT5631_SPK_MONO_OUT_CTRL, 10, 1, 1),
};


static const struct snd_kcontrol_new rt5631_spol_mux_control = 
SOC_DAPM_ENUM("Route", rt5631_enum[0]);
static const struct snd_kcontrol_new rt5631_spor_mux_control = 
SOC_DAPM_ENUM("Route", rt5631_enum[1]);
static const struct snd_kcontrol_new rt5631_mono_mux_control = 
SOC_DAPM_ENUM("Route", rt5631_enum[2]);



static int spk_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int l, r;
	
	l = rt5631_read(codec, VIRTUAL_POWER_CONTROL) & 0x0001;
	r = (rt5631_read(codec, VIRTUAL_POWER_CONTROL) & 0x0002) >> 1;
	
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		printk("spk_event --SND_SOC_DAPM_POST_PMD\n");
			rt5631_write_mask(codec, RT5631_SPK_OUT_VOL, 0x8080, 0x8080);

		if ((l == 0) && (r == 0))
			rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD1, 0x0000, 0x1000);
			
			rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD4, 0x0000, 0xC000);
		break;
	case SND_SOC_DAPM_POST_PMU:
		printk("spk_event --SND_SOC_DAPM_POST_PMU\n");
		if ((l != 0) || (r != 0))
			rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD1, 0x1000, 0x1000);
			rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD4, 0xC000, 0xC000);
			rt5631_write_mask(codec, RT5631_SPK_OUT_VOL, 0x0000, 0x8080);
		break;
	default:
		return -EINVAL;	
	}
	
	return 0;
}


static void hp_depop2(struct snd_soc_codec *codec,unsigned int EnableHPOut)
{	

	unsigned int SoftVol,HPZeroCross;

	SoftVol = rt5631_read(codec, RT5631_SOFT_VOL_CTRL);
	rt5631_write(codec,RT5631_SOFT_VOL_CTRL,0);
	HPZeroCross=rt5631_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	rt5631_write(codec,RT5631_INT_ST_IRQ_CTRL_2,HPZeroCross&0xf7ff);	//disable Zero Cross of HP
	//enable HP out
	if(EnableHPOut)
	{		
		
		rt5631_write_index_reg(codec,0x56,0x303e);
		
		rt5631_write_mask(codec,RT5631_PWR_MANAG_ADD3,PWR_CHARGE_PUMP|PWR_HP_L_AMP|PWR_HP_R_AMP
												 ,PWR_CHARGE_PUMP|PWR_HP_L_AMP|PWR_HP_R_AMP);												 
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|EN_DEPOP2_FOR_HP);
		schedule_timeout_uninterruptible(msecs_to_jiffies(100));	
		rt5631_write_mask(codec,RT5631_PWR_MANAG_ADD3,PWR_HP_DEPOP_DIS|PWR_HP_AMP_DRIVING,PWR_HP_DEPOP_DIS|PWR_HP_AMP_DRIVING);
	}
	else	//disable HP out
	{
		rt5631_write_index_reg(codec,0x56,0x303F);	
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|EN_MUTE_UNMUTE_DEPOP|PD_HPAMP_L_ST_UP|PD_HPAMP_R_ST_UP);
		schedule_timeout_uninterruptible(msecs_to_jiffies(75));
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|PD_HPAMP_L_ST_UP|PD_HPAMP_R_ST_UP);
		rt5631_write_mask(codec,RT5631_PWR_MANAG_ADD3,0,PWR_HP_DEPOP_DIS|PWR_HP_AMP_DRIVING);	
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|EN_DEPOP2_FOR_HP|PD_HPAMP_L_ST_UP|PD_HPAMP_R_ST_UP);
		schedule_timeout_uninterruptible(msecs_to_jiffies(80));	
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN);
		rt5631_write_mask(codec,RT5631_PWR_MANAG_ADD3,0,PWR_CHARGE_PUMP|PWR_HP_L_AMP|PWR_HP_R_AMP);
	}
	
	rt5631_write(codec,RT5631_SOFT_VOL_CTRL,SoftVol);
	rt5631_write(codec,RT5631_INT_ST_IRQ_CTRL_2,HPZeroCross);	

}

static void HP_Mute_Unmute_Depop(struct snd_soc_codec *codec,unsigned int EnableHPOut)
{

	unsigned int SoftVol,HPZeroCross;

	SoftVol = rt5631_read(codec, RT5631_SOFT_VOL_CTRL);
	rt5631_write(codec,RT5631_SOFT_VOL_CTRL,0);
	HPZeroCross=rt5631_read(codec, RT5631_INT_ST_IRQ_CTRL_2);
	rt5631_write(codec,RT5631_INT_ST_IRQ_CTRL_2,HPZeroCross&0xf7ff);	//disable Zero Cross of HP

	if(EnableHPOut)	//unmute HP out
	{
		schedule_timeout_uninterruptible(msecs_to_jiffies(10));
		rt5631_write_index_reg(codec,0x56,0x302f);				
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|EN_MUTE_UNMUTE_DEPOP|EN_HP_R_M_UN_MUTE_DEPOP|EN_HP_L_M_UN_MUTE_DEPOP);
		rt5631_write_mask(codec,RT5631_HP_OUT_VOL,0x0000,0x8080);
		schedule_timeout_uninterruptible(msecs_to_jiffies(160));	
		
	}
	else		//mute HP out
	{	
		rt5631_write_index_reg(codec,0x56,0x302f);	
		rt5631_write(codec,RT5631_DEPOP_FUN_CTRL_1,POW_ON_SOFT_GEN|EN_MUTE_UNMUTE_DEPOP|EN_HP_R_M_UN_MUTE_DEPOP|EN_HP_L_M_UN_MUTE_DEPOP);				
		rt5631_write_mask(codec,RT5631_HP_OUT_VOL,0x8080,0x8080);			
		schedule_timeout_uninterruptible(msecs_to_jiffies(150));
	}
	
	rt5631_write(codec,RT5631_SOFT_VOL_CTRL,SoftVol);
	rt5631_write(codec,RT5631_INT_ST_IRQ_CTRL_2,HPZeroCross);

}

static int open_hp_end_widgets(struct snd_soc_codec *codec)
{
	/*need to be fixed*/
	/*
 	* 
 	*open hp last widget, e.g. power, switch
 	*/

	HP_Mute_Unmute_Depop(codec,1);

	return 0;
}

static int close_hp_end_widgets(struct snd_soc_codec *codec)
{
	/*need to be fixed*/
	/*
 	*
	*close hp last widget, e.g. power, switch
	*/

	HP_Mute_Unmute_Depop(codec,0);

	return 0;
}
static int hp_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int l, r;
	static unsigned int hp_out_enable=0;
	
	l = (rt5631_read(codec, RT5631_PWR_MANAG_ADD4) & (0x01 << 11)) >> 11;
	r = (rt5631_read(codec, RT5631_PWR_MANAG_ADD4) & (0x01 << 10)) >> 10;
	
	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		printk("hp_event --SND_SOC_DAPM_PRE_PMD\n");	
		if ((l && r)&&(hp_out_enable))  
		{
			close_hp_end_widgets(codec);
			hp_depop2(codec,0);	
			hp_out_enable=0;
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		printk("hp_event --SND_SOC_DAPM_POST_PMU\n");	
		if ((l && r)&&(!hp_out_enable))
		{
			hp_depop2(codec,1);
		open_hp_end_widgets(codec);
			hp_out_enable=1;
		}
		break;
	default:
		return -EINVAL;	
	}
	
	return 0;
}


static const struct snd_soc_dapm_widget rt5631_dapm_widgets[] = {

SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_INPUT("AXIL"),
SND_SOC_DAPM_INPUT("AXIR"),
SND_SOC_DAPM_INPUT("MONOIN_RXN"),
SND_SOC_DAPM_INPUT("MONOIN_RXP"),


SND_SOC_DAPM_PGA("Mic1 Boost", RT5631_PWR_MANAG_ADD2, 5, 0, NULL, 0),  

SND_SOC_DAPM_PGA("Mic2 Boost", RT5631_PWR_MANAG_ADD2, 4, 0, NULL, 0), 

SND_SOC_DAPM_PGA("MONOIN_RXP Boost", RT5631_PWR_MANAG_ADD4, 7, 0, NULL, 0), 
SND_SOC_DAPM_PGA("MONOIN_RXN Boost", RT5631_PWR_MANAG_ADD4, 6, 0, NULL, 0), 

SND_SOC_DAPM_PGA("AXIL Boost", RT5631_PWR_MANAG_ADD4, 9, 0, NULL, 0), 
SND_SOC_DAPM_PGA("AXIR Boost", RT5631_PWR_MANAG_ADD4, 8, 0, NULL, 0),
SND_SOC_DAPM_MIXER("MONO_IN", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("RECMIXL Mixer", RT5631_PWR_MANAG_ADD2, 11, 0, 
	&rt5631_recmixl_mixer_controls[0], ARRAY_SIZE(rt5631_recmixl_mixer_controls)),
SND_SOC_DAPM_MIXER("RECMIXR Mixer", RT5631_PWR_MANAG_ADD2, 10, 0, 
	&rt5631_recmixr_mixer_controls[0], ARRAY_SIZE(rt5631_recmixr_mixer_controls)),

SND_SOC_DAPM_MIXER("ADC Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_ADC("Left ADC", "Left ADC HIFI Capture", RT5631_PWR_MANAG_ADD1, 11, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right ADC HIFI Capture", RT5631_PWR_MANAG_ADD1, 10, 0),
SND_SOC_DAPM_DAC("Left DAC", "Left DAC HIFI Playback", RT5631_PWR_MANAG_ADD1, 9, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right DAC HIFI Playback", RT5631_PWR_MANAG_ADD1, 8, 0),
SND_SOC_DAPM_DAC("Voice DAC", "Voice DAC Mono Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_PGA("Voice DAC Boost", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("SPKMIXL Mixer", RT5631_PWR_MANAG_ADD2, 13, 0, 
	&rt5631_spkmixl_mixer_controls[0], ARRAY_SIZE(rt5631_spkmixl_mixer_controls)),
SND_SOC_DAPM_MIXER("OUTMIXL Mixer", RT5631_PWR_MANAG_ADD2, 15, 0, 
	&rt5631_outmixl_mixer_controls[0], ARRAY_SIZE(rt5631_outmixl_mixer_controls)),
SND_SOC_DAPM_MIXER("OUTMIXR Mixer", RT5631_PWR_MANAG_ADD2, 14, 0, 
	&rt5631_outmixr_mixer_controls[0], ARRAY_SIZE(rt5631_outmixr_mixer_controls)),
SND_SOC_DAPM_MIXER("SPKMIXR Mixer", RT5631_PWR_MANAG_ADD2, 12, 0, 
	&rt5631_spkmixr_mixer_controls[0], ARRAY_SIZE(rt5631_spkmixr_mixer_controls)),
SND_SOC_DAPM_PGA("Left SPK Vol", RT5631_PWR_MANAG_ADD4, 15, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right SPK Vol", RT5631_PWR_MANAG_ADD4, 14, 0, NULL, 0),
SND_SOC_DAPM_PGA_E("Left HP Vol", RT5631_PWR_MANAG_ADD4, 11, 0, NULL, 0,
		hp_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("Right HP Vol", RT5631_PWR_MANAG_ADD4, 10, 0, NULL, 0,
		hp_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA("Left Out Vol", RT5631_PWR_MANAG_ADD4, 13, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Out Vol", RT5631_PWR_MANAG_ADD4, 12, 0, NULL, 0),

SND_SOC_DAPM_MIXER("AXO1MIX Mixer", RT5631_PWR_MANAG_ADD3, 11, 0, 
	&rt5631_AXO1MIX_mixer_controls[0], ARRAY_SIZE(rt5631_AXO1MIX_mixer_controls)),
SND_SOC_DAPM_MIXER("SPOLMIX Mixer", SND_SOC_NOPM, 0, 0, 
	&rt5631_spolmix_mixer_controls[0], ARRAY_SIZE(rt5631_spolmix_mixer_controls)),
SND_SOC_DAPM_MIXER("MONOMIX Mixer", RT5631_PWR_MANAG_ADD3, 9, 0, 
	&rt5631_monomix_mixer_controls[0], ARRAY_SIZE(rt5631_monomix_mixer_controls)),
SND_SOC_DAPM_MIXER("SPORMIX Mixer", SND_SOC_NOPM, 0, 0, 
	&rt5631_spormix_mixer_controls[0], ARRAY_SIZE(rt5631_spormix_mixer_controls)),
SND_SOC_DAPM_MIXER("AXO2MIX Mixer", RT5631_PWR_MANAG_ADD3, 10, 0, 
	&rt5631_AXO2MIX_mixer_controls[0], ARRAY_SIZE(rt5631_AXO2MIX_mixer_controls)), 


SND_SOC_DAPM_MUX("SPOL Mux", SND_SOC_NOPM, 0, 0, &rt5631_spol_mux_control),
SND_SOC_DAPM_MUX("SPOR Mux", SND_SOC_NOPM, 0, 0, &rt5631_spor_mux_control),
SND_SOC_DAPM_MUX("Mono Mux", SND_SOC_NOPM, 0, 0, &rt5631_mono_mux_control),

SND_SOC_DAPM_PGA("Mono Amp", RT5631_PWR_MANAG_ADD3, 7, 0, NULL, 0),


SND_SOC_DAPM_PGA_E("SPKL Amp", VIRTUAL_POWER_CONTROL, 0, 0, NULL, 0,
		spk_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("SPKR Amp", VIRTUAL_POWER_CONTROL, 1, 0, NULL, 0,
		spk_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_MICBIAS("Mic Bias1", RT5631_PWR_MANAG_ADD2, 3, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias2", RT5631_PWR_MANAG_ADD2, 2, 0),

SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("SPOL"),
SND_SOC_DAPM_OUTPUT("SPOR"),
SND_SOC_DAPM_OUTPUT("HPOL"),
SND_SOC_DAPM_OUTPUT("HPOR"),
SND_SOC_DAPM_OUTPUT("MONO"),

};


static const struct snd_soc_dapm_route audio_map[] = {

	{"Mic1 Boost", NULL, "MIC1"},
	{"Mic2 Boost", NULL, "MIC2"},
	{"MONOIN_RXP Boost", NULL, "MONOIN_RXP"},
	{"MONOIN_RXN Boost", NULL, "MONOIN_RXN"},
	{"AXIL Boost", NULL, "AXIL"},
	{"AXIR Boost", NULL, "AXIR"},

	{"MONO_IN", NULL, "MONOIN_RXP Boost"},
	{"MONO_IN", NULL, "MONOIN_RXN Boost"},

	{"RECMIXL Mixer", "OUTMIXL Capture Switch", "OUTMIXL Mixer"},
	{"RECMIXL Mixer", "MIC1_BST1 Capture Switch", "Mic1 Boost"},
	{"RECMIXL Mixer", "AXILVOL Capture Switch", "AXIL Boost"},
	{"RECMIXL Mixer", "MONOIN_RX Capture Switch", "MONO_IN"},

	{"RECMIXR Mixer", "OUTMIXR Capture Switch", "OUTMIXR Mixer"},
	{"RECMIXR Mixer", "MIC2_BST2 Capture Switch", "Mic2 Boost"},
	{"RECMIXR Mixer", "AXIRVOL Capture Switch", "AXIR Boost"},
	{"RECMIXR Mixer", "MONOIN_RX Capture Switch", "MONO_IN"},

	{"ADC Mixer", NULL, "RECMIXL Mixer"},
	{"ADC Mixer", NULL, "RECMIXR Mixer"},
	{"Left ADC", NULL, "ADC Mixer"},
	{"Right ADC", NULL,"ADC Mixer"},	

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
	{"OUTMIXL Mixer", "MIC1_BST1 Playback Switch", "Mic1 Boost"},
	{"OUTMIXL Mixer", "MIC2_BST2 Playback Switch", "Mic2 Boost"},
	{"OUTMIXL Mixer", "MONOIN_RXP Playback Switch", "MONOIN_RXP Boost"},
	{"OUTMIXL Mixer", "AXILVOL Playback Switch", "AXIL Boost"},
	{"OUTMIXL Mixer", "AXIRVOL Playback Switch", "AXIR Boost"},
	{"OUTMIXL Mixer", "VDAC Playback Switch", "Voice DAC Boost"},

	{"OUTMIXR Mixer", "RECMIXL Playback Switch", "RECMIXL Mixer"},
	{"OUTMIXR Mixer", "RECMIXR Playback Switch", "RECMIXR Mixer"},
	{"OUTMIXR Mixer", "DACR Playback Switch", "Right DAC"},
	{"OUTMIXR Mixer", "MIC1_BST1 Playback Switch", "Mic1 Boost"},
	{"OUTMIXR Mixer", "MIC2_BST2 Playback Switch", "Mic2 Boost"},
	{"OUTMIXR Mixer", "MONOIN_RXN Playback Switch", "MONOIN_RXN Boost"},
	{"OUTMIXR Mixer", "AXILVOL Playback Switch", "AXIL Boost"},
	{"OUTMIXR Mixer", "AXIRVOL Playback Switch", "AXIR Boost"},
	{"OUTMIXR Mixer", "VDAC Playback Switch", "Voice DAC Boost"},

	{"Left SPK Vol",  NULL, "SPKMIXL Mixer"},
	{"Right SPK Vol",  NULL, "SPKMIXR Mixer"},
	{"Left HP Vol",  NULL, "OUTMIXL Mixer"},
	{"Left Out Vol",  NULL, "OUTMIXL Mixer"},
	{"Right Out Vol",  NULL, "OUTMIXR Mixer"},
	{"Right HP Vol",  NULL, "OUTMIXR Mixer"},


	{"AXO1MIX Mixer", "MIC1_BST1 Playback Switch", "Mic1 Boost"},
	{"AXO1MIX Mixer", "OUTVOLL Playback Switch", "Left Out Vol"},
	{"AXO1MIX Mixer", "OUTVOLR Playback Switch", "Right Out Vol"},
	{"AXO1MIX Mixer", "MIC2_BST2 Playback Switch", "Mic2 Boost"},


	{"AXO2MIX Mixer", "MIC1_BST1 Playback Switch", "Mic1 Boost"},
	{"AXO2MIX Mixer", "OUTVOLL Playback Switch", "Left Out Vol"},
	{"AXO2MIX Mixer", "OUTVOLR Playback Switch", "Right Out Vol"},
	{"AXO2MIX Mixer", "MIC2_BST2 Playback Switch", "Mic2 Boost"},


	{"SPOLMIX Mixer", "SPKVOLL Playback Switch", "Left SPK Vol"},
	{"SPOLMIX Mixer", "SPKVOLR Playback Switch", "Right SPK Vol"},

	{"SPORMIX Mixer", "SPKVOLL Playback Switch", "Left SPK Vol"},
	{"SPORMIX Mixer", "SPKVOLR Playback Switch", "Right SPK Vol"},


	{"MONOMIX Mixer", "OUTVOLL Playback Switch", "Left Out Vol"},
	{"MONOMIX Mixer", "OUTVOLR Playback Switch", "Right Out Vol"},



	{"SPOL Mux", "SPOLMIX", "SPOLMIX Mixer"},
	{"SPOL Mux", "MONOIN_RX", "MONO_IN"},
	{"SPOL Mux", "VDAC", "Voice DAC Boost"},
	{"SPOL Mux", "DACL", "Left DAC"},

	{"SPOR Mux", "SPORMIX", "SPORMIX Mixer"},
	{"SPOR Mux", "MONOIN_RX", "MONO_IN"},
	{"SPOR Mux", "VDAC", "Voice DAC Boost"},
	{"SPOR Mux", "DACR", "Right DAC"},
	
	{"Mono Mux", "MONOMIX", "MONOMIX Mixer"},
	{"Mono Mux", "MONOIN_RX", "MONO_IN"},
	{"Mono Mux", "VDAC", "Voice DAC Boost"},

	{"SPKL Amp", NULL, "SPOL Mux"},
	{"SPKR Amp", NULL, "SPOR Mux"},
	{"Mono Amp", NULL, "Mono Mux"},
	
	{"LOUT", NULL, "AXO1MIX Mixer"},
	{"ROUT", NULL, "AXO2MIX Mixer"},
	{"SPOL", NULL, "SPKL Amp"},
	{"SPOR", NULL, "SPKR Amp"},
	{"HPOL", NULL, "Left HP Vol"},
	{"HPOR", NULL, "Right HP Vol"},
	{"MONO", NULL, "Mono Amp"}

};

static int rt5631_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, rt5631_dapm_widgets,
						ARRAY_SIZE(rt5631_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_new_widgets(codec);

	return 0;
}

struct _coeff_div{
	unsigned int mclk;       //pllout or MCLK
	unsigned int bclk;       //master mode
	unsigned int rate;
	unsigned int reg_val;
};
/*PLL divisors*/
struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

static const struct _pll_div codec_master_pll_div[] = {
	
	{  2048000,  8192000,	0x0ea0},		
	{  3686400,  8192000,	0x4e27},	
	{ 12000000,  8192000,	0x456b},   
	{ 13000000,  8192000,	0x495f},
	{ 13100000,	 8192000,	0x0320},	
	{  2048000,  11289600,	0xf637},
	{  3686400,  11289600,	0x2f22},	
	{ 12000000,  11289600,	0x3e2f},   
	{ 13000000,  11289600,	0x4d5b},
	{ 13100000,	 11289600,	0x363b},	
	{  2048000,  16384000,	0x1ea0},
	{  3686400,  16384000,	0x9e27},	
	{ 12000000,  16384000,	0x452b},   
	{ 13000000,  16384000,	0x542f},
	{ 13100000,	 16384000,	0x03a0},	
	{  2048000,  16934400,	0xe625},
	{  3686400,  16934400,	0x9126},	
	{ 12000000,  16934400,	0x4d2c},   
	{ 13000000,  16934400,	0x742f},
	{ 13100000,	 16934400,	0x3c27},			
	{  2048000,  22579200,	0x2aa0},
	{  3686400,  22579200,	0x2f20},	
	{ 12000000,  22579200,	0x7e2f},   
	{ 13000000,  22579200,	0x742f},
	{ 13100000,	 22579200,	0x3c27},		
	{  2048000,  24576000,	0x2ea0},
	{  3686400,  24576000,	0xee27},	
	{ 12000000,  24576000,	0x2915},   
	{ 13000000,  24576000,	0x772e},
	{ 13100000,	 24576000,	0x0d20},	
	{ 26000000,  24576000,	0x2027},
	{ 26000000,  22579200,	0x392f},	
	{ 24576000,  22579200,	0x0921},	
	{ 24576000,  24576000,	0x02a0},	
};

static const struct _pll_div codec_slave_pll_div[] = {
	{   256000,   4096000,	0x3ea0},
	{	352800,	  5644800,	0x3ea0},
	{	512000,	  8192000,	0x3ea0},
	{	705600,  11289600, 	0x3ea0},
	{  1024000,  16384000,  0x3ea0},	
	{  1411200,  22579200,	0x3ea0},
	{  1536000,	 24576000,	0x3ea0},	
	{  2048000,  16384000,  0x1ea0},	
	{  2822400,  22579200,	0x1ea0},
	{  3072000,	 24576000,	0x1ea0},
	{	705600,  11289600, 	0x3ea0},
	{ 705600, 	  8467200, 	0x3ab0},
			
};

struct _coeff_div coeff_div[] = {
	//sysclk is 256fs
	{ 2048000,  8000 * 32,  8000, 0x1000},
	{ 2048000,  8000 * 64,  8000, 0x0000},
	{ 2822400, 11025 * 32, 11025, 0x1000},
	{ 2822400, 11025 * 64, 11025, 0x0000},
	{ 4096000, 16000 * 32, 16000, 0x1000},
	{ 4096000, 16000 * 64, 16000, 0x0000},
	{ 5644800, 22050 * 32, 22050, 0x1000},
	{ 5644800, 22050 * 64, 22050, 0x0000},	
	{ 8192000, 32000 * 32, 32000, 0x1000},
	{ 8192000, 32000 * 64, 32000, 0x0000},
	{11289600, 44100 * 32, 44100, 0x1000},
	{11289600, 44100 * 64, 44100, 0x0000},
	{12288000, 48000 * 32, 48000, 0x1000},
	{12288000, 48000 * 64, 48000, 0x0000},
	//sysclk is 512fs
	{ 4096000,  8000 * 32,  8000, 0x3000},
	{ 4096000,  8000 * 64,  8000, 0x2000},
	{ 5644800, 11025 * 32, 11025, 0x3000},
	{ 5644800, 11025 * 64, 11025, 0x2000},
	{ 8192000, 16000 * 32, 16000, 0x3000},
	{ 8192000, 16000 * 64, 16000, 0x2000},
	{11289600, 22050 * 32, 22050, 0x3000},
	{11289600, 22050 * 64, 22050, 0x2000},	
	{16384000, 32000 * 32, 32000, 0x3000},
	{16384000, 32000 * 64, 32000, 0x2000},
	{22579200, 44100 * 32, 44100, 0x3000},
	{22579200, 44100 * 64, 44100, 0x2000},
	{24576000, 48000 * 32, 48000, 0x3000},
	{24576000, 48000 * 64, 48000, 0x2000},
	//sysclk is 24.576Mhz or 22.579200Mhz
	{24576000,  8000 * 32,  8000, 0x7080},
	{24576000,  8000 * 64,  8000, 0x6080},
	{24576000, 16000 * 32, 16000, 0x5080},
	{24576000, 16000 * 64, 16000, 0x4080},
	{24576000, 24000 * 32, 24000, 0x5000},
	{24576000, 24000 * 64, 24000, 0x4000},
	{24576000, 32000 * 32, 32000, 0x3080},
	{24576000, 32000 * 64, 32000, 0x2080},	
	
	{22579200, 11025 * 32, 11025, 0x7000},
	{22579200, 11025 * 64, 11025, 0x6000},
	{22579200, 22050 * 32, 22050, 0x5000},
	{22579200, 22050 * 64, 22050, 0x4000},					
	
};




static int get_coeff(int mclk, int rate, int timesofbclk)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if ((coeff_div[i].mclk == mclk) 
				&& (coeff_div[i].rate == rate)
				&& ((coeff_div[i].bclk / coeff_div[i].rate) == timesofbclk))
				return i;
	}

		return -1;
}

static int get_coeff_in_slave_mode(int mclk, int rate)
{
	return get_coeff(mclk, rate, timesofbclk);
}

static int get_coeff_in_master_mode(int mclk, int rate, int bclk)
{
	return get_coeff(mclk, rate, (bclk / rate));	
}
static void rt5631_set_dmic_params(struct snd_soc_codec *codec, struct snd_pcm_hw_params *params)
{
//	struct rt5631_priv *rt5631 = codec->private_data;

	unsigned int rate;

	printk(KERN_DEBUG "enter %s\n", __func__);	
	rt5631_write_mask(codec, RT5631_GPIO_CTRL, GPIO_PIN_FUN_SEL_GPIO_DIMC|GPIO_DMIC_FUN_SEL_DIMC
											 , GPIO_PIN_FUN_SEL_MASK|GPIO_DMIC_FUN_SEL_MASK);
	rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_ENA, DMIC_ENA_MASK);
	rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL,DMIC_L_CH_LATCH_FALLING|DMIC_R_CH_LATCH_RISING
												,DMIC_L_CH_LATCH_MASK|DMIC_R_CH_LATCH_MASK);

	rate = params_rate(params);
	switch (rate) {
	case 44100:
	case 48000:
		rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_CLK_CTRL_TO_32FS, DMIC_CLK_CTRL_MASK);
		break;
	case 32000:
	case 22050:
		rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_CLK_CTRL_TO_64FS, DMIC_CLK_CTRL_MASK);
		break;
	case 16000:
	case 11025:
	case 8000: 
		rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, DMIC_CLK_CTRL_TO_128FS, DMIC_CLK_CTRL_MASK);
		break;
	}
	
	rt5631_write_mask(codec, RT5631_DIG_MIC_CTRL, 
		DMIC_L_CH_UNMUTE | DMIC_R_CH_UNMUTE, DMIC_L_CH_MUTE_MASK | DMIC_R_CH_MUTE_MASK);
	
}

static int rt5631_hifi_pcm_params(struct snd_pcm_substream *substream, 
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
//	struct snd_soc_codec *codec = dai->codec;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct rt5631_priv *rt5631 = codec->private_data;
	int stream = substream->stream;
	unsigned int iface = 0;
	int rate = params_rate(params);
	int coeff = 0;
	unsigned int CodecRegData;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	

	if (!rt5631->master)
		coeff = get_coeff_in_slave_mode(rt5631->sysclk, rate);
	else
		coeff = get_coeff_in_master_mode(rt5631->sysclk, rate, rate * timesofbclk);

	if (coeff < 0) {
		printk(KERN_ERR "%s get_coeff err!\n", __func__);
	//	return -EINVAL;
	}

	switch (params_format(params))
	{
		case SNDRV_PCM_FORMAT_S16_LE:
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x0004;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			iface |= 0x0008;
			break;
		case SNDRV_PCM_FORMAT_S8:
			iface |= 0x000c;
			break;
		default:
			return -EINVAL;
	}

	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (rt5631->dmic_used_flag)	//use Digital Mic 
			rt5631_set_dmic_params(codec, params);
		else	//use Analog Mic
		{
			CodecRegData=rt5631_read(codec,RT5631_ADC_REC_MIXER);
			if((CodecRegData&0x4040)==0)	//use stereo mic from mic1&mic2,no copy ADC channel
			{
				rt5631_write_mask(codec, RT5631_INT_ST_IRQ_CTRL_2, 0x0000, 0xc000);
			}
			else if((CodecRegData&0x4000)==0) //use mic1,copy ADC left to right
			{
				rt5631_write_mask(codec, RT5631_INT_ST_IRQ_CTRL_2, 0x4000, 0xc000);
			}
			else if((CodecRegData&0x0040)==0)//use mic2,copy ADC right to left	
			{
				rt5631_write_mask(codec, RT5631_INT_ST_IRQ_CTRL_2, 0x8000, 0xc000);
			}
			else
			{
					rt5631_write_mask(codec, RT5631_INT_ST_IRQ_CTRL_2, 0x0000, 0xc000);
			}
			
		}	
	}
	
	rt5631_write_mask(codec, RT5631_SDP_CTRL, iface, SDP_I2S_DL_MASK);

	if(coeff>=0)
	rt5631_write(codec, RT5631_STEREO_AD_DA_CLK_CTRL, coeff_div[coeff].reg_val);


	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD1, 0x80e0, 0x80e0);

return 0;
}

static int rt5631_hifi_codec_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rt5631_priv *rt5631 = codec->private_data;
	u16 iface = 0;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5631->master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface |= (0x0001 << 15);
		rt5631->master = 0;
		break;
	default:
		return -EINVAL;
	}
	
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= (0x0001);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= (0x0002);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface  |= (0x0003);
		break;
	default:
		return -EINVAL;
	}
	
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= (0x0001 << 7);
		break;
	default:
		return -EINVAL;	
	}
	
	rt5631_write(codec, RT5631_SDP_CTRL, iface);

	return 0;
}
static int rt5631_hifi_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai, 
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rt5631_priv *rt5631 = codec->private_data;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		rt5631->sysclk = freq;
		return 0;	
	}
	
	printk("unsupported sysclk freq %u for audio i2s\n", freq);
	printk("Set sysclk to 24.576Mhz by default\n");	

	rt5631->sysclk = 24576000;
	return 0;	
	//return -EINVAL;
}

static int rt5631_codec_set_dai_pll(struct snd_soc_dai *codec_dai, 
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;	
	struct rt5631_priv *rt5631 = codec->private_data;
	int i;
	int ret = -EINVAL;
	
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
//	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD2, 0, PWR_PLL);

	if (!freq_in || !freq_out)
		return 0;
		
	if (rt5631->master) {
		for (i = 0; i < ARRAY_SIZE(codec_master_pll_div); i ++) {

			if ((freq_in == codec_master_pll_div[i].pll_in) && (freq_out == codec_master_pll_div[i].pll_out)) {

				rt5631_write(codec, RT5631_PLL_CTRL, codec_master_pll_div[i].regvalue);
				rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD2, PWR_PLL, PWR_PLL);
				schedule_timeout_uninterruptible(msecs_to_jiffies(20));
				rt5631_write(codec, RT5631_GLOBAL_CLK_CTRL, 0x4000);	
				ret = 0;

			}

		}	
	} else {

		for (i = 0; i < ARRAY_SIZE(codec_slave_pll_div); i ++) {

			if ((freq_in == codec_slave_pll_div[i].pll_in) && (freq_out == codec_slave_pll_div[i].pll_out))  {

				rt5631_write(codec, RT5631_PLL_CTRL, codec_slave_pll_div[i].regvalue);
				rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD2, PWR_PLL, PWR_PLL);
				schedule_timeout_uninterruptible(msecs_to_jiffies(20));
				rt5631_write(codec, RT5631_GLOBAL_CLK_CTRL, 0x5000);
				ret = 0;

			}

		}
	}
	
	return 0;
}

static void rt5631_hifi_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai)
{
		
}

#define RT5631_STEREO_RATES		(SNDRV_PCM_RATE_8000_48000)
#define RT5631_FORMAT			(SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt5631_ops = {
			.hw_params = rt5631_hifi_pcm_params,
			.set_fmt = rt5631_hifi_codec_set_dai_fmt,
			.set_sysclk = rt5631_hifi_codec_set_dai_sysclk,
			.set_pll = rt5631_codec_set_dai_pll,
			.shutdown = rt5631_hifi_shutdown,	
};
			
struct snd_soc_dai rt5631_dai[] = { 
	{
		.name = "RT5631 HIFI",
		.id = 1,
		.playback = {
			.stream_name = "HIFI Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_STEREO_RATES,
			.formats = RT5631_FORMAT,	
		}	,
		.capture = {
			.stream_name = "HIFI Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5631_STEREO_RATES,	
			.formats = RT5631_FORMAT,
		},
		.ops =&rt5631_ops,
	},
	
	{
		.name = "RT5631 Reserved",
		.id = 2,
	}
};

EXPORT_SYMBOL_GPL(rt5631_dai);

static int rt5631_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level)
{
	printk(KERN_DEBUG "enter %s\n", __func__);

	printk("rt5631_set_bias_level=%d\n",level);	

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD3,PWR_VREF|PWR_MAIN_BIAS, PWR_VREF|PWR_MAIN_BIAS);
		rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD2,0x000c, 0x000c);	
		break;
	case SND_SOC_BIAS_STANDBY:

		break;
	case SND_SOC_BIAS_OFF:
		rt5631_write_mask(codec, RT5631_SPK_OUT_VOL	,0x8080, 0x8080);
		rt5631_write_mask(codec, RT5631_HP_OUT_VOL	,0x8080, 0x8080);
		rt5631_write(codec, RT5631_PWR_MANAG_ADD1, 0x0000);
		rt5631_write(codec, RT5631_PWR_MANAG_ADD2, 0x0000);
		rt5631_write(codec, RT5631_PWR_MANAG_ADD3, 0x0000);
		rt5631_write(codec, RT5631_PWR_MANAG_ADD4, 0x0000);
		break;
	}
	
	codec->bias_level = level;
	return 0;
}

static int rt5631_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	codec->name = "RT5631";
	codec->owner = THIS_MODULE;
	codec->read = rt5631_read;
	codec->write = rt5631_write;
	codec->set_bias_level = rt5631_set_bias_level;
	codec->dai = rt5631_dai;
	codec->num_dai = 2;
	codec->reg_cache_size = ARRAY_SIZE(rt5631_reg);
	codec->reg_cache_step = 1;
	codec->reg_cache = kmemdup(rt5631_reg, sizeof(rt5631_reg), GFP_KERNEL);
	
	if (codec->reg_cache == NULL)
		return -ENOMEM;
		
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "rt5631: failed to create pcms\n");
		goto pcm_err;	
	}
	
	rt5631_reset(codec);
	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD3,PWR_VREF|PWR_MAIN_BIAS, PWR_VREF|PWR_MAIN_BIAS);
	schedule_timeout_uninterruptible(msecs_to_jiffies(110));
	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD3, PWR_FAST_VREF_CTRL, PWR_FAST_VREF_CTRL);	
	codec->bias_level = SND_SOC_BIAS_STANDBY;
	schedule_delayed_work(&codec->delayed_work, msecs_to_jiffies(100));

	rt5631_reg_init(codec);
	rt5631_add_controls(codec);
	rt5631_add_widgets(codec);
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "rt5631: failed to register card!\n");
		goto card_err;	
	}
	printk(KERN_INFO "rt5631 initial ok!\n");
	return 0;
	
	
card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	codec->reg_cache = NULL;
	return ret;
}


static const struct i2c_device_id rt5631_i2c_id[] = {
		{"rt5631", 0},
			{}
};
MODULE_DEVICE_TABLE(i2c, rt5631_i2c_id);

static int rt5631_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = rt5631_socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;
	
	ret = rt5631_init(socdev);
	if (ret < 0)
		pr_err("failed to initialise rt5631!\n");
		
	return ret;
	
	
}

static int rt5631_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	kfree(codec->reg_cache);
	return 0;
}

struct i2c_driver rt5631_i2c_driver = {
	.driver = {
		.name = "RT5631 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe = rt5631_i2c_probe,
	.remove = rt5631_i2c_remove,
	.id_table = rt5631_i2c_id,
};


static int rt5631_add_i2c_device(struct platform_device *pdev, 
		const struct rt5631_setup_data *setup)
{
#if 0	
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
#endif	
	int ret;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	ret = i2c_add_driver(&rt5631_i2c_driver);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c driver\n");
		return ret;	
	}	
	
#if 0	
	memset(&info, '\0', sizeof(struct i2c_board_info));
	info.addr = setup->i2c_address;
	info.platform_data = pdev;
	strlcpy(info.type, "rt5631", I2C_NAME_SIZE);
	
	adapter = i2c_get_adapter(setup->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "can't get i2c adapter %d\n", 
				setup->i2c_bus);
		goto err_driver;	
	}
	
	client = i2c_new_device(adapter, &info);
	i2c_put_adapter(adapter);
	if (!client) {
		dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
				(unsigned int)info.addr);
		goto err_driver;
	}
#endif	

	return 0;

#if 0	
err_driver:
	i2c_del_driver(&rt5631_i2c_driver);
	return -ENODEV;
#endif
	
}

static void rt5631_work(struct work_struct *work)
{
	struct snd_soc_codec *codec = container_of(work, struct snd_soc_codec, delayed_work.work);
	
	rt5631_set_bias_level(codec, codec->bias_level);	
}

static int rt5631_probe(struct platform_device *pdev) 
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct rt5631_setup_data *setup;
	struct snd_soc_codec *codec;
	struct rt5631_priv *rt5631;
	int ret = 0;
	
	printk(KERN_DEBUG "enter %s\n", __func__);	

	pr_info("RT5631 Audio Codec %s", RT5631_VERSION);

	if(socdev->codec_data)
	{
		setup = socdev->codec_data;		
	}

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL) {
		return -ENOMEM;	
	}
	
	
	rt5631 = kzalloc(sizeof(struct rt5631_priv), GFP_KERNEL);
	if (rt5631 == NULL) {
		ret = -ENOMEM;
		goto priv_err;	
	} 
	codec->private_data = rt5631;
	socdev->card->codec = codec;
	mutex_init(&codec->mutex);
	rt5631_socdev = socdev;
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	INIT_DELAYED_WORK(&codec->delayed_work, rt5631_work);
	
#if 0
	if (setup->i2c_address) {
		codec->hw_write = (hw_write_t)i2c_master_send;

		ret = rt5631_add_i2c_device(pdev, setup);	
	}
#else	
	//probe i2c device driver
	{
		codec->hw_write = (hw_write_t)i2c_master_send;
		//	codec->hw_read = (hw_read_t)i2c_master_recv;
		ret = rt5631_add_i2c_device(pdev, setup);
	}
#endif
	
	if (ret != 0) {
		goto i2c_device_err;
	}
	return 0;	

i2c_device_err:
	kfree(rt5631);
	socdev->card->codec->private_data = NULL;

priv_err:
	kfree(codec);
	socdev->card->codec = NULL;		

	return ret;
}

static int run_delayed_work(struct delayed_work *dwork) 
{
	int ret;
	
	ret = cancel_delayed_work(dwork);
	
	if (ret) {
		schedule_delayed_work(dwork, 0);
		flush_scheduled_work();	
	}	
	
	return ret;
}

static int rt5631_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	struct snd_soc_codec *codec =socdev->card->codec;

	if (codec->control_data) 
		rt5631_set_bias_level(codec, SND_SOC_BIAS_OFF);
	run_delayed_work(&codec->delayed_work);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	i2c_unregister_device(codec->control_data);
	i2c_del_driver(&rt5631_i2c_driver);
	kfree(codec->private_data);
	kfree(codec);
	
	return 0;
		
}

static int rt5631_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	struct snd_soc_codec *codec =socdev->card->codec;

	rt5631_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5631_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

//	int i;
//	u8 data[3];
//	u16 *cache = codec->reg_cache;
	
#if 1
	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD3,PWR_VREF|PWR_MAIN_BIAS, PWR_VREF|PWR_MAIN_BIAS);
	schedule_timeout_uninterruptible(msecs_to_jiffies(110));
	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD3, PWR_FAST_VREF_CTRL, PWR_FAST_VREF_CTRL);	
	rt5631_reg_init(codec);
	rt5631_write_mask(codec, RT5631_PWR_MANAG_ADD1, 0x80e0, 0x80e0);
#else	
	printk(KERN_DEBUG "enter %s\n", __func__);	
	for (i = 0; i < ARRAY_SIZE(rt5631_reg); i++) {
		if (i == RT5631_RESET)
			continue;
		data[0] = i << 1;
		data[1] = (0xff00 & cache[i]) >> 8;
		data[2] = 0x00ff & cache[i];
		codec->hw_write(codec->control_data, data, 2);
	}
	
	rt5631_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#endif
	
	if (codec->suspend_bias_level == SND_SOC_BIAS_ON) {
		rt5631_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
		codec->bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&codec->delayed_work, msecs_to_jiffies(100));	
	}
	
	return 0;

	
}

struct snd_soc_codec_device soc_codec_dev_rt5631 = 
{
	.probe = rt5631_probe,
	.remove = rt5631_remove,
	.suspend = rt5631_suspend,
	.resume = rt5631_resume,
};

EXPORT_SYMBOL_GPL(soc_codec_dev_rt5631);

static int __init rt5631_modinit(void)
{
	return snd_soc_register_dais(rt5631_dai, ARRAY_SIZE(rt5631_dai));	
}

static void __exit rt5631_modexit(void) 
{
	snd_soc_unregister_dais(rt5631_dai, ARRAY_SIZE(rt5631_dai));
}

module_init(rt5631_modinit);
module_exit(rt5631_modexit);
MODULE_DESCRIPTION("ASoC RT5631 driver");
MODULE_AUTHOR("flove");
MODULE_LICENSE("GPL");
