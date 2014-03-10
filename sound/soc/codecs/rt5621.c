/*
 * rt5621.c  --  RT5621 ALSA SoC audio codec driver
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
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

//#include <mach/gpio.h>

#include "rt5621.h"

#if REALTEK_HWDEP
#include <linux/ioctl.h>
#include <linux/types.h>
#endif

#if 0
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

#define GPIO_HIGH 1
#define GPIO_LOW 0
#define INVALID_GPIO -1

#define RT5621_VERSION "0.01 alsa 1.0.24"

int rt5621_spk_ctl_gpio = INVALID_GPIO;
static int caps_charge = 500;
module_param(caps_charge, int, 0);
MODULE_PARM_DESC(caps_charge, "RT5621 cap charge time (msecs)");

struct snd_soc_codec *rt5621_codec;

static void rt5621_work(struct work_struct *work);

static struct workqueue_struct *rt5621_workq;
static DECLARE_DELAYED_WORK(delayed_work, rt5621_work);

#define ENABLE_EQ_HREQ 1

struct rt5621_priv {
	unsigned int sysclk;
};

enum {
	NORMAL,
	CLUB,
	DANCE,
	LIVE,
	POP,
	ROCK,
	OPPO,
	TREBLE,
	BASS,
	HFREQ,	
	EQTEST,
	SPK_FR	
};

typedef struct  _HW_EQ_PRESET
{
	u16 	HwEqType;
	u16 	EqValue[14];
	u16	HwEQCtrl;

}HW_EQ_PRESET;

HW_EQ_PRESET HwEq_Preset[]={
	/* 0x0 0x1 0x2 0x3 0x4 0x5 0x6 0x7 0x 0x9 0xa 0x 0xc 0x62*/
	{NORMAL,{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x0000},			
	{CLUB  ,{0x1C10,0x0000,0xC1CC,0x1E5D,0x0699,0xCD48,0x188D,0x0699,0xC3B6,0x1CD0,0x0699,0x0436,0x0000},0x800E},
	{DANCE ,{0x1F2C,0x095B,0xC071,0x1F95,0x0616,0xC96E,0x1B11,0xFC91,0xDCF2,0x1194,0xFAF2,0x0436,0x0000},0x800F},
	{LIVE  ,{0x1EB5,0xFCB6,0xC24A,0x1DF8,0x0E7C,0xC883,0x1C10,0x0699,0xDA41,0x1561,0x0295,0x0436,0x0000},0x800F},
	{POP   ,{0x1E98,0xFCB6,0xC340,0x1D60,0x095B,0xC6AC,0x1BBC,0x0556,0x0689,0x0F33,0x0000,0xEDD1,0xF805},0x801F},
	{ROCK  ,{0x1EB5,0xFCB6,0xC071,0x1F95,0x0424,0xC30A,0x1D27,0xF900,0x0C5D,0x0FC7,0x0E23,0x0436,0x0000},0x800F},
	{OPPO  ,{0x0000,0x0000,0xCA4A,0x17F8,0x0FEC,0xCA4A,0x17F8,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x800F},
	{TREBLE,{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x188D,0x1699},0x8010},
	{BASS  ,{0x1A43,0x0C00,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x8001},
	{EQTEST,{0x1F8C,0x1830,0xC118,0x1EEF,0xFD77,0xD8CB,0x1BBC,0x0556,0x0689,0x0F33,0x0000,0xF17F,0x0FEC},0x8003},
};

static const u16 rt5621_reg[RT5621_VENDOR_ID2 + 1] = {
	[RT5621_RESET] = 0x59b4,
	[RT5621_SPK_OUT_VOL] = 0x8080,
	[RT5621_HP_OUT_VOL] = 0x8080,
	[RT5621_MONO_AUX_OUT_VOL] = 0x8080,
	[RT5621_AUXIN_VOL] = 0xe808,
	[RT5621_LINE_IN_VOL] = 0xe808,
	[RT5621_STEREO_DAC_VOL] = 0xe808,
	[RT5621_MIC_VOL] = 0x0808,
	[RT5621_MIC_ROUTING_CTRL] = 0xe0e0,
	[RT5621_ADC_REC_GAIN] = 0xf58b,
	[RT5621_ADC_REC_MIXER] = 0x7f7f,
	[RT5621_SOFT_VOL_CTRL_TIME] = 0x000a,
	[RT5621_OUTPUT_MIXER_CTRL] = 0xc000,
	[RT5621_AUDIO_INTERFACE] = 0x8000,
	[RT5621_STEREO_AD_DA_CLK_CTRL] = 0x166d,
	[RT5621_ADD_CTRL_REG] = 0x5300,
	[RT5621_GPIO_PIN_CONFIG] = 0x1c0e,
	[RT5621_GPIO_PIN_POLARITY] = 0x1c0e,
	[RT5621_GPIO_PIN_STATUS] = 0x0002,
 	[RT5621_OVER_TEMP_CURR_STATUS] = 0x003c,
 	[RT5621_PSEDUEO_SPATIAL_CTRL] = 0x0497,
 	[RT5621_AVC_CTRL] = 0x000b,
 	[RT5621_VENDOR_ID1] = 0x10ec,
	[RT5621_VENDOR_ID2] = 0x2003,
};

#define rt5621_write_mask(c, reg, value, mask) snd_soc_update_bits(c, reg, mask, value)

#define rt5621_write_index_reg(c, addr, data) \
{ \
	snd_soc_write(c, 0x6a, addr); \
	snd_soc_write(c, 0x6c, data); \
}

static int rt5621_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT5621_RESET, 0);
}

