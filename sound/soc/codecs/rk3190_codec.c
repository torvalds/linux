/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rk3190_codec.c  --  RK3190 CODEC ALSA SoC audio driver
 *
 * Copyright 2013 Rockchip
 * Author: zhangjun <showy.zhang@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/io.h>

#include "rk3190_codec.h"

#ifdef CONFIG_RK_HEADSET_DET
#include "../../../drivers/headset_observe/rk_headset.h"
#endif

#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

/* volume setting
 *  0: -39dB
 *  26: 0dB
 *  31: 6dB
 *  Step: 1.5dB
*/
#define  OUT_VOLUME    26//0~31

/* capture vol set
 * 0: -18db
 * 12: 0db
 * 31: 28.5db
 * step: 1.5db
*/
#define CAP_VOL	   18//0-31

//with capacity or  not
#define WITH_CAP
#define SPK_AMP_DELAY 200
#define HP_MOS_DELAY 200
#ifdef CONFIG_MACH_RK_FAC 
  rk3190_hdmi_ctrl=0;
#endif

#define GPIO_LOW 0
#define GPIO_HIGH 1
#define INVALID_GPIO -1

struct rk3190_codec_priv {
	struct snd_soc_codec *codec;

	unsigned int stereo_sysclk;
	unsigned int rate;

	int playback_active;
	int capture_active;

	int spk_ctl_gpio;
	int hp_ctl_gpio;
	int ear_ctl_gpio;
    int mic_sel_gpio;
	int delay_time;

	long int playback_path;
	long int capture_path;
	long int voice_call_path;

	int	regbase;
	int	regbase_phy;
	int	regsize_phy;
	struct clk	*pclk;
};

static struct rk3190_codec_priv *rk3190_priv = NULL;

#define RK3190_CODEC_ALL	0
#define RK3190_CODEC_PLAYBACK	1
#define RK3190_CODEC_CAPTURE	2
#define RK3190_CODEC_INCALL	3

static bool rk3190_for_mid = 1;

static const unsigned int rk3190_reg_defaults[RK3190_PGA_AGC_CTL5+1] = {
	[RK3190_RESET] = 0x0003,
	[RK3190_ADC_INT_CTL1] = 0x0050,
	[RK3190_ADC_INT_CTL2] = 0x000e,
	[RK3190_DAC_INT_CTL1] = 0x0050,
	[RK3190_DAC_INT_CTL2] = 0x000e,
	[RK3190_BIST_CTL] = 0x0000,
	[RK3190_SELECT_CURRENT] = 0x0001,
	[RK3190_BIAS_CTL] = 0x0000,
	[RK3190_ADC_CTL] = 0x0000,
	[RK3190_BST_CTL] = 0x0000,
	[RK3190_ALC_MUNIN_CTL] = 0x0044,
	[RK3190_ALCL_GAIN_CTL] = 0x000c,
	[RK3190_ALCR_GAIN_CTL] = 0x000c,
	[RK3190_ADC_ENABLE] = 0x0000,
	[RK3190_DAC_CTL] = 0x0000,
	[RK3190_DAC_ENABLE] = 0x0000,
	[RK3190_HPMIX_CTL] = 0x0000,
	[RK3190_HPMIX_S_SELECT] = 0x0000,
	[RK3190_HPOUT_CTL] = 0x0000,
	[RK3190_HPOUTL_GAIN] = 0x0000,
	[RK3190_HPOUTR_GAIN] = 0x0000,
	[RK3190_PGA_AGC_CTL1] = 0x0000,
	[RK3190_PGA_AGC_CTL2] = 0x0046,
	[RK3190_PGA_AGC_CTL3] = 0x0041,
	[RK3190_PGA_AGC_CTL4] = 0x002c,
	[RK3190_PGA_ASR_CTL] = 0x0000,
	[RK3190_PGA_AGC_MAX_H] = 0x0026,
	[RK3190_PGA_AGC_MAX_L] = 0x0040,
	[RK3190_PGA_AGC_MIN_H] = 0x0036,
	[RK3190_PGA_AGC_MIN_L] = 0x0020,
	[RK3190_PGA_AGC_CTL5] = 0x0038,
};

static struct rk3190_init_bit_typ rk3190_init_bit_list[] = {
	{RK3190_HPOUT_CTL, RK3190_HPOUTL_EN, RK3190_HPOUTL_WORK,RK3190_HPVREF_EN},
	{RK3190_HPOUT_CTL, RK3190_HPOUTR_EN, RK3190_HPOUTR_WORK,RK3190_HPVREF_WORK},
	{RK3190_HPMIX_CTL, RK3190_HPMIXR_EN, RK3190_HPMIXR_WORK2,RK3190_HPMIXR_WORK1},
	{RK3190_HPMIX_CTL, RK3190_HPMIXL_EN, RK3190_HPMIXL_WORK2,RK3190_HPMIXL_WORK1},

};
#define RK3190_INIT_BIT_LIST_LEN ARRAY_SIZE(rk3190_init_bit_list)

static int rk3190_init_bit_register(unsigned int reg, int i)
{
	for (; i < RK3190_INIT_BIT_LIST_LEN; i++) {
		if (rk3190_init_bit_list[i].reg == reg)
			return i;
	}

	return -1;
}

static unsigned int rk3190_codec_read(struct snd_soc_codec *codec, unsigned int reg);
static inline void rk3190_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value);

static unsigned int rk3190_set_init_value(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	unsigned int read_value, power_bit, set_bit2,set_bit1;
	int i;
	int tmp = 0;
	// read codec init register
	i = rk3190_init_bit_register(reg, 0);

	// set codec init bit
	// widget init bit should be setted 0 after widget power up or unmute,
	// and should be setted 1 after widget power down or mute.
	if (i >= 0) {
		read_value = rk3190_codec_read(codec, reg);
		while (i >= 0) {
			power_bit = rk3190_init_bit_list[i].power_bit;
			set_bit2 = rk3190_init_bit_list[i].init2_bit;
			set_bit1 = rk3190_init_bit_list[i].init1_bit;

			if ((read_value & power_bit) != (value & power_bit))
			{
				if (value & power_bit)
				{
					tmp = value | set_bit2 | set_bit1;
					writel(value, rk3190_priv->regbase+reg);
					writel(tmp, rk3190_priv->regbase+reg);
				  	
				}
				else
				{	
					tmp = value & (~set_bit2) & (~set_bit1);
					writel(tmp, rk3190_priv->regbase+reg);
					writel(value, rk3190_priv->regbase+reg);
				}	
				value = tmp;			
			}
			else
			{
				if (read_value != value)
					writel(value, rk3190_priv->regbase+reg);
			}
				
			i = rk3190_init_bit_register(reg, ++i);
			
			rk3190_write_reg_cache(codec, reg, value);
		}
	}
	else
	{
		return i;		
	}

	return value;
}

static int rk3190_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK3190_RESET:
		return 1;
	default:
		return 0;
	}
}

static int rk3190_codec_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK3190_RESET:
	case RK3190_ADC_INT_CTL1:
	case RK3190_ADC_INT_CTL2:
	case RK3190_DAC_INT_CTL1:
	case RK3190_DAC_INT_CTL2:
	case RK3190_BIST_CTL:
	case RK3190_SELECT_CURRENT:
	case RK3190_BIAS_CTL:
    case RK3190_ADC_CTL:
    case RK3190_BST_CTL:
	case RK3190_ALC_MUNIN_CTL:
	case RK3190_ALCL_GAIN_CTL:
	case RK3190_ALCR_GAIN_CTL:
	case RK3190_ADC_ENABLE:
	case RK3190_DAC_CTL:
	case RK3190_DAC_ENABLE:
	case RK3190_HPMIX_CTL:
	case RK3190_HPMIX_S_SELECT:
	case RK3190_HPOUT_CTL:
	case RK3190_HPOUTL_GAIN:
	case RK3190_HPOUTR_GAIN:
	case RK3190_PGA_AGC_CTL1:
	case RK3190_PGA_AGC_CTL2:
	case RK3190_PGA_AGC_CTL3:
	case RK3190_PGA_AGC_CTL4:
	case RK3190_PGA_ASR_CTL:
	case RK3190_PGA_AGC_MAX_H:
	case RK3190_PGA_AGC_MAX_L:
	case RK3190_PGA_AGC_MIN_H:
	case RK3190_PGA_AGC_MIN_L:
	case RK3190_PGA_AGC_CTL5:
		return 1;
	default:
		return 0;
	}
}

static inline unsigned int rk3190_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	unsigned int *cache = codec->reg_cache;
	
	if (rk3190_codec_register(codec, reg) )
		return  cache[reg];

	printk("%s : reg error!\n", __func__);

	return -EINVAL;
}

static inline void rk3190_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	unsigned int *cache = codec->reg_cache;

	if (rk3190_codec_register(codec, reg)) {
		cache[reg] = value;
		return;
	}

	printk("%s : reg error!\n", __func__);
}

