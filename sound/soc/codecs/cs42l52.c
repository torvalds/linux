/*
 * cs42l52.c -- CS42L52 ALSA SoC audio driver
 *
 * Copyright 2007 CirrusLogic, Inc. 
 *
 * Author: Bo Liu <Bo.Liu@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * Revision history
 * Nov 2007  Initial version.
 * Oct 2008  Updated to 2.6.26
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
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
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/device.h>
#include <asm/io.h>
#include "cs42l52.h"
#include <mach/board.h>
//#include "cs42L52_control.h"


#if defined (CONFIG_SND_RK29_CODEC_SOC_MASTER) 
	#define AUTO_DETECT_DISABLE
#else
	//#define AUTO_DETECT_DISABLE
	#undef AUTO_DETECT_DISABLE
#endif

//#define DEBUG
#ifdef DEBUG
#define SOCDBG(fmt, arg...)	printk(KERN_ERR "%s: %s() " fmt, SOC_CS42L52_NAME, __FUNCTION__, ##arg)
#else
#define SOCDBG(fmt, arg...)	do { } while (0)
#endif
#define SOCINF(fmt, args...)	printk(KERN_INFO "%s: " fmt, SOC_CS42L52_NAME,  ##args)
#define SOCERR(fmt, args...)	printk(KERN_ERR "%s: " fmt, SOC_CS42L52_NAME,  ##args)



static void soc_cs42l52_work(struct work_struct *work);

static struct snd_soc_codec *cs42l52_codec;

//added for suspend
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend cs42l52_early_suspend;
#endif

static struct delayed_work delaywork;
static u8   hp_detected;

/**
 * snd_soc_get_volsw - single mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to get the value of a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_cs42l5x_get_volsw(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    int reg = kcontrol->private_value & 0xff;
    int shift = (kcontrol->private_value >> 8) & 0x0f;
    int rshift = (kcontrol->private_value >> 12) & 0x0f;
    int max = (kcontrol->private_value >> 16) & 0xff;
    int mask = (1 << fls(max)) - 1;
    int min = (kcontrol->private_value >> 24) & 0xff;

    ucontrol->value.integer.value[0] =
	((snd_soc_read(codec, reg) >> shift) - min) & mask;
    if (shift != rshift)
	ucontrol->value.integer.value[1] =
	    ((snd_soc_read(codec, reg) >> rshift) - min) & mask;

    return 0;
}

/**
 * snd_soc_put_volsw - single mixer put callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a single mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_cs42l5x_put_volsw(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
SOCDBG("i am here\n");
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    int reg = kcontrol->private_value & 0xff;
    int shift = (kcontrol->private_value >> 8) & 0x0f;
    int rshift = (kcontrol->private_value >> 12) & 0x0f;
    int max = (kcontrol->private_value >> 16) & 0xff;
    int mask = (1 << fls(max)) - 1;
    int min = (kcontrol->private_value >> 24) & 0xff;
    unsigned short val, val2, val_mask;

    val = ((ucontrol->value.integer.value[0] + min) & mask);

    val_mask = mask << shift;
    val = val << shift;
    if (shift != rshift) {
	val2 = ((ucontrol->value.integer.value[1] + min) & mask);
	val_mask |= mask << rshift;
	val |= val2 << rshift;
    }
    return snd_soc_update_bits(codec, reg, val_mask, val);
}

/**
 * snd_soc_info_volsw_2r - double mixer info callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a double mixer control that
 * spans 2 codec registers.
 *
 * Returns 0 for success.
 */
int snd_soc_cs42l5x_info_volsw_2r(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_info *uinfo)
{
    int max = (kcontrol->private_value >> 8) & 0xff;

    if (max == 1)
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
    else
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

    uinfo->count = 2;
    uinfo->value.integer.min = 0;
    uinfo->value.integer.max = max;
    return 0;
}

/**
 * snd_soc_get_volsw_2r - double mixer get callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to get the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_cs42l5x_get_volsw_2r(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    int reg = kcontrol->private_value & 0xff;
    int reg2 = (kcontrol->private_value >> 24) & 0xff;
    int max = (kcontrol->private_value >> 8) & 0xff;
    int min = (kcontrol->private_value >> 16) & 0xff;
    int mask = (1<<fls(max))-1;
    int val, val2;

    val = snd_soc_read(codec, reg);
    val2 = snd_soc_read(codec, reg2);
    ucontrol->value.integer.value[0] = (val - min) & mask;
    ucontrol->value.integer.value[1] = (val2 - min) & mask;
/*    
    SOCDBG("reg[%02x:%02x] = %02x:%02x ucontrol[%02x:%02x], min = %02x, max = %02x, mask %02x\n",
	    reg, reg2, val,val2, 
	    ucontrol->value.integer.value[0], ucontrol->value.integer.value[1], 
	    min, max, mask);
*/
    return 0;
}

/**
 * snd_soc_put_volsw_2r - double mixer set callback
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to set the value of a double mixer control that spans 2 registers.
 *
 * Returns 0 for success.
 */
int snd_soc_cs42l5x_put_volsw_2r(struct snd_kcontrol *kcontrol,
    struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
    int reg = kcontrol->private_value & 0xff;
    int reg2 = (kcontrol->private_value >> 24) & 0xff;
    int max = (kcontrol->private_value >> 8) & 0xff;
    int min = (kcontrol->private_value >> 16) & 0xff;
    int mask = (1 << fls(max)) - 1;
    int err;
    unsigned short val, val2;

    val = (ucontrol->value.integer.value[0] + min) & mask;
    val2 = (ucontrol->value.integer.value[1] + min) & mask;

    if ((err = snd_soc_update_bits(codec, reg, mask, val)) < 0)
	return err;

    err = snd_soc_update_bits(codec, reg2, mask, val2);
/*
    SOCDBG("reg[%02x:%02x] = %02x:%02x, ucontrol[%02x:%02x], min = %02x, max = %02x, mask = %02x\n",
	    reg, reg2, val, val2, 
	    ucontrol->value.integer.value[0], ucontrol->value.integer.value[1], 
	    min, max, mask);
*/
    return err;
}

#define SOC_SINGLE_CS42L52(xname, reg, shift, max, min) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
        .info = snd_soc_info_volsw, .get = snd_soc_cs42l5x_get_volsw,\
        .put = snd_soc_cs42l5x_put_volsw, \
        .private_value =  SOC_SINGLE_VALUE(reg, shift, max, min) }

#define SOC_DOUBLE_CS42L52(xname, reg, shift_left, shift_right, max, min) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname),\
        .info = snd_soc_info_volsw, .get = snd_soc_cs42l5x_get_volsw, \
        .put = snd_soc_cs42l5x_put_volsw, \
        .private_value = (reg) | ((shift_left) << 8) | \
                ((shift_right) << 12) | ((max) << 16) | ((min) << 24) }

/* No shifts required */
#define SOC_DOUBLE_R_CS42L52(xname, reg_left, reg_right, max, min) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
        .info = snd_soc_cs42l5x_info_volsw_2r, \
        .get = snd_soc_cs42l5x_get_volsw_2r, .put = snd_soc_cs42l5x_put_volsw_2r, \
        .private_value = (reg_left) | ((max) << 8) | ((min) << 16) | \
		((reg_right) << 24) }


/*
 * CS42L52 register default value
 */
static const u8 soc_cs42l52_reg_default[] = {
	0x00, 0xE0, 0x01, 0x06, 0x05, /*4*/
	0xa0, 0x00, 0x00, 0x81, /*8*/
	0x81, 0xa5, 0x00, 0x00, /*12*/
	0x60, 0x02, 0x00, 0x00, /*16*/
	0x00, 0x00, 0x00, 0x00, /*20*/
	0x00, 0x00, 0x00, 0x80, /*24*/
	0x80, 0x00, 0x00, 0x00, /*28*/
	0x00, 0x00, 0x88, 0x00, /*32*/
	0x00, 0x00, 0x00, 0x00, /*36*/
	0x00, 0x00, 0x00, 0x7f, /*40*/
	0xc0, 0x00, 0x3f, 0x00, /*44*/
	0x00, 0x00, 0x00, 0x00, /*48*/
	0x00, 0x3b, 0x00, 0x5f, /*52*/
};

