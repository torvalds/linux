/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rk3026.c  --  RK3026 CODEC ALSA SoC audio driver
 *
 * Copyright 2013 Rockchip
 * Author: chenjq <chenjq@rock-chips.com>
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
#include <mach/board.h>
#include <mach/io.h>
#include <mach/iomux.h>
#include <mach/cru.h>
#include "rk3026_codec.h"


#ifdef CONFIG_RK_HEADSET_DET
#include "../../../drivers/headset_observe/rk_headset.h"
#endif

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dbg_codec(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(fmt , ## arg); } while (0)

#define	DBG(fmt,...)	dbg_codec(1,fmt,## __VA_ARGS__)


/* volume setting
 *  0: -39dB
 *  26: 0dB
 *  31: 6dB
 *  Step: 1.5dB
*/
#define  OUT_VOLUME    31//0~31

/* capture vol set
 * 0: -18db
 * 12: 0db
 * 31: 28.5db
 * step: 1.5db
*/
#define CAP_VOL	   17//0-31

//with capacity or  not
#define WITH_CAP
#ifdef CONFIG_MACH_RK_FAC 
	rk3026_hdmi_ctrl=0;
#endif
struct rk3026_codec_priv {
	struct snd_soc_codec *codec;

	unsigned int stereo_sysclk;
	unsigned int rate;

	int playback_active;
	int capture_active;

	int spk_ctl_gpio;
	int hp_ctl_gpio;
	int delay_time;

	long int playback_path;
	long int capture_path;
	long int voice_call_path;

	int	 regbase;
	int	regbase_phy;
	int	regsize_phy;
	struct clk	*pclk;
};

static struct rk3026_codec_priv *rk3026_priv = NULL;

#define RK3026_CODEC_ALL	0
#define RK3026_CODEC_PLAYBACK	1
#define RK3026_CODEC_CAPTURE	2
#define RK3026_CODEC_INCALL	3

#define RK3026_CODEC_WORK_NULL	0
#define RK3026_CODEC_WORK_POWER_DOWN	1
#define RK3026_CODEC_WORK_POWER_UP	2

static struct workqueue_struct *rk3026_codec_workq;

static void rk3026_codec_capture_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(capture_delayed_work, rk3026_codec_capture_work);
static int rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
static bool rk3026_for_mid = 1;

static int rk3026_get_parameter(void)
{
	int val;
	char *command_line = strstr(saved_command_line, "ap_has_alsa=");

	if (command_line == NULL) {
		printk("%s : Can not get ap_has_alsa from kernel command line!\n", __func__);
		return 0;
	}

	command_line += 12;

	val = simple_strtol(command_line, NULL, 10);
	if (val == 0 || val == 1) {
		rk3026_for_mid = (val ? 0 : 1);
		printk("%s : THIS IS FOR %s\n", __func__, rk3026_for_mid ? "mid" : "phone");
	} else {
		printk("%s : get ap_has_alsa error, val = %d\n", __func__, val);
	}

	return 0;
}

static const unsigned int rk3026_reg_defaults[RK3026_PGAR_AGC_CTL5+1] = {
	[RK3026_RESET] = 0x0003,
	[RK3026_ADC_INT_CTL1] = 0x0050,
	[RK3026_ADC_INT_CTL2] = 0x000e,
	[RK3026_DAC_INT_CTL1] = 0x0050,
	[RK3026_DAC_INT_CTL2] = 0x000e,
	[RK3026_DAC_INT_CTL3] = 0x22,
	[RK3026_ADC_MIC_CTL] = 0x0000,
	[RK3026_BST_CTL] = 0x000,
	[RK3026_ALC_MUNIN_CTL] = 0x0044,
	[RK3026_BSTL_ALCL_CTL] = 0x000c,
	[RK3026_ALCR_GAIN_CTL] = 0x000C,
	[RK3026_ADC_ENABLE] = 0x0000,
	[RK3026_DAC_CTL] = 0x0000,
	[RK3026_DAC_ENABLE] = 0x0000,
	[RK3026_HPMIX_CTL] = 0x0000,
	[RK3026_HPMIX_S_SELECT] = 0x0000,
	[RK3026_HPOUT_CTL] = 0x0000,
	[RK3026_HPOUTL_GAIN] = 0x0000,
	[RK3026_HPOUTR_GAIN] = 0x0000,
	[RK3026_SELECT_CURRENT] = 0x003e,
	[RK3026_PGAL_AGC_CTL1] = 0x0000,
	[RK3026_PGAL_AGC_CTL2] = 0x0046,
	[RK3026_PGAL_AGC_CTL3] = 0x0041,
	[RK3026_PGAL_AGC_CTL4] = 0x002c,
	[RK3026_PGAL_ASR_CTL] = 0x0000,
	[RK3026_PGAL_AGC_MAX_H] = 0x0026,
	[RK3026_PGAL_AGC_MAX_L] = 0x0040,
	[RK3026_PGAL_AGC_MIN_H] = 0x0036,
	[RK3026_PGAL_AGC_MIN_L] = 0x0020,
	[RK3026_PGAL_AGC_CTL5] = 0x0038,
	[RK3026_PGAR_AGC_CTL1] = 0x0000,
	[RK3026_PGAR_AGC_CTL2] = 0x0046,
	[RK3026_PGAR_AGC_CTL3] = 0x0041,
	[RK3026_PGAR_AGC_CTL4] = 0x002c,
	[RK3026_PGAR_ASR_CTL] = 0x0000,
	[RK3026_PGAR_AGC_MAX_H] = 0x0026,
	[RK3026_PGAR_AGC_MAX_L] = 0x0040,
	[RK3026_PGAR_AGC_MIN_H] = 0x0036,
	[RK3026_PGAR_AGC_MIN_L] = 0x0020,
	[RK3026_PGAR_AGC_CTL5] = 0x0038,
};

static struct rk3026_init_bit_typ rk3026_init_bit_list[] = {
	{RK3026_HPOUT_CTL, RK3026_HPOUTL_EN, RK3026_HPOUTL_WORK,RK3026_HPVREF_EN},
	{RK3026_HPOUT_CTL, RK3026_HPOUTR_EN, RK3026_HPOUTR_WORK,RK3026_HPVREF_WORK},
	{RK3026_HPMIX_CTL, RK3026_HPMIXR_EN, RK3026_HPMIXR_WORK2,RK3026_HPMIXR_WORK1},
	{RK3026_HPMIX_CTL, RK3026_HPMIXL_EN, RK3026_HPMIXL_WORK2,RK3026_HPMIXL_WORK1},

};
#define RK3026_INIT_BIT_LIST_LEN ARRAY_SIZE(rk3026_init_bit_list)

static int rk3026_init_bit_register(unsigned int reg, int i)
{
	for (; i < RK3026_INIT_BIT_LIST_LEN; i++) {
		if (rk3026_init_bit_list[i].reg == reg)
			return i;
	}

	return -1;
}

static unsigned int rk3026_codec_read(struct snd_soc_codec *codec, unsigned int reg);
static inline void rk3026_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value);

static unsigned int rk3026_set_init_value(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	unsigned int read_value, power_bit, set_bit2,set_bit1;
	int i;
	int tmp = 0;
	// read codec init register
	i = rk3026_init_bit_register(reg, 0);

	// set codec init bit
	// widget init bit should be setted 0 after widget power up or unmute,
	// and should be setted 1 after widget power down or mute.
	if (i >= 0) {
		read_value = rk3026_codec_read(codec, reg);
		while (i >= 0) {
			power_bit = rk3026_init_bit_list[i].power_bit;
			set_bit2 = rk3026_init_bit_list[i].init2_bit;
			set_bit1 = rk3026_init_bit_list[i].init1_bit;

			if ((read_value & power_bit) != (value & power_bit))
			{
				if (value & power_bit)
				{
					tmp = value | set_bit2 | set_bit1;
					writel(value, rk3026_priv->regbase+reg);
					writel(tmp, rk3026_priv->regbase+reg);
				  	
				}
				else
				{	
					tmp = value & (~set_bit2) & (~set_bit1);
					writel(tmp, rk3026_priv->regbase+reg);
					writel(value, rk3026_priv->regbase+reg);
				}	
				value = tmp;			
			}
			else
			{
				if (read_value != value)
					writel(value, rk3026_priv->regbase+reg);
			}
				
			i = rk3026_init_bit_register(reg, ++i);
			
			rk3026_write_reg_cache(codec, reg, value);
		}
	}
	else
	{
		return i;		
	}

	return value;
}

static int rk3026_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK3026_RESET:
		return 1;
	default:
		return 0;
	}
}