static unsigned int rk3190_codec_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int value;

	if (!rk3190_priv) {
		printk("%s : rk3190 is NULL\n", __func__);
		return -EINVAL;
	}

	if (!rk3190_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	if (rk3190_volatile_register(codec, reg) == 0) {
		value = rk3190_read_reg_cache(codec, reg);
	} else {
		value = readl_relaxed(rk3190_priv->regbase+reg);	
	}

	value = readl_relaxed(rk3190_priv->regbase+reg);
	DBG("%s : reg = 0x%x, val= 0x%x\n", __func__, reg, value);

	return value;
}

static int rk3190_codec_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	int new_value = -1;

	if (!rk3190_priv) {
		printk("%s : rk3190 is NULL\n", __func__);
		return -EINVAL;
	} else if (!rk3190_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	//new_value = rk3190_set_init_value(codec, reg, value);

	if (new_value == -1)
	{
		writel(value, rk3190_priv->regbase+reg);
		rk3190_write_reg_cache(codec, reg, value);
	}
		
	DBG("%s : reg = 0x%x, val = 0x%x, new_value=%d\n", __func__, reg, value,new_value);
	return 0;
}

static int rk3190_hw_write(const struct i2c_client *client, const char *buf, int count)
{
	unsigned int reg, value;

	if (!rk3190_priv || !rk3190_priv->codec) {
		printk("%s : rk3190_priv or rk3190_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (count == 3) {
		reg = (unsigned int)buf[0];
		value = (buf[1] & 0xff00) | (0x00ff & buf[2]);
		writel(value, rk3190_priv->regbase+reg);
	} else {
		printk("%s : i2c len error\n", __func__);
	}

	return  count;
}

static int rk3190_reset(struct snd_soc_codec *codec)
{
	writel(0x00, rk3190_priv->regbase+RK3190_RESET);
	mdelay(10);
	writel(0x03, rk3190_priv->regbase+RK3190_RESET);
	mdelay(10);

	memcpy(codec->reg_cache, rk3190_reg_defaults,
	       sizeof(rk3190_reg_defaults));

	return 0;
}

int rk3190_headset_mic_detect(bool headset_status)
{
#if 0
	struct snd_soc_codec *codec = rk3190_priv->codec;

	DBG("%s\n", __func__);

	if (!rk3190_priv || !rk3190_priv->codec) {
		printk("%s : rk3190_priv or rk3190_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (headset_status) {
		snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL,
				RK3190_MICBIAS2_PWRD | RK3190_MICBIAS2_V_MASK,
				RK3190_MICBIAS2_V_1_7);
	} else {// headset is out, disable MIC2 && MIC1 Bias
		DBG("%s : headset is out,disable Mic2 Bias\n", __func__);
		snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL,
				RK3190_MICBIAS2_PWRD | RK3190_MICBIAS2_V_MASK,
				RK3190_MICBIAS2_V_1_0|RK3190_MICBIAS2_PWRD);
	}
#endif
	return 0;
}
EXPORT_SYMBOL(rk3190_headset_mic_detect);

#if 0
static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -3900, 150, 0);
static const DECLARE_TLV_DB_SCALE(pga_vol_tlv, -1800, 150, 0);
static const DECLARE_TLV_DB_SCALE(bst_vol_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_max_vol_tlv, -1350, 600, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_min_vol_tlv, -1800, 600, 0);

static const char *rk3190_input_mode[] = {"Differential","Single-Ended"}; 

static const char *rk3190_micbias_ratio[] = {"1.0 Vref", "1.1 Vref",
		"1.2 Vref", "1.3 Vref", "1.4 Vref", "1.5 Vref", "1.6 Vref", "1.7 Vref",};

static const char *rk3190_dis_en_sel[] = {"Disable", "Enable"};

static const char *rk3190_pga_agc_way[] = {"Normal", "Jack"};

static const char *rk3190_agc_backup_way[] = {"Normal", "Jack1", "Jack2", "Jack3"};

static const char *rk3190_pga_agc_hold_time[] = {"0ms", "2ms",
		"4ms", "8ms", "16ms", "32ms", "64ms", "128ms", "256ms", "512ms", "1s"};

static const char *rk3190_pga_agc_ramp_up_time[] = {"Normal:500us Jack:125us",
		"Normal:1ms Jack:250us", "Normal:2ms Jack:500us", "Normal:4ms Jack:1ms",
		"Normal:8ms Jack:2ms", "Normal:16ms Jack:4ms", "Normal:32ms Jack:8ms",
		"Normal:64ms Jack:16ms", "Normal:128ms Jack:32ms", "Normal:256ms Jack:64ms",
		"Normal:512ms Jack:128ms"};

static const char *rk3190_pga_agc_ramp_down_time[] = {"Normal:125us Jack:32us",
		"Normal:250us Jack:64us", "Normal:500us Jack:125us", "Normal:1ms Jack:250us",
		"Normal:2ms Jack:500us", "Normal:4ms Jack:1ms", "Normal:8ms Jack:2ms",
		"Normal:16ms Jack:4ms", "Normal:32ms Jack:8ms", "Normal:64ms Jack:16ms",
		"Normal:128ms Jack:32ms"};

static const char *rk3190_pga_agc_mode[] = {"Normal", "Limiter"};

static const char *rk3190_pga_agc_recovery_mode[] = {"Right Now", "After AGC to Limiter"};

static const char *rk3190_pga_agc_noise_gate_threhold[] = {"-39dB", "-45dB", "-51dB",
		"-57dB", "-63dB", "-69dB", "-75dB", "-81dB"};

static const char *rk3190_pga_agc_update_gain[] = {"Right Now", "After 1st Zero Cross"};

static const char *rk3190_pga_agc_approximate_sample_rate[] = {"96KHZ","48KHz","441KHZ", "32KHz",
		"24KHz", "16KHz", "12KHz", "8KHz"};

static const struct soc_enum rk3190_bst_enum[] = {
SOC_ENUM_SINGLE(RK3190_BSTL_ALCL_CTL, RK3190_BSTL_MODE_SFT, 2, rk3190_input_mode),
};


static const struct soc_enum rk3190_micbias_enum[] = {
SOC_ENUM_SINGLE(RK3190_ADC_MIC_CTL, RK3190_MICBIAS_VOL_SHT, 8, rk3190_micbias_ratio),
};

static const struct soc_enum rk3190_agcl_enum[] = {
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL1, RK3190_PGA_AGC_BK_WAY_SFT, 4, rk3190_agc_backup_way),/*0*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL1, RK3190_PGA_AGC_WAY_SFT, 2, rk3190_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL1, RK3190_PGA_AGC_HOLD_T_SFT, 11, rk3190_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL2, RK3190_PGA_AGC_GRU_T_SFT, 11, rk3190_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL2, RK3190_PGA_AGC_GRD_T_SFT, 11, rk3190_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_MODE_SFT, 2, rk3190_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_ZO_SFT, 2, rk3190_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_REC_MODE_SFT, 2, rk3190_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_FAST_D_SFT, 2, rk3190_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_NG_SFT, 2, rk3190_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL3, RK3190_PGA_AGC_NG_THR_SFT, 8, rk3190_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL4, RK3190_PGA_AGC_ZO_MODE_SFT, 2, rk3190_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK3190_PGAL_ASR_CTL, RK3190_PGA_SLOW_CLK_SFT, 2, rk3190_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK3190_PGAL_ASR_CTL, RK3190_PGA_ASR_SFT, 8, rk3190_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK3190_PGAL_AGC_CTL5, RK3190_PGA_AGC_SFT, 2, rk3190_dis_en_sel),/*14*/
};

static const struct soc_enum rk3190_agcr_enum[] = {
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL1, RK3190_PGA_AGC_BK_WAY_SFT, 4, rk3190_agc_backup_way),/*0*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL1, RK3190_PGA_AGC_WAY_SFT, 2, rk3190_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL1, RK3190_PGA_AGC_HOLD_T_SFT, 11, rk3190_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL2, RK3190_PGA_AGC_GRU_T_SFT, 11, rk3190_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL2, RK3190_PGA_AGC_GRD_T_SFT, 11, rk3190_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_MODE_SFT, 2, rk3190_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_ZO_SFT, 2, rk3190_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_REC_MODE_SFT, 2, rk3190_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_FAST_D_SFT, 2, rk3190_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_NG_SFT, 2, rk3190_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL3, RK3190_PGA_AGC_NG_THR_SFT, 8, rk3190_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL4, RK3190_PGA_AGC_ZO_MODE_SFT, 2, rk3190_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK3190_PGAR_ASR_CTL, RK3190_PGA_SLOW_CLK_SFT, 2, rk3190_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK3190_PGAR_ASR_CTL, RK3190_PGA_ASR_SFT, 8, rk3190_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK3190_PGAR_AGC_CTL5, RK3190_PGA_AGC_SFT, 2, rk3190_dis_en_sel),/*14*/
};