static inline int soc_cs42l52_read_reg_cache(struct snd_soc_codec *codec,
		u_int reg)
{
	u8 *cache = codec->reg_cache;

	return reg > SOC_CS42L52_REG_NUM ? -EINVAL : cache[reg];
}

static inline void soc_cs42l52_write_reg_cache(struct snd_soc_codec *codec,
		u_int reg, u_int val)
{
	u8 *cache = codec->reg_cache;
	
	if(reg > SOC_CS42L52_REG_NUM)
		return;
	cache[reg] = val & 0xff;
}

static int soc_cs42l52_write(struct snd_soc_codec *codec,
		unsigned reg, u_int val)
{
#if 1
	u8 datas[2];
	int i,num, ret = 0;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*)codec->private_data;

	datas[0] = reg & 0xff;
	datas[1] = val & 0xff;
	codec->num_dai = 1;
	if(info->flags & SOC_CS42L52_ALL_IN_ONE)
	{
		for(i = 0; i < codec->num_dai; i++)
		{
			if(codec->hw_write(codec->control_data, datas, 2) != 2)
			{
				ret = -EIO;
				break;
			}
		}
	}
	else
	{
		if(info->flags & SOC_CS42L52_CHIP_SWICTH)
		{
			num = info->flags & SOC_CS42L52_CHIP_MASK;
		}
		
		if(codec->hw_write(codec->control_data, datas, 2) != 2)
			ret = -EIO;
	}

	if(ret >= 0)
		soc_cs42l52_write_reg_cache(codec, reg, val);

	return ret;
#else
	return codec->write(codec, reg, val);
#endif
}

static unsigned int soc_cs42l52_read(struct snd_soc_codec *codec,
		u_int reg)
{
#if 1
	u8 data;
	if(i2c_master_reg8_recv(codec->control_data,reg,&data,1,50*1000)>0) {

		if(reg!=0x31)
			printk(" cs42l52  reg =0x%x, val=0x%x\n",reg,data);
		return data;
	}
	else {
		printk("cs42l52  read error\n");
		return -1;
	}
#else
	return codec->read(codec, reg);
#endif	
}

struct soc_cs42l52_clk_para {
	u32 mclk;
	u32 rate;
	u8 speed;
	u8 group;
	u8 videoclk;
	u8 ratio;
	u8 mclkdiv2;
};