static int rk3026_codec_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK3026_RESET:
	case RK3026_ADC_INT_CTL1:
	case RK3026_ADC_INT_CTL2:
	case RK3026_DAC_INT_CTL1:
	case RK3026_DAC_INT_CTL2:
	case RK3026_DAC_INT_CTL3:
	case RK3026_ADC_MIC_CTL:
	case RK3026_BST_CTL:
	case RK3026_ALC_MUNIN_CTL:
	case RK3026_BSTL_ALCL_CTL:
	case RK3026_ALCR_GAIN_CTL:
	case RK3026_ADC_ENABLE:
	case RK3026_DAC_CTL:
	case RK3026_DAC_ENABLE:
	case RK3026_HPMIX_CTL:
	case RK3026_HPMIX_S_SELECT:
	case RK3026_HPOUT_CTL:
	case RK3026_HPOUTL_GAIN:
	case RK3026_HPOUTR_GAIN:
	case RK3026_SELECT_CURRENT:
	case RK3026_PGAL_AGC_CTL1:
	case RK3026_PGAL_AGC_CTL2:
	case RK3026_PGAL_AGC_CTL3:
	case RK3026_PGAL_AGC_CTL4:
	case RK3026_PGAL_ASR_CTL:
	case RK3026_PGAL_AGC_MAX_H:
	case RK3026_PGAL_AGC_MAX_L:
	case RK3026_PGAL_AGC_MIN_H:
	case RK3026_PGAL_AGC_MIN_L:
	case RK3026_PGAL_AGC_CTL5:
	case RK3026_PGAR_AGC_CTL1:
	case RK3026_PGAR_AGC_CTL2:
	case RK3026_PGAR_AGC_CTL3:
	case RK3026_PGAR_AGC_CTL4:
	case RK3026_PGAR_ASR_CTL:
	case RK3026_PGAR_AGC_MAX_H:
	case RK3026_PGAR_AGC_MAX_L:
	case RK3026_PGAR_AGC_MIN_H:
	case RK3026_PGAR_AGC_MIN_L:
	case RK3026_PGAR_AGC_CTL5:
		return 1;
	default:
		return 0;
	}
}

static inline unsigned int rk3026_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	unsigned int *cache = codec->reg_cache;
	
	if (rk3026_codec_register(codec, reg) )
		return  cache[reg];

	printk("%s : reg error!\n", __func__);

	return -EINVAL;
}

static inline void rk3026_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	unsigned int *cache = codec->reg_cache;

	if (rk3026_codec_register(codec, reg)) {
		cache[reg] = value;
		return;
	}

	printk("%s : reg error!\n", __func__);
}

static unsigned int rk3026_codec_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int value;

	if (!rk3026_priv) {
		printk("%s : rk3026 is NULL\n", __func__);
		return -EINVAL;
	}

	if (!rk3026_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	if (rk3026_volatile_register(codec, reg) == 0) {
		value = rk3026_read_reg_cache(codec, reg);
	} else {
		value = readl_relaxed(rk3026_priv->regbase+reg);	
	}

	value = readl_relaxed(rk3026_priv->regbase+reg);
	dbg_codec(2,"%s : reg = 0x%x, val= 0x%x\n", __func__, reg, value);

	return value;
}

static int rk3026_codec_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
	int new_value;

	if (!rk3026_priv) {
		printk("%s : rk3026 is NULL\n", __func__);
		return -EINVAL;
	} else if (!rk3026_codec_register(codec, reg)) {
		printk("%s : reg error!\n", __func__);
		return -EINVAL;
	}

	new_value = rk3026_set_init_value(codec, reg, value);

	if (new_value == -1)
	{
		writel(value, rk3026_priv->regbase+reg);
		rk3026_write_reg_cache(codec, reg, value);
	}
		
	dbg_codec(2,"%s : reg = 0x%x, val = 0x%x, new_value=%d\n", __func__, reg, value,new_value);
	return 0;
}

static int rk3026_hw_write(const struct i2c_client *client, const char *buf, int count)
{
	unsigned int reg, value;

	if (!rk3026_priv || !rk3026_priv->codec) {
		printk("%s : rk3026_priv or rk3026_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (count == 3) {
		reg = (unsigned int)buf[0];
		value = (buf[1] & 0xff00) | (0x00ff & buf[2]);
		writel(value, rk3026_priv->regbase+reg);
	} else {
		printk("%s : i2c len error\n", __func__);
	}

	return  count;
}

static int rk3026_reset(struct snd_soc_codec *codec)
{
	writel(0x00, rk3026_priv->regbase+RK3026_RESET);
	mdelay(10);
	writel(0x03, rk3026_priv->regbase+RK3026_RESET);
	mdelay(10);

	memcpy(codec->reg_cache, rk3026_reg_defaults,
	       sizeof(rk3026_reg_defaults));

	return 0;
}

int rk3026_headset_mic_detect(bool headset_status)
{
#if 0
	struct snd_soc_codec *codec = rk3026_priv->codec;

	DBG("%s\n", __func__);

	if (!rk3026_priv || !rk3026_priv->codec) {
		printk("%s : rk3026_priv or rk3026_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	if (headset_status) {
		snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL,
				RK3026_MICBIAS2_PWRD | RK3026_MICBIAS2_V_MASK,
				RK3026_MICBIAS2_V_1_7);
	} else {// headset is out, disable MIC2 && MIC1 Bias
		DBG("%s : headset is out,disable Mic2 Bias\n", __func__);
		snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL,
				RK3026_MICBIAS2_PWRD | RK3026_MICBIAS2_V_MASK,
				RK3026_MICBIAS2_V_1_0|RK3026_MICBIAS2_PWRD);
	}
#endif
	return 0;
}
EXPORT_SYMBOL(rk3026_headset_mic_detect);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -3900, 150, 0);
static const DECLARE_TLV_DB_SCALE(pga_vol_tlv, -1800, 150, 0);
static const DECLARE_TLV_DB_SCALE(bst_vol_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_max_vol_tlv, -1350, 600, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_min_vol_tlv, -1800, 600, 0);

static const char *rk3026_input_mode[] = {"Differential","Single-Ended"}; 

static const char *rk3026_micbias_ratio[] = {"1.0 Vref", "1.1 Vref",
		"1.2 Vref", "1.3 Vref", "1.4 Vref", "1.5 Vref", "1.6 Vref", "1.7 Vref",};

static const char *rk3026_dis_en_sel[] = {"Disable", "Enable"};

static const char *rk3026_pga_agc_way[] = {"Normal", "Jack"};

static const char *rk3026_agc_backup_way[] = {"Normal", "Jack1", "Jack2", "Jack3"};

static const char *rk3026_pga_agc_hold_time[] = {"0ms", "2ms",
		"4ms", "8ms", "16ms", "32ms", "64ms", "128ms", "256ms", "512ms", "1s"};

static const char *rk3026_pga_agc_ramp_up_time[] = {"Normal:500us Jack:125us",
		"Normal:1ms Jack:250us", "Normal:2ms Jack:500us", "Normal:4ms Jack:1ms",
		"Normal:8ms Jack:2ms", "Normal:16ms Jack:4ms", "Normal:32ms Jack:8ms",
		"Normal:64ms Jack:16ms", "Normal:128ms Jack:32ms", "Normal:256ms Jack:64ms",
		"Normal:512ms Jack:128ms"};

static const char *rk3026_pga_agc_ramp_down_time[] = {"Normal:125us Jack:32us",
		"Normal:250us Jack:64us", "Normal:500us Jack:125us", "Normal:1ms Jack:250us",
		"Normal:2ms Jack:500us", "Normal:4ms Jack:1ms", "Normal:8ms Jack:2ms",
		"Normal:16ms Jack:4ms", "Normal:32ms Jack:8ms", "Normal:64ms Jack:16ms",
		"Normal:128ms Jack:32ms"};

static const char *rk3026_pga_agc_mode[] = {"Normal", "Limiter"};

static const char *rk3026_pga_agc_recovery_mode[] = {"Right Now", "After AGC to Limiter"};

static const char *rk3026_pga_agc_noise_gate_threhold[] = {"-39dB", "-45dB", "-51dB",
		"-57dB", "-63dB", "-69dB", "-75dB", "-81dB"};

static const char *rk3026_pga_agc_update_gain[] = {"Right Now", "After 1st Zero Cross"};

static const char *rk3026_pga_agc_approximate_sample_rate[] = {"96KHZ","48KHz","441KHZ", "32KHz",
		"24KHz", "16KHz", "12KHz", "8KHz"};