static int rt5621_volatile_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT5621_RESET:
	case RT5621_HID_CTRL_DATA:
	case RT5621_GPIO_PIN_STATUS:
	case RT5621_OVER_TEMP_CURR_STATUS:
		return 1;
	default:
		return 0;
	}
}

static int rt5621_readable_register(
	struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RT5621_RESET:
	case RT5621_SPK_OUT_VOL:
	case RT5621_HP_OUT_VOL:
	case RT5621_MONO_AUX_OUT_VOL:
	case RT5621_AUXIN_VOL:
	case RT5621_LINE_IN_VOL:
	case RT5621_STEREO_DAC_VOL:
	case RT5621_MIC_VOL:
	case RT5621_MIC_ROUTING_CTRL:
	case RT5621_ADC_REC_GAIN:
	case RT5621_ADC_REC_MIXER:
	case RT5621_SOFT_VOL_CTRL_TIME:
	case RT5621_OUTPUT_MIXER_CTRL:
	case RT5621_MIC_CTRL:
	case RT5621_AUDIO_INTERFACE:
	case RT5621_STEREO_AD_DA_CLK_CTRL:
	case RT5621_COMPANDING_CTRL:
	case RT5621_PWR_MANAG_ADD1:
	case RT5621_PWR_MANAG_ADD2:
	case RT5621_PWR_MANAG_ADD3:
	case RT5621_ADD_CTRL_REG:
	case RT5621_GLOBAL_CLK_CTRL_REG:
	case RT5621_PLL_CTRL:
	case RT5621_GPIO_OUTPUT_PIN_CTRL:
	case RT5621_GPIO_PIN_CONFIG:
	case RT5621_GPIO_PIN_POLARITY:
	case RT5621_GPIO_PIN_STICKY:
	case RT5621_GPIO_PIN_WAKEUP:
	case RT5621_GPIO_PIN_STATUS:
	case RT5621_GPIO_PIN_SHARING:
	case RT5621_OVER_TEMP_CURR_STATUS:
	case RT5621_JACK_DET_CTRL:
	case RT5621_MISC_CTRL:
	case RT5621_PSEDUEO_SPATIAL_CTRL:
	case RT5621_EQ_CTRL:
	case RT5621_EQ_MODE_ENABLE:
	case RT5621_AVC_CTRL:
	case RT5621_HID_CTRL_INDEX:
	case RT5621_HID_CTRL_DATA:
	case RT5621_VENDOR_ID1:
	case RT5621_VENDOR_ID2:
		return 1;
	default:
		return 0;
	}
}


//static const char *rt5621_spkl_pga[] = {"Vmid","HPL mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_spkn_source_sel[] = {"RN", "RP", "LN"};
static const char *rt5621_spk_pga[] = {"Vmid","HP mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_hpl_pga[]  = {"Vmid","HPL mixer"};
static const char *rt5621_hpr_pga[]  = {"Vmid","HPR mixer"};
static const char *rt5621_mono_pga[] = {"Vmid","HP mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_amp_type_sel[] = {"Class AB","Class D"};
static const char *rt5621_mic_boost_sel[] = {"Bypass","20db","30db","40db"};

static const struct soc_enum rt5621_enum[] = {
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 14, 3, rt5621_spkn_source_sel), /* spkn source from hp mixer */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 10, 4, rt5621_spk_pga), /* spk input sel 1 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 9, 2, rt5621_hpl_pga), /* hp left input sel 2 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 8, 2, rt5621_hpr_pga), /* hp right input sel 3 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 6, 4, rt5621_mono_pga), /* mono input sel 4 */
SOC_ENUM_SINGLE(RT5621_MIC_CTRL, 10,4, rt5621_mic_boost_sel), /*Mic1 boost sel 5 */
SOC_ENUM_SINGLE(RT5621_MIC_CTRL, 8,4,rt5621_mic_boost_sel), /*Mic2 boost sel 6 */
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL,13,2,rt5621_amp_type_sel), /*Speaker AMP sel 7 */
};

static int rt5621_amp_sel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val;
	unsigned short mask, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1);

	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

	snd_soc_update_bits(codec, e->reg, mask, val);
	val &= (0x1 << 13);

	if (val == 0)
	{
		snd_soc_update_bits(codec, 0x3c, 0x0000, 0x4000);       /*power off classd*/
		snd_soc_update_bits(codec, 0x3c, 0x8000, 0x8000);       /*power on classab*/
	} else {
	 	snd_soc_update_bits(codec, 0x3c, 0x0000, 0x8000);       /*power off classab*/
		snd_soc_update_bits(codec, 0x3c, 0x4000, 0x4000);       /*power on classd*/
	}

	return 0;
}