static const struct soc_cs42l52_clk_para clk_map_table[] = {
	/*8k*/
	{12288000, 8000, CLK_CTL_S_QS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{18432000, 8000, CLK_CTL_S_QS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{12000000, 8000, CLK_CTL_S_QS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 0},
	{24000000, 8000, CLK_CTL_S_QS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 1},
	{27000000, 8000, CLK_CTL_S_QS_MODE, CLK_CTL_32K_SR, CLK_CTL_27M_MCLK, CLK_CTL_RATIO_125, 0}, /*4*/

	/*11.025k*/
	{11289600, 11025, CLK_CTL_S_QS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{16934400, 11025, CLK_CTL_S_QS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	
	/*16k*/
	{12288000, 16000, CLK_CTL_S_HS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{18432000, 16000, CLK_CTL_S_HS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{12000000, 16000, CLK_CTL_S_HS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 0},/*9*/
	{24000000, 16000, CLK_CTL_S_HS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 1},
	{27000000, 16000, CLK_CTL_S_HS_MODE, CLK_CTL_32K_SR, CLK_CTL_27M_MCLK, CLK_CTL_RATIO_125, 1},

	/*22.05k*/
	{11289600, 22050, CLK_CTL_S_HS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{16934400, 22050, CLK_CTL_S_HS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	
	/* 32k */
	{12288000, 32000, CLK_CTL_S_SS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},/*14*/
	{18432000, 32000, CLK_CTL_S_SS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{12000000, 32000, CLK_CTL_S_SS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 0},
	{24000000, 32000, CLK_CTL_S_SS_MODE, CLK_CTL_32K_SR, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 1},
	{27000000, 32000, CLK_CTL_S_SS_MODE, CLK_CTL_32K_SR, CLK_CTL_27M_MCLK, CLK_CTL_RATIO_125, 0},

	/* 44.1k */
	{11289600, 44100, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},/*19*/
	{16934400, 44100, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{12000000, 44100, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_136, 0},

	/* 48k */
	{12288000, 48000, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{18432000, 48000, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{12000000, 48000, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 0},
	{24000000, 48000, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 1},/*25*/
	{27000000, 48000, CLK_CTL_S_SS_MODE, CLK_CTL_NOT_32K, CLK_CTL_27M_MCLK, CLK_CTL_RATIO_125, 1},

	/* 88.2k */
	{11289600, 88200, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{16934400, 88200, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},

	/* 96k */
	{12288000, 96000, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},
	{18432000, 96000, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_128, 0},/*30*/
	{12000000, 96000, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 0},
	{24000000, 96000, CLK_CTL_S_DS_MODE, CLK_CTL_NOT_32K, CLK_CTL_NOT_27M, CLK_CTL_RATIO_125, 1},
};

static int soc_cs42l52_get_clk(int mclk, int rate)
{
	int i , ret = 0;
	u_int mclk1, mclk2 = 0;

	for(i = 0; i < ARRAY_SIZE(clk_map_table); i++){
		if(clk_map_table[i].rate == rate){
			mclk1 = clk_map_table[i].mclk;
			if(abs(mclk - mclk1) < abs(mclk - mclk2)){
				mclk2 = mclk1;
				ret = i;
			}
		}
	}

	return ret < ARRAY_SIZE(clk_map_table) ? ret : -EINVAL;
}

static const char *cs42l52_mic_bias[] = {"0.5VA", "0.6VA", "0.7VA", "0.8VA", "0.83VA", "0.91VA"};
static const char *cs42l52_hpf_freeze[] = {"Continuous DC Subtraction", "Frozen DC Subtraction"};
static const char *cs42l52_hpf_corner_freq[] = {"Normal", "119Hz", "236Hz", "464Hz"};
static const char *cs42l52_adc_sum[] = {"Normal", "Sum half", "Sub half", "Inverted"};
static const char *cs42l52_sig_polarity[] = {"Normal", "Inverted"};
static const char *cs42l52_spk_mono_channel[] = {"ChannelA", "ChannelB"};
static const char *cs42l52_beep_type[] = {"Off", "Single", "Multiple", "Continuous"};
static const char *cs42l52_treble_freq[] = {"5kHz", "7kHz", "10kHz", "15kHz"};
static const char *cs42l52_bass_freq[] = {"50Hz", "100Hz", "200Hz", "250Hz"};
static const char *cs42l52_target_sel[] = {"Apply Specific", "Apply All"};
static const char *cs42l52_noise_gate_delay[] = {"50ms", "100ms", "150ms", "200ms"};
static const char *cs42l52_adc_mux[] = {"AIN1", "AIN2", "AIN3", "AIN4", "PGA"};
static const char *cs42l52_mic_mux[] = {"MIC1", "MIC2"};
static const char *cs42l52_stereo_mux[] = {"Mono", "Stereo"};
static const char *cs42l52_off[] = {"On", "Off"};
static const char *cs42l52_hpmux[] = {"Off", "On"};

static const struct soc_enum soc_cs42l52_enum[] = {
SOC_ENUM_DOUBLE(CODEC_CS42L52_ANALOG_HPF_CTL, 4, 6, 2, cs42l52_hpf_freeze), /*0*/
SOC_ENUM_SINGLE(CODEC_CS42L52_ADC_HPF_FREQ, 0, 4, cs42l52_hpf_corner_freq),
SOC_ENUM_SINGLE(CODEC_CS42L52_ADC_MISC_CTL, 4, 4, cs42l52_adc_sum),
SOC_ENUM_DOUBLE(CODEC_CS42L52_ADC_MISC_CTL, 2, 3, 2, cs42l52_sig_polarity),
SOC_ENUM_DOUBLE(CODEC_CS42L52_PB_CTL1, 2, 3, 2, cs42l52_sig_polarity),
SOC_ENUM_SINGLE(CODEC_CS42L52_PB_CTL2, 2, 2, cs42l52_spk_mono_channel), /*5*/
SOC_ENUM_SINGLE(CODEC_CS42L52_BEEP_TONE_CTL, 6, 4, cs42l52_beep_type),
SOC_ENUM_SINGLE(CODEC_CS42L52_BEEP_TONE_CTL, 3, 4, cs42l52_treble_freq),
SOC_ENUM_SINGLE(CODEC_CS42L52_BEEP_TONE_CTL, 1, 4, cs42l52_bass_freq),
SOC_ENUM_SINGLE(CODEC_CS42L52_LIMITER_CTL2, 6, 2, cs42l52_target_sel),
SOC_ENUM_SINGLE(CODEC_CS42L52_NOISE_GATE_CTL, 7, 2, cs42l52_target_sel), /*10*/
SOC_ENUM_SINGLE(CODEC_CS42L52_NOISE_GATE_CTL, 0, 4, cs42l52_noise_gate_delay),
SOC_ENUM_SINGLE(CODEC_CS42L52_ADC_PGA_A, 5, 5, cs42l52_adc_mux),
SOC_ENUM_SINGLE(CODEC_CS42L52_ADC_PGA_B, 5, 5, cs42l52_adc_mux),
SOC_ENUM_SINGLE(CODEC_CS42L52_MICA_CTL, 6, 2, cs42l52_mic_mux),
SOC_ENUM_SINGLE(CODEC_CS42L52_MICB_CTL, 6, 2, cs42l52_mic_mux), /*15*/
SOC_ENUM_SINGLE(CODEC_CS42L52_MICA_CTL, 5, 2, cs42l52_stereo_mux),
SOC_ENUM_SINGLE(CODEC_CS42L52_MICB_CTL, 5, 2, cs42l52_stereo_mux),
SOC_ENUM_SINGLE(CODEC_CS42L52_IFACE_CTL2, 0, 6, cs42l52_mic_bias), /*18*/
SOC_ENUM_SINGLE(CODEC_CS42L52_PWCTL2, 0, 2, cs42l52_off),
SOC_ENUM_SINGLE(CODEC_CS42L52_MISC_CTL, 6, 2, cs42l52_hpmux),
SOC_ENUM_SINGLE(CODEC_CS42L52_MISC_CTL, 7, 2, cs42l52_hpmux),
};

static const struct snd_kcontrol_new soc_cs42l52_controls[] = {

SOC_ENUM("Mic VA Capture Switch", soc_cs42l52_enum[18]), /*0*/
SOC_DOUBLE("HPF Capture Switch", CODEC_CS42L52_ANALOG_HPF_CTL, 5, 7, 1, 0),
SOC_ENUM("HPF Freeze Capture Switch", soc_cs42l52_enum[0]),

SOC_DOUBLE("Analog SR Capture Switch", CODEC_CS42L52_ANALOG_HPF_CTL, 1, 3, 1, 1),
SOC_DOUBLE("Analog ZC Capture Switch", CODEC_CS42L52_ANALOG_HPF_CTL, 0, 2, 1, 1),
SOC_ENUM("HPF corner freq Capture Switch", soc_cs42l52_enum[1]), /*5*/

SOC_SINGLE("Ganged Ctl Capture Switch", CODEC_CS42L52_ADC_MISC_CTL, 7, 1, 1), /* should be enabled init */
SOC_ENUM("Mix/Swap Capture Switch",soc_cs42l52_enum[2]),
SOC_ENUM("Signal Polarity Capture Switch", soc_cs42l52_enum[3]),

//SOC_SINGLE("HP Analog Gain Playback Volume", CODEC_CS42L52_PB_CTL1, 5, 7, 0),    //rocky liu 
SOC_SINGLE("Playback B=A Volume Playback Switch", CODEC_CS42L52_PB_CTL1, 4, 1, 0), /*10*/ /*should be enabled init*/ 
SOC_ENUM("PCM Signal Polarity Playback Switch",soc_cs42l52_enum[4]),

SOC_SINGLE("Digital De-Emphasis Playback Switch", CODEC_CS42L52_MISC_CTL, 2, 1, 0),
SOC_SINGLE("Digital SR Playback Switch", CODEC_CS42L52_MISC_CTL, 1, 1, 0),
SOC_SINGLE("Digital ZC Playback Switch", CODEC_CS42L52_MISC_CTL, 0, 1, 0),

SOC_SINGLE("Spk Volume Equal Playback Switch", CODEC_CS42L52_PB_CTL2, 3, 1, 0) , /*15*/ /*should be enabled init*/
SOC_SINGLE("Spk Mute 50/50 Playback Switch", CODEC_CS42L52_PB_CTL2, 0, 1, 0),
SOC_ENUM("Spk Swap Channel Playback Switch", soc_cs42l52_enum[5]),
SOC_SINGLE("Spk Full-Bridge Playback Switch", CODEC_CS42L52_PB_CTL2, 1, 1, 0),
SOC_DOUBLE_R("Mic Gain Capture Volume", CODEC_CS42L52_MICA_CTL, CODEC_CS42L52_MICB_CTL, 0, 31, 0),

SOC_DOUBLE_R("ALC SR Capture Switch", CODEC_CS42L52_PGAA_CTL, CODEC_CS42L52_PGAB_CTL, 7, 1, 1), /*20*/
SOC_DOUBLE_R("ALC ZC Capture Switch", CODEC_CS42L52_PGAA_CTL, CODEC_CS42L52_PGAB_CTL, 6, 1, 1),
SOC_DOUBLE_R_CS42L52("PGA Capture Volume", CODEC_CS42L52_PGAA_CTL, CODEC_CS42L52_PGAB_CTL, 0x30, 0x18),

SOC_DOUBLE_R_CS42L52("Passthru Playback Volume", CODEC_CS42L52_PASSTHRUA_VOL, CODEC_CS42L52_PASSTHRUB_VOL, 0x90, 0x88),
SOC_DOUBLE("Passthru Playback Switch", CODEC_CS42L52_MISC_CTL, 4, 5, 1, 1),
SOC_DOUBLE_R_CS42L52("ADC Capture Volume", CODEC_CS42L52_ADCA_VOL, CODEC_CS42L52_ADCB_VOL, 0x80, 0xA0),
SOC_DOUBLE("ADC Capture Switch", CODEC_CS42L52_ADC_MISC_CTL, 0, 1, 1, 1),
SOC_DOUBLE_R_CS42L52("ADC Mixer Capture Volume", CODEC_CS42L52_ADCA_MIXER_VOL, CODEC_CS42L52_ADCB_MIXER_VOL, 0x7f, 0x19),
SOC_DOUBLE_R("ADC Mixer Capture Switch", CODEC_CS42L52_ADCA_MIXER_VOL, CODEC_CS42L52_ADCB_MIXER_VOL, 7, 1, 1),
SOC_DOUBLE_R_CS42L52("PCM Mixer Playback Volume", CODEC_CS42L52_PCMA_MIXER_VOL, CODEC_CS42L52_PCMB_MIXER_VOL, 0x7f, 0x19),
SOC_DOUBLE_R("PCM Mixer Playback Switch", CODEC_CS42L52_PCMA_MIXER_VOL, CODEC_CS42L52_PCMB_MIXER_VOL, 7, 1, 1),

SOC_SINGLE("Beep Freq", CODEC_CS42L52_BEEP_FREQ, 4, 15, 0),
SOC_SINGLE("Beep OnTime", CODEC_CS42L52_BEEP_FREQ, 0, 15, 0), /*30*/
SOC_SINGLE_CS42L52("Beep Volume", CODEC_CS42L52_BEEP_VOL, 0, 0x1f, 0x07),
SOC_SINGLE("Beep OffTime", CODEC_CS42L52_BEEP_VOL, 5, 7, 0),
SOC_ENUM("Beep Type", soc_cs42l52_enum[6]),
SOC_SINGLE("Beep Mix Switch", CODEC_CS42L52_BEEP_TONE_CTL, 5, 1, 1),

SOC_ENUM("Treble Corner Freq Playback Switch", soc_cs42l52_enum[7]), /*35*/
SOC_ENUM("Bass Corner Freq Playback Switch",soc_cs42l52_enum[8]),
SOC_SINGLE("Tone Control Playback Switch", CODEC_CS42L52_BEEP_TONE_CTL, 0, 1, 0),
SOC_SINGLE("Treble Gain Playback Volume", CODEC_CS42L52_TONE_CTL, 4, 15, 1),
SOC_SINGLE("Bass Gain Playback Volume", CODEC_CS42L52_TONE_CTL, 0, 15, 1),

SOC_DOUBLE_R_CS42L52("Master Playback Volume", CODEC_CS42L52_MASTERA_VOL, CODEC_CS42L52_MASTERB_VOL,0x18, 0x18), /* koffu 40*/
SOC_DOUBLE_R_CS42L52("HP Digital Playback Volume", CODEC_CS42L52_HPA_VOL, CODEC_CS42L52_HPB_VOL, 0xff, 0x1),
SOC_DOUBLE("HP Digital Playback Switch", CODEC_CS42L52_PB_CTL2, 6, 7, 1, 1),
SOC_DOUBLE_R_CS42L52("Speaker Playback Volume", CODEC_CS42L52_SPKA_VOL, CODEC_CS42L52_SPKB_VOL, 0xff, 0x1),
SOC_DOUBLE("Speaker Playback Switch", CODEC_CS42L52_PB_CTL2, 4, 5, 1, 1),

SOC_SINGLE("Limiter Max Threshold Playback Volume", CODEC_CS42L52_LIMITER_CTL1, 5, 7, 0),
SOC_SINGLE("Limiter Cushion Threshold Playback Volume", CODEC_CS42L52_LIMITER_CTL1, 2, 7, 0),
SOC_SINGLE("Limiter SR Playback Switch", CODEC_CS42L52_LIMITER_CTL1, 1, 1, 0), /*45*/
SOC_SINGLE("Limiter ZC Playback Switch", CODEC_CS42L52_LIMITER_CTL1, 0, 1, 0),
SOC_SINGLE("Limiter Playback Switch", CODEC_CS42L52_LIMITER_CTL2, 7, 1, 0),
SOC_ENUM("Limiter Attnenuate Playback Switch", soc_cs42l52_enum[9]),
SOC_SINGLE("Limiter Release Rate Playback Volume", CODEC_CS42L52_LIMITER_CTL2, 0, 63, 0),
SOC_SINGLE("Limiter Attack Rate Playback Volume", CODEC_CS42L52_LIMITER_AT_RATE, 0, 63, 0), /*50*/

SOC_DOUBLE("ALC Capture Switch",CODEC_CS42L52_ALC_CTL, 6, 7, 1, 0),
SOC_SINGLE("ALC Attack Rate Capture Volume", CODEC_CS42L52_ALC_CTL, 0, 63, 0),
SOC_SINGLE("ALC Release Rate Capture Volume", CODEC_CS42L52_ALC_RATE, 0, 63, 0),
SOC_SINGLE("ALC Max Threshold Capture Volume", CODEC_CS42L52_ALC_THRESHOLD, 5, 7, 0),
SOC_SINGLE("ALC Min Threshold Capture Volume", CODEC_CS42L52_ALC_THRESHOLD, 2, 7, 0), /*55*/

SOC_ENUM("Noise Gate Type Capture Switch", soc_cs42l52_enum[10]),
SOC_SINGLE("Noise Gate Capture Switch", CODEC_CS42L52_NOISE_GATE_CTL, 6, 1, 0),
SOC_SINGLE("Noise Gate Boost Capture Switch", CODEC_CS42L52_NOISE_GATE_CTL, 5, 1, 1),
SOC_SINGLE("Noise Gate Threshold Capture Volume", CODEC_CS42L52_NOISE_GATE_CTL, 2, 7, 0),
SOC_ENUM("Noise Gate Delay Time Capture Switch", soc_cs42l52_enum[11]), /*60*/

SOC_SINGLE("Batt Compensation Switch", CODEC_CS42L52_BATT_COMPEN, 7, 1, 0),
SOC_SINGLE("Batt VP Monitor Switch", CODEC_CS42L52_BATT_COMPEN, 6, 1, 0),
SOC_SINGLE("Batt VP ref", CODEC_CS42L52_BATT_COMPEN, 0, 0x0f, 0),
SOC_SINGLE("Playback Charge Pump Freq", CODEC_CS42L52_CHARGE_PUMP, 4, 15, 0), /*64*/

};

static int soc_cs42l52_add_controls(struct snd_soc_codec *codec)
{
	int i,ret = 0;

	for(i = 0; i < ARRAY_SIZE(soc_cs42l52_controls); i++)
	{
		ret = snd_ctl_add(codec->card,
		snd_soc_cnew(&soc_cs42l52_controls[i], codec, NULL));

		if(ret < 0)
		{
			SOCDBG("add cs42l52 controls failed\n");
			break;
		}
	}
	return ret;
}

static const struct snd_kcontrol_new cs42l52_adca_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[12]);

static const struct snd_kcontrol_new cs42l52_adcb_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[13]);

static const struct snd_kcontrol_new cs42l52_mica_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[14]);

static const struct snd_kcontrol_new cs42l52_micb_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[15]);

static const struct snd_kcontrol_new cs42l52_mica_stereo_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[16]);

static const struct snd_kcontrol_new cs42l52_micb_stereo_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[17]);

static const struct snd_kcontrol_new cs42l52_passa_switch =
SOC_DAPM_SINGLE("Switch", CODEC_CS42L52_MISC_CTL, 6, 1, 0);

static const struct snd_kcontrol_new cs42l52_passb_switch =
SOC_DAPM_SINGLE("Switch", CODEC_CS42L52_MISC_CTL, 7, 1, 0);

static const struct snd_kcontrol_new cs42l52_micbias_switch =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[19]);

static const struct snd_kcontrol_new cs42l52_hpa_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[20]);

static const struct snd_kcontrol_new cs42l52_hpb_mux =
SOC_DAPM_ENUM("Route", soc_cs42l52_enum[21]);

static const struct snd_soc_dapm_widget soc_cs42l52_dapm_widgets[] = {

	/* Input path */
	SND_SOC_DAPM_ADC("ADC Left", "Capture", CODEC_CS42L52_PWCTL1, 1, 1),
	//SND_SOC_DAPM_ADC("ADC Right", "Capture", CODEC_CS42L52_PWCTL1, 2, 1),

	
	SND_SOC_DAPM_MUX("MICA Mux Capture Switch", SND_SOC_NOPM, 0, 0, &cs42l52_mica_mux),
	SND_SOC_DAPM_MUX("MICB Mux Capture Switch", SND_SOC_NOPM, 0, 0, &cs42l52_micb_mux),
	SND_SOC_DAPM_MUX("MICA Stereo Mux Capture Switch", SND_SOC_NOPM, 1, 0, &cs42l52_mica_stereo_mux),
	SND_SOC_DAPM_MUX("MICB Stereo Mux Capture Switch", SND_SOC_NOPM, 2, 0, &cs42l52_micb_stereo_mux),

	SND_SOC_DAPM_MUX("ADC Mux Left Capture Switch", SND_SOC_NOPM, 1, 1, &cs42l52_adca_mux),
	SND_SOC_DAPM_MUX("ADC Mux Right Capture Switch", SND_SOC_NOPM, 2, 1, &cs42l52_adcb_mux),


	/* Sum switches */
	SND_SOC_DAPM_PGA("AIN1A Switch", CODEC_CS42L52_ADC_PGA_A, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN2A Switch", CODEC_CS42L52_ADC_PGA_A, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN3A Switch", CODEC_CS42L52_ADC_PGA_A, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN4A Switch", CODEC_CS42L52_ADC_PGA_A, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MICA Switch" , CODEC_CS42L52_ADC_PGA_A, 4, 0, NULL, 0),

	SND_SOC_DAPM_PGA("AIN1B Switch", CODEC_CS42L52_ADC_PGA_B, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN2B Switch", CODEC_CS42L52_ADC_PGA_B, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN3B Switch", CODEC_CS42L52_ADC_PGA_B, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AIN4B Switch", CODEC_CS42L52_ADC_PGA_B, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("MICB Switch" , CODEC_CS42L52_ADC_PGA_B, 4, 0, NULL, 0),

	/* MIC PGA Power */
	SND_SOC_DAPM_PGA("PGA MICA", CODEC_CS42L52_PWCTL2, PWCTL2_PDN_MICA_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGA MICB", CODEC_CS42L52_PWCTL2, PWCTL2_PDN_MICB_SHIFT, 1, NULL, 0),

	/* MIC bias switch */
	SND_SOC_DAPM_MUX("Mic Bias Capture Switch", SND_SOC_NOPM, 0, 0, &cs42l52_micbias_switch),
	SND_SOC_DAPM_PGA("Mic-Bias", CODEC_CS42L52_PWCTL2, 0, 1, NULL, 0),

	/* PGA Power */
	SND_SOC_DAPM_PGA("PGA Left", CODEC_CS42L52_PWCTL1, PWCTL1_PDN_PGAA_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGA Right", CODEC_CS42L52_PWCTL1, PWCTL1_PDN_PGAB_SHIFT, 1, NULL, 0),

	/* Output path */
	SND_SOC_DAPM_MUX("Passthrough Left Playback Switch", SND_SOC_NOPM, 0, 0, &cs42l52_hpa_mux),
	SND_SOC_DAPM_MUX("Passthrough Right Playback Switch", SND_SOC_NOPM, 0, 0, &cs42l52_hpb_mux),

	SND_SOC_DAPM_DAC("DAC Left", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC Right", "Playback", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA("HP Amp Left", CODEC_CS42L52_PWCTL3, 4, 1, NULL, 0),
	SND_SOC_DAPM_PGA("HP Amp Right", CODEC_CS42L52_PWCTL3, 6, 1, NULL, 0),

	SND_SOC_DAPM_PGA("SPK Pwr Left", CODEC_CS42L52_PWCTL3, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPK Pwr Right", CODEC_CS42L52_PWCTL3, 2, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPA"),
	SND_SOC_DAPM_OUTPUT("HPB"),
	SND_SOC_DAPM_OUTPUT("SPKA"),
	SND_SOC_DAPM_OUTPUT("SPKB"),
	SND_SOC_DAPM_OUTPUT("MICBIAS"),

	SND_SOC_DAPM_INPUT("INPUT1A"),
	SND_SOC_DAPM_INPUT("INPUT2A"),
	SND_SOC_DAPM_INPUT("INPUT3A"),
	SND_SOC_DAPM_INPUT("INPUT4A"),
	SND_SOC_DAPM_INPUT("INPUT1B"),
	SND_SOC_DAPM_INPUT("INPUT2B"),
	SND_SOC_DAPM_INPUT("INPUT3B"),
	SND_SOC_DAPM_INPUT("INPUT4B"),
	SND_SOC_DAPM_INPUT("MICA"),
	SND_SOC_DAPM_INPUT("MICB"),
};

static const struct snd_soc_dapm_route soc_cs42l52_audio_map[] = {

	/* adc select path */
	{"ADC Mux Left Capture Switch", "AIN1", "INPUT1A"},
	{"ADC Mux Right Capture Switch", "AIN1", "INPUT1B"},
	{"ADC Mux Left Capture Switch", "AIN2", "INPUT2A"},
	{"ADC Mux Right Capture Switch", "AIN2", "INPUT2B"},
	{"ADC Mux Left Capture Switch", "AIN3", "INPUT3A"},
	{"ADC Mux Right Capture Switch", "AIN3", "INPUT3B"},
	{"ADC Mux Left Capture Switch", "AIN4", "INPUT4A"},
	{"ADC Mux Right Capture Switch", "AIN4", "INPUT4B"},

	/* left capture part */
	{"AIN1A Switch", NULL, "INPUT1A"},
	{"AIN2A Switch", NULL, "INPUT2A"},
	{"AIN3A Switch", NULL, "INPUT3A"},
	{"AIN4A Switch", NULL, "INPUT4A"},
	{"MICA Switch",  NULL, "MICA"},
	{"PGA MICA", NULL, "MICA Switch"},

	{"PGA Left", NULL, "AIN1A Switch"},
	{"PGA Left", NULL, "AIN2A Switch"},
	{"PGA Left", NULL, "AIN3A Switch"},
	{"PGA Left", NULL, "AIN4A Switch"},
	{"PGA Left", NULL, "PGA MICA"},

	/* right capture part */
	{"AIN1B Switch", NULL, "INPUT1B"},
	{"AIN2B Switch", NULL, "INPUT2B"},
	{"AIN3B Switch", NULL, "INPUT3B"},
	{"AIN4B Switch", NULL, "INPUT4B"},
	{"MICB Switch",  NULL, "MICB"},
	{"PGA MICB", NULL, "MICB Switch"},

	{"PGA Right", NULL, "AIN1B Switch"},
	{"PGA Right", NULL, "AIN2B Switch"},
	{"PGA Right", NULL, "AIN3B Switch"},
	{"PGA Right", NULL, "AIN4B Switch"},
	{"PGA Right", NULL, "PGA MICB"},

	{"ADC Mux Left Capture Switch", "PGA", "PGA Left"},
	{"ADC Mux Right Capture Switch", "PGA", "PGA Right"},
	{"ADC Left", NULL, "ADC Mux Left Capture Switch"},
	{"ADC Right", NULL, "ADC Mux Right Capture Switch"},

	/* Mic Bias */
	{"Mic Bias Capture Switch", "On", "PGA MICA"},
	{"Mic Bias Capture Switch", "On", "PGA MICB"},
	{"Mic-Bias", NULL, "Mic Bias Capture Switch"},
	{"Mic-Bias", NULL, "Mic Bias Capture Switch"},
	{"ADC Mux Left Capture Switch",  "PGA", "Mic-Bias"},
	{"ADC Mux Right Capture Switch", "PGA", "Mic-Bias"},
	{"Passthrough Left Playback Switch",  "On", "Mic-Bias"},
	{"Passthrough Right Playback Switch", "On", "Mic-Bias"},

	/* loopback path */
	{"Passthrough Left Playback Switch",  "On",  "PGA Left"},
	{"Passthrough Right Playback Switch", "On",  "PGA Right"},
	{"Passthrough Left Playback Switch",  "Off", "DAC Left"},
	{"Passthrough Right Playback Switch", "Off", "DAC Right"},

	/* Output map */
	/* Headphone */
	{"HP Amp Left",  NULL, "Passthrough Left Playback Switch"},
	{"HP Amp Right", NULL, "Passthrough Right Playback Switch"},
	{"HPA", NULL, "HP Amp Left"},
	{"HPB", NULL, "HP Amp Right"},

	/* Speakers */
	{"SPK Pwr Left",  NULL, "DAC Left"},
	{"SPK Pwr Right", NULL, "DAC Right"},
	{"SPKA", NULL, "SPK Pwr Left"},
	{"SPKB", NULL, "SPK Pwr Right"},

	/* terminator */
	//{NULL, NULL, NULL},
};

static int soc_cs42l52_add_widgets(struct snd_soc_codec *soc_codec)
{
	snd_soc_dapm_new_controls(soc_codec, soc_cs42l52_dapm_widgets,
				ARRAY_SIZE(soc_cs42l52_dapm_widgets));

	snd_soc_dapm_add_routes(soc_codec, soc_cs42l52_audio_map,
                        	ARRAY_SIZE(soc_cs42l52_audio_map));	

	snd_soc_dapm_new_widgets(soc_codec);
        return 0;
}
#if 0
#define SOC_CS42L52_RATES ( SNDRV_PCM_RATE_8000  | SNDRV_PCM_RATE_11025 | \
                            SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
                            SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
                            SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | \
                            SNDRV_PCM_RATE_96000 )
#else
#define SOC_CS42L52_RATES  SNDRV_PCM_RATE_44100
#endif
#define SOC_CS42L52_FORMATS ( SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE | \
                              SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_U18_3LE | \
                              SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_U20_3LE | \
                              SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE )


/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_set_bias_level
 * Purpose  : This function is to get triggered when dapm events occurs.
 *            
 *----------------------------------------------------------------------------
 */
int soc_cs42l52_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level)
{
	u8 pwctl1 = soc_cs42l52_read(codec, CODEC_CS42L52_PWCTL1) & 0x9f;
	u8 pwctl2 = soc_cs42l52_read(codec, CODEC_CS42L52_PWCTL2) & 0x07;

	switch (level) {
        case SND_SOC_BIAS_ON: /* full On */
		SOCDBG("full on\n");
		break;
        case SND_SOC_BIAS_PREPARE: /* partial On */
		SOCDBG("partial on\n");
		pwctl1 &= ~(PWCTL1_PDN_CHRG | PWCTL1_PDN_CODEC);
                soc_cs42l52_write(codec, CODEC_CS42L52_PWCTL1, pwctl1);
                break;
        case SND_SOC_BIAS_STANDBY: /* Off, with power */
		SOCDBG("off with power\n");
		pwctl1 &= ~(PWCTL1_PDN_CHRG | PWCTL1_PDN_CODEC);
                soc_cs42l52_write(codec, CODEC_CS42L52_PWCTL1, pwctl1);
                break;
        case SND_SOC_BIAS_OFF: /* Off, without power */
		SOCDBG("off without power\n");
              soc_cs42l52_write(codec, CODEC_CS42L52_PWCTL1, pwctl1 | 0x9f);
		soc_cs42l52_write(codec, CODEC_CS42L52_PWCTL2, pwctl2 | 0x06);
                break;
        }
        codec->bias_level = level;

        return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : cs42l52_power_init
 * Purpose  : This function is toinit codec to a normal status
 *   
 *----------------------------------------------------------------------------
 */
static void cs42l52_power_init (struct snd_soc_codec *soc_codec)
{
	int i,ret;

	SOCDBG("\n");
	for(i = 0; i < soc_codec->num_dai; i++)
	{
		SOCINF("Cirrus CS42L52 codec , revision %d\n", ret & CHIP_REV_MASK);

		/*set hp default volume*/
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_HPA_VOL, DEFAULT_HP_VOL);
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_HPB_VOL, DEFAULT_HP_VOL);

		/*set spk default volume*/
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_SPKA_VOL, DEFAULT_SPK_VOL);
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_SPKB_VOL, DEFAULT_SPK_VOL);

		/*set output default powerstate*/
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_PWCTL3, 5);

#ifdef AUTO_DETECT_DISABLE
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_CLK_CTL, 
				(soc_cs42l52_read(soc_codec, CODEC_CS42L52_CLK_CTL) 
				 & ~CLK_CTL_AUTODECT_ENABLE));
#else
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_CLK_CTL,
				(soc_cs42l52_read(soc_codec, CODEC_CS42L52_CLK_CTL)
				 |CLK_CTL_AUTODECT_ENABLE));
#endif

		/*default output stream configure*/
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_PB_CTL1, 0x60);
		/*
					(soc_cs42l52_read(soc_codec, CODEC_CS42L52_PB_CTL1)
					| (PB_CTL1_HP_GAIN_07099 << PB_CTL1_HP_GAIN_SHIFT)));
		*///rocky
		/*
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_MISC_CTL,
				(soc_cs42l52_read(soc_codec, CODEC_CS42L52_MISC_CTL))
				| (MISC_CTL_DEEMPH | MISC_CTL_DIGZC | MISC_CTL_DIGSFT));
		*/ //rocky
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_MICA_CTL,
				(soc_cs42l52_read(soc_codec, CODEC_CS42L52_MICA_CTL)
				| 0<<6));/*pre-amplifer 16db*/
		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_MICB_CTL,
				(soc_cs42l52_read(soc_codec, CODEC_CS42L52_MICB_CTL)
				| 0<<6));/*pre-amplifer 16db*/

		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_PWCTL2, 0x00);
		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_ADC_PGA_A, 0x90); 
		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_ADC_PGA_B, 0x90); 

		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_MICA_CTL, 0x2c);
		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_MICB_CTL, 0x2c);

		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_NOISE_GATE_CTL, 0xe3);


		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_IFACE_CTL2,
		 	(soc_cs42l52_read(soc_codec, CODEC_CS42L52_IFACE_CTL2)
				| 0x5));
		

		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_PGAA_CTL, 0x00);  //0dB PGA
		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_PGAB_CTL, 0x00);  //0dB PGA

		 soc_cs42l52_write(soc_codec, CODEC_CS42L52_ADC_HPF_FREQ, 0x0F);  //enable 464Hz HPF


		hp_detected = (soc_cs42l52_read(soc_codec,CODEC_CS42L52_SPK_STATUS) &0x8);	//rocky

		if(hp_detected==0)
		{		
			soc_cs42l52_write(soc_codec, CODEC_CS42L52_MASTERA_VOL, 0x0);
			soc_cs42l52_write(soc_codec, CODEC_CS42L52_MASTERB_VOL, 0x0);
		}
		else
		{
			soc_cs42l52_write(soc_codec, CODEC_CS42L52_MASTERA_VOL, 0x06);
			soc_cs42l52_write(soc_codec, CODEC_CS42L52_MASTERB_VOL, 0x06);
		}
		
	

	}

	return;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_work
 * Purpose  : This function is to power on bias.
 *            
 *----------------------------------------------------------------------------
 */