static const struct snd_kcontrol_new rk3190_snd_controls[] = {
	//Add for set voice volume
	SOC_DOUBLE_R_TLV("Speaker Playback Volume", RK3190_HPOUTL_GAIN,
		RK3190_HPOUTR_GAIN, RK3190_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Speaker Playback Switch", RK3190_HPOUT_CTL,
		RK3190_HPOUTL_MUTE_SHT, RK3190_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RK3190_HPOUTL_GAIN,
		RK3190_HPOUTR_GAIN, RK3190_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Headphone Playback Switch", RK3190_HPOUT_CTL,
		RK3190_HPOUTL_MUTE_SHT, RK3190_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Earpiece Playback Volume", RK3190_HPOUTL_GAIN,
		RK3190_HPOUTR_GAIN, RK3190_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Earpiece Playback Switch", RK3190_HPOUT_CTL,
		RK3190_HPOUTL_MUTE_SHT, RK3190_HPOUTR_MUTE_SHT, 1, 0),


	//Add for set capture mute
	SOC_SINGLE_TLV("Main Mic Capture Volume", RK3190_BST_CTL,
		RK3190_BSTL_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Main Mic Capture Switch", RK3190_BST_CTL,
		RK3190_BSTL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("Headset Mic Capture Volume", RK3190_BST_CTL,
		RK3190_BSTR_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Headset Mic Capture Switch", RK3190_BST_CTL,
		RK3190_BSTR_MUTE_SHT, 1, 0),

	SOC_SINGLE("ALCL Switch", RK3190_ALC_MUNIN_CTL,
		RK3190_ALCL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCL Capture Volume", RK3190_BSTL_ALCL_CTL,
		RK3190_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),
	SOC_SINGLE("ALCR Switch", RK3190_ALC_MUNIN_CTL,
		RK3190_ALCR_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCR Capture Volume", RK3190_ALCR_GAIN_CTL,
		RK3190_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),

	SOC_ENUM("BST_L Mode",  rk3190_bst_enum[0]),

	SOC_ENUM("Micbias Voltage",  rk3190_micbias_enum[0]),
	SOC_ENUM("PGAL AGC Back Way",  rk3190_agcl_enum[0]),
	SOC_ENUM("PGAL AGC Way",  rk3190_agcl_enum[1]),
	SOC_ENUM("PGAL AGC Hold Time",  rk3190_agcl_enum[2]),
	SOC_ENUM("PGAL AGC Ramp Up Time",  rk3190_agcl_enum[3]),
	SOC_ENUM("PGAL AGC Ramp Down Time",  rk3190_agcl_enum[4]),
	SOC_ENUM("PGAL AGC Mode",  rk3190_agcl_enum[5]),
	SOC_ENUM("PGAL AGC Gain Update Zero Enable",  rk3190_agcl_enum[6]),
	SOC_ENUM("PGAL AGC Gain Recovery LPGA VOL",  rk3190_agcl_enum[7]),
	SOC_ENUM("PGAL AGC Fast Decrement Enable",  rk3190_agcl_enum[8]),
	SOC_ENUM("PGAL AGC Noise Gate Enable",  rk3190_agcl_enum[9]),
	SOC_ENUM("PGAL AGC Noise Gate Threhold",  rk3190_agcl_enum[10]),
	SOC_ENUM("PGAL AGC Upate Gain",  rk3190_agcl_enum[11]),
	SOC_ENUM("PGAL AGC Slow Clock Enable",  rk3190_agcl_enum[12]),
	SOC_ENUM("PGAL AGC Approximate Sample Rate",  rk3190_agcl_enum[13]),
	SOC_ENUM("PGAL AGC Enable",  rk3190_agcl_enum[14]),

	SOC_SINGLE_TLV("PGAL AGC Volume", RK3190_PGAL_AGC_CTL4,
		RK3190_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 5 is 1

	SOC_SINGLE("PGAL AGC Max Level High 8 Bits", RK3190_PGAL_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Max Level Low 8 Bits", RK3190_PGAL_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level High 8 Bits", RK3190_PGAL_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level Low 8 Bits", RK3190_PGAL_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAL AGC Max Gain", RK3190_PGAL_AGC_CTL5,
		RK3190_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x0a bit 5 is 1
	SOC_SINGLE_TLV("PGAL AGC Min Gain", RK3190_PGAL_AGC_CTL5,
		RK3190_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x0a bit 5 is 1

	SOC_ENUM("PGAR AGC Back Way",  rk3190_agcr_enum[0]),
	SOC_ENUM("PGAR AGC Way",  rk3190_agcr_enum[1]),
	SOC_ENUM("PGAR AGC Hold Time",  rk3190_agcr_enum[2]),
	SOC_ENUM("PGAR AGC Ramp Up Time",  rk3190_agcr_enum[3]),
	SOC_ENUM("PGAR AGC Ramp Down Time",  rk3190_agcr_enum[4]),
	SOC_ENUM("PGAR AGC Mode",  rk3190_agcr_enum[5]),
	SOC_ENUM("PGAR AGC Gain Update Zero Enable",  rk3190_agcr_enum[6]),
	SOC_ENUM("PGAR AGC Gain Recovery LPGA VOL",  rk3190_agcr_enum[7]),
	SOC_ENUM("PGAR AGC Fast Decrement Enable",  rk3190_agcr_enum[8]),
	SOC_ENUM("PGAR AGC Noise Gate Enable",  rk3190_agcr_enum[9]),
	SOC_ENUM("PGAR AGC Noise Gate Threhold",  rk3190_agcr_enum[10]),
	SOC_ENUM("PGAR AGC Upate Gain",  rk3190_agcr_enum[11]),
	SOC_ENUM("PGAR AGC Slow Clock Enable",  rk3190_agcr_enum[12]),
	SOC_ENUM("PGAR AGC Approximate Sample Rate",  rk3190_agcr_enum[13]),
	SOC_ENUM("PGAR AGC Enable",  rk3190_agcr_enum[14]),

	SOC_SINGLE_TLV("PGAR AGC Volume", RK3190_PGAR_AGC_CTL4,
		RK3190_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 4 is 1

	SOC_SINGLE("PGAR AGC Max Level High 8 Bits", RK3190_PGAR_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Max Level Low 8 Bits", RK3190_PGAR_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level High 8 Bits", RK3190_PGAR_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level Low 8 Bits", RK3190_PGAR_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAR AGC Max Gain", RK3190_PGAR_AGC_CTL5,
		RK3190_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x06 bit 4 is 1
	SOC_SINGLE_TLV("PGAR AGC Min Gain", RK3190_PGAR_AGC_CTL5,
		RK3190_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x06 bit 4 is 1

};
#endif

//For tiny alsa playback/capture/voice call path
static const char *rk3190_playback_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", //0-6
		"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"};//7-10

static const char *rk3190_capture_path_mode[] = {"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const char *rk3190_voice_call_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT"};//0-5

static const SOC_ENUM_SINGLE_DECL(rk3190_playback_path_type, 0, 0, rk3190_playback_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk3190_capture_path_type, 0, 0, rk3190_capture_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk3190_voice_call_path_type, 0, 0, rk3190_voice_call_path_mode);

static int rk3190_codec_power_up(int type);
static int rk3190_codec_power_down(int type);

static int rk3190_playback_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path %ld\n",__func__,ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3190->playback_path;

	return 0;
}

static int rk3190_playback_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3190->playback_path == ucontrol->value.integer.value[0]){
		DBG("%s : playback_path is not changed!\n",__func__);
		return 0;
	}

	pre_path = rk3190->playback_path;
	rk3190->playback_path = ucontrol->value.integer.value[0];

	printk("%s : set playback_path %ld, pre_path %ld\n", __func__,
		rk3190->playback_path, pre_path);


	// mute output for pop noise
	if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
		DBG("%s : set spk ctl gpio LOW\n", __func__);
		gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
	}

	if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
		DBG("%s : set hp ctl gpio LOW\n", __func__);
		gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
	}

	switch (rk3190->playback_path) {
	case OFF:
		if (pre_path != OFF)
			rk3190_codec_power_down(RK3190_CODEC_PLAYBACK);
		break;
	case RCV:
		if (rk3190->voice_call_path != OFF) {
			//close incall route
			rk3190_codec_power_down(RK3190_CODEC_INCALL);

			rk3190->voice_call_path = OFF;
		}
		break;
	case SPK_PATH:
	case RING_SPK:
        DBG("%s : PUT SPK_PATH\n",__func__);
		if (pre_path == OFF)
			rk3190_codec_power_up(RK3190_CODEC_PLAYBACK);
#if 0
		snd_soc_update_bits(codec, RK3190_SPKL_CTL,
			rk3190_VOL_MASK, SPKOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, SPKOUT_VOLUME);
#endif
		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set hp ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(HP_MOS_DELAY);
		}
        if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->spk_ctl_gpio, GPIO_HIGH);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(SPK_AMP_DELAY);
		}        

		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
		if (pre_path == OFF)
			rk3190_codec_power_up(RK3190_CODEC_PLAYBACK);
#if 0
		snd_soc_update_bits(codec, rk3190_SPKL_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME);
#endif
		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set hp ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_HIGH);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(HP_MOS_DELAY);
		}
        if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(SPK_AMP_DELAY);
		} 
        
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
		if (pre_path == OFF)
			rk3190_codec_power_up(RK3190_CODEC_PLAYBACK);
#if 0
		snd_soc_update_bits(codec, rk3190_SPKL_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME);