//*****************************************************************************
//
//function:Change audio codec power status
//
//*****************************************************************************
static int rt5621_ChangeCodecPowerStatus(struct snd_soc_codec *codec,int power_state)
{
	unsigned short int PowerDownState=0;

	switch(power_state) {
	case POWER_STATE_D0: //FULL ON-----power on all power
			
		snd_soc_write(codec,RT5621_PWR_MANAG_ADD1,~PowerDownState);
		snd_soc_write(codec,RT5621_PWR_MANAG_ADD2,~PowerDownState);
		snd_soc_write(codec,RT5621_PWR_MANAG_ADD3,~PowerDownState);
		break;
	case POWER_STATE_D1: //LOW ON-----
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2 ,PWR_VREF |
			PWR_DAC_REF_CIR | PWR_L_DAC_CLK | PWR_R_DAC_CLK |
			PWR_L_HP_MIXER | PWR_R_HP_MIXER | PWR_L_ADC_CLK_GAIN |
			PWR_R_ADC_CLK_GAIN | PWR_L_ADC_REC_MIXER | PWR_R_ADC_REC_MIXER |
			PWR_CLASS_AB, PWR_VREF | PWR_DAC_REF_CIR | PWR_L_DAC_CLK |
			PWR_R_DAC_CLK | PWR_L_HP_MIXER | PWR_R_HP_MIXER |
			PWR_L_ADC_CLK_GAIN | PWR_R_ADC_CLK_GAIN |
			PWR_L_ADC_REC_MIXER | PWR_R_ADC_REC_MIXER | PWR_CLASS_AB);

		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3 ,PWR_MAIN_BIAS |
			PWR_HP_R_OUT_VOL | PWR_HP_L_OUT_VOL | PWR_SPK_OUT |
			PWR_MIC1_FUN_CTRL | PWR_MIC1_BOOST_MIXER, PWR_MAIN_BIAS |
			PWR_HP_R_OUT_VOL | PWR_HP_L_OUT_VOL | PWR_SPK_OUT |
			PWR_MIC1_FUN_CTRL | PWR_MIC1_BOOST_MIXER);

		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1 ,PWR_MAIN_I2S_EN |
			PWR_HP_OUT_ENH_AMP | PWR_HP_OUT_AMP | PWR_MIC1_BIAS_EN,
			PWR_MAIN_I2S_EN | PWR_HP_OUT_ENH_AMP | PWR_HP_OUT_AMP |
			PWR_MIC1_BIAS_EN);
		break;

	case POWER_STATE_D1_PLAYBACK: //Low on of Playback
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2, PWR_VREF | PWR_DAC_REF_CIR |
			PWR_L_DAC_CLK | PWR_R_DAC_CLK | PWR_L_HP_MIXER | PWR_R_HP_MIXER |
			PWR_CLASS_AB | PWR_CLASS_D|PWR_SPK_MIXER, PWR_VREF | PWR_DAC_REF_CIR |
			PWR_L_DAC_CLK | PWR_R_DAC_CLK | PWR_L_HP_MIXER | PWR_R_HP_MIXER |
			PWR_CLASS_AB | PWR_CLASS_D|PWR_SPK_MIXER);

		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3, PWR_MAIN_BIAS |
			PWR_HP_R_OUT_VOL | PWR_HP_L_OUT_VOL | PWR_SPK_OUT, 
			PWR_MAIN_BIAS | PWR_HP_R_OUT_VOL | PWR_HP_L_OUT_VOL | PWR_SPK_OUT);		

		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, PWR_MAIN_I2S_EN |
			PWR_HP_OUT_ENH_AMP | PWR_HP_OUT_AMP, PWR_MAIN_I2S_EN |
			PWR_HP_OUT_ENH_AMP | PWR_HP_OUT_AMP);
		break;

	case POWER_STATE_D1_RECORD: //Low on of Record
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, PWR_MAIN_I2S_EN |
			PWR_MIC1_BIAS_EN, PWR_MAIN_I2S_EN | PWR_MIC1_BIAS_EN);							 
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2, PWR_VREF |
			PWR_L_ADC_CLK_GAIN | PWR_R_ADC_CLK_GAIN | PWR_L_ADC_REC_MIXER |
			PWR_R_ADC_REC_MIXER, PWR_VREF | PWR_L_ADC_CLK_GAIN |
			PWR_R_ADC_CLK_GAIN | PWR_L_ADC_REC_MIXER | PWR_R_ADC_REC_MIXER);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3, PWR_MAIN_BIAS |
			PWR_MIC2_BOOST_MIXER | PWR_MIC1_BOOST_MIXER, PWR_MAIN_BIAS |
			PWR_MIC2_BOOST_MIXER | PWR_MIC1_BOOST_MIXER);
		break;

	case POWER_STATE_D2: //STANDBY----
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, 0, PWR_MAIN_I2S_EN |
			PWR_HP_OUT_ENH_AMP | PWR_HP_OUT_AMP | PWR_MIC1_BIAS_EN);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3, 0, PWR_HP_R_OUT_VOL |
			PWR_HP_L_OUT_VOL | PWR_SPK_OUT | PWR_MIC1_FUN_CTRL |
			PWR_MIC1_BOOST_MIXER);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2, 0, PWR_DAC_REF_CIR |
			PWR_L_DAC_CLK | PWR_R_DAC_CLK | PWR_L_HP_MIXER | PWR_R_HP_MIXER|
			PWR_L_ADC_CLK_GAIN | PWR_R_ADC_CLK_GAIN | PWR_L_ADC_REC_MIXER |
			PWR_R_ADC_REC_MIXER | PWR_CLASS_AB | PWR_CLASS_D);
		break;

	case POWER_STATE_D2_PLAYBACK: //STANDBY of playback
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3, 0, /*PWR_HP_R_OUT_VOL |
			PWR_HP_L_OUT_VOL |*/ PWR_SPK_OUT);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, 0, PWR_HP_OUT_ENH_AMP |
			PWR_HP_OUT_AMP);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2, 0, PWR_DAC_REF_CIR |
			PWR_L_DAC_CLK | PWR_R_DAC_CLK | PWR_L_HP_MIXER | PWR_R_HP_MIXER |
			PWR_CLASS_AB | PWR_CLASS_D);
		break;

	case POWER_STATE_D2_RECORD: //STANDBY of record
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, 0, PWR_MIC1_BIAS_EN);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD2, 0, PWR_L_ADC_CLK_GAIN |
			PWR_R_ADC_CLK_GAIN | PWR_L_ADC_REC_MIXER | PWR_R_ADC_REC_MIXER);
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD3, 0, PWR_MIC2_BOOST_MIXER |
			PWR_MIC1_BOOST_MIXER);		
		break;

	case POWER_STATE_D3: //SLEEP
	case POWER_STATE_D4: //OFF----power off all power
		rt5621_write_mask(codec, RT5621_PWR_MANAG_ADD1, 0, PWR_HP_OUT_ENH_AMP |
			PWR_HP_OUT_AMP);		
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD3, 0);
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD1, 0);
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD2, 0);
		break;

	default:
		break;
	}

	return 0;
}