static const struct soc_enum rk3026_bst_enum[] = {
SOC_ENUM_SINGLE(RK3026_BSTL_ALCL_CTL, RK3026_BSTL_MODE_SFT, 2, rk3026_input_mode),
};


static const struct soc_enum rk3026_micbias_enum[] = {
SOC_ENUM_SINGLE(RK3026_ADC_MIC_CTL, RK3026_MICBIAS_VOL_SHT, 8, rk3026_micbias_ratio),
};

static const struct soc_enum rk3026_agcl_enum[] = {
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL1, RK3026_PGA_AGC_BK_WAY_SFT, 4, rk3026_agc_backup_way),/*0*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL1, RK3026_PGA_AGC_WAY_SFT, 2, rk3026_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL1, RK3026_PGA_AGC_HOLD_T_SFT, 11, rk3026_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL2, RK3026_PGA_AGC_GRU_T_SFT, 11, rk3026_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL2, RK3026_PGA_AGC_GRD_T_SFT, 11, rk3026_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_MODE_SFT, 2, rk3026_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_ZO_SFT, 2, rk3026_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_REC_MODE_SFT, 2, rk3026_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_FAST_D_SFT, 2, rk3026_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_NG_SFT, 2, rk3026_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL3, RK3026_PGA_AGC_NG_THR_SFT, 8, rk3026_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL4, RK3026_PGA_AGC_ZO_MODE_SFT, 2, rk3026_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK3026_PGAL_ASR_CTL, RK3026_PGA_SLOW_CLK_SFT, 2, rk3026_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK3026_PGAL_ASR_CTL, RK3026_PGA_ASR_SFT, 8, rk3026_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK3026_PGAL_AGC_CTL5, RK3026_PGA_AGC_SFT, 2, rk3026_dis_en_sel),/*14*/
};

static const struct soc_enum rk3026_agcr_enum[] = {
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL1, RK3026_PGA_AGC_BK_WAY_SFT, 4, rk3026_agc_backup_way),/*0*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL1, RK3026_PGA_AGC_WAY_SFT, 2, rk3026_pga_agc_way),/*1*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL1, RK3026_PGA_AGC_HOLD_T_SFT, 11, rk3026_pga_agc_hold_time),/*2*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL2, RK3026_PGA_AGC_GRU_T_SFT, 11, rk3026_pga_agc_ramp_up_time),/*3*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL2, RK3026_PGA_AGC_GRD_T_SFT, 11, rk3026_pga_agc_ramp_down_time),/*4*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_MODE_SFT, 2, rk3026_pga_agc_mode),/*5*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_ZO_SFT, 2, rk3026_dis_en_sel),/*6*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_REC_MODE_SFT, 2, rk3026_pga_agc_recovery_mode),/*7*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_FAST_D_SFT, 2, rk3026_dis_en_sel),/*8*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_NG_SFT, 2, rk3026_dis_en_sel),/*9*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL3, RK3026_PGA_AGC_NG_THR_SFT, 8, rk3026_pga_agc_noise_gate_threhold),/*10*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL4, RK3026_PGA_AGC_ZO_MODE_SFT, 2, rk3026_pga_agc_update_gain),/*11*/
SOC_ENUM_SINGLE(RK3026_PGAR_ASR_CTL, RK3026_PGA_SLOW_CLK_SFT, 2, rk3026_dis_en_sel),/*12*/
SOC_ENUM_SINGLE(RK3026_PGAR_ASR_CTL, RK3026_PGA_ASR_SFT, 8, rk3026_pga_agc_approximate_sample_rate),/*13*/
SOC_ENUM_SINGLE(RK3026_PGAR_AGC_CTL5, RK3026_PGA_AGC_SFT, 2, rk3026_dis_en_sel),/*14*/
};

static const struct snd_kcontrol_new rk3026_snd_controls[] = {
	//Add for set voice volume
	SOC_DOUBLE_R_TLV("Speaker Playback Volume", RK3026_HPOUTL_GAIN,
		RK3026_HPOUTR_GAIN, RK3026_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Speaker Playback Switch", RK3026_HPOUT_CTL,
		RK3026_HPOUTL_MUTE_SHT, RK3026_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RK3026_HPOUTL_GAIN,
		RK3026_HPOUTR_GAIN, RK3026_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Headphone Playback Switch", RK3026_HPOUT_CTL,
		RK3026_HPOUTL_MUTE_SHT, RK3026_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Earpiece Playback Volume", RK3026_HPOUTL_GAIN,
		RK3026_HPOUTR_GAIN, RK3026_HPOUT_GAIN_SFT, 31, 0, out_vol_tlv),
	SOC_DOUBLE("Earpiece Playback Switch", RK3026_HPOUT_CTL,
		RK3026_HPOUTL_MUTE_SHT, RK3026_HPOUTR_MUTE_SHT, 1, 0),


	//Add for set capture mute
	SOC_SINGLE_TLV("Main Mic Capture Volume", RK3026_BST_CTL,
		RK3026_BSTL_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Main Mic Capture Switch", RK3026_BST_CTL,
		RK3026_BSTL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("Headset Mic Capture Volume", RK3026_BST_CTL,
		RK3026_BSTR_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Headset Mic Capture Switch", RK3026_BST_CTL,
		RK3026_BSTR_MUTE_SHT, 1, 0),

	SOC_SINGLE("ALCL Switch", RK3026_ALC_MUNIN_CTL,
		RK3026_ALCL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCL Capture Volume", RK3026_BSTL_ALCL_CTL,
		RK3026_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),
	SOC_SINGLE("ALCR Switch", RK3026_ALC_MUNIN_CTL,
		RK3026_ALCR_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCR Capture Volume", RK3026_ALCR_GAIN_CTL,
		RK3026_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),

	SOC_ENUM("BST_L Mode",  rk3026_bst_enum[0]),

	SOC_ENUM("Micbias Voltage",  rk3026_micbias_enum[0]),
	SOC_ENUM("PGAL AGC Back Way",  rk3026_agcl_enum[0]),
	SOC_ENUM("PGAL AGC Way",  rk3026_agcl_enum[1]),
	SOC_ENUM("PGAL AGC Hold Time",  rk3026_agcl_enum[2]),
	SOC_ENUM("PGAL AGC Ramp Up Time",  rk3026_agcl_enum[3]),
	SOC_ENUM("PGAL AGC Ramp Down Time",  rk3026_agcl_enum[4]),
	SOC_ENUM("PGAL AGC Mode",  rk3026_agcl_enum[5]),
	SOC_ENUM("PGAL AGC Gain Update Zero Enable",  rk3026_agcl_enum[6]),
	SOC_ENUM("PGAL AGC Gain Recovery LPGA VOL",  rk3026_agcl_enum[7]),
	SOC_ENUM("PGAL AGC Fast Decrement Enable",  rk3026_agcl_enum[8]),
	SOC_ENUM("PGAL AGC Noise Gate Enable",  rk3026_agcl_enum[9]),
	SOC_ENUM("PGAL AGC Noise Gate Threhold",  rk3026_agcl_enum[10]),
	SOC_ENUM("PGAL AGC Upate Gain",  rk3026_agcl_enum[11]),
	SOC_ENUM("PGAL AGC Slow Clock Enable",  rk3026_agcl_enum[12]),
	SOC_ENUM("PGAL AGC Approximate Sample Rate",  rk3026_agcl_enum[13]),
	SOC_ENUM("PGAL AGC Enable",  rk3026_agcl_enum[14]),

	SOC_SINGLE_TLV("PGAL AGC Volume", RK3026_PGAL_AGC_CTL4,
		RK3026_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 5 is 1

	SOC_SINGLE("PGAL AGC Max Level High 8 Bits", RK3026_PGAL_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Max Level Low 8 Bits", RK3026_PGAL_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level High 8 Bits", RK3026_PGAL_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level Low 8 Bits", RK3026_PGAL_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAL AGC Max Gain", RK3026_PGAL_AGC_CTL5,
		RK3026_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x0a bit 5 is 1
	SOC_SINGLE_TLV("PGAL AGC Min Gain", RK3026_PGAL_AGC_CTL5,
		RK3026_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x0a bit 5 is 1

	SOC_ENUM("PGAR AGC Back Way",  rk3026_agcr_enum[0]),
	SOC_ENUM("PGAR AGC Way",  rk3026_agcr_enum[1]),
	SOC_ENUM("PGAR AGC Hold Time",  rk3026_agcr_enum[2]),
	SOC_ENUM("PGAR AGC Ramp Up Time",  rk3026_agcr_enum[3]),
	SOC_ENUM("PGAR AGC Ramp Down Time",  rk3026_agcr_enum[4]),
	SOC_ENUM("PGAR AGC Mode",  rk3026_agcr_enum[5]),
	SOC_ENUM("PGAR AGC Gain Update Zero Enable",  rk3026_agcr_enum[6]),
	SOC_ENUM("PGAR AGC Gain Recovery LPGA VOL",  rk3026_agcr_enum[7]),
	SOC_ENUM("PGAR AGC Fast Decrement Enable",  rk3026_agcr_enum[8]),
	SOC_ENUM("PGAR AGC Noise Gate Enable",  rk3026_agcr_enum[9]),
	SOC_ENUM("PGAR AGC Noise Gate Threhold",  rk3026_agcr_enum[10]),
	SOC_ENUM("PGAR AGC Upate Gain",  rk3026_agcr_enum[11]),
	SOC_ENUM("PGAR AGC Slow Clock Enable",  rk3026_agcr_enum[12]),
	SOC_ENUM("PGAR AGC Approximate Sample Rate",  rk3026_agcr_enum[13]),
	SOC_ENUM("PGAR AGC Enable",  rk3026_agcr_enum[14]),

	SOC_SINGLE_TLV("PGAR AGC Volume", RK3026_PGAR_AGC_CTL4,
		RK3026_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),//AGC disable and 0x0a bit 4 is 1

	SOC_SINGLE("PGAR AGC Max Level High 8 Bits", RK3026_PGAR_AGC_MAX_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Max Level Low 8 Bits", RK3026_PGAR_AGC_MAX_L,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level High 8 Bits", RK3026_PGAR_AGC_MIN_H,
		0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level Low 8 Bits", RK3026_PGAR_AGC_MIN_L,
		0, 255, 0),

	SOC_SINGLE_TLV("PGAR AGC Max Gain", RK3026_PGAR_AGC_CTL5,
		RK3026_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),//AGC enable and 0x06 bit 4 is 1
	SOC_SINGLE_TLV("PGAR AGC Min Gain", RK3026_PGAR_AGC_CTL5,
		RK3026_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),//AGC enable and 0x06 bit 4 is 1

};

//For tiny alsa playback/capture/voice call path
static const char *rk3026_playback_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", //0-6
		"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"};//7-10

static const char *rk3026_capture_path_mode[] = {"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const char *rk3026_voice_call_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT"};//0-5

static const SOC_ENUM_SINGLE_DECL(rk3026_playback_path_type, 0, 0, rk3026_playback_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk3026_capture_path_type, 0, 0, rk3026_capture_path_mode);

static const SOC_ENUM_SINGLE_DECL(rk3026_voice_call_path_type, 0, 0, rk3026_voice_call_path_mode);

static int rk3026_codec_power_up(int type);
static int rk3026_codec_power_down(int type);

static int rk3026_playback_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n",__func__,ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3026_priv->playback_path;

	return 0;
}

static int rk3026_playback_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3026_priv->playback_path == ucontrol->value.integer.value[0]){
		printk("%s : playback_path is not changed!\n",__func__);
		return 0;
	}
	
	pre_path = rk3026_priv->playback_path;
	rk3026_priv->playback_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld\n", __func__,
		rk3026_priv->playback_path);

	switch (rk3026_priv->playback_path) {
	case OFF:
		if (pre_path != OFF)
			rk3026_codec_power_down(RK3026_CODEC_PLAYBACK);
		break;
	case RCV:
		break;
	case SPK_PATH:
	case RING_SPK:
		if (pre_path == OFF)
			rk3026_codec_power_up(RK3026_CODEC_PLAYBACK);
		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
		if (pre_path == OFF)
			rk3026_codec_power_up(RK3026_CODEC_PLAYBACK);
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
		if (pre_path == OFF)
			rk3026_codec_power_up(RK3026_CODEC_PLAYBACK);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk3026_capture_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : capture_path = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3026_priv->capture_path;

	return 0;
}

