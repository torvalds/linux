/*
 * max98095.c -- MAX98095 ALSA SoC Audio driver
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

#include <sound/max98095.h>
#include "max98095.h"

//----------------------------------------------------------------------------------------
// audio gain parameter
//----------------------------------------------------------------------------------------
#define   TUNNING_DAI1_VOL    	0x00 // 0dB
#define   TUNNING_DAI1_RCV_VOL  0x00 // 0dB

#define   TUNNING_SPKMIX_VOL    0x00 // 0dB
#define   TUNNING_HPMIX_VOL     0x00 // 0dB

#define   TUNNING_RCV_VOL    	0x1F // +8dB
#define   TUNNING_SPK_VOL    	0x18 // +8dB
#define   TUNNING_HP_VOL     	0x11 // -5dB

#define 	MIC_PRE_AMP_GAIN	(3<<5) // +30dB
#define 	MIC_PGA_GAIN		(0x00) // +20dB
#define   	TUNNING_MIC_PGA_GAIN    MIC_PRE_AMP_GAIN

#define 	MIC_ADC_GAIN		(3<<4) // +18dB
#define 	MIC_ADC_VOLUME		(0x03) // 0dB
#define   	TUNNING_ADC_GAIN    MIC_ADC_VOLUME

//----------------------------------------------------------------------------------------
// playback function
//----------------------------------------------------------------------------------------
void max98095_set_playback_speaker(struct snd_soc_codec *codec)
{
	int reg;
	
	// 0x92 Power DAC L/R, SPLEN
	reg = snd_soc_read(codec, M98095_091_PWR_EN_OUT);
	reg &= ~(M98095_DAREN|M98095_DALEN|M98095_DAMEN|M98095_RECEN|M98095_SPREN|M98095_SPLEN|M98095_HPREN|M98095_HPLEN);
	reg |= (M98095_DAREN|M98095_DALEN|M98095_SPREN|M98095_SPLEN);
	snd_soc_write(codec, M98095_091_PWR_EN_OUT, reg);

	// DAI1 L/R to L/R DAC
	reg = (M98095_DAI1R_TO_DACR|M98095_DAI1L_TO_DACL);
	snd_soc_write(codec, M98095_048_MIX_DAC_LR, reg);

	// Mono DAC to SPK_L, SPK_R Mute
	reg = M98095_DACL_TO_SPKL;
	snd_soc_write(codec, M98095_050_MIX_SPK_LEFT, reg);
	reg = M98095_DACR_TO_SPKR;
	snd_soc_write(codec, M98095_051_MIX_SPK_RIGHT, reg);

	reg = TUNNING_DAI1_VOL;
	snd_soc_write(codec, M98095_058_LVL_DAI1_PLAY, reg);

	reg = TUNNING_SPK_VOL;
	snd_soc_write(codec, M98095_068_LVL_SPK_R, reg);
	snd_soc_write(codec, M98095_067_LVL_SPK_L, reg);

	reg = TUNNING_SPKMIX_VOL;
	snd_soc_write(codec, M98095_052_MIX_SPK_CFG, reg);

	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_set_playback_headset(struct snd_soc_codec *codec)
{
	int reg;
	
	// 0x92 Power DAC L/R, SPLEN
	reg = snd_soc_read(codec, M98095_091_PWR_EN_OUT);
	reg &= ~(M98095_DAREN|M98095_DALEN|M98095_DAMEN|M98095_RECEN|M98095_SPREN|M98095_SPLEN|M98095_HPREN|M98095_HPLEN);
	reg |= (M98095_DAREN|M98095_DALEN|M98095_HPREN|M98095_HPLEN);
	snd_soc_write(codec, M98095_091_PWR_EN_OUT, reg);

	// DAI1 L/R to L/R DAC
	reg = (M98095_DAI1R_TO_DACR|M98095_DAI1L_TO_DACL);
	snd_soc_write(codec, M98095_048_MIX_DAC_LR, reg);

	// DAC L/R to HP L/R
	reg = M98095_DACL_TO_HPL;
	snd_soc_write(codec, M98095_04C_MIX_HP_LEFT, reg);
	reg = M98095_DACR_TO_HPR;
	snd_soc_write(codec, M98095_04D_MIX_HP_RIGHT, reg);

	reg = TUNNING_DAI1_VOL;
	snd_soc_write(codec, M98095_058_LVL_DAI1_PLAY, reg);

	// HPMIX L/R to HP_AMP L/R
	reg = M98095_HPNORMAL;
	reg |= TUNNING_HPMIX_VOL;
	snd_soc_write(codec, M98095_04E_CFG_HP, reg);

	reg = TUNNING_HP_VOL;
	snd_soc_write(codec, M98095_064_LVL_HP_L, reg);
	snd_soc_write(codec, M98095_065_LVL_HP_R, reg);

	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_set_playback_earpiece(struct snd_soc_codec *codec)
{
	int reg;
	
	// 0x92 Power DAC L/R, SPLEN
	reg = snd_soc_read(codec, M98095_091_PWR_EN_OUT);
	reg &= ~(M98095_DAREN|M98095_DALEN|M98095_DAMEN|M98095_RECEN|M98095_SPREN|M98095_SPLEN|M98095_HPREN|M98095_HPLEN);
	reg |= (M98095_DAREN|M98095_DALEN|M98095_RECEN);
	snd_soc_write(codec, M98095_091_PWR_EN_OUT, reg);

	// DAI1 L/R to L/R DAC
	reg = (M98095_DAI1R_TO_DACR|M98095_DAI1L_TO_DACL);
	snd_soc_write(codec, M98095_048_MIX_DAC_LR, reg);

	// DAC L/R to RCV
	reg = (M98095_DACL_TO_RCV|M98095_DACR_TO_RCV);
	snd_soc_write(codec, M98095_04F_MIX_RCV, reg);

	reg = TUNNING_DAI1_VOL;
	snd_soc_write(codec, M98095_058_LVL_DAI1_PLAY, reg);

	reg = TUNNING_RCV_VOL;
	snd_soc_write(codec, M98095_066_LVL_RCV, reg);

	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_set_playback_speaker_headset(struct snd_soc_codec *codec)
{
	int reg;
	
	// 0x92 Power DAC L/R, SPLEN
	reg = snd_soc_read(codec, M98095_091_PWR_EN_OUT);
	reg &= ~(M98095_DAREN|M98095_DALEN|M98095_DAMEN|M98095_RECEN|M98095_SPREN|M98095_SPLEN|M98095_HPREN|M98095_HPLEN);
	reg |= (M98095_DAREN|M98095_DALEN|M98095_HPREN|M98095_HPLEN|M98095_SPREN|M98095_SPLEN|M98095_RECEN);
	snd_soc_write(codec, M98095_091_PWR_EN_OUT, reg);

	// DAI1 L/R to L/R DAC
	reg = (M98095_DAI1R_TO_DACR|M98095_DAI1L_TO_DACL);
	snd_soc_write(codec, M98095_048_MIX_DAC_LR, reg);

	// Stereo DAC to SPK_L, SPK_R Mute
	reg = M98095_DACL_TO_SPKL;
	snd_soc_write(codec, M98095_050_MIX_SPK_LEFT, reg);
	reg = M98095_DACR_TO_SPKR;
	snd_soc_write(codec, M98095_051_MIX_SPK_RIGHT, reg);

	// DAC L/R to HP L/R
	reg = (M98095_DACL_TO_RCV|M98095_DACR_TO_RCV);
	snd_soc_write(codec, M98095_04F_MIX_RCV, reg);

	// DAC L/R to HP L/R
	reg = M98095_DACL_TO_HPL;
	snd_soc_write(codec, M98095_04C_MIX_HP_LEFT, reg);
	reg = M98095_DACR_TO_HPR;
	snd_soc_write(codec, M98095_04D_MIX_HP_RIGHT, reg);

	reg = TUNNING_SPK_VOL;
	snd_soc_write(codec, M98095_058_LVL_DAI1_PLAY, reg);

	reg = TUNNING_SPK_VOL;
	snd_soc_write(codec, M98095_068_LVL_SPK_R, reg);
	snd_soc_write(codec, M98095_067_LVL_SPK_L, reg);

	reg = TUNNING_RCV_VOL;
	snd_soc_write(codec, M98095_066_LVL_RCV, reg);

	reg = TUNNING_SPKMIX_VOL;
	snd_soc_write(codec, M98095_052_MIX_SPK_CFG, reg);

	// HPMIX L/R to HP_AMP L/R
	reg = M98095_HPNORMAL;
	reg |= TUNNING_HPMIX_VOL;
	snd_soc_write(codec, M98095_04E_CFG_HP, reg);

	reg = TUNNING_HP_VOL;
	snd_soc_write(codec, M98095_064_LVL_HP_L, reg);
	snd_soc_write(codec, M98095_065_LVL_HP_R, reg);

	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_disable_playback_path(struct snd_soc_codec *codec, enum playback_path path)
{

	int reg;
	
	reg = snd_soc_read(codec, M98095_091_PWR_EN_OUT);

	switch(path) {
		case REV : 
		case HP : 
				reg &= ~(M98095_HPREN|M98095_HPLEN);
			break;
		case SPK : 
				reg &= ~(M98095_SPREN|M98095_SPLEN);
			break;
		case SPK_HP :
				reg &= ~(M98095_HPREN|M98095_HPLEN | M98095_SPREN|M98095_SPLEN);
			break;
		case TV_OUT :
				reg &= ~(M98095_HPREN|M98095_HPLEN | M98095_HPREN|M98095_HPLEN);
			break;
		default :
			break;
	}
	snd_soc_write(codec, M98095_091_PWR_EN_OUT, reg);
	return;
}

//----------------------------------------------------------------------------------------
// recording function
//----------------------------------------------------------------------------------------
void max98095_set_record_main_mic(struct snd_soc_codec *codec)
{
	int reg;

	// 0x4C Mic bias, ADC L enable
//	reg = snd_soc_read(codec, M98095_090_PWR_EN_IN);
//	reg &= ~(M98095_MB1EN | M98095_MB2EN | M98095_INEN | M98095_ADREN | M98095_ADLEN);
//	reg |= (M98095_ADREN|M98095_ADLEN);
//	snd_soc_write(codec, M98095_090_PWR_EN_IN, reg);

	reg = snd_soc_read(codec, M98095_045_CFG_DSP);
	reg |= (1<<6); // DMIC CLK enable, DMIC CLK = MCLK/8
	snd_soc_write(codec, M98095_045_CFG_DSP, reg);
	
	reg = 0x04; // DMICL enable, EXTMIC disable
	snd_soc_write(codec, M98095_087_CFG_MIC, reg);

//	reg = 0x21;
//	snd_soc_write(codec, M98095_056_LVL_SIDETONE_DAI12, reg);

//	reg = (1<<7); // MIC1 to ADC L/R Mixer
//	snd_soc_write(codec, M98095_04A_MIX_ADC_LEFT, reg);
//	snd_soc_write(codec, M98095_04B_MIX_ADC_RIGHT, reg);
//
//	reg = 0x00; // AGC disable
//	snd_soc_write(codec, M98095_069_MICAGC_CFG, reg);
//	reg = 0x00; // Noisegate disable
//	snd_soc_write(codec, M98095_06A_MICAGC_THRESH, reg);

//	reg = TUNNING_MIC_PGA_GAIN;
//	snd_soc_write(codec, M98095_05F_LVL_MIC1, reg);

//	reg = 0x11;
//	snd_soc_write(codec, M98095_02F_DAI1_LVL1, reg);
//
//	reg = TUNNING_ADC_GAIN;
//	snd_soc_write(codec, M98095_05D_LVL_ADC_L, reg);
//	snd_soc_write(codec, M98095_05E_LVL_ADC_R, reg);

	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_set_record_headset_mic(struct snd_soc_codec *codec)
{
	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
void max98095_disable_record_path(struct snd_soc_codec *codec, enum record_path rec_path)
{
	printk("\t[MAX98095] %s(%d)\n",__FUNCTION__,__LINE__);
	return;	
}