//*****************************************************************************
//
//function AudioOutEnable:Mute/Unmute audio out channel
//WavOutPath:output channel
//Mute :Mute/Unmute output channel
//
//*****************************************************************************
static int rt5621_AudioOutEnable(struct snd_soc_codec *codec,
	unsigned short int WavOutPath, int Mute)
{
	int RetVal=0;	

	if(Mute) {
		switch(WavOutPath) {
		case RT_WAVOUT_ALL_ON:
			RetVal = rt5621_write_mask(codec, RT5621_SPK_OUT_VOL, RT_L_MUTE | RT_R_MUTE,
				RT_L_MUTE | RT_R_MUTE); //Mute Speaker right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_HP_OUT_VOL, RT_L_MUTE | RT_R_MUTE,
				RT_L_MUTE | RT_R_MUTE); //Mute headphone right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, RT_L_MUTE |
				RT_R_MUTE, RT_L_MUTE | RT_R_MUTE); //Mute Aux/Mono right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_STEREO_DAC_VOL, RT_M_HP_MIXER |
				RT_M_SPK_MIXER | RT_M_MONO_MIXER, RT_M_HP_MIXER |
				RT_M_SPK_MIXER | RT_M_MONO_MIXER); //Mute DAC to HP,Speaker,Mono Mixer
			break;

		case RT_WAVOUT_HP:
			RetVal = rt5621_write_mask(codec, RT5621_HP_OUT_VOL, RT_L_MUTE | RT_R_MUTE,
				RT_L_MUTE | RT_R_MUTE); //Mute headphone right/left channel
			break;

		case RT_WAVOUT_SPK:
			RetVal = rt5621_write_mask(codec, RT5621_SPK_OUT_VOL, RT_L_MUTE | RT_R_MUTE,
				RT_L_MUTE | RT_R_MUTE); //Mute Speaker right/left channel			
			break;

		case RT_WAVOUT_AUXOUT:
			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, RT_L_MUTE |
				RT_R_MUTE, RT_L_MUTE | RT_R_MUTE); //Mute AuxOut right/left channel
			break;

		case RT_WAVOUT_MONO:

			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, RT_L_MUTE,
				RT_L_MUTE); //Mute MonoOut channel
			break;

		case RT_WAVOUT_DAC:
			RetVal = rt5621_write_mask(codec, RT5621_STEREO_DAC_VOL, RT_M_HP_MIXER |
				RT_M_SPK_MIXER | RT_M_MONO_MIXER, RT_M_HP_MIXER | RT_M_SPK_MIXER |
				RT_M_MONO_MIXER); //Mute DAC to HP,Speaker,Mono Mixer				
			break;

		default:
			return 0;
		}
	} else {
		switch(WavOutPath) {
		case RT_WAVOUT_ALL_ON:
			RetVal = rt5621_write_mask(codec, RT5621_SPK_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //Mute Speaker right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_HP_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //Mute headphone right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //Mute Aux/Mono right/left channel
			RetVal = rt5621_write_mask(codec, RT5621_STEREO_DAC_VOL, 0, RT_M_HP_MIXER |
				RT_M_SPK_MIXER | RT_M_MONO_MIXER); //Mute DAC to HP,Speaker,Mono Mixer
			break;
		case RT_WAVOUT_HP:
			RetVal = rt5621_write_mask(codec, RT5621_HP_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //UnMute headphone right/left channel
			break;

		case RT_WAVOUT_SPK:
			RetVal = rt5621_write_mask(codec, RT5621_SPK_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //unMute Speaker right/left channel
			break;
		case RT_WAVOUT_AUXOUT:
			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, 0, RT_L_MUTE |
				RT_R_MUTE); //unMute AuxOut right/left channel
			break;

		case RT_WAVOUT_MONO:
			RetVal = rt5621_write_mask(codec, RT5621_MONO_AUX_OUT_VOL, 0, 
				RT_L_MUTE); //unMute MonoOut channel
			break;

		case RT_WAVOUT_DAC:
			RetVal = rt5621_write_mask(codec, RT5621_STEREO_DAC_VOL, 0, RT_M_HP_MIXER |
				RT_M_SPK_MIXER | RT_M_MONO_MIXER); //unMute DAC to HP,Speaker,Mono Mixer
			break;
		default:
			return 0;
		}

	}

	return RetVal;
}


//*****************************************************************************
//
//function:Enable/Disable ADC input source control
//
//*****************************************************************************
static int Enable_ADC_Input_Source(struct snd_soc_codec *codec,unsigned short int ADC_Input_Sour,int Enable)
{
	int bRetVal=0;
	
	if(Enable) {
		//Enable ADC source 
		bRetVal=rt5621_write_mask(codec,RT5621_ADC_REC_MIXER,0,ADC_Input_Sour);
	} else {
		//Disable ADC source		
		bRetVal=rt5621_write_mask(codec,RT5621_ADC_REC_MIXER,ADC_Input_Sour,ADC_Input_Sour);
	}

	return bRetVal;
}

static void rt5621_update_eqmode(struct snd_soc_codec *codec, int mode)
{
	u16 HwEqIndex=0;

	if (mode == NORMAL) {
		/*clear EQ parameter*/
		for (HwEqIndex=0; HwEqIndex<=0x0C; HwEqIndex++) {

			rt5621_write_index_reg(codec, HwEqIndex, HwEq_Preset[mode].EqValue[HwEqIndex])
		}
		
		snd_soc_write(codec, 0x62, 0x0);		/*disable EQ block*/
	} else {
		snd_soc_write(codec, 0x62, HwEq_Preset[mode].HwEQCtrl);

		/*Fill EQ parameter*/
		for (HwEqIndex=0; HwEqIndex<=0x0C; HwEqIndex++) {

			rt5621_write_index_reg(codec, HwEqIndex, HwEq_Preset[mode].EqValue[HwEqIndex]) 
		}		
		//update EQ parameter
		snd_soc_write(codec, 0x66, 0x1f);
		schedule_timeout_uninterruptible(msecs_to_jiffies(1));
		snd_soc_write(codec, 0x66, 0x0);
	}
}

static const struct snd_kcontrol_new rt5621_snd_controls[] = {
SOC_DOUBLE("Speaker Playback Volume", 	RT5621_SPK_OUT_VOL, 8, 0, 31, 1),	
SOC_DOUBLE("Speaker Playback Switch", 	RT5621_SPK_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("Headphone Playback Volume", RT5621_HP_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Headphone Playback Switch", RT5621_HP_OUT_VOL,15, 7, 1, 1),
SOC_DOUBLE("AUX Playback Volume", 		RT5621_MONO_AUX_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("AUX Playback Switch", 		RT5621_MONO_AUX_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("PCM Playback Volume", 		RT5621_STEREO_DAC_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Line In Volume", 			RT5621_LINE_IN_VOL, 8, 0, 31, 1),
SOC_SINGLE("Mic 1 Volume", 				RT5621_MIC_VOL, 8, 31, 1),
SOC_SINGLE("Mic 2 Volume", 				RT5621_MIC_VOL, 0, 31, 1),
SOC_ENUM("Mic 1 Boost", 				rt5621_enum[5]),
SOC_ENUM("Mic 2 Boost", 				rt5621_enum[6]),
SOC_ENUM_EXT("Speaker Amp Type",			rt5621_enum[7], snd_soc_get_enum_double, rt5621_amp_sel_put),
SOC_DOUBLE("AUX In Volume", 			RT5621_AUXIN_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Capture Volume", 			RT5621_ADC_REC_GAIN, 7, 0, 31, 0),
};

void hp_depop_mode2(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, 0x3e, 0x8000, 0x8000);
	snd_soc_update_bits(codec, 0x04, 0x8080, 0x8080);
	snd_soc_update_bits(codec, 0x3a, 0x0100, 0x0100);
	snd_soc_update_bits(codec, 0x3c, 0x2000, 0x2000);
	snd_soc_update_bits(codec, 0x3e, 0x0600, 0x0600);
	snd_soc_update_bits(codec, 0x5e, 0x0200, 0x0200);
	schedule_timeout_uninterruptible(msecs_to_jiffies(300));
}

void aux_depop_mode2(struct snd_soc_codec *codec)
{
	snd_soc_update_bits(codec, 0x3e, 0x8000, 0x8000);
	snd_soc_update_bits(codec, 0x06, 0x8080, 0x8080);
	snd_soc_update_bits(codec, 0x3a, 0x0100, 0x0100);
	snd_soc_update_bits(codec, 0x3c, 0x2000, 0x2000);
	snd_soc_update_bits(codec, 0x3e, 0x6000, 0x6000);
	snd_soc_update_bits(codec, 0x5e, 0x0020, 0x0200);
	schedule_timeout_uninterruptible(msecs_to_jiffies(300));
	snd_soc_update_bits(codec, 0x3a, 0x0002, 0x0002);
	snd_soc_update_bits(codec, 0x3a, 0x0001, 0x0001);	
}

static int rt5621_pcm_hw_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *codec_dai)
{

	struct snd_soc_codec *codec = codec_dai->codec;
	int stream = substream->stream;

	switch (stream)
	{
		case SNDRV_PCM_STREAM_PLAYBACK:

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D1_PLAYBACK);	//power on dac to hp and speaker out
						
			rt5621_AudioOutEnable(codec,RT_WAVOUT_SPK,0);	//unmute speaker out
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_HP,0);	//unmute hp
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_AUXOUT,0);	//unmute auxout out

			break;
		case SNDRV_PCM_STREAM_CAPTURE:

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D1_RECORD);	//power on input to adc

			Enable_ADC_Input_Source(codec,RT_WAVIN_L_MIC1|RT_WAVIN_R_MIC1,1);	//enable record	source from mic1

			break;			
	}
	
	return 0;
}

/* PLL divisors */
struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

static const struct _pll_div codec_pll_div[] = {
	
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
};

static const struct _pll_div codec_bclk_pll_div[] = {

	{  1536000,	 24576000,	0x3ea0},	
	{  3072000,	 24576000,	0x1ea0},
	{  512000, 	 24576000,  0x8e90},
	{  256000,   24576000,  0xbe80},
	{  2822400,	 11289600,	0x1ee0},
	{  3072000,	 12288000,	0x1ee0},		
};


static int rt5621_set_dai_pll(struct snd_soc_dai *dai,
		int pll_id,int source, unsigned int freq_in, unsigned int freq_out)
{
	int i;
	int ret = -EINVAL;
	struct snd_soc_codec *codec = dai->codec;

	if (pll_id < RT5621_PLL_FR_MCLK || pll_id > RT5621_PLL_FR_BCLK)
		return -EINVAL;

	//rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2, 0x0000,0x1000);	//disable PLL power	
	
	if (!freq_in || !freq_out) {

		return 0;
	}		

	if (RT5621_PLL_FR_MCLK == pll_id) {
	for (i = 0; i < ARRAY_SIZE(codec_pll_div); i++) {
			
		if (codec_pll_div[i].pll_in == freq_in && codec_pll_div[i].pll_out == freq_out)
			{
				snd_soc_update_bits(codec, RT5621_GLOBAL_CLK_CTRL_REG, 0x0000, 0x4000);			 	
			 	snd_soc_write(codec,RT5621_PLL_CTRL,codec_pll_div[i].regvalue);//set PLL parameter 	
			 	snd_soc_update_bits(codec,RT5621_PWR_MANAG_ADD2, 0x1000,0x1000);	//enable PLL power	
				ret = 0;
			}
	}
	}
	else if (RT5621_PLL_FR_BCLK == pll_id)
	{
		for (i = 0; i < ARRAY_SIZE(codec_bclk_pll_div); i++)
		{
			if ((freq_in == codec_bclk_pll_div[i].pll_in) && (freq_out == codec_bclk_pll_div[i].pll_out))
			{
				snd_soc_update_bits(codec, RT5621_GLOBAL_CLK_CTRL_REG, 0x4000, 0x4000);
				snd_soc_write(codec,RT5621_PLL_CTRL,codec_bclk_pll_div[i].regvalue);//set PLL parameter 
				snd_soc_update_bits(codec,RT5621_PWR_MANAG_ADD2, 0x1000,0x1000);	//enable PLL power	
				ret = 0;
			}
		}
	}

	snd_soc_update_bits(codec,RT5621_GLOBAL_CLK_CTRL_REG,0x8000,0x8000);//Codec sys-clock from PLL 	
	return ret;
}


struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u16 regvalue;
};