#endif
		if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->spk_ctl_gpio, GPIO_HIGH);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(SPK_AMP_DELAY);
		}

		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set hp ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_HIGH);
			//sleep for MOSFET or SPK power amplifier chip
			msleep(HP_MOS_DELAY);
		}
        
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk3190_capture_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : capture_path %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3190->capture_path;

	return 0;
}

static int rk3190_capture_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;
	//struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3190->capture_path == ucontrol->value.integer.value[0]){
		DBG("%s : capture_path is not changed!\n", __func__);
		return 0;
	}

	pre_path = rk3190->capture_path;
	rk3190->capture_path = ucontrol->value.integer.value[0];

	printk("%s : set capture_path %ld, pre_path %ld\n", __func__,
		rk3190->capture_path, pre_path);

	switch (rk3190->capture_path) {
	case MIC_OFF:
		if (pre_path != MIC_OFF)
			rk3190_codec_power_down(RK3190_CODEC_CAPTURE);
		break;
	case Main_Mic:
    DBG("%s : PUT MAIN_MIC_PATH\n",__func__);
		if (pre_path == MIC_OFF)
			rk3190_codec_power_up(RK3190_CODEC_CAPTURE);
#if 0
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_HIGH);
		}
#endif
		break;
	case Hands_Free_Mic:
		if (pre_path == MIC_OFF)
			rk3190_codec_power_up(RK3190_CODEC_CAPTURE);
#if 0
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_LOW);
		}
#endif
		break;
	case BT_Sco_Mic:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk3190_voice_call_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : voice_call_path %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3190->voice_call_path;

	return 0;
}

static int rk3190_voice_call_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3190) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3190->voice_call_path == ucontrol->value.integer.value[0]){
		DBG("%s : voice_call_path is not changed!\n",__func__);
		return 0;
	}

	pre_path = rk3190->voice_call_path;
	rk3190->voice_call_path = ucontrol->value.integer.value[0];

	printk("%s : set voice_call_path %ld, pre_path %ld\n", __func__,
		rk3190->voice_call_path, pre_path);
/*
	if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
		DBG("%s : set spk ctl gpio LOW\n", __func__);
		gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
	}

	if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
		DBG("%s : set hp ctl gpio LOW\n", __func__);
		gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
	}
*/
	switch (rk3190->voice_call_path) {
	case OFF:
        
	    if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
		}

		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set hp ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
		}

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set ear ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->ear_ctl_gpio, GPIO_LOW);
		}
        
        rk3190_codec_power_down(RK3190_CODEC_INCALL);
		rk3190_codec_power_up(RK3190_CODEC_PLAYBACK);
		break;
	case RCV:
#if 0
		//set mic for modem
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_HIGH);
		}
#endif

#if 0
		//rcv is controled by modem, so close incall route
		if (pre_path != OFF && pre_path != BT)
			rk3190_codec_power_down(RK3190_CODEC_INCALL);
#endif
        rk3190_codec_power_up(RK3190_CODEC_INCALL);

		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
        }

		if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
		}

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set ear ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->ear_ctl_gpio, GPIO_HIGH);
		}
        
		break;
	case SPK_PATH:
#if 0
		//set mic for modem
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_HIGH);
		}
#endif
		//open incall route
		if (pre_path == OFF ||
			pre_path == RCV ||
			pre_path == BT)
			rk3190_codec_power_up(RK3190_CODEC_INCALL);
#if 0
		snd_soc_update_bits(codec, rk3190_SPKL_CTL,
			rk3190_VOL_MASK, SPKOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, SPKOUT_VOLUME);
#endif
		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
		}

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
    		DBG("%s : set ear ctl gpio LOW\n", __func__);
    		gpio_set_value(rk3190->ear_ctl_gpio, GPIO_LOW);
		}
    	if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
    		DBG("%s : set spk ctl gpio HIGH\n", __func__);
    		gpio_set_value(rk3190->spk_ctl_gpio, GPIO_HIGH);
    		//sleep for MOSFET or SPK power amplifier chip
    		msleep(SPK_AMP_DELAY);
		}
		break;
	case HP_PATH:
#if 0
		//set mic for modem
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_LOW);
		}
#endif
		//open incall route
		if (pre_path == OFF ||
			pre_path == RCV ||
			pre_path == BT)
			rk3190_codec_power_up(RK3190_CODEC_INCALL);
#if 0
		snd_soc_update_bits(codec, rk3190_SPKL_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME);
#endif
		
		if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set spk ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3190->hp_ctl_gpio, GPIO_HIGH);
            	//sleep for MOSFET or SPK power amplifier chip
			msleep(HP_MOS_DELAY);
		}

    	if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
    		DBG("%s : set spk ctl gpio LOW\n", __func__);
    		gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
		}	

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set ear ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->ear_ctl_gpio, GPIO_LOW);
		}

        break;
	case HP_NO_MIC:
#if 0
		//set mic for modem
		if (rk3190 && rk3190->mic_sel_gpio != INVALID_GPIO) {
			DBG("%s : set mic sel gpio HIGH\n", __func__);
			gpio_set_value(rk3190->mic_sel_gpio, GPIO_HIGH);
		}
#endif
		//open incall route
		if (pre_path == OFF ||
			pre_path == RCV ||
			pre_path == BT)
			rk3190_codec_power_up(RK3190_CODEC_INCALL);
#if 0
		snd_soc_update_bits(codec, rk3190_SPKL_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME); //, volume (bit 0-4)
		snd_soc_update_bits(codec, rk3190_SPKR_CTL,
			rk3190_VOL_MASK, HPOUT_VOLUME);
#endif

    	if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
    		DBG("%s : set spk ctl gpio HIGH\n", __func__);
    		gpio_set_value(rk3190->hp_ctl_gpio, GPIO_HIGH);
            	//sleep for MOSFET or SPK power amplifier chip
    		msleep(HP_MOS_DELAY);
    	}

    	if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
    		DBG("%s : set spk ctl gpio LOW\n", __func__);
    		gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
    	}

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
			DBG("%s : set ear ctl gpio LOW\n", __func__);
			gpio_set_value(rk3190->ear_ctl_gpio, GPIO_LOW);
		}

		break;
	case BT:
		//BT is controled by modem, so close incall route
		if (pre_path != OFF &&
			pre_path != RCV)
			rk3190_codec_power_down(RK3190_CODEC_INCALL);
		break;

        if (rk3190 && rk3190->hp_ctl_gpio != INVALID_GPIO) {
            DBG("%s : set spk ctl gpio LOW\n", __func__);
            gpio_set_value(rk3190->hp_ctl_gpio, GPIO_LOW);
                //sleep for MOSFET or SPK power amplifier chip
            msleep(HP_MOS_DELAY);
        }

        if (rk3190 && rk3190->spk_ctl_gpio != INVALID_GPIO) {
            DBG("%s : set spk ctl gpio LOW\n", __func__);
            gpio_set_value(rk3190->spk_ctl_gpio, GPIO_LOW);
        }

        if (rk3190 && rk3190->ear_ctl_gpio != INVALID_GPIO) {
            DBG("%s : set ear ctl gpio LOW\n", __func__);
            gpio_set_value(rk3190->ear_ctl_gpio, GPIO_LOW);
        }

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new rk3190_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", rk3190_playback_path_type,
		rk3190_playback_path_get, rk3190_playback_path_put),

	SOC_ENUM_EXT("Capture MIC Path", rk3190_capture_path_type,
		rk3190_capture_path_get, rk3190_capture_path_put),

	SOC_ENUM_EXT("Voice Call Path", rk3190_voice_call_path_type,
		rk3190_voice_call_path_get, rk3190_voice_call_path_put),
};

#if 0
static int rk3190_dacl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACL_WORK,0);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACL_EN | RK3190_DACL_CLK_EN, 
			RK3190_DACL_EN | RK3190_DACL_CLK_EN);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACL_WORK, RK3190_DACL_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACL_EN | RK3190_DACL_CLK_EN,0);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACL_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3190_dacr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACR_WORK,0);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACR_EN | RK3190_DACR_CLK_EN, 
			RK3190_DACR_EN | RK3190_DACR_CLK_EN);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACR_WORK, RK3190_DACR_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACR_EN | RK3190_DACR_CLK_EN,0);
		snd_soc_update_bits(codec, RK3190_DAC_ENABLE,
			RK3190_DACR_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3190_adcl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
			RK3190_ADCL_CLK_EN_SFT | RK3190_ADCL_AMP_EN_SFT, 
			RK3190_ADCL_CLK_EN | RK3190_ADCL_AMP_EN);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
			RK3190_ADCL_CLK_EN_SFT | RK3190_ADCL_AMP_EN_SFT,0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3190_adcr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
			RK3190_ADCR_CLK_EN_SFT | RK3190_ADCR_AMP_EN_SFT, 
			RK3190_ADCR_CLK_EN | RK3190_ADCR_AMP_EN );
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
			RK3190_ADCR_CLK_EN_SFT | RK3190_ADCR_AMP_EN_SFT,0);
		break;

	default:
		return 0;
	}

	return 0;
}

/* HPmix */
static const struct snd_kcontrol_new rk3190_hpmixl[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXL_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXL_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXL_SEL_DACL_SFT, 1, 0),
};