static void soc_cs42l52_work(struct work_struct *work)
{
	struct snd_soc_codec *codec =
                container_of(work, struct snd_soc_codec, delayed_work.work);

	soc_cs42l52_set_bias_level(codec, codec->bias_level);

	return;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_trigger
 * Purpose  : This function is to respond to trigger.
 *            
 *----------------------------------------------------------------------------
 */
static int soc_cs42l52_trigger(struct snd_pcm_substream *substream,
			  int status,
			  struct snd_soc_dai *dai)
{	
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *codec_dai = machine->codec_dai;

	SOCDBG ("substream->stream:%s status:%d\n",
		   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK":"CAPTURE", status);

	if(status == 1 || status == 0){
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
			codec_dai->playback.active = status;
		}else{
			codec_dai->capture.active = status;
		}
	}

	return 0;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_hw_params
 * Purpose  : This function is to set the hardware parameters for CS42L52.
 *            The functions set the sample rate and audio serial data word 
 *            length.
 *            
 *----------------------------------------------------------------------------
 */
static int soc_cs42l52_hw_params(struct snd_pcm_substream *substream,
                        struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *soc_dev = rtd->socdev;
	struct snd_soc_codec *soc_codec = soc_dev->card->codec;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*)soc_codec->private_data;

	u32 clk = 0;
	int ret = 0;
	int index = soc_cs42l52_get_clk(info->sysclk, params_rate(params));

	SOCDBG("----------sysclk=%d,rate=%d\n",info->sysclk, params_rate(params));

	if(index >= 0)
	{
		info->sysclk = clk_map_table[index].mclk;
		clk |= (clk_map_table[index].speed << CLK_CTL_SPEED_SHIFT) | 
		      (clk_map_table[index].group << CLK_CTL_32K_SR_SHIFT) | 
		      (clk_map_table[index].videoclk << CLK_CTL_27M_MCLK_SHIFT) | 
		      (clk_map_table[index].ratio << CLK_CTL_RATIO_SHIFT) | 
		      clk_map_table[index].mclkdiv2;

#ifdef AUTO_DETECT_DISABLE
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_CLK_CTL, clk);
#else
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_CLK_CTL, 0xa0);
#endif
	}
	else{
		SOCDBG("can't find out right mclk\n");
		ret = -EINVAL;
	}

	return ret;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_set_sysclk
 * Purpose  : This function is to set the DAI system clock
 *            
 *----------------------------------------------------------------------------
 */