/* codec hifi mclk (after PLL) clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{ 8192000,  8000, 256*4, 0x2a2d},
	{12288000,  8000, 384*4, 0x2c2f},

	/* 11.025k */
	{11289600, 11025, 256*4, 0x2a2d},
	{16934400, 11025, 384*4, 0x2c2f},

	/* 16k */
	{12288000, 16000, 384*2, 0x1c2f},
	{16384000, 16000, 256*4, 0x2a2d},
	{24576000, 16000, 384*4, 0x2c2f},

	/* 22.05k */	
	{11289600, 22050, 256*2, 0x1a2d},
	{16934400, 22050, 384*2, 0x1c2f},

	/* 32k */
	{12288000, 32000, 384  , 0x0c2f},
	{16384000, 32000, 256*2, 0x1a2d},
	{24576000, 32000, 384*2, 0x1c2f},

	/* 44.1k */
	{11289600, 44100, 256*1, 0x0a2d},
	{22579200, 44100, 256*2, 0x1a2d},
	{45158400, 44100, 256*4, 0x2a2d},	

	/* 48k */
	{12288000, 48000, 256*1, 0x0a2d},
	{24576000, 48000, 256*2, 0x1a2d},
	{49152000, 48000, 256*4, 0x2a2d},

	//MCLK is 24.576Mhz(for 8k,16k,32k)
	{24576000,  8000, 384*8, 0x3c6b},
	{24576000, 16000, 384*4, 0x2c6b},
	{24576000, 32000, 384*2, 0x1c6b},

	//MCLK is 22.5792mHz(for 11k,22k)
	{22579200, 11025, 256*8, 0x3a2d},
	{22579200, 22050, 256*4, 0x2a2d},
};