static int rk3026_capture_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3026_priv->capture_path == ucontrol->value.integer.value[0]){
		printk("%s : capture_path is not changed!\n", __func__);
		//return 0;
	}

	pre_path = rk3026_priv->capture_path;
	rk3026_priv->capture_path = ucontrol->value.integer.value[0];

	DBG("%s : set capture_path = %ld\n", __func__, rk3026_priv->capture_path);

	switch (rk3026_priv->capture_path) {
	case MIC_OFF:
		if (pre_path != MIC_OFF)
			rk3026_codec_power_down(RK3026_CODEC_CAPTURE);
		break;
	case Main_Mic:
		if (pre_path == MIC_OFF)
			rk3026_codec_power_up(RK3026_CODEC_CAPTURE);
		break;
	case Hands_Free_Mic:
		if (pre_path == MIC_OFF)
			rk3026_codec_power_up(RK3026_CODEC_CAPTURE);
		break;
	case BT_Sco_Mic:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int rk3026_voice_call_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk3026_priv->voice_call_path;

	return 0;
}

static int rk3026_voice_call_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	long int pre_path;

	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk3026_priv->voice_call_path == ucontrol->value.integer.value[0]){
		printk("%s : playback_path is not changed!\n",__func__);
		//return 0;
	}

	pre_path = rk3026_priv->voice_call_path;
	rk3026_priv->voice_call_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld\n", __func__,
		rk3026_priv->voice_call_path);

	//open playback route for incall route and keytone
	if (pre_path == OFF) {
		if (rk3026_priv->playback_path != OFF) {
			//mute output for incall route pop nosie
				mdelay(100);
		} else
			rk3026_codec_power_up(RK3026_CODEC_PLAYBACK);
	}

	switch (rk3026_priv->voice_call_path) {
	case OFF:
		if (pre_path != MIC_OFF)
			rk3026_codec_power_down(RK3026_CODEC_CAPTURE);
		break;
	case RCV:
		break;
	case SPK_PATH:
		//open incall route
		if (pre_path == OFF || 	pre_path == RCV || pre_path == BT)
			rk3026_codec_power_up(RK3026_CODEC_INCALL);

		break;
	case HP_PATH:
	case HP_NO_MIC:
		//open incall route
		if (pre_path == OFF || 	pre_path == RCV || pre_path == BT)
			rk3026_codec_power_up(RK3026_CODEC_INCALL);
		break;
	case BT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new rk3026_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", rk3026_playback_path_type,
		rk3026_playback_path_get, rk3026_playback_path_put),

	SOC_ENUM_EXT("Capture MIC Path", rk3026_capture_path_type,
		rk3026_capture_path_get, rk3026_capture_path_put),

	SOC_ENUM_EXT("Voice Call Path", rk3026_voice_call_path_type,
		rk3026_voice_call_path_get, rk3026_voice_call_path_put),
};

static int rk3026_dacl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACL_WORK,0);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACL_EN | RK3026_DACL_CLK_EN, 
			RK3026_DACL_EN | RK3026_DACL_CLK_EN);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACL_WORK, RK3026_DACL_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACL_EN | RK3026_DACL_CLK_EN,0);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACL_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3026_dacr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACR_WORK,0);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACR_EN | RK3026_DACR_CLK_EN, 
			RK3026_DACR_EN | RK3026_DACR_CLK_EN);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACR_WORK, RK3026_DACR_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACR_EN | RK3026_DACR_CLK_EN,0);
		snd_soc_update_bits(codec, RK3026_DAC_ENABLE,
			RK3026_DACR_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3026_adcl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
			RK3026_ADCL_CLK_EN_SFT | RK3026_ADCL_AMP_EN_SFT, 
			RK3026_ADCL_CLK_EN | RK3026_ADCL_AMP_EN);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
			RK3026_ADCL_CLK_EN_SFT | RK3026_ADCL_AMP_EN_SFT,0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3026_adcr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
			RK3026_ADCR_CLK_EN_SFT | RK3026_ADCR_AMP_EN_SFT, 
			RK3026_ADCR_CLK_EN | RK3026_ADCR_AMP_EN );
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
			RK3026_ADCR_CLK_EN_SFT | RK3026_ADCR_AMP_EN_SFT,0);
		break;

	default:
		return 0;
	}

	return 0;
}

/* HPmix */
static const struct snd_kcontrol_new rk3026_hpmixl[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXL_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXL_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXL_SEL_DACL_SFT, 1, 0),
};

static const struct snd_kcontrol_new rk3026_hpmixr[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXR_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXR_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", RK3026_HPMIX_S_SELECT,
				RK3026_HPMIXR_SEL_DACR_SFT, 1, 0),
};