static int soc_cs42l52_set_sysclk(struct snd_soc_dai *codec_dai,
			int clk_id, u_int freq, int dir)
{
	int ret = 0;
	struct snd_soc_codec *soc_codec = codec_dai->codec;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*)soc_codec->private_data;

	SOCDBG("info->sysclk=%dHz,freq=%d\n", info->sysclk,freq);

	if((freq >= SOC_CS42L52_MIN_CLK) && (freq <= SOC_CS42L52_MAX_CLK)){
		info->sysclk = freq;
		SOCDBG("info->sysclk set to %d Hz\n", info->sysclk);
	}
	else{
		printk("invalid paramter\n");
		ret = -EINVAL;
	}
	return ret;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_set_fmt
 * Purpose  : This function is to set the DAI format
 *            
 *----------------------------------------------------------------------------
 */
static int soc_cs42l52_set_fmt(struct snd_soc_dai *codec_dai,
			u_int fmt)
{
	struct snd_soc_codec *soc_codec = codec_dai->codec;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*)soc_codec->private_data;
	int ret = 0;
	u8 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		SOCDBG("codec dai fmt master\n");
		iface = IFACE_CTL1_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		SOCDBG("codec dai fmt slave\n");
		break;
	default:
		SOCDBG("invaild formate\n");
		ret = -EINVAL;
		goto done;
	}

	 /* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		SOCDBG("codec dai fmt i2s\n");
		iface |= (IFACE_CTL1_ADC_FMT_I2S | IFACE_CTL1_DAC_FMT_I2S);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		SOCDBG("codec dai fmt right justified\n");
		iface |= IFACE_CTL1_DAC_FMT_RIGHT_J;
		SOCINF("warning only playback stream support this format\n");
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		SOCDBG("codec dai fmt left justified\n");
		iface |= (IFACE_CTL1_ADC_FMT_LEFT_J | IFACE_CTL1_DAC_FMT_LEFT_J);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= IFACE_CTL1_DSP_MODE_EN;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		SOCINF("unsupported format\n");
		ret = -EINVAL;
		goto done;
	default:
		SOCINF("invaild format\n");
		ret = -EINVAL;
		goto done;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		SOCDBG("codec dai fmt normal sclk\n");
		break;
	case SND_SOC_DAIFMT_IB_IF:
		SOCDBG("codec dai fmt inversed sclk\n");
		iface |= IFACE_CTL1_INV_SCLK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= IFACE_CTL1_INV_SCLK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		SOCDBG("unsupported format\n");
		ret = -EINVAL;
	}

	info->format = iface;
done:
	soc_cs42l52_write(soc_codec, CODEC_CS42L52_IFACE_CTL1, info->format);

	return ret;
}

/*
 *----------------------------------------------------------------------------
 * Function : soc_cs42l52_digital_mute
 * Purpose  : This function is to mute DAC or not
 *            
 *----------------------------------------------------------------------------
 */