static const struct snd_kcontrol_new rk3190_hpmixr[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXR_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXR_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", RK3190_HPMIX_S_SELECT,
				RK3190_HPMIXR_SEL_DACR_SFT, 1, 0),
};

static int rk3190_hpmixl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_DAC_CTL,
			RK3190_ZO_DET_VOUTR_SFT, RK3190_ZO_DET_VOUTR_EN); 
		snd_soc_update_bits(codec, RK3190_DAC_CTL,
			RK3190_ZO_DET_VOUTL_SFT, RK3190_ZO_DET_VOUTL_EN); 
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RK3190_DAC_CTL,
			RK3190_ZO_DET_VOUTR_SFT, RK3190_ZO_DET_VOUTR_DIS); 
		snd_soc_update_bits(codec, RK3190_DAC_CTL,
			RK3190_ZO_DET_VOUTL_SFT, RK3190_ZO_DET_VOUTL_DIS); 
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3190_hpmixr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
#if 0
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3190_HPMIX_CTL,
			RK3190_HPMIXR_WORK2, RK3190_HPMIXR_WORK2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RK3190_HPMIX_CTL,
			RK3190_HPMIXR_WORK2, 0);
		break;

	default:
		return 0;
	}
#endif
	return 0;
}

/* HP MUX */

static const char *hpl_sel[] = {"HPMIXL", "DACL"};

static const struct soc_enum hpl_sel_enum =
	SOC_ENUM_SINGLE(RK3190_HPMIX_S_SELECT, RK3190_HPMIXL_BYPASS_SFT,
			ARRAY_SIZE(hpl_sel), hpl_sel);

static const struct snd_kcontrol_new hpl_sel_mux =
	SOC_DAPM_ENUM("HPL select Mux", hpl_sel_enum);

static const char *hpr_sel[] = {"HPMIXR", "DACR"};

static const struct soc_enum hpr_sel_enum =
	SOC_ENUM_SINGLE(RK3190_HPMIX_S_SELECT, RK3190_HPMIXR_BYPASS_SFT,
			ARRAY_SIZE(hpr_sel), hpr_sel);

static const struct snd_kcontrol_new hpr_sel_mux =
	SOC_DAPM_ENUM("HPR select Mux", hpr_sel_enum);

/* IN_L MUX */
static const char *lnl_sel[] = {"NO","BSTL", "LINEL","NOUSE"};

static const struct soc_enum lnl_sel_enum =
	SOC_ENUM_SINGLE(RK3190_ALC_MUNIN_CTL, RK3190_MUXINL_F_SHT,
			ARRAY_SIZE(lnl_sel), lnl_sel);

static const struct snd_kcontrol_new lnl_sel_mux =
	SOC_DAPM_ENUM("MUXIN_L select", lnl_sel_enum);

/* IN_R MUX */
static const char *lnr_sel[] = {"NO","BSTR", "LINER","NOUSE"};

static const struct soc_enum lnr_sel_enum =
	SOC_ENUM_SINGLE(RK3190_ALC_MUNIN_CTL, RK3190_MUXINR_F_SHT,
			ARRAY_SIZE(lnr_sel), lnr_sel);

static const struct snd_kcontrol_new lnr_sel_mux =
	SOC_DAPM_ENUM("MUXIN_R select", lnr_sel_enum);


static const struct snd_soc_dapm_widget rk3190_dapm_widgets[] = {

	/* microphone bias */
	SND_SOC_DAPM_MICBIAS("Mic Bias", RK3190_ADC_MIC_CTL,
		RK3190_MICBIAS_VOL_ENABLE, 0),

	/* DACs */
	SND_SOC_DAPM_DAC_E("DACL", NULL, SND_SOC_NOPM,
		0, 0, rk3190_dacl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC_E("DACR", NULL, SND_SOC_NOPM,
		0, 0, rk3190_dacr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* ADCs */
	SND_SOC_DAPM_ADC_E("ADCL", NULL, SND_SOC_NOPM,
		0, 0, rk3190_adcl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADCR", NULL, SND_SOC_NOPM,
		0, 0, rk3190_adcr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* PGA */
	SND_SOC_DAPM_PGA("BSTL", RK3190_BST_CTL,
		RK3190_BSTL_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BSTR", RK3190_BST_CTL,
		RK3190_BSTR_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCL", RK3190_ALC_MUNIN_CTL,
		RK3190_ALCL_PWR_SHT , 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCR", RK3190_ALC_MUNIN_CTL,
		RK3190_ALCR_PWR_SHT , 0, NULL, 0),	
	SND_SOC_DAPM_PGA("HPL", RK3190_HPOUT_CTL,
		RK3190_HPOUTL_PWR_SHT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR", RK3190_HPOUT_CTL,
		RK3190_HPOUTR_PWR_SHT, 0, NULL, 0),

	/* MIXER */
	SND_SOC_DAPM_MIXER_E("HPMIXL", RK3190_HPMIX_CTL,
		RK3190_HPMIXL_SFT, 0, rk3190_hpmixl,
		ARRAY_SIZE(rk3190_hpmixl),rk3190_hpmixl_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("HPMIXR", RK3190_HPMIX_CTL,
		RK3190_HPMIXR_SFT, 0, rk3190_hpmixr,
		ARRAY_SIZE(rk3190_hpmixr),rk3190_hpmixr_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* MUX */
	SND_SOC_DAPM_MUX("IN_R Mux", SND_SOC_NOPM, 0, 0,
		&lnr_sel_mux),
	SND_SOC_DAPM_MUX("IN_L Mux", SND_SOC_NOPM, 0, 0,
		&lnl_sel_mux),
	SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0,
		&hpl_sel_mux),
	SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0,
		&hpr_sel_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("I2S DAC", "HiFi Playback", 0,
		SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("I2S ADC", "HiFi Capture", 0,
		SND_SOC_NOPM, 0, 0),

	/* Input */
	SND_SOC_DAPM_INPUT("LINEL"),
	SND_SOC_DAPM_INPUT("LINER"),
	SND_SOC_DAPM_INPUT("MICP"),
	SND_SOC_DAPM_INPUT("MICN"),

	/* Output */
	SND_SOC_DAPM_OUTPUT("HPOUTL"),
	SND_SOC_DAPM_OUTPUT("HPOUTR"),

};

static const struct snd_soc_dapm_route rk3190_dapm_routes[] = {
	/* Input */
	{"BSTR", NULL, "MICP"},
	{"BSTL", NULL, "MICP"},
	{"BSTL", NULL, "MICN"},

	{"IN_R Mux", "LINER", "LINER"},
	{"IN_R Mux", "BSTR", "BSTR"},
	{"IN_L Mux", "LINEL", "LINEL"},
	{"IN_L Mux", "BSTL", "BSTL"},

	{"ALCL", NULL, "IN_L Mux"},
	{"ALCR", NULL, "IN_R Mux"},


	{"ADCR", NULL, "ALCR"},
	{"ADCL", NULL, "ALCL"},

	{"I2S ADC", NULL, "ADCR"},
	{"I2S ADC", NULL, "ADCL"},

	/* Output */

	{"DACR", NULL, "I2S DAC"},
	{"DACL", NULL, "I2S DAC"},

	{"HPMIXR", "ALCR Switch", "ALCR"},
	{"HPMIXR", "ALCL Switch", "ALCL"},
	{"HPMIXR", "DACR Switch", "DACR"},

	{"HPMIXL", "ALCR Switch", "ALCR"},
	{"HPMIXL", "ALCL Switch", "ALCL"},
	{"HPMIXL", "DACL Switch", "DACL"},


	{"HPR Mux", "DACR", "DACR"},
	{"HPR Mux", "HPMIXR", "HPMIXR"},
	{"HPL Mux", "DACL", "DACL"},
	{"HPL Mux", "HPMIXL", "HPMIXL"},

	{"HPR", NULL, "HPR Mux"},
	{"HPL", NULL, "HPL Mux"},

	{"HPOUTR", NULL, "HPR"},
	{"HPOUTL", NULL, "HPL"},
};
#endif

static int rk3190_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	DBG("%s  level=%d\n",__func__,level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
#if 0
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			writel(0x32, rk3190_priv->regbase+RK3190_DAC_INT_CTL3);
			snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL,
				RK3190_ADC_CURRENT_ENABLE, RK3190_ADC_CURRENT_ENABLE);
			snd_soc_update_bits(codec, RK3190_DAC_CTL, 
				RK3190_CURRENT_EN, RK3190_CURRENT_EN);
			/* set power */
			snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
				RK3190_ADCL_REF_VOL_EN_SFT | RK3190_ADCR_REF_VOL_EN_SFT, 
				RK3190_ADCL_REF_VOL_EN | RK3190_ADCR_REF_VOL_EN);

			snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL, 
				RK3190_ADCL_ZERO_DET_EN_SFT | RK3190_ADCR_ZERO_DET_EN_SFT,
				RK3190_ADCL_ZERO_DET_EN | RK3190_ADCR_ZERO_DET_EN);

			snd_soc_update_bits(codec, RK3190_DAC_CTL, 
				RK3190_REF_VOL_DACL_EN_SFT | RK3190_REF_VOL_DACR_EN_SFT,
				RK3190_REF_VOL_DACL_EN | RK3190_REF_VOL_DACR_EN );

			snd_soc_update_bits(codec, RK3190_DAC_ENABLE, 
				RK3190_DACL_REF_VOL_EN_SFT | RK3190_DACR_REF_VOL_EN_SFT,
				RK3190_DACL_REF_VOL_EN | RK3190_DACR_REF_VOL_EN );
		}
		break;
#endif
	case SND_SOC_BIAS_OFF:
#if 0
			snd_soc_update_bits(codec, RK3190_DAC_ENABLE, 
				RK3190_DACL_REF_VOL_EN_SFT | RK3190_DACR_REF_VOL_EN_SFT,0);
			snd_soc_update_bits(codec, RK3190_DAC_CTL, 
				RK3190_REF_VOL_DACL_EN_SFT | RK3190_REF_VOL_DACR_EN_SFT,0);
			snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL, 
				RK3190_ADCL_ZERO_DET_EN_SFT | RK3190_ADCR_ZERO_DET_EN_SFT,0);
			snd_soc_update_bits(codec, RK3190_ADC_ENABLE,
				RK3190_ADCL_REF_VOL_EN_SFT | RK3190_ADCR_REF_VOL_EN_SFT,0);
			snd_soc_update_bits(codec, RK3190_ADC_MIC_CTL,
				RK3190_ADC_CURRENT_ENABLE, 0);
			snd_soc_update_bits(codec, RK3190_DAC_CTL, 
				RK3190_CURRENT_EN, 0);
			writel(0x22, rk3190_priv->regbase+RK3190_DAC_INT_CTL3);
#endif
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static int rk3190_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct rk3190_codec_priv *rk3190 = rk3190_priv;

	if (!rk3190) {
		printk("%s : rk3190 is NULL\n", __func__);
		return -EINVAL;
	}

	rk3190->stereo_sysclk = freq;

	return 0;
}