static int rk3026_hpmixl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_DAC_CTL,
			RK3026_ZO_DET_VOUTR_SFT, RK3026_ZO_DET_VOUTR_EN); 
		snd_soc_update_bits(codec, RK3026_DAC_CTL,
			RK3026_ZO_DET_VOUTL_SFT, RK3026_ZO_DET_VOUTL_EN); 
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RK3026_DAC_CTL,
			RK3026_ZO_DET_VOUTR_SFT, RK3026_ZO_DET_VOUTR_DIS); 
		snd_soc_update_bits(codec, RK3026_DAC_CTL,
			RK3026_ZO_DET_VOUTL_SFT, RK3026_ZO_DET_VOUTL_DIS); 
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk3026_hpmixr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
#if 0
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RK3026_HPMIX_CTL,
			RK3026_HPMIXR_WORK2, RK3026_HPMIXR_WORK2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RK3026_HPMIX_CTL,
			RK3026_HPMIXR_WORK2, 0);
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
	SOC_ENUM_SINGLE(RK3026_HPMIX_S_SELECT, RK3026_HPMIXL_BYPASS_SFT,
			ARRAY_SIZE(hpl_sel), hpl_sel);

static const struct snd_kcontrol_new hpl_sel_mux =
	SOC_DAPM_ENUM("HPL select Mux", hpl_sel_enum);

static const char *hpr_sel[] = {"HPMIXR", "DACR"};

static const struct soc_enum hpr_sel_enum =
	SOC_ENUM_SINGLE(RK3026_HPMIX_S_SELECT, RK3026_HPMIXR_BYPASS_SFT,
			ARRAY_SIZE(hpr_sel), hpr_sel);

static const struct snd_kcontrol_new hpr_sel_mux =
	SOC_DAPM_ENUM("HPR select Mux", hpr_sel_enum);

/* IN_L MUX */
static const char *lnl_sel[] = {"NO","BSTL", "LINEL","NOUSE"};

static const struct soc_enum lnl_sel_enum =
	SOC_ENUM_SINGLE(RK3026_ALC_MUNIN_CTL, RK3026_MUXINL_F_SHT,
			ARRAY_SIZE(lnl_sel), lnl_sel);

static const struct snd_kcontrol_new lnl_sel_mux =
	SOC_DAPM_ENUM("MUXIN_L select", lnl_sel_enum);

/* IN_R MUX */
static const char *lnr_sel[] = {"NO","BSTR", "LINER","NOUSE"};

static const struct soc_enum lnr_sel_enum =
	SOC_ENUM_SINGLE(RK3026_ALC_MUNIN_CTL, RK3026_MUXINR_F_SHT,
			ARRAY_SIZE(lnr_sel), lnr_sel);

static const struct snd_kcontrol_new lnr_sel_mux =
	SOC_DAPM_ENUM("MUXIN_R select", lnr_sel_enum);