static int soc_cs42l52_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *soc_codec = dai->codec;
	u8 mute_val = soc_cs42l52_read(soc_codec, CODEC_CS42L52_PB_CTL1) & PB_CTL1_MUTE_MASK;

	SOCDBG("%d\n",mute);

	if(mute) {
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_PB_CTL1, mute_val \
			| PB_CTL1_MSTB_MUTE | PB_CTL1_MSTA_MUTE);
	}
	else {
		soc_cs42l52_write(soc_codec, CODEC_CS42L52_PB_CTL1, mute_val );
	}

	return 0;
}

static struct snd_soc_dai_ops cs42l52_ops = {
			.hw_params		= soc_cs42l52_hw_params,
			.set_sysclk		= soc_cs42l52_set_sysclk,
			.set_fmt		= soc_cs42l52_set_fmt,
			.trigger		= soc_cs42l52_trigger,
			.digital_mute	= soc_cs42l52_digital_mute,
};
/*
 *----------------------------------------------------------------------------
 * @struct  soc_cs42l52_dai |
 *          It is SoC Codec DAI structure which has DAI capabilities viz., 
 *          playback and capture, DAI runtime information viz. state of DAI 
 *			and pop wait state, and DAI private data. 
 *          The AIC3111 rates ranges from 8k to 192k
 *          The PCM bit format supported are 16, 20, 24 and 32 bits
 *----------------------------------------------------------------------------
 */