static int get_coeff(int mclk, int rate)
{
	int i;
	
	DBG("get_coeff mclk=%d,rate=%d\n",mclk,rate);

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}


/*
 * Clock after PLL and dividers
 */
 /*in this driver, you have to set sysclk to be 24576000,
 * but you don't need to give a clk to be 24576000, our 
 * internal pll will generate this clock! so it won't make
 * you any difficult.
 */
static int rt5621_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5621_priv *rt5621 = snd_soc_codec_get_drvdata(codec);

	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		rt5621->sysclk = freq;
		return 0;
	}

	printk("unsupported sysclk freq %u for audio i2s\n", freq);

	return -EINVAL;
}


static int rt5621_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = 0x0000;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface = 0x8000;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0000;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x4003;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface |= 0x0000;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec,RT5621_AUDIO_INTERFACE,iface);
	return 0;
}


static int rt5621_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5621_priv *rt5621 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec,RT5621_AUDIO_INTERFACE)&0xfff3;
	int coeff = get_coeff(rt5621->sysclk, params_rate(params));

	DBG("rt5621_pcm_hw_params\n");
	if (coeff < 0)
		coeff = get_coeff(24576000, params_rate(params));	  /*if not set sysclk, default to be 24.576MHz*/

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		iface |= 0x0000;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x000c;
		break;
	}

	/* set iface & srate */
	snd_soc_write(codec, RT5621_AUDIO_INTERFACE, iface);

	if (coeff >= 0)
		snd_soc_write(codec, RT5621_STEREO_AD_DA_CLK_CTRL, coeff_div[coeff].regvalue);
	else
	{
		printk(KERN_ERR "cant find matched sysclk and rate config\n");
		return -EINVAL;
		
	}
	return 0;
}