static int rk3190_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK3190_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK3190_I2S_MODE_MST;
		break;
	default:
		printk("%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK3190_ADC_DF_PCM;
		dac_aif1 |= RK3190_DAC_DF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK3190_ADC_DF_I2S;
		dac_aif1 |= RK3190_DAC_DF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK3190_ADC_DF_RJ;
		dac_aif1 |= RK3190_DAC_DF_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK3190_ADC_DF_LJ;
		dac_aif1 |= RK3190_DAC_DF_LJ;
		break;
	default:
		printk("%s : set format failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK3190_ALRCK_POL_DIS;
		adc_aif2 |= RK3190_ABCLK_POL_DIS;
		dac_aif1 |= RK3190_DLRCK_POL_DIS;
		dac_aif2 |= RK3190_DBCLK_POL_DIS;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK3190_ALRCK_POL_EN;
		adc_aif2 |= RK3190_ABCLK_POL_EN;
		dac_aif1 |= RK3190_DLRCK_POL_EN;
		dac_aif2 |= RK3190_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK3190_ALRCK_POL_DIS;
		adc_aif2 |= RK3190_ABCLK_POL_EN;
		dac_aif1 |= RK3190_DLRCK_POL_DIS;
		dac_aif2 |= RK3190_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK3190_ALRCK_POL_EN;
		adc_aif2 |= RK3190_ABCLK_POL_DIS;
		dac_aif1 |= RK3190_DLRCK_POL_EN;
		dac_aif2 |= RK3190_DBCLK_POL_DIS;
		break;
	default:
		printk("%s : set dai format failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RK3190_ADC_INT_CTL1,
			RK3190_ALRCK_POL_MASK | RK3190_ADC_DF_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK3190_ADC_INT_CTL2,
			RK3190_ABCLK_POL_MASK | RK3190_I2S_MODE_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK3190_DAC_INT_CTL1,
			RK3190_DLRCK_POL_MASK | RK3190_DAC_DF_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK3190_DAC_INT_CTL2,
			RK3190_DBCLK_POL_MASK, dac_aif2);

	return 0;
}

static int rk3190_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	struct rk3190_codec_priv *rk3190 = rk3190_priv;
	unsigned int rate = params_rate(params);
	unsigned int div;
	unsigned int adc_aif1 = 0, adc_aif2  = 0, dac_aif1 = 0, dac_aif2  = 0;

	if (!rk3190) {
		printk("%s : rk3190 is NULL\n", __func__);
		return -EINVAL;
	}

	// bclk = codec_clk / 4
	// lrck = bclk / (wl * 2)
	div = (((rk3190->stereo_sysclk / 4) / rate) / 2);

	if ((rk3190->stereo_sysclk % (4 * rate * 2) > 0) ||
	    (div != 16 && div != 20 && div != 24 && div != 32)) {
		printk("%s : need PLL\n", __func__);
		return -EINVAL;
	}

	switch (div) {
	case 16:
		adc_aif2 |= RK3190_ADC_WL_16;
		dac_aif2 |= RK3190_DAC_WL_16;
		break;
	case 20:
		adc_aif2 |= RK3190_ADC_WL_20;
		dac_aif2 |= RK3190_DAC_WL_20;
		break;
	case 24:
		adc_aif2 |= RK3190_ADC_WL_24;
		dac_aif2 |= RK3190_DAC_WL_24;
		break;
	case 32:
		adc_aif2 |= RK3190_ADC_WL_32;
		dac_aif2 |= RK3190_DAC_WL_32;
		break;
	default:
		return -EINVAL;
	}


	DBG("%s : MCLK = %dHz, sample rate = %dHz, div = %d\n", __func__,
		rk3190->stereo_sysclk, rate, div);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK3190_ADC_VWL_16;
		dac_aif1 |= RK3190_DAC_VWL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK3190_ADC_VWL_20;
		dac_aif1 |= RK3190_DAC_VWL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK3190_ADC_VWL_24;
		dac_aif1 |= RK3190_DAC_VWL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK3190_ADC_VWL_32;
		dac_aif1 |= RK3190_DAC_VWL_32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case RK3190_MONO:
		adc_aif1 |= RK3190_ADC_TYPE_MONO;
		break;
	case RK3190_STEREO:
		adc_aif1 |= RK3190_ADC_TYPE_STEREO;
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= RK3190_ADC_SWAP_DIS;
	adc_aif2 |= RK3190_ADC_RST_DIS;
	dac_aif1 |= RK3190_DAC_SWAP_DIS;
	dac_aif2 |= RK3190_DAC_RST_DIS;

	rk3190->rate = rate;

	snd_soc_update_bits(codec, RK3190_ADC_INT_CTL1,
			 RK3190_ADC_VWL_MASK | RK3190_ADC_SWAP_MASK |
			 RK3190_ADC_TYPE_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK3190_ADC_INT_CTL2,
			RK3190_ADC_WL_MASK | RK3190_ADC_RST_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK3190_DAC_INT_CTL1,
			 RK3190_DAC_VWL_MASK | RK3190_DAC_SWAP_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK3190_DAC_INT_CTL2,
			RK3190_DAC_WL_MASK | RK3190_DAC_RST_MASK, dac_aif2);

	return 0;
}

static int rk3190_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}

static struct rk3190_reg_val_typ playback_power_up_list[] = {
	//{RK3190_DAC_INT_CTL3,0x32},
	{RK3190_DAC_CTL,0x40},
	{RK3190_DAC_CTL,0x62},
	{RK3190_DAC_ENABLE,0x88},
	{RK3190_DAC_ENABLE,0xcc},
	{RK3190_DAC_ENABLE,0xee},
	{RK3190_HPMIX_CTL,0x44},
	{RK3190_HPOUT_CTL,0x90},
	{RK3190_HPOUT_CTL,0xd8},
	{RK3190_HPMIX_S_SELECT,0x11},//DAC
	{RK3190_HPMIX_CTL,0x55},
	{RK3190_HPMIX_CTL,0x77},
	{RK3190_DAC_ENABLE,0xff},
	{RK3190_HPOUT_CTL,0xfc},
	{RK3190_DAC_CTL,0x73},
	{RK3190_HPOUTL_GAIN,OUT_VOLUME},
	{RK3190_HPOUTR_GAIN,OUT_VOLUME},
};
#define RK3190_CODEC_PLAYBACK_POWER_UP_LIST_LEN ARRAY_SIZE(playback_power_up_list)

static struct rk3190_reg_val_typ playback_power_down_list[] = {
	{RK3190_HPOUT_CTL,0xdb},
	{RK3190_HPMIX_CTL,0x44},
	{RK3190_HPMIX_S_SELECT,0x00},
	{RK3190_HPOUT_CTL,0x92},
	{RK3190_DAC_CTL,0x22},
	{RK3190_HPOUT_CTL,0x00},
	{RK3190_HPMIX_CTL,0x00},
	{RK3190_DAC_ENABLE,0x00},
	{RK3190_DAC_CTL,0x00},
	//{RK3190_DAC_INT_CTL3,0x22},
#ifdef WITH_CAP
	//{RK3190_SELECT_CURRENT,0x08},
#endif
	{RK3190_HPOUTL_GAIN,0x0},
	{RK3190_HPOUTR_GAIN,0x0},
};
#define RK3190_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN ARRAY_SIZE(playback_power_down_list)