struct  snd_soc_dai soc_cs42l52_dai = {
            .name = SOC_CS42L52_NAME,
            .playback = {
                    .stream_name = "Playback",
                    .channels_min = 1,
                    .channels_max = SOC_CS42L52_DEFAULT_MAX_CHANS,
                    .rates = SOC_CS42L52_RATES,
                    .formats = SOC_CS42L52_FORMATS,
            },
            .capture = {
                    .stream_name = "Capture",
                    .channels_min = 1,
                    .channels_max = SOC_CS42L52_DEFAULT_MAX_CHANS,
                    .rates = SOC_CS42L52_RATES,
                    .formats = SOC_CS42L52_FORMATS,
            },
			.ops = &cs42l52_ops,
};
EXPORT_SYMBOL_GPL(soc_cs42l52_dai);

//added by koffu for Work
/*
 *----------------------------------------------------------------------------
 * Function: soc_codec_detect_hp
 * Purpose : Read  CODEC_CS42L52_SPK_STATUS (Speaker Status(Address 31h)) 
 *               Indicates the status of the SPKR/HP pin.                  
 *               if (1<<3) ,  select Speaker mod , if (0<<3) , select  Headset mod;    
 *----------------------------------------------------------------------------
 */
static void  soc_codec_detect_hp(struct work_struct *work)
{

	u8 val =  soc_cs42l52_read(cs42l52_codec,CODEC_CS42L52_SPK_STATUS);
	val = val&0x08;
	
	if(val == hp_detected){
		schedule_delayed_work(&delaywork, msecs_to_jiffies(200));
		return;
	}

	hp_detected = val;
	
	if(val == 0){
		SOCDBG("hp is detected \n");
		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_MASTERA_VOL, 0x00);
		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_MASTERB_VOL, 0x00);

		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_BEEP_TONE_CTL, 0X00);
		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_TONE_CTL, 0X00);		
	}
	else
	{
		SOCDBG("speaker is detected \n");
		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_MASTERA_VOL, 0x06);
		soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_MASTERB_VOL, 0x06);

	}

	schedule_delayed_work(&delaywork, msecs_to_jiffies(200));

}

#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
static int cs42l52_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{	
	struct snd_soc_codec *soc_codec;
	struct soc_codec_cs42l52 * info;
	struct cs42l52_platform_data *pdata = i2c->dev.platform_data;
	int ret = 0;

	soc_codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (soc_codec == NULL)
		return -ENOMEM;

	soc_codec->name = SOC_CS42L52_NAME;
	soc_codec->owner = THIS_MODULE;
	soc_codec->write = soc_cs42l52_write;
	soc_codec->read = soc_cs42l52_read;
	soc_codec->hw_write = (hw_write_t)i2c_master_send;
	mutex_init(&soc_codec->mutex);
	INIT_LIST_HEAD(&soc_codec->dapm_widgets);
	INIT_LIST_HEAD(&soc_codec->dapm_paths);
	
	soc_codec->set_bias_level = soc_cs42l52_set_bias_level;
	soc_codec->dai = &soc_cs42l52_dai;
	soc_codec->dai->playback.channels_max = 2;
	soc_codec->dai->capture.channels_max = 2;
	soc_codec->num_dai = 1;
	soc_codec->control_data = i2c;
	soc_codec->dev = &i2c->dev;	
	soc_codec->pcm_devs = 0;
	soc_codec->pop_time = 2;
	soc_codec->dai[0].codec = soc_codec;

	soc_codec->reg_cache_size = sizeof(soc_cs42l52_reg_default);

	soc_codec->reg_cache = kmemdup(soc_cs42l52_reg_default, sizeof(soc_cs42l52_reg_default), GFP_KERNEL);

	info = (struct soc_codec_cs42l52 *)kmalloc(sizeof(struct soc_codec_cs42l52),GFP_KERNEL);
	if (info == NULL) {
		kfree(soc_codec);
		return -ENOMEM;
	}

	info->sysclk = SOC_CS42L52_DEFAULT_CLK;
	info->format = SOC_CS42L52_DEFAULT_FORMAT;

	soc_codec->private_data =(void*)info;	
	if(!soc_codec->reg_cache) {
		SOCERR("%s: err out of memory\n", __FUNCTION__);
		ret = -ENOMEM;
		goto err;
	}

	if (pdata->init_platform_hw)                              
		pdata->init_platform_hw();

	/*initialize codec*/
	cs42l52_power_init(soc_codec);

    INIT_DELAYED_WORK(&soc_codec->delayed_work, soc_cs42l52_work);

	soc_cs42l52_dai.dev = &i2c->dev;
	cs42l52_codec = soc_codec;

	INIT_DELAYED_WORK(&delaywork, soc_codec_detect_hp);
	schedule_delayed_work(&delaywork, msecs_to_jiffies(200));

	ret = snd_soc_register_codec(soc_codec);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

	ret = snd_soc_register_dai(&soc_cs42l52_dai);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}

	return ret;

err_codec:
	snd_soc_unregister_codec(soc_codec);
err:
	kfree(cs42l52_codec);
	cs42l52_codec = NULL;
	return ret;
}

static int cs42l52_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_dai(&soc_cs42l52_dai);
	snd_soc_unregister_codec(cs42l52_codec);

	soc_cs42l52_set_bias_level(cs42l52_codec, SND_SOC_BIAS_OFF);

	soc_cs42l52_dai.dev = NULL;
	if(cs42l52_codec->reg_cache)		
		kfree(cs42l52_codec->reg_cache);
	if(cs42l52_codec->private_data)		
		kfree(cs42l52_codec->private_data);
	kfree(cs42l52_codec);
	cs42l52_codec = NULL;

	return 0;
}