static const struct snd_soc_dapm_widget rk3026_dapm_widgets[] = {

	/* microphone bias */
	SND_SOC_DAPM_MICBIAS("Mic Bias", RK3026_ADC_MIC_CTL,
		RK3026_MICBIAS_VOL_ENABLE, 0),

	/* DACs */
	SND_SOC_DAPM_DAC_E("DACL", NULL, SND_SOC_NOPM,
		0, 0, rk3026_dacl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC_E("DACR", NULL, SND_SOC_NOPM,
		0, 0, rk3026_dacr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* ADCs */
	SND_SOC_DAPM_ADC_E("ADCL", NULL, SND_SOC_NOPM,
		0, 0, rk3026_adcl_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADCR", NULL, SND_SOC_NOPM,
		0, 0, rk3026_adcr_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* PGA */
	SND_SOC_DAPM_PGA("BSTL", RK3026_BST_CTL,
		RK3026_BSTL_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BSTR", RK3026_BST_CTL,
		RK3026_BSTR_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCL", RK3026_ALC_MUNIN_CTL,
		RK3026_ALCL_PWR_SHT , 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCR", RK3026_ALC_MUNIN_CTL,
		RK3026_ALCR_PWR_SHT , 0, NULL, 0),	
	SND_SOC_DAPM_PGA("HPL", RK3026_HPOUT_CTL,
		RK3026_HPOUTL_PWR_SHT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR", RK3026_HPOUT_CTL,
		RK3026_HPOUTR_PWR_SHT, 0, NULL, 0),

	/* MIXER */
	SND_SOC_DAPM_MIXER_E("HPMIXL", RK3026_HPMIX_CTL,
		RK3026_HPMIXL_SFT, 0, rk3026_hpmixl,
		ARRAY_SIZE(rk3026_hpmixl),rk3026_hpmixl_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("HPMIXR", RK3026_HPMIX_CTL,
		RK3026_HPMIXR_SFT, 0, rk3026_hpmixr,
		ARRAY_SIZE(rk3026_hpmixr),rk3026_hpmixr_event,
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

static const struct snd_soc_dapm_route rk3026_dapm_routes[] = {
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

static int rk3026_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	DBG("%s  level=%d\n",__func__,level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			writel(0x32, rk3026_priv->regbase+RK3026_DAC_INT_CTL3);
			snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL,
				RK3026_ADC_CURRENT_ENABLE, RK3026_ADC_CURRENT_ENABLE);
			snd_soc_update_bits(codec, RK3026_DAC_CTL, 
				RK3026_CURRENT_EN, RK3026_CURRENT_EN);
			/* set power */
			snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
				RK3026_ADCL_REF_VOL_EN_SFT | RK3026_ADCR_REF_VOL_EN_SFT, 
				RK3026_ADCL_REF_VOL_EN | RK3026_ADCR_REF_VOL_EN);

			snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL, 
				RK3026_ADCL_ZERO_DET_EN_SFT | RK3026_ADCR_ZERO_DET_EN_SFT,
				RK3026_ADCL_ZERO_DET_EN | RK3026_ADCR_ZERO_DET_EN);

			snd_soc_update_bits(codec, RK3026_DAC_CTL, 
				RK3026_REF_VOL_DACL_EN_SFT | RK3026_REF_VOL_DACR_EN_SFT,
				RK3026_REF_VOL_DACL_EN | RK3026_REF_VOL_DACR_EN );

			snd_soc_update_bits(codec, RK3026_DAC_ENABLE, 
				RK3026_DACL_REF_VOL_EN_SFT | RK3026_DACR_REF_VOL_EN_SFT,
				RK3026_DACL_REF_VOL_EN | RK3026_DACR_REF_VOL_EN );
		}
		break;

	case SND_SOC_BIAS_OFF:
			snd_soc_update_bits(codec, RK3026_DAC_ENABLE, 
				RK3026_DACL_REF_VOL_EN_SFT | RK3026_DACR_REF_VOL_EN_SFT,0);
			snd_soc_update_bits(codec, RK3026_DAC_CTL, 
				RK3026_REF_VOL_DACL_EN_SFT | RK3026_REF_VOL_DACR_EN_SFT,0);
			snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL, 
				RK3026_ADCL_ZERO_DET_EN_SFT | RK3026_ADCR_ZERO_DET_EN_SFT,0);
			snd_soc_update_bits(codec, RK3026_ADC_ENABLE,
				RK3026_ADCL_REF_VOL_EN_SFT | RK3026_ADCR_REF_VOL_EN_SFT,0);
			snd_soc_update_bits(codec, RK3026_ADC_MIC_CTL,
				RK3026_ADC_CURRENT_ENABLE, 0);
			snd_soc_update_bits(codec, RK3026_DAC_CTL, 
				RK3026_CURRENT_EN, 0);
			writel(0x22, rk3026_priv->regbase+RK3026_DAC_INT_CTL3);
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static int rk3026_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct rk3026_codec_priv *rk3026 = rk3026_priv;

	if (!rk3026) {
		printk("%s : rk3026 is NULL\n", __func__);
		return -EINVAL;
	}

	rk3026->stereo_sysclk = freq;

	return 0;
}

static int rk3026_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK3026_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK3026_I2S_MODE_MST;
		break;
	default:
		printk("%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK3026_ADC_DF_PCM;
		dac_aif1 |= RK3026_DAC_DF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK3026_ADC_DF_I2S;
		dac_aif1 |= RK3026_DAC_DF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK3026_ADC_DF_RJ;
		dac_aif1 |= RK3026_DAC_DF_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK3026_ADC_DF_LJ;
		dac_aif1 |= RK3026_DAC_DF_LJ;
		break;
	default:
		printk("%s : set format failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK3026_ALRCK_POL_DIS;
		adc_aif2 |= RK3026_ABCLK_POL_DIS;
		dac_aif1 |= RK3026_DLRCK_POL_DIS;
		dac_aif2 |= RK3026_DBCLK_POL_DIS;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK3026_ALRCK_POL_EN;
		adc_aif2 |= RK3026_ABCLK_POL_EN;
		dac_aif1 |= RK3026_DLRCK_POL_EN;
		dac_aif2 |= RK3026_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK3026_ALRCK_POL_DIS;
		adc_aif2 |= RK3026_ABCLK_POL_EN;
		dac_aif1 |= RK3026_DLRCK_POL_DIS;
		dac_aif2 |= RK3026_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK3026_ALRCK_POL_EN;
		adc_aif2 |= RK3026_ABCLK_POL_DIS;
		dac_aif1 |= RK3026_DLRCK_POL_EN;
		dac_aif2 |= RK3026_DBCLK_POL_DIS;
		break;
	default:
		printk("%s : set dai format failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RK3026_ADC_INT_CTL1,
			RK3026_ALRCK_POL_MASK | RK3026_ADC_DF_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK3026_ADC_INT_CTL2,
			RK3026_ABCLK_POL_MASK | RK3026_I2S_MODE_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK3026_DAC_INT_CTL1,
			RK3026_DLRCK_POL_MASK | RK3026_DAC_DF_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK3026_DAC_INT_CTL2,
			RK3026_DBCLK_POL_MASK, dac_aif2);

	return 0;
}

static int rk3026_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec =rtd->codec;
	struct rk3026_codec_priv *rk3026 = rk3026_priv;
	unsigned int rate = params_rate(params);
	unsigned int div;
	unsigned int adc_aif1 = 0, adc_aif2  = 0, dac_aif1 = 0, dac_aif2  = 0;

	if (!rk3026) {
		printk("%s : rk3026 is NULL\n", __func__);
		return -EINVAL;
	}

	// bclk = codec_clk / 4
	// lrck = bclk / (wl * 2)
	div = (((rk3026->stereo_sysclk / 4) / rate) / 2);

	if ((rk3026->stereo_sysclk % (4 * rate * 2) > 0) ||
	    (div != 16 && div != 20 && div != 24 && div != 32)) {
		printk("%s : need PLL\n", __func__);
		return -EINVAL;
	}

	switch (div) {
	case 16:
		adc_aif2 |= RK3026_ADC_WL_16;
		dac_aif2 |= RK3026_DAC_WL_16;
		break;
	case 20:
		adc_aif2 |= RK3026_ADC_WL_20;
		dac_aif2 |= RK3026_DAC_WL_20;
		break;
	case 24:
		adc_aif2 |= RK3026_ADC_WL_24;
		dac_aif2 |= RK3026_DAC_WL_24;
		break;
	case 32:
		adc_aif2 |= RK3026_ADC_WL_32;
		dac_aif2 |= RK3026_DAC_WL_32;
		break;
	default:
		return -EINVAL;
	}


	DBG("%s : MCLK = %dHz, sample rate = %dHz, div = %d\n", __func__,
		rk3026->stereo_sysclk, rate, div);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK3026_ADC_VWL_16;
		dac_aif1 |= RK3026_DAC_VWL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK3026_ADC_VWL_20;
		dac_aif1 |= RK3026_DAC_VWL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK3026_ADC_VWL_24;
		dac_aif1 |= RK3026_DAC_VWL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK3026_ADC_VWL_32;
		dac_aif1 |= RK3026_DAC_VWL_32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case RK3026_MONO:
		adc_aif1 |= RK3026_ADC_TYPE_MONO;
		break;
	case RK3026_STEREO:
		adc_aif1 |= RK3026_ADC_TYPE_STEREO;
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= RK3026_ADC_SWAP_DIS;
	adc_aif2 |= RK3026_ADC_RST_DIS;
	dac_aif1 |= RK3026_DAC_SWAP_DIS;
	dac_aif2 |= RK3026_DAC_RST_DIS;

	rk3026->rate = rate;

	snd_soc_update_bits(codec, RK3026_ADC_INT_CTL1,
			 RK3026_ADC_VWL_MASK | RK3026_ADC_SWAP_MASK |
			 RK3026_ADC_TYPE_MASK, adc_aif1);
	snd_soc_update_bits(codec, RK3026_ADC_INT_CTL2,
			RK3026_ADC_WL_MASK | RK3026_ADC_RST_MASK, adc_aif2);
	snd_soc_update_bits(codec, RK3026_DAC_INT_CTL1,
			 RK3026_DAC_VWL_MASK | RK3026_DAC_SWAP_MASK, dac_aif1);
	snd_soc_update_bits(codec, RK3026_DAC_INT_CTL2,
			RK3026_DAC_WL_MASK | RK3026_DAC_RST_MASK, dac_aif2);

	return 0;
}

static int rk3026_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int is_hp_pd;


	is_hp_pd = (RK3026_HPOUTL_MSK | RK3026_HPOUTR_MSK) & snd_soc_read(codec, RK3026_HPOUT_CTL);

	if (mute) {
		if (rk3026_priv && rk3026_priv->hp_ctl_gpio != INVALID_GPIO &&
		    is_hp_pd) {
			DBG("%s : set hp ctl gpio LOW\n", __func__);
			gpio_set_value(rk3026_priv->hp_ctl_gpio, GPIO_LOW);
			msleep(200);//rk3026_priv->delay_time);
			}

	} else {
		if (rk3026_priv && rk3026_priv->hp_ctl_gpio != INVALID_GPIO &&
		    is_hp_pd) {
			DBG("%s : set hp ctl gpio HIGH\n", __func__);
			gpio_set_value(rk3026_priv->hp_ctl_gpio, GPIO_HIGH);
			msleep(100);//rk3026_priv->delay_time);
		}
	}
	return 0;
}

static struct rk3026_reg_val_typ playback_power_up_list[] = {
	{0x18,0x32},
	{0xa0,0x40},
	{0xa0,0x62},
	{0xa4,0x88},
	{0xa4,0xcc},
	{0xa4,0xee},
	{0xa8,0x44},
	{0xb0,0x92},
	{0xb0,0xdb},
	{0xac,0x11},//DAC
	{0xa8,0x55},
	{0xa8,0x77},
	{0xa4,0xff},
	{0xb0,0xff},
	{0xa0,0x73},
	{0xb4,OUT_VOLUME},
	{0xb8,OUT_VOLUME},
};
#define RK3026_CODEC_PLAYBACK_POWER_UP_LIST_LEN ARRAY_SIZE(playback_power_up_list)

static struct rk3026_reg_val_typ playback_power_down_list[] = {
	{0xb0,0xdb},
	{0xa8,0x44},
	{0xac,0x00},
	{0xb0,0x92},
	{0xa0,0x22},
	{0xb0,0x00},
	{0xa8,0x00},
	{0xa4,0x00},
	{0xa0,0x00},
	{0x18,0x22},
#ifdef WITH_CAP
	//{0xbc,0x08},
#endif
	{0xb4,0x0},
	{0xb8,0x0},
	{0x18,0x22},
};
#define RK3026_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN ARRAY_SIZE(playback_power_down_list)

static struct rk3026_reg_val_typ capture_power_up_list[] = {
	{0x88, 0x80},
	{0x88, 0xc0},
	{0x88, 0xc7},
	{0x9c, 0x88},
	{0x8c, 0x04},
	{0x90, 0x66},
	{0x9c, 0xcc},
	{0x9c, 0xee},
	{0x8c, 0x07},
	{0x90, 0x77},
	{0x94, 0x20 | CAP_VOL},
	{0x98, CAP_VOL},
	{0x88, 0xf7},

};
#define RK3026_CODEC_CAPTURE_POWER_UP_LIST_LEN ARRAY_SIZE(capture_power_up_list)

static struct rk3026_reg_val_typ capture_power_down_list[] = {
	{0x9c, 0xcc},
	{0x90, 0x66},
	{0x8c, 0x44},
	{0x9c, 0x88},
	{0x88, 0xc7},
	{0x88, 0xc0},
	{0x88, 0x80},
	{0x8c, 0x00},
	{0X94, 0x0c},
	{0X98, 0x0c},
	{0x9c, 0x00},
	{0x88, 0x00},
	{0x90, 0x44},
};
#define RK3026_CODEC_CAPTURE_POWER_DOWN_LIST_LEN ARRAY_SIZE(capture_power_down_list)

static int rk3026_codec_power_up(int type)
{
	struct snd_soc_codec *codec = rk3026_priv->codec;
	int i;

	if (!rk3026_priv || !rk3026_priv->codec) {
		printk("%s : rk3026_priv or rk3026_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}

	printk("%s : power up %s%s\n", __func__,
		type == RK3026_CODEC_PLAYBACK ? "playback" : "",
		type == RK3026_CODEC_CAPTURE ? "capture" : "");

	if (type == RK3026_CODEC_PLAYBACK) {
		for (i = 0; i < RK3026_CODEC_PLAYBACK_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_up_list[i].reg,
				playback_power_up_list[i].value);
		}
	} else if (type == RK3026_CODEC_CAPTURE) {
		for (i = 0; i < RK3026_CODEC_CAPTURE_POWER_UP_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_up_list[i].reg,
				capture_power_up_list[i].value);
		}
	} else if (type == RK3026_CODEC_INCALL) {
		snd_soc_update_bits(codec, RK3026_ALC_MUNIN_CTL,
			 RK3026_MUXINL_F_MSK | RK3026_MUXINR_F_MSK, 
			 RK3026_MUXINR_F_INR | RK3026_MUXINL_F_INL);
		
	}

	return 0;
}

static int rk3026_codec_power_down(int type)
{
	struct snd_soc_codec *codec = rk3026_priv->codec;
	int i;
      
	if (!rk3026_priv || !rk3026_priv->codec) {
		printk("%s : rk3026_priv or rk3026_priv->codec is NULL\n", __func__);
		return -EINVAL;
	}
	
	printk("%s : power down %s%s%s\n", __func__,
		type == RK3026_CODEC_PLAYBACK ? "playback" : "",
		type == RK3026_CODEC_CAPTURE ? "capture" : "",
		type == RK3026_CODEC_ALL ? "all" : "");

	if ((type == RK3026_CODEC_CAPTURE) || (type == RK3026_CODEC_INCALL)) {
		for (i = 0; i < RK3026_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, capture_power_down_list[i].reg,
				capture_power_down_list[i].value);
		}
	} else if (type == RK3026_CODEC_PLAYBACK) {
#if 0
		snd_soc_write(codec, 0xa0,0x62);
		for ( i = OUT_VOLUME; i >= 0; i--)
		{
			snd_soc_write(codec, 0xb4,i);
			snd_soc_write(codec, 0xb8,i);
		}
		msleep(20);
#endif
		for (i = 0; i < RK3026_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_write(codec, playback_power_down_list[i].reg,
				playback_power_down_list[i].value);

		}

	} else if (type == RK3026_CODEC_ALL) {
		rk3026_reset(codec);
	}

	return 0;
}