static int rt5621_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_update_bits(codec, RT5621_SPK_OUT_VOL, RT_L_MUTE | RT_R_MUTE, 0);
		snd_soc_update_bits(codec, RT5621_HP_OUT_VOL, RT_L_MUTE | RT_R_MUTE, 0);
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, RT5621_SPK_OUT_VOL, RT_L_MUTE | RT_R_MUTE, RT_L_MUTE | RT_R_MUTE);
		snd_soc_update_bits(codec, RT5621_HP_OUT_VOL, RT_L_MUTE | RT_R_MUTE, RT_L_MUTE | RT_R_MUTE);
		if (SND_SOC_BIAS_OFF == codec->dapm.bias_level) {
			snd_soc_write(codec, RT5621_PWR_MANAG_ADD3, 0x8000);//enable Main bias
			snd_soc_write(codec, RT5621_PWR_MANAG_ADD2, 0x2000);//enable Vref
			codec->cache_only = false;
			snd_soc_cache_sync(codec);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, RT5621_SPK_OUT_VOL, RT_L_MUTE | RT_R_MUTE, RT_L_MUTE | RT_R_MUTE);
		snd_soc_update_bits(codec, RT5621_HP_OUT_VOL, RT_L_MUTE | RT_R_MUTE, RT_L_MUTE | RT_R_MUTE);
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD3, 0x0000);
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD2, 0x0000);
		snd_soc_write(codec, RT5621_PWR_MANAG_ADD1, 0x0000);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}



struct rt5621_init_reg{

	u8 reg_index;
	u16 reg_value;
};

static struct rt5621_init_reg init_data[] = {
	{RT5621_AUDIO_INTERFACE,        0x8000},    //set I2S codec to slave mode
	{RT5621_STEREO_DAC_VOL,         0x0808},    //default stereo DAC volume to 0db
	{RT5621_OUTPUT_MIXER_CTRL,      0x2b40},    //default output mixer control	
	{RT5621_ADC_REC_MIXER,          0x3f3f},    //set record source is Mic1 by default
	{RT5621_MIC_CTRL,               0x0a00},    //set Mic1,Mic2 boost 20db	
	{RT5621_SPK_OUT_VOL,            0x8080},    //default speaker volume to 0db 
	{RT5621_HP_OUT_VOL,             0x8080},    //default HP volume to -12db	
	{RT5621_ADD_CTRL_REG,           0x4b00},    //Class AB/D speaker ratio is 1.25VDD
	{RT5621_STEREO_AD_DA_CLK_CTRL,  0x066d},    //set Dac filter to 256fs
	{RT5621_ADC_REC_GAIN,		    0xfa95},    //set ADC boost to 15db	
	{RT5621_HID_CTRL_INDEX,         0x46},	    //Class D setting
	{RT5621_MIC_VOL,                0x0808},
	{RT5621_MIC_ROUTING_CTRL, 		0xf0e0},
	{RT5621_HID_CTRL_DATA,          0xFFFF},    //power on Class D Internal register
	{RT5621_JACK_DET_CTRL,          0x4810},    //power on Class D Internal register
};
#define RT5621_INIT_REG_NUM ARRAY_SIZE(init_data)

static int rt5621_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5621_INIT_REG_NUM; i++)
		snd_soc_write(codec, init_data[i].reg_index, init_data[i].reg_value);

	return 0;
}

static void rt5621_work(struct work_struct *work)
{
	struct snd_soc_codec *codec = rt5621_codec;
	
	rt5621_set_bias_level(codec, codec->dapm.bias_level);
}

static int rt5621_probe(struct snd_soc_codec *codec)
{
	int ret;

	printk("##################### %s ######################\n", __FUNCTION__);

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	codec->cache_bypass = 1;

	rt5621_reset(codec);
	snd_soc_write(codec, RT5621_PWR_MANAG_ADD3, 0x8000);//enable Main bias
	snd_soc_write(codec, RT5621_PWR_MANAG_ADD2, 0x2000);//enable Vref
	hp_depop_mode2(codec);
	rt5621_reg_init(codec);

#if ENABLE_EQ_HREQ 		

	rt5621_write_index_reg(codec, 0x11,0x1);
	rt5621_write_index_reg(codec, 0x12,0x1);
	rt5621_update_eqmode(codec, HFREQ);
	
#endif
	rt5621_workq = create_freezable_workqueue("rt5621");
	if (rt5621_workq == NULL) {
		printk("wm8900_probe::create_freezeable_workqueue ERROR !");
		kfree(codec);
		return -ENOMEM;
	}

	rt5621_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;

	queue_delayed_work(rt5621_workq, &delayed_work,
		msecs_to_jiffies(caps_charge));

	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;

	rt5621_codec = codec;

	return 0;
}

static int rt5621_remove(struct snd_soc_codec *codec)
{
	rt5621_set_bias_level(codec, SND_SOC_BIAS_OFF);

	cancel_delayed_work_sync(&delayed_work);
	return 0;
}