static struct rk3190_reg_val_typ capture_power_up_list[] = {
	{RK3190_ADC_CTL, 0x40},
	//{RK3190_BIAS_CTL, 0x08},
	//{RK3190_BIAS_CTL, 0x0f},
	{RK3190_ADC_CTL, 0x62},
	{RK3190_BST_CTL, 0x88},
	{RK3190_ALC_MUNIN_CTL, 0x66},
	{RK3190_ADC_ENABLE, 0x44},
	{RK3190_ADC_ENABLE, 0x66},
	{RK3190_BST_CTL, 0xee},
	{RK3190_BST_CTL, 0xfe}, //single-ended
	{RK3190_ALC_MUNIN_CTL, 0x77},
	{RK3190_ALCL_GAIN_CTL, CAP_VOL},
	{RK3190_ALCR_GAIN_CTL, CAP_VOL},
	{RK3190_ADC_CTL, 0x73},

};
#define RK3190_CODEC_CAPTURE_POWER_UP_LIST_LEN ARRAY_SIZE(capture_power_up_list)

static struct rk3190_reg_val_typ capture_power_down_list[] = {
	{RK3190_ADC_ENABLE, 0x44},
	{RK3190_ALC_MUNIN_CTL, 0x66},
	{RK3190_BST_CTL, 0x88},
	{RK3190_ADC_ENABLE, 0x00},
	{RK3190_ADC_CTL, 0x62},
	//{RK3190_BIAS_CTL, 0x08},
	{RK3190_ADC_CTL, 0x40},
	{RK3190_BST_CTL, 0x00},
	{RK3190_ALCL_GAIN_CTL, 0x00},
	{RK3190_ALCR_GAIN_CTL, 0x00},
	{RK3190_ADC_CTL, 0x00},
	//{RK3190_BIAS_CTL, 0x00},
	{RK3190_ALC_MUNIN_CTL, 0x00},
};
#define RK3190_CODEC_CAPTURE_POWER_DOWN_LIST_LEN ARRAY_SIZE(capture_power_down_list)

static struct rk3190_reg_val_typ lineIn_bypass_power_up_list[] = {
#if 1
    //{RK3190_DAC_INT_CTL3, 0x32},
    {RK3190_ADC_CTL, 0x40},
    {RK3190_BIAS_CTL, 0x08},
    {RK3190_BIAS_CTL, 0x0f},
    {RK3190_ALC_MUNIN_CTL, 0x22},
    {RK3190_ALC_MUNIN_CTL, 0x66},
    {RK3190_ADC_CTL, 0x62},
    {RK3190_DAC_CTL, 0x40},
    {RK3190_DAC_CTL, 0x62},
    {RK3190_DAC_ENABLE, 0x88},
	{RK3190_DAC_ENABLE, 0xcc},
	{RK3190_DAC_ENABLE, 0xee},
    {RK3190_HPMIX_CTL, 0x44},
    {RK3190_HPOUT_CTL, 0x92}, 
    {RK3190_HPOUT_CTL, 0xdb},
    {RK3190_HPMIX_S_SELECT, 0x22},//ALCL/R+DACL/R
    {RK3190_HPMIX_CTL, 0x55},
    {RK3190_HPMIX_CTL, 0x77},
    {RK3190_DAC_ENABLE, 0xff},
    {RK3190_HPOUT_CTL, 0xff},
    {RK3190_ALC_MUNIN_CTL, 0x77},
    {RK3190_ALCL_GAIN_CTL, CAP_VOL},
    {RK3190_ALCR_GAIN_CTL, CAP_VOL},
    {RK3190_HPOUTL_GAIN, OUT_VOLUME},
    {RK3190_HPOUTR_GAIN, OUT_VOLUME}, 
    {RK3190_ADC_CTL, 0x73},
    {RK3190_DAC_CTL, 0x73},
#endif
};
#define RK3190_CODEC_LINEIN_BYPASS_POWER_UP_LIST_LEN ARRAY_SIZE(lineIn_bypass_power_up_list)

static struct rk3190_reg_val_typ lineIn_bypass_power_down_list[] = {
    {RK3190_ALC_MUNIN_CTL, 0xaa},
    {RK3190_BIAS_CTL, 0xc7},
    //{RK3190_BIAS_CTL, 0x80},
    {RK3190_ADC_CTL, 0x62},
    //{RK3190_BIAS_CTL, 0x00},
    {RK3190_ALC_MUNIN_CTL, 0x00},
    {RK3190_HPOUT_CTL, 0xdb},
	{RK3190_HPMIX_CTL, 0x44},
	{RK3190_HPMIX_S_SELECT, 0x00},
	{RK3190_HPOUT_CTL, 0x92},
	{RK3190_DAC_CTL, 0x22},
	{RK3190_ADC_CTL, 0x00},
	{RK3190_HPOUT_CTL, 0x00},
	{RK3190_HPMIX_CTL, 0x00},
	{RK3190_DAC_CTL, 0x00},
	{RK3190_DAC_ENABLE, 0x00},
	{RK3190_HPOUTL_GAIN, 0x00},
	{RK3190_HPOUTR_GAIN, 0x00},
	{RK3190_ALCL_GAIN_CTL, 0x00},
	{RK3190_ALCR_GAIN_CTL, 0x00},
	//{RK3190_DAC_INT_CTL3, 0x00},
};
#define RK3190_CODEC_LINEIN_BYPASS_POWER_DOWN_LIST_LEN ARRAY_SIZE(lineIn_bypass_power_down_list)


static int rk3190_codec_power_up(int type)
{
	struct snd_soc_codec *codec = rk3190_priv->codec;
	int i;

	if (!rk3190_priv || !rk3190_priv->codec) {
		printk("%s : rk3190_priv or rk3190_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	printk("%s : power up %s%s%s\n", __func__,
		type == RK3190_CODEC_PLAYBACK ? "playback" : "",
		type == RK3190_CODEC_CAPTURE ? "capture" : "",
		type == RK3190_CODEC_INCALL ? "incall" : "");

	if (type == RK3190_CODEC_PLAYBACK) {
		for (i = 0; i < RK3190_CODEC_PLAYBACK_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_up_list[i].reg,
				playback_power_up_list[i].value);
         msleep(10);
		}
	} else if (type == RK3190_CODEC_CAPTURE) {
		for (i = 0; i < RK3190_CODEC_CAPTURE_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_up_list[i].reg,
				capture_power_up_list[i].value);
         msleep(10);
		}
	} else if (type == RK3190_CODEC_INCALL) {
        /*To be perfect*/
        for (i = 0; i < RK3190_CODEC_LINEIN_BYPASS_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, lineIn_bypass_power_up_list[i].reg,
				lineIn_bypass_power_up_list[i].value);
         msleep(10);
        }
    }

	return 0;
}

static int rk3190_codec_power_down(int type)
{
	struct snd_soc_codec *codec = rk3190_priv->codec;
	int i;
      
	if (!rk3190_priv || !rk3190_priv->codec) {
		printk("%s : rk3190_priv or rk3190_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}
	
	printk("%s : power down %s%s%s%s\n", __func__,
		type == RK3190_CODEC_PLAYBACK ? "playback" : "",
		type == RK3190_CODEC_CAPTURE ? "capture" : "",
		type == RK3190_CODEC_INCALL ? "incall" : "",
		type == RK3190_CODEC_ALL ? "all" : "");

	if ((type == RK3190_CODEC_CAPTURE) || (type == RK3190_CODEC_INCALL)) {
		for (i = 0; i < RK3190_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_down_list[i].reg,
				capture_power_down_list[i].value);
		}
	} else if (type == RK3190_CODEC_PLAYBACK) {
#if 0
		snd_soc_write(codec, RK3190_DAC_CTL,0x62);
		for ( i = OUT_VOLUME; i >= 0; i--)
		{
			snd_soc_write(codec, 0xb4,i);
			snd_soc_write(codec, 0xb8,i);
		}
		msleep(20);
#endif
		for (i = 0; i < RK3190_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_down_list[i].reg,
				playback_power_down_list[i].value);

		}
    } else if (type == RK3190_CODEC_INCALL) {
        /*To be perfect*/
        for (i = 0; i < RK3190_CODEC_LINEIN_BYPASS_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, lineIn_bypass_power_down_list[i].reg,
				lineIn_bypass_power_down_list[i].value);
        }
	} else if (type == RK3190_CODEC_ALL) {
		rk3190_reset(codec);
	}

	return 0;
}

#define RK3190_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK3190_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK3190_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk3190_dai_ops = {
	.hw_params	= rk3190_hw_params,
	.set_fmt	= rk3190_set_dai_fmt,
	.set_sysclk	= rk3190_set_dai_sysclk,
	.digital_mute	= rk3190_digital_mute,
};

