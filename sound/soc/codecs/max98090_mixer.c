/*
 * max98090.c -- MAX98090 ALSA SoC Audio driver
 *
 * Copyright 2010 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/map-base.h>
#include <mach/regs-clock.h>

#include <sound/max98090.h>
#include "max98090.h"

//----------------------------------------------------------------------------------------
// audio gain parameter
//----------------------------------------------------------------------------------------
#define   TUNNING_DAI1_VOL    	0x00 // 0dB
#define   TUNNING_DAI1_RCV_VOL  0x00 // 0dB

#define   TUNNING_SPKMIX_VOL    0x00 // 0dB
#define   TUNNING_HPMIX_VOL     0x00 // 0dB

#define   TUNNING_RCV_VOL    	0x1F // +8dB
#define   TUNNING_SPK_VOL    	0x2C // 0dB
#define   TUNNING_HP_VOL     	0x1A // 0dB

#define 	MIC_PRE_AMP_GAIN	(3<<5) // +30dB
#define 	MIC_PGA_GAIN		(0x00) // +20dB
#define   	TUNNING_MIC_PGA_GAIN    0x40

#define   	TUNNING_MIC_BIAS_VOL    0x00	// 2.2V

#define 	MIC_ADC_GAIN		(3<<4) // +18dB
#define 	MIC_ADC_VOLUME		(0x03) // 0dB
#define   	TUNNING_ADC_GAIN    0x30

//----------------------------------------------------------------------------------------
// playback function
//----------------------------------------------------------------------------------------
void max98090_set_playback_speaker(struct snd_soc_codec *codec)
{
	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_set_playback_headset(struct snd_soc_codec *codec)
{
	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_set_playback_earpiece(struct snd_soc_codec *codec)
{

	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_set_playback_speaker_headset(struct snd_soc_codec *codec)
{
	int reg;
	
	// 0x92 Power DAC L/R, SPLEN
	reg = snd_soc_read(codec, M98090_03F_OUTPUT_ENABLE);
	reg &= ~(M98090_DAREN|M98090_DALEN|M98090_RCVREN|M98090_RCVLEN|M98090_SPREN|M98090_SPLEN|M98090_HPREN|M98090_HPLEN);
	reg |= (M98090_DAREN|M98090_DALEN|M98090_HPREN|M98090_HPLEN);
	snd_soc_write(codec, M98090_03F_OUTPUT_ENABLE, reg);

//	// Stereo DAC to SPK_L, SPK_R Mute
//	reg = M98090_DACL_TO_SPKL;
//	snd_soc_write(codec, M98090_02E_MIX_SPK_L, reg);
//	reg = M98090_DACR_TO_SPKR;
//	snd_soc_write(codec, M98090_02F_MIX_SPK_R, reg);

	// DAC L/R to HP L/R
	reg = M98090_DACL_TO_HPL;
	snd_soc_write(codec, M98090_029_MIX_HP_L, reg);
	reg = M98090_DACR_TO_HPR;
	snd_soc_write(codec, M98090_02A_MIX_HP_R, reg);

//	reg = TUNNING_SPK_VOL;
//	snd_soc_write(codec, M98090_031_SPK_L_VOL, reg);
//	snd_soc_write(codec, M98090_032_SPK_R_VOL, reg);
//
//	reg = TUNNING_SPKMIX_VOL;
//	snd_soc_write(codec, M98090_030_MIX_SPK_CNTL, reg);

	// HPMIX L/R to HP_AMP L/R
//	reg = M98090_HPNORMAL;
	reg = TUNNING_HPMIX_VOL;
	snd_soc_write(codec, M98090_02B_MIX_HP_CNTL, reg);

	reg = TUNNING_HP_VOL;
	snd_soc_write(codec, M98090_02C_HP_L_VOL, reg);
	snd_soc_write(codec, M98090_02D_HP_R_VOL, reg);

	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_disable_playback_path(struct snd_soc_codec *codec, enum playback_path path)
{

	return;
}

//----------------------------------------------------------------------------------------
// recording function
//----------------------------------------------------------------------------------------
void max98090_set_record_main_mic(struct snd_soc_codec *codec)
{
	int reg;

	// 0x4C Mic bias, ADC L enable
	reg = snd_soc_read(codec, M98090_03E_IPUT_ENABLE);
	reg &= ~(M98090_ADLEN | M98090_ADREN | M98090_MBEN | M98090_LINEAEN | M98090_LINEBEN);
	reg |= (M98090_ADLEN | M98090_ADREN | M98090_MBEN);
	snd_soc_write(codec, M98090_03E_IPUT_ENABLE, reg);

	reg = TUNNING_MIC_BIAS_VOL;
	snd_soc_write(codec, M98090_012_MIC_BIAS_VOL, reg);

	reg = M98090_MIC1_TO_ADCL; // MIC2 to ADC L/R Mixer
	snd_soc_write(codec, M98090_015_MIX_ADC_L, reg);
	snd_soc_write(codec, M98090_016_MIX_ADC_R, reg);

	reg = TUNNING_MIC_PGA_GAIN;
	snd_soc_write(codec, M98090_010_MIC1_LVL, reg);

	reg = TUNNING_ADC_GAIN;
	snd_soc_write(codec, M98090_017_ADC_L_LVL, reg);
	snd_soc_write(codec, M98090_018_ADC_R_LVL, reg);

	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_set_record_headset_mic(struct snd_soc_codec *codec)
{
	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98090_disable_record_path(struct snd_soc_codec *codec, enum record_path rec_path)
{
	printk("\t[MAX98090] %s(%d)\n",__FUNCTION__,__LINE__);
	return;	
}