#ifdef CONFIG_PM
static int rt5621_suspend(struct snd_soc_codec *codec)
{
	rt5621_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5621_resume(struct snd_soc_codec *codec)
{

	rt5621_reset(codec);
	snd_soc_write(codec, RT5621_PWR_MANAG_ADD3, 0x8000);//enable Main bias
	snd_soc_write(codec, RT5621_PWR_MANAG_ADD2, 0x2000);//enable Vref

	hp_depop_mode2(codec);

	rt5621_reg_init(codec);

#if ENABLE_EQ_HREQ 		

	rt5621_write_index_reg(codec, 0x11,0x1);
	rt5621_write_index_reg(codec, 0x12,0x1);
	rt5621_update_eqmode(codec, HFREQ);
#endif	

	rt5621_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	if (codec->dapm.suspend_bias_level == SND_SOC_BIAS_ON) {
		rt5621_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
		codec->dapm.bias_level = SND_SOC_BIAS_ON;
		queue_delayed_work(rt5621_workq, &delayed_work,
			msecs_to_jiffies(caps_charge));
	}
	return 0;
}
#else
#define rt5621_suspend NULL
#define rt5621_resume NULL
#endif

static void rt5621_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int stream = substream->stream;
	
	switch (stream)
	{
		case SNDRV_PCM_STREAM_PLAYBACK:

			rt5621_AudioOutEnable(codec,RT_WAVOUT_SPK,1);	//mute speaker out
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_HP,1);	//mute hp out
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_AUXOUT,1);	//mute auxout

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D2_PLAYBACK);	//power off dac to hp and speaker out and auxout
						


			break;
		case SNDRV_PCM_STREAM_CAPTURE:

			Enable_ADC_Input_Source(codec,RT_WAVIN_L_MIC1|RT_WAVIN_R_MIC1,0);	//disable record source from mic1

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D2_RECORD);
			

			break;			
	}	
}

//#define RT5621_HIFI_RATES SNDRV_PCM_RATE_8000_48000
#define RT5621_HIFI_RATES (SNDRV_PCM_RATE_44100) // zyy 20110704, playback and record use same sample rate

#define RT5621_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_dai_ops rt5621_hifi_ops = {
	.hw_params = rt5621_pcm_hw_params,	
	.set_fmt = rt5621_set_dai_fmt,
	.set_sysclk = rt5621_set_dai_sysclk,
	.set_pll = rt5621_set_dai_pll,
	.prepare = rt5621_pcm_hw_prepare,
	.shutdown = rt5621_shutdown,
};

struct snd_soc_dai_driver rt5621_dai = { 
	.name = "RT5621 HiFi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5621_HIFI_RATES,
		.formats = RT5621_FORMATS,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT5621_HIFI_RATES,
		.formats = RT5621_FORMATS,
	},
	.ops = &rt5621_hifi_ops,
};

static struct snd_soc_codec_driver soc_codec_dev_rt5621 = {
	.probe = 	rt5621_probe,
	.remove = rt5621_remove,
	.suspend = rt5621_suspend,
	.resume = rt5621_resume,
	.set_bias_level = rt5621_set_bias_level,
	.reg_cache_size = RT5621_VENDOR_ID2 + 1,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = rt5621_reg,
	.volatile_register = rt5621_volatile_register,
	.readable_register = rt5621_readable_register,
	.reg_cache_step = 1,
	.controls = rt5621_snd_controls,
	.num_controls = ARRAY_SIZE(rt5621_snd_controls),
};

static const struct i2c_device_id rt5621_i2c_id[] = {
	{ "rt5621", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5621_i2c_id);

/*
dts:
	codec@1a {
		compatible = "rt5621";
		reg = <0x1a>;
		spk-ctl-gpio = <&gpio6 GPIO_B6 GPIO_ACTIVE_HIGH>;
	};
*/
static int rt5621_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5621_priv *rt5621;
	int ret;
	
	printk("##################### %s ######################\n", __FUNCTION__);

#ifdef CONFIG_OF
	rt5621_spk_ctl_gpio= of_get_named_gpio_flags(i2c->dev.of_node, "spk-ctl-gpio", 0, NULL);
	if (rt5621_spk_ctl_gpio < 0) {
		DBG("%s() Can not read property spk-ctl-gpio\n", __FUNCTION__);
		rt5621_spk_ctl_gpio = INVALID_GPIO;
	}
#endif //#ifdef CONFIG_OF

	ret = gpio_request(rt5621_spk_ctl_gpio, "spk_con");
	if(ret < 0){
		printk("gpio request spk_con error!\n");
	}
	else{
		printk("########################### set spk_con HIGH ##################################\n");
		gpio_direction_output(rt5621_spk_ctl_gpio, GPIO_HIGH);
		gpio_set_value(rt5621_spk_ctl_gpio, GPIO_HIGH);
	}

	rt5621 = kzalloc(sizeof(struct rt5621_priv), GFP_KERNEL);
	if (NULL == rt5621)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5621);

	ret = snd_soc_register_codec(&i2c->dev,
		&soc_codec_dev_rt5621, &rt5621_dai, 1);
	if (ret < 0)
		kfree(rt5621);

	return ret;
}

static int rt5621_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	kfree(i2c_get_clientdata(i2c));
	return 0;
}

struct i2c_driver rt5621_i2c_driver = {
	.driver = {
		.name = "RT5621",
		.owner = THIS_MODULE,
	},
	.probe = rt5621_i2c_probe,
	.remove   = rt5621_i2c_remove,
	.id_table = rt5621_i2c_id,
};

static int __init rt5621_modinit(void)
{
	return i2c_add_driver(&rt5621_i2c_driver);
}
module_init(rt5621_modinit);

static void __exit rt5621_modexit(void)
{
	i2c_del_driver(&rt5621_i2c_driver);
}
module_exit(rt5621_modexit);

MODULE_DESCRIPTION("ASoC RT5621 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL");