static struct snd_soc_dai_driver rk3190_dai[] = {
	{
		.name = "rk3190-hifi",
		.id = RK3190_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK3190_PLAYBACK_RATES,
			.formats = RK3190_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK3190_CAPTURE_RATES,
			.formats = RK3190_FORMATS,
		},
		.ops = &rk3190_dai_ops,
	},
	{
		.name = "rk3190-voice",
		.id = RK3190_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3190_PLAYBACK_RATES,
			.formats = RK3190_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3190_CAPTURE_RATES,
			.formats = RK3190_FORMATS,
		},
		.ops = &rk3190_dai_ops,
	},

};

static int rk3190_suspend(struct snd_soc_codec *codec)
{
	if (rk3190_for_mid)
	{
		rk3190_codec_power_down(RK3190_CODEC_PLAYBACK);
		rk3190_codec_power_down(RK3190_CODEC_ALL);
#ifdef   WITH_CAP
        snd_soc_write(codec, RK3190_SELECT_CURRENT,0x3e);
		snd_soc_write(codec, RK3190_SELECT_CURRENT,0x1e);	
#endif
	}
	else
		rk3190_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int rk3190_resume(struct snd_soc_codec *codec)
{
	if (!rk3190_for_mid)
		rk3190_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#ifdef   WITH_CAP    
    snd_soc_write(codec, RK3190_SELECT_CURRENT,0x1e);
    snd_soc_write(codec, RK3190_SELECT_CURRENT,0x3e);
#endif
    return 0;
}

static int rk3190_probe(struct snd_soc_codec *codec)
{
	struct rk3190_codec_priv *rk3190;
	struct rk3190_codec_pdata  *rk3190_plt = codec->dev->platform_data;
	struct platform_device *pdev = to_platform_device(codec->dev);
	struct resource *res, *mem;
	int ret;
	unsigned int val;

	DBG("%s\n", __func__);

	rk3190 = kzalloc(sizeof(struct rk3190_codec_priv), GFP_KERNEL);
	if (!rk3190) {
		printk("%s : rk3190 priv kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	rk3190->codec = codec;

	res = pdev->resource;
	rk3190->regbase_phy = res->start;
	rk3190->regsize_phy = (res->end - res->start) + 1;

	mem = request_mem_region(res->start, (res->end - res->start) + 1, pdev->name);
	if (!mem)
	{
    		dev_err(&pdev->dev, "failed to request mem region for rk2928 codec\n");
    		ret = -ENOENT;
    		goto err__;
	}
	
	rk3190->regbase = (int)ioremap(res->start, (res->end - res->start) + 1);
	if (!rk3190->regbase) {
		dev_err(&pdev->dev, "cannot ioremap acodec registers\n");
		ret = -ENXIO;
		goto err__;
	}
	
	rk3190->pclk = clk_get(NULL,"pclk_acodec");
	if(IS_ERR(rk3190->pclk))
	{
		dev_err(&pdev->dev, "Unable to get acodec hclk\n");
		ret = -ENXIO;
		goto err__;
	}
	clk_enable(rk3190->pclk);

	rk3190_priv = rk3190;

	if (rk3190_priv && rk3190_plt->spk_ctl_gpio) {
		gpio_request(rk3190_plt->spk_ctl_gpio, NULL);
		gpio_direction_output(rk3190_plt->spk_ctl_gpio, GPIO_LOW);
		rk3190->spk_ctl_gpio = rk3190_plt->spk_ctl_gpio;
	} else {
		printk("%s : rk3190 spk_ctl_gpio is NULL!\n", __func__);
		rk3190->spk_ctl_gpio = INVALID_GPIO;
	}	

	if (rk3190_priv && rk3190_plt->hp_ctl_gpio) {
		gpio_request(rk3190_plt->hp_ctl_gpio, NULL);
		gpio_direction_output(rk3190_plt->hp_ctl_gpio, GPIO_LOW);
		rk3190->hp_ctl_gpio = rk3190_plt->hp_ctl_gpio;
	} else {
		printk("%s : rk3190 hp_ctl_gpio is NULL!\n", __func__);
		rk3190->hp_ctl_gpio = INVALID_GPIO;
	}

	if (rk3190_plt->delay_time) {
		rk3190->delay_time = rk3190_plt->delay_time;
	} else {
		printk("%s : rk3190 delay_time is NULL!\n", __func__);
		rk3190->delay_time = 10;
	}
    
    if (rk3190_plt->ear_ctl_gpio) {
        gpio_request(rk3190_plt->ear_ctl_gpio, NULL);
    	gpio_direction_output(rk3190_plt->ear_ctl_gpio, GPIO_LOW);
    	rk3190->ear_ctl_gpio = rk3190_plt->ear_ctl_gpio;
    } else {
		printk("%s : rk3190 ear_ctl_gpio is NULL!\n", __func__);
		rk3190->ear_ctl_gpio = INVALID_GPIO;
	}

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		printk("%s : Failed to set cache I/O: %d\n", __func__, ret);
		goto err__;
	}

	codec->hw_read = rk3190_codec_read;
	codec->hw_write = (hw_write_t)rk3190_hw_write;
	codec->read = rk3190_codec_read;
	codec->write = rk3190_codec_write;

    rk3190_reset(codec);
    
	val = snd_soc_read(codec, RK3190_RESET);
	if (val != rk3190_reg_defaults[RK3190_RESET]) {
		printk("%s : codec register 0: %x is not a 0x00000003\n", __func__, val);
		ret = -ENODEV;
		goto err__;
	}

	if (!rk3190_for_mid)
	{
		codec->dapm.bias_level = SND_SOC_BIAS_OFF;
		rk3190_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	}

#ifdef   WITH_CAP
	//set for capacity output,clear up noise
	snd_soc_write(codec, RK3190_SELECT_CURRENT,0x1e);
	snd_soc_write(codec, RK3190_SELECT_CURRENT,0x3e);
#endif

#if 1
	// select  used with internal audio codec  soc_con[4] bit 7
	val = readl(RK319X_GRF_BASE+GRF_SOC_CON4);
	writel(val | 0x00800080,RK319X_GRF_BASE+GRF_SOC_CON4);
	val = readl(RK319X_GRF_BASE+GRF_SOC_CON4);
	printk("%s : i2s used with internal audio codec val=0x%x,soc_con[4] bit 7 =1 is correct\n",__func__,val);
#endif

    /*ENABLE MICBIAS and always ON*/
#if 1
    snd_soc_write(codec, RK3190_BIAS_CTL, 0x08);
    snd_soc_write(codec, RK3190_BIAS_CTL, 0x0f);
#endif
	if(rk3190_for_mid) {
        snd_soc_add_codec_controls(codec, rk3190_snd_path_controls,
				ARRAY_SIZE(rk3190_snd_path_controls));
    }

    return 0;

err__:
	release_mem_region(res->start,(res->end - res->start) + 1);
	kfree(rk3190);
	rk3190 = NULL;
	rk3190_priv = NULL;

	return ret;
}

/* power down chip */
static int rk3190_remove(struct snd_soc_codec *codec)
{
	
	DBG("%s\n", __func__);

	if (!rk3190_priv) {
		printk("%s : rk3190_priv is NULL\n", __func__);
		return 0;
	}

	if (rk3190_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3190_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk3190_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3190_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	snd_soc_write(codec, RK3190_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(codec, RK3190_RESET, 0x3);
	mdelay(10);

	if (rk3190_priv)
		kfree(rk3190_priv);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_rk3190 = {
	.probe =rk3190_probe,
	.remove =rk3190_remove,
	.suspend =rk3190_suspend,
	.resume = rk3190_resume,
	.set_bias_level = rk3190_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(rk3190_reg_defaults),
	.reg_word_size = sizeof(unsigned int),
	.reg_cache_default = rk3190_reg_defaults,
	.volatile_register = rk3190_volatile_register,
	.readable_register = rk3190_codec_register,
	.reg_cache_step = sizeof(unsigned int),
};

static int rk3190_platform_probe(struct platform_device *pdev)
{
	DBG("%s\n", __func__);

	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_rk3190, rk3190_dai, ARRAY_SIZE(rk3190_dai));
}

static int rk3190_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

void rk3190_platform_shutdown(struct platform_device *pdev)
{

	DBG("%s\n", __func__);

	if (!rk3190_priv || !rk3190_priv->codec) {
		printk("%s : rk3190_priv or rk3190_priv->codec is NULL\n", __func__);
		return;
	}

	if (rk3190_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3190_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk3190_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3190_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	writel(0xfc, rk3190_priv->regbase+RK3190_RESET);
	mdelay(10);
	writel(0x03, rk3190_priv->regbase+RK3190_RESET);

	if (rk3190_priv)
		kfree(rk3190_priv);
}

static struct platform_driver rk3190_codec_driver = {
	.driver = {
		   .name = "rk3190-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = rk3190_platform_probe,
	.remove = rk3190_platform_remove,
	.shutdown = rk3190_platform_shutdown,
};


static __init int rk3190_modinit(void)
{
	return platform_driver_register(&rk3190_codec_driver);
}
module_init(rk3190_modinit);

static __exit void rk3190_exit(void)
{
	platform_driver_unregister(&rk3190_codec_driver);
}
module_exit(rk3190_exit);

MODULE_DESCRIPTION("ASoC RK3190 driver");
MODULE_AUTHOR("zhangjun <showy.zhang@rock-chips.com>");
MODULE_LICENSE("GPL");