static void  rk3026_codec_capture_work(struct work_struct *work)
{
	DBG("%s : rk3026_codec_work_capture_type = %d\n", __func__,
		rk3026_codec_work_capture_type);

	switch (rk3026_codec_work_capture_type) {
	case RK3026_CODEC_WORK_POWER_DOWN:
		rk3026_codec_power_down(RK3026_CODEC_CAPTURE);
		break;
	case RK3026_CODEC_WORK_POWER_UP:
		rk3026_codec_power_up(RK3026_CODEC_CAPTURE);
		break;
	default:
		break;
	}

	rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
}

static int rk3026_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct rk3026_codec_priv *rk3026 = rk3026_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running = rk3026->playback_active > 0 ;
	bool is_codec_capture_running = rk3026->capture_active > 0;

	if (!rk3026_for_mid)
	{
		DBG("%s immediately return for phone\n",__func__);
		return 0;
	}

	if (!rk3026) {
		printk("%s : rk3026 is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : substream->stream : %s \n", __func__,
		playback ? "PLAYBACK":"CAPTURE");

	if (playback)
		rk3026->playback_active++;
	else
		rk3026->capture_active++;

	if (playback) {
		if (rk3026->playback_active > 0) {
			if (!is_codec_playback_running)
				rk3026_codec_power_up(RK3026_CODEC_PLAYBACK);
			else
				DBG(" Warning : playback has been opened, so return! \n");
		}
	} else {//capture
		if (rk3026->capture_active > 0 && !is_codec_capture_running) {
			if (rk3026_codec_work_capture_type != RK3026_CODEC_WORK_POWER_UP) {
				cancel_delayed_work_sync(&capture_delayed_work);
				if (rk3026_codec_work_capture_type == RK3026_CODEC_WORK_NULL) {
					rk3026_codec_power_up(RK3026_CODEC_CAPTURE);
				} else {
					DBG(" Warning : capture being closed, so interrupt the shutdown process ! \n");
					rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
				}
			} else {
				DBG("Warning : capture being opened, so return ! \n");
			}
		}
	}
	return 0;
}

static void rk3026_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct rk3026_codec_priv *rk3026 = rk3026_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running = rk3026->playback_active > 0;
	bool is_codec_capture_running = rk3026->capture_active > 0;
	
	if (!rk3026_for_mid)
	{
		DBG("%s immediately return for phone\n", __func__);
		return;
	}

	if (!rk3026) {
		printk("%s : rk3026 is NULL\n", __func__);
		return;
	}

	DBG("%s : substream->stream : %s \n", __func__,
		playback ? "PLAYBACK":"CAPTURE");

	if (playback)
		rk3026->playback_active--;
	else
		rk3026->capture_active--;

	if (playback) {
		if (rk3026->playback_active <= 0) {
			if (is_codec_playback_running == true)
				rk3026_codec_power_down(RK3026_CODEC_PLAYBACK);
			else
				DBG(" Warning : playback has been closed, so return !\n");
		}
	} else {//capture
		if (rk3026->capture_active <= 0) {
			if ((rk3026_codec_work_capture_type != RK3026_CODEC_WORK_POWER_DOWN) &&
			    (is_codec_capture_running == true)) {
				cancel_delayed_work_sync(&capture_delayed_work);
				/*
				* If rk3026_codec_work_capture_type is NULL means codec already power down,
				* so power up codec.
				* If rk3026_codec_work_capture_type is RK3026_CODEC_WORK_POWER_UP it means
				* codec haven't be powered up, so we don't need to power down codec.
				* If is playback call power down, power down immediatly, because audioflinger
				* already has delay 3s.
				*/
				if (rk3026_codec_work_capture_type == RK3026_CODEC_WORK_NULL) {
					rk3026_codec_work_capture_type = RK3026_CODEC_WORK_POWER_DOWN;
					queue_delayed_work(rk3026_codec_workq, &capture_delayed_work,msecs_to_jiffies(3000));
				} else {
					rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
					DBG(" Warning : capture being opened, so interrupt the open process ! \n");
				}
			} else {
				DBG(" Warning : capture has been closed or it being closed, so return !\n");
			}
		}
	}
}

#define RK3026_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK3026_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK3026_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk3026_dai_ops = {
	.hw_params	= rk3026_hw_params,
	.set_fmt	= rk3026_set_dai_fmt,
	.set_sysclk	= rk3026_set_dai_sysclk,
	.digital_mute	= rk3026_digital_mute,
	.startup	= rk3026_startup,
	.shutdown	= rk3026_shutdown,
};

static struct snd_soc_dai_driver rk3026_dai[] = {
	{
		.name = "rk3026-hifi",
		.id = RK3026_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK3026_PLAYBACK_RATES,
			.formats = RK3026_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK3026_CAPTURE_RATES,
			.formats = RK3026_FORMATS,
		},
		.ops = &rk3026_dai_ops,
	},
	{
		.name = "rk3026-voice",
		.id = RK3026_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3026_PLAYBACK_RATES,
			.formats = RK3026_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3026_CAPTURE_RATES,
			.formats = RK3026_FORMATS,
		},
		.ops = &rk3026_dai_ops,
	},

};