static int cs42l52_i2c_shutdown(struct i2c_client *client)
{
	SOCDBG("i am here\n");        
	snd_soc_unregister_dai(&soc_cs42l52_dai);
	snd_soc_unregister_codec(cs42l52_codec);

	soc_cs42l52_set_bias_level(cs42l52_codec, SND_SOC_BIAS_OFF);
	
	cancel_delayed_work(&delaywork);

	soc_cs42l52_dai.dev = NULL;
	if(cs42l52_codec->reg_cache)		
		kfree(cs42l52_codec->reg_cache);
	if(cs42l52_codec->private_data)		
		kfree(cs42l52_codec->private_data);
	kfree(cs42l52_codec);
	cs42l52_codec = NULL;

	return 0;
}

static const struct i2c_device_id cs42l52_i2c_id[] = {
	{ "cs42l52", 0 },
};
MODULE_DEVICE_TABLE(i2c, cs42l52_i2c_id);

static struct i2c_driver cs42l52_i2c_drv = {
	.driver = {
		.name = "CS42L52",
		.owner = THIS_MODULE,
	},
	.probe =    cs42l52_i2c_probe,
	.remove =   cs42l52_i2c_remove,
	.shutdown =  cs42l52_i2c_shutdown,
	.id_table = cs42l52_i2c_id,
};

#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
static int soc_cs42l52_suspend(struct early_suspend *h)
{
	
	soc_cs42l52_write(cs42l52_codec, CODEC_CS42L52_PWCTL1, PWCTL1_PDN_CODEC);
	soc_cs42l52_set_bias_level(cs42l52_codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int soc_cs42l52_resume(struct early_suspend *h)
{
	struct snd_soc_codec *soc_codec = cs42l52_codec;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*) soc_codec->private_data;
	int i, reg;
	u8 data[2];
	u8 *reg_cache = (u8*) soc_codec->reg_cache;
	soc_codec->num_dai = 1;
	/* Sync reg_cache with the hardware */
	for(i = 0; i < soc_codec->num_dai; i++) {

	    for(reg = 0; reg < ARRAY_SIZE(soc_cs42l52_reg_default); reg++) {
		data[0] = reg;
		data[1] = reg_cache[reg];
		if(soc_codec->hw_write(soc_codec->control_data, data, 2) != 2)
		    break;
	    }
	}

	soc_cs42l52_set_bias_level(soc_codec, SND_SOC_BIAS_STANDBY);

	/*charge cs42l52 codec*/
	if(soc_codec->suspend_bias_level == SND_SOC_BIAS_ON)
	{
		soc_cs42l52_set_bias_level(soc_codec, SND_SOC_BIAS_PREPARE);
		soc_codec->bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&soc_codec->delayed_work, msecs_to_jiffies(1000));
	}
	return 0;

}
#else
static int soc_cs42l52_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *soc_dev = (struct snd_soc_device*)platform_get_drvdata(pdev);
	struct snd_soc_codec *soc_codec = soc_dev->card->codec;
	
	soc_cs42l52_write(soc_codec, CODEC_CS42L52_PWCTL1, PWCTL1_PDN_CODEC);
	soc_cs42l52_set_bias_level(soc_codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int soc_cs42l52_resume(struct platform_device *pdev)
{
	struct snd_soc_device *soc_dev = (struct snd_soc_device*) platform_get_drvdata(pdev);
	struct snd_soc_codec *soc_codec = soc_dev->card->codec;
	struct soc_codec_cs42l52 *info = (struct soc_codec_cs42l52*) soc_codec->private_data;
	int i, reg;
	u8 data[2];
	u8 *reg_cache = (u8*) soc_codec->reg_cache;
	soc_codec->num_dai = 1;
	/* Sync reg_cache with the hardware */
	for(i = 0; i < soc_codec->num_dai; i++) {

	    for(reg = 0; reg < ARRAY_SIZE(soc_cs42l52_reg_default); reg++) {
		data[0] = reg;
		data[1] = reg_cache[reg];
		if(soc_codec->hw_write(soc_codec->control_data, data, 2) != 2)
		    break;
	    }
	}

	soc_cs42l52_set_bias_level(soc_codec, SND_SOC_BIAS_STANDBY);

	/*charge cs42l52 codec*/
	if(soc_codec->suspend_bias_level == SND_SOC_BIAS_ON)
	{
		soc_cs42l52_set_bias_level(soc_codec, SND_SOC_BIAS_PREPARE);
		soc_codec->bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&soc_codec->delayed_work, msecs_to_jiffies(1000));
	}
	return 0;

}
#endif
static int soc_cs42l52_probe(struct platform_device *pdev)
{
	struct snd_soc_device *soc_dev = platform_get_drvdata(pdev);
	struct snd_soc_codec *soc_codec;
	int ret = 0;

	if (cs42l52_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	soc_dev->card->codec = cs42l52_codec;
	soc_codec = cs42l52_codec;

	ret = snd_soc_new_pcms(soc_dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if(ret)
	{
		SOCERR("%s: add new pcms failed\n",__FUNCTION__);
		goto pcm_err;
	}

	soc_cs42l52_add_controls(soc_codec);
	soc_cs42l52_add_widgets(soc_codec);

	ret = snd_soc_init_card(soc_dev);

	INIT_DELAYED_WORK(&soc_codec->delayed_work, soc_cs42l52_work);

	if(ret)
	{
		SOCERR("add snd card failed\n");
		goto card_err;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND   
	cs42l52_early_suspend.suspend =soc_cs42l52_suspend;  
	cs42l52_early_suspend.resume =soc_cs42l52_resume;//   cs42l52_early_suspend.level = 0x2;    
	register_early_suspend(&cs42l52_early_suspend);
#endif
	return ret;

card_err:
	snd_soc_free_pcms(soc_dev);
	snd_soc_dapm_free(soc_dev);
pcm_err:
	return ret;

}

static int soc_cs42l52_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
#ifdef CONFIG_HAS_EARLYSUSPEND		
	unregister_early_suspend(&cs42l52_early_suspend);
#endif 
	return 0;
}

struct snd_soc_codec_device soc_codec_dev_cs42l52 = {
	.probe = soc_cs42l52_probe,
	.remove = soc_cs42l52_remove,
#ifndef 	CONFIG_HAS_EARLYSUSPEND
	.suspend = soc_cs42l52_suspend,
	.resume = soc_cs42l52_resume,
#endif	
};

EXPORT_SYMBOL_GPL(soc_codec_dev_cs42l52);

static int __init cs42l52_modinit(void)
{
	return i2c_add_driver(&cs42l52_i2c_drv);
}
module_init(cs42l52_modinit);

static void __exit cs42l52_exit(void)
{
	i2c_del_driver(&cs42l52_i2c_drv);
}
module_exit(cs42l52_exit);



MODULE_DESCRIPTION("ALSA SoC CS42L52 Codec");
MODULE_AUTHOR("Bo Liu, Bo.Liu@cirrus.com, www.cirrus.com");
MODULE_LICENSE("GPL");




#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
static int proc_cs42l52_show (struct seq_file *s, void *v)
{
	struct snd_soc_codec *codec = cs42l52_codec;
	int reg;

	seq_printf (s, "    cs42l52 registers:\n");
	for (reg = 0; reg < 53; reg++) {
		if (reg%10 == 0) 
			seq_printf (s, "\n            ");
		seq_printf (s, "0x%02x ", soc_cs42l52_read(codec, reg));
	}
	seq_printf (s, "\n\n");

#if 0//for check cache
	u8 *cache = codec->reg_cache;
	seq_printf (s, "            cache:\n");
	for (reg = 0; reg < 53; reg++) {
		if (reg%10 == 0) 
			seq_printf (s, "\n            ");
		seq_printf (s, "0x%02x ", cache[reg]);
	}
	seq_printf (s, "\n\n");
#endif

	return 0;
}

static int proc_cs42l52_open (struct inode *inode, struct file *file)
{
	return single_open (file, proc_cs42l52_show, NULL);
}

static const struct file_operations proc_cs42l52_fops = {
	.open		= proc_cs42l52_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init codec_proc_init (void)
{
	proc_create ("cs42l52", 0, NULL, &proc_cs42l52_fops);
	return 0;
}
late_initcall (codec_proc_init);
#endif /* CONFIG_PROC_FS */