static int rk3026_suspend(struct snd_soc_codec *codec)
{
	if (rk3026_for_mid)
	{
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk3026_codec_work_capture_type != RK3026_CODEC_WORK_NULL) {
			rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
		}
		rk3026_codec_power_down(RK3026_CODEC_PLAYBACK);
		rk3026_codec_power_down(RK3026_CODEC_ALL);
		snd_soc_write(codec, RK3026_SELECT_CURRENT,0x1e);
		snd_soc_write(codec, RK3026_SELECT_CURRENT,0x3e);
	}
	else
		rk3026_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int rk3026_resume(struct snd_soc_codec *codec)
{
	if (!rk3026_for_mid)
		rk3026_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int rk3026_probe(struct snd_soc_codec *codec)
{
	struct rk3026_codec_priv *rk3026;
	struct rk3026_codec_pdata  *rk3026_plt = codec->dev->platform_data;
	struct platform_device *pdev = to_platform_device(codec->dev);
	struct resource *res, *mem;
	int ret;
	unsigned int val;

	DBG("%s\n", __func__);

	rk3026 = kzalloc(sizeof(struct rk3026_codec_priv), GFP_KERNEL);
	if (!rk3026) {
		printk("%s : rk3026 priv kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	rk3026->codec = codec;

	res = pdev->resource;
	rk3026->regbase_phy = res->start;
	rk3026->regsize_phy = (res->end - res->start) + 1;

	mem = request_mem_region(res->start, (res->end - res->start) + 1, pdev->name);
	if (!mem)
	{
    		dev_err(&pdev->dev, "failed to request mem region for rk2928 codec\n");
    		ret = -ENOENT;
    		goto err__;
	}
	
	rk3026->regbase = (int)ioremap(res->start, (res->end - res->start) + 1);
	if (!rk3026->regbase) {
		dev_err(&pdev->dev, "cannot ioremap acodec registers\n");
		ret = -ENXIO;
		goto err__;
	}
	
	rk3026->pclk = clk_get(NULL,"pclk_acodec");
	if(IS_ERR(rk3026->pclk))
	{
		dev_err(&pdev->dev, "Unable to get acodec hclk\n");
		ret = -ENXIO;
		goto err__;
	}
	clk_enable(rk3026->pclk);

	rk3026_priv = rk3026;

	if (rk3026_priv && rk3026_plt->spk_ctl_gpio) {
		gpio_request(rk3026_plt->spk_ctl_gpio, NULL);
		gpio_direction_output(rk3026_plt->spk_ctl_gpio, GPIO_LOW);
		rk3026->spk_ctl_gpio = rk3026_plt->spk_ctl_gpio;
		rk3026->hp_ctl_gpio = rk3026_plt->hp_ctl_gpio;
	} else {
		printk("%s : rk3026 spk_ctl_gpio is NULL!\n", __func__);
		rk3026->spk_ctl_gpio = INVALID_GPIO;
	}	

	if (rk3026_priv && rk3026_plt->hp_ctl_gpio) {
		gpio_request(rk3026_plt->hp_ctl_gpio, NULL);
		gpio_direction_output(rk3026_plt->hp_ctl_gpio, GPIO_LOW);
		rk3026->hp_ctl_gpio = rk3026_plt->hp_ctl_gpio;
	} else {
		printk("%s : rk3026 hp_ctl_gpio is NULL!\n", __func__);
		rk3026->hp_ctl_gpio = INVALID_GPIO;
	}

	if (rk3026_plt->delay_time) {
		rk3026->delay_time = rk3026_plt->delay_time;
	} else {
		printk("%s : rk3026 delay_time is NULL!\n", __func__);
		rk3026->delay_time = 10;
	}


	if (rk3026_for_mid)
	{
		rk3026->playback_active = 0;
		rk3026->capture_active = 0;

		rk3026_codec_workq = create_freezable_workqueue("rk3026-codec");

		if (rk3026_codec_workq == NULL) {
			printk("%s : create work FAIL! rk3026_codec_workq is NULL!\n", __func__);
			ret = -ENOMEM;
			goto err__;
		}
	}

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		printk("%s : Failed to set cache I/O: %d\n", __func__, ret);
		goto err__;
	}

	codec->hw_read = rk3026_codec_read;
	codec->hw_write = (hw_write_t)rk3026_hw_write;
	codec->read = rk3026_codec_read;
	codec->write = rk3026_codec_write;

	val = snd_soc_read(codec, RK3026_RESET);
	if (val != rk3026_reg_defaults[RK3026_RESET]) {
		printk("%s : codec register 0: %x is not a 0x00000003\n", __func__, val);
		ret = -ENODEV;
		goto err__;
	}

	rk3026_reset(codec);

	if (!rk3026_for_mid)
	{
		codec->dapm.bias_level = SND_SOC_BIAS_OFF;
		rk3026_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	}

#ifdef   WITH_CAP
	//set for capacity output,clear up noise
	snd_soc_write(codec, RK3026_SELECT_CURRENT,0x1e);
	snd_soc_write(codec, RK3026_SELECT_CURRENT,0x3e);
	//snd_soc_write(codec, 0xbc,0x28);
#endif
	// select  i2s sdi from  acodec  soc_con[0] bit 10
	val = readl(RK2928_GRF_BASE+GRF_SOC_CON0);
	writel(val | 0x04000400,RK2928_GRF_BASE+GRF_SOC_CON0);
	val = readl(RK2928_GRF_BASE+GRF_SOC_CON0);
	printk("%s : i2s sdi from acodec val=0x%x,soc_con[0] bit 10 =1 is correct\n",__func__,val);


	if(!rk3026_for_mid) 
	{
		codec->dapm.bias_level = SND_SOC_BIAS_OFF;
		rk3026_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

		snd_soc_add_codec_controls(codec, rk3026_snd_controls,
			ARRAY_SIZE(rk3026_snd_controls));
		snd_soc_dapm_new_controls(&codec->dapm, rk3026_dapm_widgets,
			ARRAY_SIZE(rk3026_dapm_widgets));
		snd_soc_dapm_add_routes(&codec->dapm, rk3026_dapm_routes,
			ARRAY_SIZE(rk3026_dapm_routes));

	}
	
#ifdef CONFIG_MACH_RK_FAC 
	rk3026_hdmi_ctrl=1;
#endif
	return 0;

err__:
	release_mem_region(res->start,(res->end - res->start) + 1);
	kfree(rk3026);
	rk3026 = NULL;
	rk3026_priv = NULL;

	return ret;
}

/* power down chip */
static int rk3026_remove(struct snd_soc_codec *codec)
{
	
	DBG("%s\n", __func__);

	if (!rk3026_priv) {
		printk("%s : rk3026_priv is NULL\n", __func__);
		return 0;
	}

	if (rk3026_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3026_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk3026_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3026_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	if (rk3026_for_mid)
	{
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk3026_codec_work_capture_type != RK3026_CODEC_WORK_NULL) {
			rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
		}
	}

	snd_soc_write(codec, RK3026_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(codec, RK3026_RESET, 0x3);
	mdelay(10);

	if (rk3026_priv)
		kfree(rk3026_priv);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_rk3026 = {
	.probe =rk3026_probe,
	.remove =rk3026_remove,
	.suspend =rk3026_suspend,
	.resume = rk3026_resume,
	.set_bias_level = rk3026_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(rk3026_reg_defaults),
	.reg_word_size = sizeof(unsigned int),
	.reg_cache_default = rk3026_reg_defaults,
	.volatile_register = rk3026_volatile_register,
	.readable_register = rk3026_codec_register,
	.reg_cache_step = sizeof(unsigned int),
};

static int rk3026_platform_probe(struct platform_device *pdev)
{
	DBG("%s\n", __func__);

	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_rk3026, rk3026_dai, ARRAY_SIZE(rk3026_dai));
}

static int rk3026_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

void rk3026_platform_shutdown(struct platform_device *pdev)
{

	DBG("%s\n", __func__);

	if (!rk3026_priv || !rk3026_priv->codec) {
		printk("%s : rk3026_priv or rk3026_priv->codec is NULL\n", __func__);
		return;
	}

	if (rk3026_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3026_priv->spk_ctl_gpio, GPIO_LOW);

	if (rk3026_priv->hp_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3026_priv->hp_ctl_gpio, GPIO_LOW);

	mdelay(10);

	if (rk3026_for_mid) {
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk3026_codec_work_capture_type != RK3026_CODEC_WORK_NULL) {
			rk3026_codec_work_capture_type = RK3026_CODEC_WORK_NULL;
		}
	}

	writel(0xfc, rk3026_priv->regbase+RK3026_RESET);
	mdelay(10);
	writel(0x03, rk3026_priv->regbase+RK3026_RESET);

	if (rk3026_priv)
		kfree(rk3026_priv);
}

static struct platform_driver rk3026_codec_driver = {
	.driver = {
		   .name = "rk3026-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = rk3026_platform_probe,
	.remove = rk3026_platform_remove,
	.shutdown = rk3026_platform_shutdown,
};


static __init int rk3026_modinit(void)
{
	rk3026_get_parameter();
	return platform_driver_register(&rk3026_codec_driver);
}
module_init(rk3026_modinit);

static __exit void rk3026_exit(void)
{
	platform_driver_unregister(&rk3026_codec_driver);
}
module_exit(rk3026_exit);

MODULE_DESCRIPTION("ASoC RK3026 driver");
MODULE_AUTHOR("yj <yangjie@rock-chips.com>");
MODULE_LICENSE("GPL");
