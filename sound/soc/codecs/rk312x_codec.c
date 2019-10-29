/*
 * rk312x_codec.c
 *
 * Driver for rockchip rk312x codec
 * Copyright (C) 2014
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/rockchip/grf.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/dmaengine_pcm.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <sound/tlv.h>
#include <linux/extcon-provider.h>
#include "rk312x_codec.h"

static int debug = -1;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dbg_codec(level, fmt, arg...)		\
	do {					\
		if (debug >= level)		\
			printk(fmt , ## arg);	\
	 } while (0)

#define	DBG(fmt, ...)	dbg_codec(0, fmt, ## __VA_ARGS__)

#define INVALID_GPIO   -1
#define CODEC_SET_SPK 1
#define CODEC_SET_HP 2
#define SWITCH_SPK 1
#define GRF_ACODEC_CON  0x013c
#define GRF_SOC_STATUS0 0x014c
/* volume setting
 *  0: -39dB
 *  26: 0dB
 *  31: 6dB
 *  Step: 1.5dB
*/
#define  OUT_VOLUME    25

/* capture vol set
 * 0: -18db
 * 12: 0db
 * 31: 28.5db
 * step: 1.5db
*/
#define CAP_VOL		26	/*0-31 */
/*with capacity or not*/
#define WITH_CAP

struct rk312x_codec_priv {
	void __iomem	*regbase;
	struct regmap *regmap;
	struct regmap *grf;
	struct device *dev;
	unsigned int irq;
	struct snd_soc_component *component;

	unsigned int stereo_sysclk;
	unsigned int rate;

	int playback_active;
	int capture_active;
	struct gpio_desc	*spk_ctl_gpio;
	struct gpio_desc	*hp_ctl_gpio;
	int spk_mute_delay;
	int hp_mute_delay;
	int spk_hp_switch_gpio;
	/* 1 spk; */
	/* 0 hp; */
	enum of_gpio_flags spk_io;

	/* 0 for box; */
	/* 1 default for mid; */
	int rk312x_for_mid;
	/* 1: for rk3128 */
	/* 0: for rk3126 */
	int is_rk3128;
	int gpio_debug;
	int codec_hp_det;
	unsigned int spk_volume;
	unsigned int hp_volume;
	unsigned int capture_volume;

	long int playback_path;
	long int capture_path;
	long int voice_call_path;
	struct clk	*pclk;
	struct clk	*mclk;
	struct extcon_dev *edev;
	struct work_struct work;
	struct delayed_work init_delayed_work;
	struct delayed_work mute_delayed_work;
	struct delayed_work hpdet_work;
};

static const unsigned int headset_extcon_cable[] = {
	EXTCON_JACK_MICROPHONE,
	EXTCON_JACK_HEADPHONE,
	EXTCON_NONE,
};

static struct rk312x_codec_priv *rk312x_priv;

#define RK312x_CODEC_ALL	0
#define RK312x_CODEC_PLAYBACK	1
#define RK312x_CODEC_CAPTURE	2
#define RK312x_CODEC_INCALL	3

#define RK312x_CODEC_WORK_NULL	0
#define RK312x_CODEC_WORK_POWER_DOWN	1
#define RK312x_CODEC_WORK_POWER_UP	2
static struct workqueue_struct *rk312x_codec_workq;

static void rk312x_codec_capture_work(struct work_struct *work);
static void rk312x_codec_unpop(struct work_struct *work);
static DECLARE_DELAYED_WORK(capture_delayed_work, rk312x_codec_capture_work);
static int rk312x_codec_work_capture_type = RK312x_CODEC_WORK_NULL;
/* static bool rk312x_for_mid = 1; */
static int rk312x_codec_power_up(int type);
static const unsigned int rk312x_reg_defaults[RK312x_PGAR_AGC_CTL5+1] = {
	[RK312x_RESET] = 0x0003,
	[RK312x_ADC_INT_CTL1] = 0x0050,
	[RK312x_ADC_INT_CTL2] = 0x000e,
	[RK312x_DAC_INT_CTL1] = 0x0050,
	[RK312x_DAC_INT_CTL2] = 0x000e,
	[RK312x_DAC_INT_CTL3] = 0x22,
	[RK312x_ADC_MIC_CTL] = 0x0000,
	[RK312x_BST_CTL] = 0x000,
	[RK312x_ALC_MUNIN_CTL] = 0x0044,
	[RK312x_BSTL_ALCL_CTL] = 0x000c,
	[RK312x_ALCR_GAIN_CTL] = 0x000C,
	[RK312x_ADC_ENABLE] = 0x0000,
	[RK312x_DAC_CTL] = 0x0000,
	[RK312x_DAC_ENABLE] = 0x0000,
	[RK312x_HPMIX_CTL] = 0x0000,
	[RK312x_HPMIX_S_SELECT] = 0x0000,
	[RK312x_HPOUT_CTL] = 0x0000,
	[RK312x_HPOUTL_GAIN] = 0x0000,
	[RK312x_HPOUTR_GAIN] = 0x0000,
	[RK312x_SELECT_CURRENT] = 0x003e,
	[RK312x_PGAL_AGC_CTL1] = 0x0000,
	[RK312x_PGAL_AGC_CTL2] = 0x0046,
	[RK312x_PGAL_AGC_CTL3] = 0x0041,
	[RK312x_PGAL_AGC_CTL4] = 0x002c,
	[RK312x_PGAL_ASR_CTL] = 0x0000,
	[RK312x_PGAL_AGC_MAX_H] = 0x0026,
	[RK312x_PGAL_AGC_MAX_L] = 0x0040,
	[RK312x_PGAL_AGC_MIN_H] = 0x0036,
	[RK312x_PGAL_AGC_MIN_L] = 0x0020,
	[RK312x_PGAL_AGC_CTL5] = 0x0038,
	[RK312x_PGAR_AGC_CTL1] = 0x0000,
	[RK312x_PGAR_AGC_CTL2] = 0x0046,
	[RK312x_PGAR_AGC_CTL3] = 0x0041,
	[RK312x_PGAR_AGC_CTL4] = 0x002c,
	[RK312x_PGAR_ASR_CTL] = 0x0000,
	[RK312x_PGAR_AGC_MAX_H] = 0x0026,
	[RK312x_PGAR_AGC_MAX_L] = 0x0040,
	[RK312x_PGAR_AGC_MIN_H] = 0x0036,
	[RK312x_PGAR_AGC_MIN_L] = 0x0020,
	[RK312x_PGAR_AGC_CTL5] = 0x0038,
};

static bool rk312x_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK312x_RESET:
		return true;
	default:
		return false;
	}
}

static bool rk312x_codec_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK312x_RESET:
	case RK312x_ADC_INT_CTL1:
	case RK312x_ADC_INT_CTL2:
	case RK312x_DAC_INT_CTL1:
	case RK312x_DAC_INT_CTL2:
	case RK312x_DAC_INT_CTL3:
	case RK312x_ADC_MIC_CTL:
	case RK312x_BST_CTL:
	case RK312x_ALC_MUNIN_CTL:
	case RK312x_BSTL_ALCL_CTL:
	case RK312x_ALCR_GAIN_CTL:
	case RK312x_ADC_ENABLE:
	case RK312x_DAC_CTL:
	case RK312x_DAC_ENABLE:
	case RK312x_HPMIX_CTL:
	case RK312x_HPMIX_S_SELECT:
	case RK312x_HPOUT_CTL:
	case RK312x_HPOUTL_GAIN:
	case RK312x_HPOUTR_GAIN:
	case RK312x_SELECT_CURRENT:
	case RK312x_PGAL_AGC_CTL1:
	case RK312x_PGAL_AGC_CTL2:
	case RK312x_PGAL_AGC_CTL3:
	case RK312x_PGAL_AGC_CTL4:
	case RK312x_PGAL_ASR_CTL:
	case RK312x_PGAL_AGC_MAX_H:
	case RK312x_PGAL_AGC_MAX_L:
	case RK312x_PGAL_AGC_MIN_H:
	case RK312x_PGAL_AGC_MIN_L:
	case RK312x_PGAL_AGC_CTL5:
	case RK312x_PGAR_AGC_CTL1:
	case RK312x_PGAR_AGC_CTL2:
	case RK312x_PGAR_AGC_CTL3:
	case RK312x_PGAR_AGC_CTL4:
	case RK312x_PGAR_ASR_CTL:
	case RK312x_PGAR_AGC_MAX_H:
	case RK312x_PGAR_AGC_MAX_L:
	case RK312x_PGAR_AGC_MIN_H:
	case RK312x_PGAR_AGC_MIN_L:
	case RK312x_PGAR_AGC_CTL5:
	case RK312x_ALC_CTL:
		return true;
	default:
		return false;
	}
}

static int rk312x_codec_ctl_gpio(int gpio, int level)
{

	if (!rk312x_priv) {
		DBG("%s : rk312x is NULL\n", __func__);
		return -EINVAL;
	}

	if ((gpio & CODEC_SET_SPK) && rk312x_priv &&
	    rk312x_priv->spk_ctl_gpio) {
		gpiod_set_value(rk312x_priv->spk_ctl_gpio, level);
		DBG(KERN_INFO"%s set spk clt %d\n", __func__, level);
		msleep(rk312x_priv->spk_mute_delay);
	}

	if ((gpio & CODEC_SET_HP) && rk312x_priv &&
	    rk312x_priv->hp_ctl_gpio) {
		gpiod_set_value(rk312x_priv->hp_ctl_gpio, level);
		DBG(KERN_INFO"%s set hp clt %d\n", __func__, level);
		msleep(rk312x_priv->hp_mute_delay);
	}

	return 0;
}

#if 0
static int switch_to_spk(int enable)
{
	if (!rk312x_priv) {
		DBG(KERN_ERR"%s : rk312x is NULL\n", __func__);
		return -EINVAL;
	}
	if (enable) {
		if (rk312x_priv->spk_hp_switch_gpio != INVALID_GPIO) {
			gpio_set_value(rk312x_priv->spk_hp_switch_gpio, rk312x_priv->spk_io);
			DBG(KERN_INFO"%s switch to spk\n", __func__);
			msleep(rk312x_priv->spk_mute_delay);
		}
	} else {
		if (rk312x_priv->spk_hp_switch_gpio != INVALID_GPIO) {
			gpio_set_value(rk312x_priv->spk_hp_switch_gpio, !rk312x_priv->spk_io);
			DBG(KERN_INFO"%s switch to hp\n", __func__);
			msleep(rk312x_priv->hp_mute_delay);
		}
	}
	return 0;
}
#endif

static int rk312x_reset(struct snd_soc_component *component)
{
	DBG("%s\n", __func__);
	regmap_write(rk312x_priv->regmap, RK312x_RESET, 0x00);
	mdelay(10);
	regmap_write(rk312x_priv->regmap, RK312x_RESET, 0x43);
	mdelay(10);

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -3900, 150, 0);
static const DECLARE_TLV_DB_SCALE(pga_vol_tlv, -1800, 150, 0);
static const DECLARE_TLV_DB_SCALE(bst_vol_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_max_vol_tlv, -1350, 600, 0);
static const DECLARE_TLV_DB_SCALE(pga_agc_min_vol_tlv, -1800, 600, 0);

static const char *const rk312x_input_mode[] = {
			"Differential", "Single-Ended"};

static const char *const rk312x_micbias_ratio[] = {
			"1.0 Vref", "1.1 Vref",
			"1.2 Vref", "1.3 Vref",
			"1.4 Vref", "1.5 Vref",
			"1.6 Vref", "1.7 Vref",};

static const char *const rk312x_dis_en_sel[] = {"Disable", "Enable"};

static const char *const rk312x_pga_agc_way[] = {"Normal", "Jack"};

static const char *const rk312x_agc_backup_way[] = {
			"Normal", "Jack1", "Jack2", "Jack3"};

static const char *const rk312x_pga_agc_hold_time[] = {
			"0ms", "2ms", "4ms", "8ms",
			"16ms", "32ms", "64ms",
			"128ms", "256ms", "512ms", "1s"};

static const char *const rk312x_pga_agc_ramp_up_time[] = {
		"Normal:500us Jack:125us",
		"Normal:1ms Jack:250us",
		"Normal:2ms Jack:500us",
		"Normal:4ms Jack:1ms",
		"Normal:8ms Jack:2ms",
		"Normal:16ms Jack:4ms",
		"Normal:32ms Jack:8ms",
		"Normal:64ms Jack:16ms",
		"Normal:128ms Jack:32ms",
		"Normal:256ms Jack:64ms",
		"Normal:512ms Jack:128ms"};

static const char *const rk312x_pga_agc_ramp_down_time[] = {
		"Normal:125us Jack:32us",
		"Normal:250us Jack:64us",
		"Normal:500us Jack:125us",
		"Normal:1ms Jack:250us",
		"Normal:2ms Jack:500us",
		"Normal:4ms Jack:1ms",
		"Normal:8ms Jack:2ms",
		"Normal:16ms Jack:4ms",
		"Normal:32ms Jack:8ms",
		"Normal:64ms Jack:16ms",
		"Normal:128ms Jack:32ms"};

static const char *const rk312x_pga_agc_mode[] = {"Normal", "Limiter"};

static const char *const rk312x_pga_agc_recovery_mode[] = {
		"Right Now", "After AGC to Limiter"};

static const char *const rk312x_pga_agc_noise_gate_threhold[] = {
		"-39dB", "-45dB", "-51dB",
		"-57dB", "-63dB", "-69dB", "-75dB", "-81dB"};

static const char *const rk312x_pga_agc_update_gain[] = {
		"Right Now", "After 1st Zero Cross"};

static const char *const rk312x_pga_agc_approximate_sample_rate[] = {
		"96KHZ", "48KHz", "441KHZ", "32KHz",
		"24KHz", "16KHz", "12KHz", "8KHz"};

static const struct soc_enum rk312x_bst_enum[] = {
		SOC_ENUM_SINGLE(RK312x_BSTL_ALCL_CTL,
				RK312x_BSTL_MODE_SFT, 2,
				rk312x_input_mode),
};


static const struct soc_enum rk312x_micbias_enum[] = {
	SOC_ENUM_SINGLE(RK312x_ADC_MIC_CTL,
			RK312x_MICBIAS_VOL_SHT, 8,
			rk312x_micbias_ratio),
};

static const struct soc_enum rk312x_agcl_enum[] = {
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL1,
			RK312x_PGA_AGC_BK_WAY_SFT, 4,
			rk312x_agc_backup_way),/*0*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL1,
			RK312x_PGA_AGC_WAY_SFT, 2,
			rk312x_pga_agc_way),/*1*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL1,
			RK312x_PGA_AGC_HOLD_T_SFT, 11,
			rk312x_pga_agc_hold_time),/*2*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL2,
			RK312x_PGA_AGC_GRU_T_SFT, 11,
			rk312x_pga_agc_ramp_up_time),/*3*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL2,
			RK312x_PGA_AGC_GRD_T_SFT, 11,
			rk312x_pga_agc_ramp_down_time),/*4*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_MODE_SFT, 2,
			rk312x_pga_agc_mode),/*5*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_ZO_SFT, 2,
			rk312x_dis_en_sel),/*6*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_REC_MODE_SFT, 2,
			rk312x_pga_agc_recovery_mode),/*7*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_FAST_D_SFT, 2,
			rk312x_dis_en_sel),/*8*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_NG_SFT, 2,
			rk312x_dis_en_sel),/*9*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL3,
			RK312x_PGA_AGC_NG_THR_SFT, 8,
			rk312x_pga_agc_noise_gate_threhold),/*10*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL4,
			RK312x_PGA_AGC_ZO_MODE_SFT, 2,
			rk312x_pga_agc_update_gain),/*11*/
	SOC_ENUM_SINGLE(RK312x_PGAL_ASR_CTL,
			RK312x_PGA_SLOW_CLK_SFT, 2,
			rk312x_dis_en_sel),/*12*/
	SOC_ENUM_SINGLE(RK312x_PGAL_ASR_CTL,
			RK312x_PGA_ASR_SFT, 8,
			rk312x_pga_agc_approximate_sample_rate),/*13*/
	SOC_ENUM_SINGLE(RK312x_PGAL_AGC_CTL5,
			RK312x_PGA_AGC_SFT, 2,
			rk312x_dis_en_sel),/*14*/
};

static const struct soc_enum rk312x_agcr_enum[] = {
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL1,
			RK312x_PGA_AGC_BK_WAY_SFT, 4,
			rk312x_agc_backup_way),/*0*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL1,
			RK312x_PGA_AGC_WAY_SFT, 2,
			rk312x_pga_agc_way),/*1*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL1,
			RK312x_PGA_AGC_HOLD_T_SFT, 11,
			rk312x_pga_agc_hold_time),/*2*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL2,
			RK312x_PGA_AGC_GRU_T_SFT, 11,
			rk312x_pga_agc_ramp_up_time),/*3*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL2,
			RK312x_PGA_AGC_GRD_T_SFT, 11,
			rk312x_pga_agc_ramp_down_time),/*4*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_MODE_SFT, 2,
			rk312x_pga_agc_mode),/*5*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_ZO_SFT, 2,
			rk312x_dis_en_sel),/*6*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_REC_MODE_SFT, 2,
			rk312x_pga_agc_recovery_mode),/*7*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_FAST_D_SFT, 2,
			rk312x_dis_en_sel),/*8*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_NG_SFT, 2, rk312x_dis_en_sel),/*9*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL3,
			RK312x_PGA_AGC_NG_THR_SFT, 8,
			rk312x_pga_agc_noise_gate_threhold),/*10*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL4,
			RK312x_PGA_AGC_ZO_MODE_SFT, 2,
			rk312x_pga_agc_update_gain),/*11*/
	SOC_ENUM_SINGLE(RK312x_PGAR_ASR_CTL,
			RK312x_PGA_SLOW_CLK_SFT, 2,
			rk312x_dis_en_sel),/*12*/
	SOC_ENUM_SINGLE(RK312x_PGAR_ASR_CTL,
			RK312x_PGA_ASR_SFT, 8,
			rk312x_pga_agc_approximate_sample_rate),/*13*/
	SOC_ENUM_SINGLE(RK312x_PGAR_AGC_CTL5,
			RK312x_PGA_AGC_SFT, 2,
			rk312x_dis_en_sel),/*14*/
};

static const struct snd_kcontrol_new rk312x_snd_controls[] = {
	/* Add for set voice volume */
	SOC_DOUBLE_R_TLV("Speaker Playback Volume", RK312x_HPOUTL_GAIN,
			 RK312x_HPOUTR_GAIN, RK312x_HPOUT_GAIN_SFT,
			 31, 0, out_vol_tlv),
	SOC_DOUBLE("Speaker Playback Switch", RK312x_HPOUT_CTL,
		   RK312x_HPOUTL_MUTE_SHT, RK312x_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RK312x_HPOUTL_GAIN,
			 RK312x_HPOUTR_GAIN, RK312x_HPOUT_GAIN_SFT,
			 31, 0, out_vol_tlv),
	SOC_DOUBLE("Headphone Playback Switch", RK312x_HPOUT_CTL,
		   RK312x_HPOUTL_MUTE_SHT, RK312x_HPOUTR_MUTE_SHT, 1, 0),
	SOC_DOUBLE_R_TLV("Earpiece Playback Volume", RK312x_HPOUTL_GAIN,
			 RK312x_HPOUTR_GAIN, RK312x_HPOUT_GAIN_SFT,
			 31, 0, out_vol_tlv),
	SOC_DOUBLE("Earpiece Playback Switch", RK312x_HPOUT_CTL,
		   RK312x_HPOUTL_MUTE_SHT, RK312x_HPOUTR_MUTE_SHT, 1, 0),


	/* Add for set capture mute */
	SOC_SINGLE_TLV("Main Mic Capture Volume", RK312x_BST_CTL,
		       RK312x_BSTL_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Main Mic Capture Switch", RK312x_BST_CTL,
		   RK312x_BSTL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("Headset Mic Capture Volume", RK312x_BST_CTL,
		       RK312x_BSTR_GAIN_SHT, 1, 0, bst_vol_tlv),
	SOC_SINGLE("Headset Mic Capture Switch", RK312x_BST_CTL,
		   RK312x_BSTR_MUTE_SHT, 1, 0),

	SOC_SINGLE("ALCL Switch", RK312x_ALC_MUNIN_CTL,
		   RK312x_ALCL_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCL Capture Volume", RK312x_BSTL_ALCL_CTL,
		       RK312x_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),
	SOC_SINGLE("ALCR Switch", RK312x_ALC_MUNIN_CTL,
		   RK312x_ALCR_MUTE_SHT, 1, 0),
	SOC_SINGLE_TLV("ALCR Capture Volume", RK312x_ALCR_GAIN_CTL,
		       RK312x_ALCL_GAIN_SHT, 31, 0, pga_vol_tlv),

	SOC_ENUM("BST_L Mode",  rk312x_bst_enum[0]),

	SOC_ENUM("Micbias Voltage",  rk312x_micbias_enum[0]),
	SOC_ENUM("PGAL AGC Back Way",  rk312x_agcl_enum[0]),
	SOC_ENUM("PGAL AGC Way",  rk312x_agcl_enum[1]),
	SOC_ENUM("PGAL AGC Hold Time",  rk312x_agcl_enum[2]),
	SOC_ENUM("PGAL AGC Ramp Up Time",  rk312x_agcl_enum[3]),
	SOC_ENUM("PGAL AGC Ramp Down Time",  rk312x_agcl_enum[4]),
	SOC_ENUM("PGAL AGC Mode",  rk312x_agcl_enum[5]),
	SOC_ENUM("PGAL AGC Gain Update Zero Enable",  rk312x_agcl_enum[6]),
	SOC_ENUM("PGAL AGC Gain Recovery LPGA VOL",  rk312x_agcl_enum[7]),
	SOC_ENUM("PGAL AGC Fast Decrement Enable",  rk312x_agcl_enum[8]),
	SOC_ENUM("PGAL AGC Noise Gate Enable",  rk312x_agcl_enum[9]),
	SOC_ENUM("PGAL AGC Noise Gate Threhold",  rk312x_agcl_enum[10]),
	SOC_ENUM("PGAL AGC Upate Gain",  rk312x_agcl_enum[11]),
	SOC_ENUM("PGAL AGC Slow Clock Enable",  rk312x_agcl_enum[12]),
	SOC_ENUM("PGAL AGC Approximate Sample Rate",  rk312x_agcl_enum[13]),
	SOC_ENUM("PGAL AGC Enable",  rk312x_agcl_enum[14]),

	SOC_SINGLE_TLV("PGAL AGC Volume", RK312x_PGAL_AGC_CTL4,
		       RK312x_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),

	SOC_SINGLE("PGAL AGC Max Level High 8 Bits",
		   RK312x_PGAL_AGC_MAX_H,
		   0, 255, 0),
	SOC_SINGLE("PGAL AGC Max Level Low 8 Bits",
		   RK312x_PGAL_AGC_MAX_L,
		   0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level High 8 Bits",
		   RK312x_PGAL_AGC_MIN_H,
		   0, 255, 0),
	SOC_SINGLE("PGAL AGC Min Level Low 8 Bits",
		   RK312x_PGAL_AGC_MIN_L,
		   0, 255, 0),

	SOC_SINGLE_TLV("PGAL AGC Max Gain",
		       RK312x_PGAL_AGC_CTL5,
		       RK312x_PGA_AGC_MAX_G_SFT, 7, 0,
		       pga_agc_max_vol_tlv),
	/* AGC enable and 0x0a bit 5 is 1 */
	SOC_SINGLE_TLV("PGAL AGC Min Gain", RK312x_PGAL_AGC_CTL5,
		       RK312x_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),
	/* AGC enable and 0x0a bit 5 is 1 */

	SOC_ENUM("PGAR AGC Back Way",  rk312x_agcr_enum[0]),
	SOC_ENUM("PGAR AGC Way",  rk312x_agcr_enum[1]),
	SOC_ENUM("PGAR AGC Hold Time",  rk312x_agcr_enum[2]),
	SOC_ENUM("PGAR AGC Ramp Up Time",  rk312x_agcr_enum[3]),
	SOC_ENUM("PGAR AGC Ramp Down Time",  rk312x_agcr_enum[4]),
	SOC_ENUM("PGAR AGC Mode",  rk312x_agcr_enum[5]),
	SOC_ENUM("PGAR AGC Gain Update Zero Enable",  rk312x_agcr_enum[6]),
	SOC_ENUM("PGAR AGC Gain Recovery LPGA VOL",  rk312x_agcr_enum[7]),
	SOC_ENUM("PGAR AGC Fast Decrement Enable",  rk312x_agcr_enum[8]),
	SOC_ENUM("PGAR AGC Noise Gate Enable",  rk312x_agcr_enum[9]),
	SOC_ENUM("PGAR AGC Noise Gate Threhold",  rk312x_agcr_enum[10]),
	SOC_ENUM("PGAR AGC Upate Gain",  rk312x_agcr_enum[11]),
	SOC_ENUM("PGAR AGC Slow Clock Enable",  rk312x_agcr_enum[12]),
	SOC_ENUM("PGAR AGC Approximate Sample Rate",  rk312x_agcr_enum[13]),
	SOC_ENUM("PGAR AGC Enable",  rk312x_agcr_enum[14]),
	/* AGC disable and 0x0a bit 4 is 1 */
	SOC_SINGLE_TLV("PGAR AGC Volume", RK312x_PGAR_AGC_CTL4,
		       RK312x_PGA_AGC_VOL_SFT, 31, 0, pga_vol_tlv),

	SOC_SINGLE("PGAR AGC Max Level High 8 Bits", RK312x_PGAR_AGC_MAX_H,
		   0, 255, 0),
	SOC_SINGLE("PGAR AGC Max Level Low 8 Bits", RK312x_PGAR_AGC_MAX_L,
		   0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level High 8 Bits", RK312x_PGAR_AGC_MIN_H,
		   0, 255, 0),
	SOC_SINGLE("PGAR AGC Min Level Low 8 Bits", RK312x_PGAR_AGC_MIN_L,
		   0, 255, 0),
	/* AGC enable and 0x06 bit 4 is 1 */
	SOC_SINGLE_TLV("PGAR AGC Max Gain", RK312x_PGAR_AGC_CTL5,
		       RK312x_PGA_AGC_MAX_G_SFT, 7, 0, pga_agc_max_vol_tlv),
	/*  AGC enable and 0x06 bit 4 is 1 */
	SOC_SINGLE_TLV("PGAR AGC Min Gain", RK312x_PGAR_AGC_CTL5,
		       RK312x_PGA_AGC_MIN_G_SFT, 7, 0, pga_agc_min_vol_tlv),

};

/* For tiny alsa playback/capture/voice call path */
static const char *const rk312x_playback_path_mode[] = {
		"OFF", "RCV", "SPK", "HP", "HP_NO_MIC",
		"BT", "SPK_HP", "RING_SPK", "RING_HP",
		"RING_HP_NO_MIC", "RING_SPK_HP"};

static const char *const rk312x_capture_path_mode[] = {
		"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const char *const rk312x_voice_call_path_mode[] = {
		"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT"};


static SOC_ENUM_SINGLE_DECL(rk312x_playback_path_type, 0, 0,
			    rk312x_playback_path_mode);
static SOC_ENUM_SINGLE_DECL(rk312x_capture_path_type, 0, 0,
			    rk312x_capture_path_mode);
static SOC_ENUM_SINGLE_DECL(rk312x_voice_call_path_type, 0, 0,
			    rk312x_voice_call_path_mode);


/* static int rk312x_codec_power_up(int type); */
static int rk312x_codec_power_down(int type);

int rk312x_codec_mute_dac(int mute)
{
	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}
	if (mute) {
		snd_soc_component_write(rk312x_priv->component, 0xb4, 0x40);
		snd_soc_component_write(rk312x_priv->component, 0xb8, 0x40);
	}
	return 0;
}
EXPORT_SYMBOL(rk312x_codec_mute_dac);

static int rk312x_playback_path_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n",
	    __func__, ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk312x_priv->playback_path;

	return 0;
}

static int rk312x_playback_path_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	long int pre_path;

	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk312x_priv->playback_path ==
	    ucontrol->value.integer.value[0]) {
		DBG("%s : playback_path is not changed!\n", __func__);
		return 0;
	}

	pre_path = rk312x_priv->playback_path;
	rk312x_priv->playback_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld\n", __func__,
	    rk312x_priv->playback_path);

	switch (rk312x_priv->playback_path) {
	case OFF:
		if (pre_path != OFF)
			rk312x_codec_power_down(RK312x_CODEC_PLAYBACK);
		break;
	case RCV:
		break;
	case SPK_PATH:
	case RING_SPK:
		if (pre_path == OFF) {
			rk312x_codec_power_up(RK312x_CODEC_PLAYBACK);
			snd_soc_component_write(rk312x_priv->component,
						0xb4, rk312x_priv->spk_volume);
			snd_soc_component_write(rk312x_priv->component,
						0xb8, rk312x_priv->spk_volume);
		}
		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
		if (pre_path == OFF) {
			rk312x_codec_power_up(RK312x_CODEC_PLAYBACK);
			snd_soc_component_write(rk312x_priv->component,
						0xb4, rk312x_priv->hp_volume);
			snd_soc_component_write(rk312x_priv->component,
						0xb8, rk312x_priv->hp_volume);
		}
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
		if (pre_path == OFF) {
			rk312x_codec_power_up(RK312x_CODEC_PLAYBACK);
			snd_soc_component_write(rk312x_priv->component,
						0xb4, rk312x_priv->spk_volume);
			snd_soc_component_write(rk312x_priv->component,
						0xb8, rk312x_priv->spk_volume);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk312x_capture_path_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : capture_path = %ld\n", __func__,
	    ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk312x_priv->capture_path;

	return 0;
}

static int rk312x_capture_path_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	long int pre_path;

	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk312x_priv->capture_path == ucontrol->value.integer.value[0])
		DBG("%s : capture_path is not changed!\n", __func__);

	pre_path = rk312x_priv->capture_path;
	rk312x_priv->capture_path = ucontrol->value.integer.value[0];

	DBG("%s : set capture_path = %ld\n", __func__,
	    rk312x_priv->capture_path);

	switch (rk312x_priv->capture_path) {
	case MIC_OFF:
		if (pre_path != MIC_OFF)
			rk312x_codec_power_down(RK312x_CODEC_CAPTURE);
		break;
	case Main_Mic:
		if (pre_path == MIC_OFF) {
			rk312x_codec_power_up(RK312x_CODEC_CAPTURE);
			snd_soc_component_write(rk312x_priv->component, 0x10c,
						0x20 | rk312x_priv->capture_volume);
			snd_soc_component_write(rk312x_priv->component, 0x14c,
						0x20 | rk312x_priv->capture_volume);
		}
		break;
	case Hands_Free_Mic:
		if (pre_path == MIC_OFF) {
			rk312x_codec_power_up(RK312x_CODEC_CAPTURE);
			snd_soc_component_write(rk312x_priv->component,
						0x10c, 0x20 | rk312x_priv->capture_volume);
			snd_soc_component_write(rk312x_priv->component,
						0x14c, 0x20 | rk312x_priv->capture_volume);
		}
		break;
	case BT_Sco_Mic:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int rk312x_voice_call_path_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	DBG("%s : playback_path = %ld\n", __func__,
	    ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = rk312x_priv->voice_call_path;

	return 0;
}

static int rk312x_voice_call_path_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	long int pre_path;

	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (rk312x_priv->voice_call_path == ucontrol->value.integer.value[0])
		DBG("%s : playback_path is not changed!\n", __func__);

	pre_path = rk312x_priv->voice_call_path;
	rk312x_priv->voice_call_path = ucontrol->value.integer.value[0];

	DBG("%s : set playback_path = %ld\n", __func__,
	    rk312x_priv->voice_call_path);

	/* open playback route for incall route and keytone */
	if (pre_path == OFF) {
		if (rk312x_priv->playback_path != OFF) {
			/* mute output for incall route pop nosie */
				mdelay(100);
		} else {
			rk312x_codec_power_up(RK312x_CODEC_PLAYBACK);
			snd_soc_component_write(rk312x_priv->component,
						0xb4, rk312x_priv->spk_volume);
			snd_soc_component_write(rk312x_priv->component,
						0xb8, rk312x_priv->spk_volume);
		}
	}

	switch (rk312x_priv->voice_call_path) {
	case OFF:
		if (pre_path != MIC_OFF)
			rk312x_codec_power_down(RK312x_CODEC_CAPTURE);
		break;
	case RCV:
		break;
	case SPK_PATH:
		/* open incall route */
		if (pre_path == OFF ||	pre_path == RCV || pre_path == BT)
			rk312x_codec_power_up(RK312x_CODEC_INCALL);

		break;
	case HP_PATH:
	case HP_NO_MIC:
		/* open incall route */
		if (pre_path == OFF ||	pre_path == RCV || pre_path == BT)
			rk312x_codec_power_up(RK312x_CODEC_INCALL);
		break;
	case BT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new rk312x_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", rk312x_playback_path_type,
		     rk312x_playback_path_get, rk312x_playback_path_put),
	SOC_ENUM_EXT("Capture MIC Path", rk312x_capture_path_type,
		     rk312x_capture_path_get, rk312x_capture_path_put),
	SOC_ENUM_EXT("Voice Call Path", rk312x_voice_call_path_type,
		     rk312x_voice_call_path_get, rk312x_voice_call_path_put),
};

static int rk312x_dacl_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_WORK, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_EN | RK312x_DACL_CLK_EN,
					      RK312x_DACL_EN | RK312x_DACL_CLK_EN);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_WORK, RK312x_DACL_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_EN | RK312x_DACL_CLK_EN, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk312x_dacr_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACR_WORK, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACR_EN
					      | RK312x_DACR_CLK_EN,
					      RK312x_DACR_EN
					      | RK312x_DACR_CLK_EN);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACR_WORK,
					      RK312x_DACR_WORK);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACR_EN
					      | RK312x_DACR_CLK_EN, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACR_WORK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk312x_adcl_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
					      RK312x_ADCL_CLK_EN_SFT
					      | RK312x_ADCL_AMP_EN_SFT,
					      RK312x_ADCL_CLK_EN
					      | RK312x_ADCL_AMP_EN);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
					      RK312x_ADCL_CLK_EN_SFT
					      | RK312x_ADCL_AMP_EN_SFT, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk312x_adcr_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
					      RK312x_ADCR_CLK_EN_SFT
					      | RK312x_ADCR_AMP_EN_SFT,
					      RK312x_ADCR_CLK_EN
					      | RK312x_ADCR_AMP_EN);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
					      RK312x_ADCR_CLK_EN_SFT
					      | RK312x_ADCR_AMP_EN_SFT, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

/* HPmix */
static const struct snd_kcontrol_new rk312x_hpmixl[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXL_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXL_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXL_SEL_DACL_SFT, 1, 0),
};

static const struct snd_kcontrol_new rk312x_hpmixr[] = {
	SOC_DAPM_SINGLE("ALCR Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXR_SEL_ALCR_SFT, 1, 0),
	SOC_DAPM_SINGLE("ALCL Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXR_SEL_ALCL_SFT, 1, 0),
	SOC_DAPM_SINGLE("DACR Switch", RK312x_HPMIX_S_SELECT,
			RK312x_HPMIXR_SEL_DACR_SFT, 1, 0),
};

static int rk312x_hpmixl_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_ZO_DET_VOUTR_SFT,
					      RK312x_ZO_DET_VOUTR_EN);
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_ZO_DET_VOUTL_SFT,
					      RK312x_ZO_DET_VOUTL_EN);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_ZO_DET_VOUTR_SFT,
					      RK312x_ZO_DET_VOUTR_DIS);
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_ZO_DET_VOUTL_SFT,
					      RK312x_ZO_DET_VOUTL_DIS);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rk312x_hpmixr_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
#if 0
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RK312x_HPMIX_CTL,
					      RK312x_HPMIXR_WORK2, RK312x_HPMIXR_WORK2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RK312x_HPMIX_CTL,
					      RK312x_HPMIXR_WORK2, 0);
		break;

	default:
		return 0;
	}
#endif
	return 0;
}

/* HP MUX */

static const char *const hpl_sel[] = {"HPMIXL", "DACL"};

static const struct soc_enum hpl_sel_enum =
	SOC_ENUM_SINGLE(RK312x_HPMIX_S_SELECT, RK312x_HPMIXL_BYPASS_SFT,
			ARRAY_SIZE(hpl_sel), hpl_sel);

static const struct snd_kcontrol_new hpl_sel_mux =
	SOC_DAPM_ENUM("HPL select Mux", hpl_sel_enum);

static const char *const hpr_sel[] = {"HPMIXR", "DACR"};

static const struct soc_enum hpr_sel_enum =
	SOC_ENUM_SINGLE(RK312x_HPMIX_S_SELECT, RK312x_HPMIXR_BYPASS_SFT,
			ARRAY_SIZE(hpr_sel), hpr_sel);

static const struct snd_kcontrol_new hpr_sel_mux =
	SOC_DAPM_ENUM("HPR select Mux", hpr_sel_enum);

/* IN_L MUX */
static const char *const lnl_sel[] = {"NO", "BSTL", "LINEL", "NOUSE"};

static const struct soc_enum lnl_sel_enum =
	SOC_ENUM_SINGLE(RK312x_ALC_MUNIN_CTL, RK312x_MUXINL_F_SHT,
			ARRAY_SIZE(lnl_sel), lnl_sel);

static const struct snd_kcontrol_new lnl_sel_mux =
	SOC_DAPM_ENUM("MUXIN_L select", lnl_sel_enum);

/* IN_R MUX */
static const char *const lnr_sel[] = {"NO", "BSTR", "LINER", "NOUSE"};

static const struct soc_enum lnr_sel_enum =
	SOC_ENUM_SINGLE(RK312x_ALC_MUNIN_CTL, RK312x_MUXINR_F_SHT,
			ARRAY_SIZE(lnr_sel), lnr_sel);

static const struct snd_kcontrol_new lnr_sel_mux =
	SOC_DAPM_ENUM("MUXIN_R select", lnr_sel_enum);


static const struct snd_soc_dapm_widget rk312x_dapm_widgets[] = {
	/* microphone bias */
	SND_SOC_DAPM_MICBIAS("Mic Bias", RK312x_ADC_MIC_CTL,
			     RK312x_MICBIAS_VOL_ENABLE, 0),

	/* DACs */
	SND_SOC_DAPM_DAC_E("DACL", NULL, SND_SOC_NOPM,
			   0, 0, rk312x_dacl_event,
			   SND_SOC_DAPM_POST_PMD
			   | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_DAC_E("DACR", NULL, SND_SOC_NOPM,
			   0, 0, rk312x_dacr_event,
			   SND_SOC_DAPM_POST_PMD
			   | SND_SOC_DAPM_POST_PMU),

	/* ADCs */
	SND_SOC_DAPM_ADC_E("ADCL", NULL, SND_SOC_NOPM,
			   0, 0, rk312x_adcl_event,
			   SND_SOC_DAPM_POST_PMD
			   | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADCR", NULL, SND_SOC_NOPM,
			   0, 0, rk312x_adcr_event,
			   SND_SOC_DAPM_POST_PMD
			   | SND_SOC_DAPM_POST_PMU),

	/* PGA */
	SND_SOC_DAPM_PGA("BSTL", RK312x_BST_CTL,
			 RK312x_BSTL_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BSTR", RK312x_BST_CTL,
			 RK312x_BSTR_PWRD_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCL", RK312x_ALC_MUNIN_CTL,
			 RK312x_ALCL_PWR_SHT , 0, NULL, 0),
	SND_SOC_DAPM_PGA("ALCR", RK312x_ALC_MUNIN_CTL,
			 RK312x_ALCR_PWR_SHT , 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPL", RK312x_HPOUT_CTL,
			 RK312x_HPOUTL_PWR_SHT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR", RK312x_HPOUT_CTL,
			 RK312x_HPOUTR_PWR_SHT, 0, NULL, 0),

	/* MIXER */
	SND_SOC_DAPM_MIXER_E("HPMIXL", RK312x_HPMIX_CTL,
			     RK312x_HPMIXL_SFT, 0,
			     rk312x_hpmixl,
			     ARRAY_SIZE(rk312x_hpmixl),
			     rk312x_hpmixl_event,
			     SND_SOC_DAPM_PRE_PMD
			     | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("HPMIXR", RK312x_HPMIX_CTL,
			     RK312x_HPMIXR_SFT, 0,
			     rk312x_hpmixr,
			     ARRAY_SIZE(rk312x_hpmixr),
			     rk312x_hpmixr_event,
			     SND_SOC_DAPM_PRE_PMD
			     | SND_SOC_DAPM_POST_PMU),

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

static const struct snd_soc_dapm_route rk312x_dapm_routes[] = {
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

static int rk312x_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	DBG("%s  level=%d\n", __func__, level);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regmap_write(rk312x_priv->regmap, RK312x_DAC_INT_CTL3, 0x32);
			snd_soc_component_update_bits(component, RK312x_ADC_MIC_CTL,
						      RK312x_ADC_CURRENT_ENABLE,
						      RK312x_ADC_CURRENT_ENABLE);
			snd_soc_component_update_bits(component, RK312x_DAC_CTL,
						      RK312x_CURRENT_EN,
						      RK312x_CURRENT_EN);
			/* set power */
			snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
						      RK312x_ADCL_REF_VOL_EN_SFT
						      | RK312x_ADCR_REF_VOL_EN_SFT,
						      RK312x_ADCL_REF_VOL_EN
						      | RK312x_ADCR_REF_VOL_EN);

			snd_soc_component_update_bits(component, RK312x_ADC_MIC_CTL,
						      RK312x_ADCL_ZERO_DET_EN_SFT
						      | RK312x_ADCR_ZERO_DET_EN_SFT,
						      RK312x_ADCL_ZERO_DET_EN
						      | RK312x_ADCR_ZERO_DET_EN);

			snd_soc_component_update_bits(component, RK312x_DAC_CTL,
						      RK312x_REF_VOL_DACL_EN_SFT
						      | RK312x_REF_VOL_DACR_EN_SFT,
						      RK312x_REF_VOL_DACL_EN
						      | RK312x_REF_VOL_DACR_EN);

			snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
						      RK312x_DACL_REF_VOL_EN_SFT
						      | RK312x_DACR_REF_VOL_EN_SFT,
						      RK312x_DACL_REF_VOL_EN
						      | RK312x_DACR_REF_VOL_EN);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, RK312x_DAC_ENABLE,
					      RK312x_DACL_REF_VOL_EN_SFT
					      | RK312x_DACR_REF_VOL_EN_SFT, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_REF_VOL_DACL_EN_SFT
					      | RK312x_REF_VOL_DACR_EN_SFT, 0);
		snd_soc_component_update_bits(component, RK312x_ADC_MIC_CTL,
					      RK312x_ADCL_ZERO_DET_EN_SFT
					      | RK312x_ADCR_ZERO_DET_EN_SFT, 0);
		snd_soc_component_update_bits(component, RK312x_ADC_ENABLE,
					      RK312x_ADCL_REF_VOL_EN_SFT
					      | RK312x_ADCR_REF_VOL_EN_SFT, 0);
		snd_soc_component_update_bits(component, RK312x_ADC_MIC_CTL,
					      RK312x_ADC_CURRENT_ENABLE, 0);
		snd_soc_component_update_bits(component, RK312x_DAC_CTL,
					      RK312x_CURRENT_EN, 0);
		regmap_write(rk312x_priv->regmap, RK312x_DAC_INT_CTL3, 0x22);
		break;
	}

	return 0;
}

static int rk312x_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct rk312x_codec_priv *rk312x = rk312x_priv;

	if (!rk312x) {
		DBG("%s : rk312x is NULL\n", __func__);
		return -EINVAL;
	}

	rk312x->stereo_sysclk = freq;

	return 0;
}

static int rk312x_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	unsigned int adc_aif1 = 0, adc_aif2 = 0, dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		adc_aif2 |= RK312x_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		adc_aif2 |= RK312x_I2S_MODE_MST;
		break;
	default:
		DBG("%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		adc_aif1 |= RK312x_ADC_DF_PCM;
		dac_aif1 |= RK312x_DAC_DF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		adc_aif1 |= RK312x_ADC_DF_I2S;
		dac_aif1 |= RK312x_DAC_DF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adc_aif1 |= RK312x_ADC_DF_RJ;
		dac_aif1 |= RK312x_DAC_DF_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adc_aif1 |= RK312x_ADC_DF_LJ;
		dac_aif1 |= RK312x_DAC_DF_LJ;
		break;
	default:
		DBG("%s : set format failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		adc_aif1 |= RK312x_ALRCK_POL_DIS;
		adc_aif2 |= RK312x_ABCLK_POL_DIS;
		dac_aif1 |= RK312x_DLRCK_POL_DIS;
		dac_aif2 |= RK312x_DBCLK_POL_DIS;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		adc_aif1 |= RK312x_ALRCK_POL_EN;
		adc_aif2 |= RK312x_ABCLK_POL_EN;
		dac_aif1 |= RK312x_DLRCK_POL_EN;
		dac_aif2 |= RK312x_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		adc_aif1 |= RK312x_ALRCK_POL_DIS;
		adc_aif2 |= RK312x_ABCLK_POL_EN;
		dac_aif1 |= RK312x_DLRCK_POL_DIS;
		dac_aif2 |= RK312x_DBCLK_POL_EN;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		adc_aif1 |= RK312x_ALRCK_POL_EN;
		adc_aif2 |= RK312x_ABCLK_POL_DIS;
		dac_aif1 |= RK312x_DLRCK_POL_EN;
		dac_aif2 |= RK312x_DBCLK_POL_DIS;
		break;
	default:
		DBG("%s : set dai format failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RK312x_ADC_INT_CTL1,
				      RK312x_ALRCK_POL_MASK
				      | RK312x_ADC_DF_MASK, adc_aif1);
	snd_soc_component_update_bits(component, RK312x_ADC_INT_CTL2,
				      RK312x_ABCLK_POL_MASK
				      | RK312x_I2S_MODE_MASK, adc_aif2);
	snd_soc_component_update_bits(component, RK312x_DAC_INT_CTL1,
				      RK312x_DLRCK_POL_MASK
				      | RK312x_DAC_DF_MASK, dac_aif1);
	snd_soc_component_update_bits(component, RK312x_DAC_INT_CTL2,
				      RK312x_DBCLK_POL_MASK, dac_aif2);

	return 0;
}

static int rk312x_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct rk312x_codec_priv *rk312x = rk312x_priv;
	unsigned int rate = params_rate(params);
	unsigned int div;
	unsigned int adc_aif1 = 0, adc_aif2  = 0, dac_aif1 = 0, dac_aif2  = 0;

	if (!rk312x) {
		DBG("%s : rk312x is NULL\n", __func__);
		return -EINVAL;
	}

	/* bclk = codec_clk / 4 */
	/* lrck = bclk / (wl * 2) */
	div = (((rk312x->stereo_sysclk / 4) / rate) / 2);

	if ((rk312x->stereo_sysclk % (4 * rate * 2) > 0) ||
	    (div != 16 && div != 20 && div != 24 && div != 32)) {
		DBG("%s : need PLL\n", __func__);
		return -EINVAL;
	}

	switch (div) {
	case 16:
		adc_aif2 |= RK312x_ADC_WL_16;
		dac_aif2 |= RK312x_DAC_WL_16;
		break;
	case 20:
		adc_aif2 |= RK312x_ADC_WL_20;
		dac_aif2 |= RK312x_DAC_WL_20;
		break;
	case 24:
		adc_aif2 |= RK312x_ADC_WL_24;
		dac_aif2 |= RK312x_DAC_WL_24;
		break;
	case 32:
		adc_aif2 |= RK312x_ADC_WL_32;
		dac_aif2 |= RK312x_DAC_WL_32;
		break;
	default:
		return -EINVAL;
	}


	DBG("%s : MCLK = %dHz, sample rate = %dHz, div = %d\n",
	    __func__, rk312x->stereo_sysclk, rate, div);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adc_aif1 |= RK312x_ADC_VWL_16;
		dac_aif1 |= RK312x_DAC_VWL_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adc_aif1 |= RK312x_ADC_VWL_20;
		dac_aif1 |= RK312x_DAC_VWL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		adc_aif1 |= RK312x_ADC_VWL_24;
		dac_aif1 |= RK312x_DAC_VWL_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adc_aif1 |= RK312x_ADC_VWL_32;
		dac_aif1 |= RK312x_DAC_VWL_32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case RK312x_MONO:
		adc_aif1 |= RK312x_ADC_TYPE_MONO;
		DBG("mono\n");
		break;
	case RK312x_STEREO:
		adc_aif1 |= RK312x_ADC_TYPE_STEREO;
		DBG("stero\n");
		break;
	default:
		return -EINVAL;
	}

	adc_aif1 |= RK312x_ADC_SWAP_DIS;
	adc_aif2 |= RK312x_ADC_RST_DIS;
	dac_aif1 |= RK312x_DAC_SWAP_DIS;
	dac_aif2 |= RK312x_DAC_RST_DIS;

	rk312x->rate = rate;

	snd_soc_component_update_bits(component, RK312x_ADC_INT_CTL1,
				      RK312x_ADC_VWL_MASK
				      | RK312x_ADC_SWAP_MASK
				      | RK312x_ADC_TYPE_MASK, adc_aif1);
	snd_soc_component_update_bits(component, RK312x_ADC_INT_CTL2,
				      RK312x_ADC_WL_MASK
				      | RK312x_ADC_RST_MASK, adc_aif2);
	snd_soc_component_update_bits(component, RK312x_DAC_INT_CTL1,
				      RK312x_DAC_VWL_MASK
				      | RK312x_DAC_SWAP_MASK, dac_aif1);
	snd_soc_component_update_bits(component, RK312x_DAC_INT_CTL2,
				      RK312x_DAC_WL_MASK
				      | RK312x_DAC_RST_MASK, dac_aif2);

	return 0;
}

static void rk312x_codec_unpop(struct work_struct *work)
{
	rk312x_codec_ctl_gpio(CODEC_SET_SPK, 1);
}

static int rk312x_digital_mute(struct snd_soc_dai *dai, int mute)
{

	if (mute) {
		rk312x_codec_ctl_gpio(CODEC_SET_SPK, 0);
		rk312x_codec_ctl_gpio(CODEC_SET_HP, 0);
	} else {
		if (!rk312x_priv->rk312x_for_mid) {
			schedule_delayed_work(&rk312x_priv->mute_delayed_work,
					      msecs_to_jiffies(rk312x_priv->spk_mute_delay));
		} else {
			switch (rk312x_priv->playback_path) {
			case SPK_PATH:
			case RING_SPK:
				rk312x_codec_ctl_gpio(CODEC_SET_SPK, 1);
				rk312x_codec_ctl_gpio(CODEC_SET_HP, 0);
				break;
			case HP_PATH:
			case HP_NO_MIC:
			case RING_HP:
			case RING_HP_NO_MIC:
				rk312x_codec_ctl_gpio(CODEC_SET_SPK, 0);
				rk312x_codec_ctl_gpio(CODEC_SET_HP, 1);
				break;
			case SPK_HP:
			case RING_SPK_HP:
				rk312x_codec_ctl_gpio(CODEC_SET_SPK, 1);
				rk312x_codec_ctl_gpio(CODEC_SET_HP, 1);
				break;
			default:
				break;
			}
		}
	}
	return 0;
}

static struct rk312x_reg_val_typ playback_power_up_list[] = {
	{0x18, 0x32},
	{0xa0, 0x40|0x08},
	{0xa0, 0x62|0x08},

	{0xb4, 0x80},
	{0xb8, 0x80},
	{0xa8, 0x44},
	{0xa8, 0x55},

	{0xb0, 0x90},
	{0xb0, 0xd8},

	{0xa4, 0x88},
	{0xa4, 0xcc},
	{0xa4, 0xee},
	{0xa4, 0xff},

	{0xac, 0x11}, /*DAC*/
	{0xa8, 0x77},
	{0xb0, 0xfc},

	{0xb4, OUT_VOLUME},
	{0xb8, OUT_VOLUME},
	{0xb0, 0xff},
	{0xa0, 0x73|0x08},
};
#define RK312x_CODEC_PLAYBACK_POWER_UP_LIST_LEN ARRAY_SIZE( \
					playback_power_up_list)

static struct rk312x_reg_val_typ playback_power_down_list[] = {
	{0xb0, 0xdb},
	{0xa8, 0x44},
	{0xac, 0x00},
	{0xb0, 0x92},
	{0xa0, 0x22|0x08},
	{0xb0, 0x00},
	{0xa8, 0x00},
	{0xa4, 0x00},
	{0xa0, 0x00|0x08},
	{0x18, 0x22},
#ifdef WITH_CAP
	/* {0xbc, 0x08},*/
#endif
	{0xb4, 0x0},
	{0xb8, 0x0},
	{0x18, 0x22},
};
#define RK312x_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN ARRAY_SIZE( \
				playback_power_down_list)

static struct rk312x_reg_val_typ capture_power_up_list[] = {
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
	{0x28, 0x3c},
	/* {0x124, 0x78}, */
	/* {0x164, 0x78}, */
	{0x10c, 0x20 | CAP_VOL},
	{0x14c, 0x20 | CAP_VOL},
	/*close left channel*/
	{0x90, 0x07},
	{0x88, 0xd7},
	{0x8c, 0x07},
	{0x9c, 0x0e},

};
#define RK312x_CODEC_CAPTURE_POWER_UP_LIST_LEN ARRAY_SIZE(capture_power_up_list)

static struct rk312x_reg_val_typ capture_power_down_list[] = {
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
	{0x28, 0x0c},
	{0x10c, 0x2c},
	{0x14c, 0x2c},
	/* {0x124, 0x38}, */
	/* {0x164, 0x38}, */
};
#define RK312x_CODEC_CAPTURE_POWER_DOWN_LIST_LEN ARRAY_SIZE(\
				capture_power_down_list)

static int rk312x_codec_power_up(int type)
{
	struct snd_soc_component *component;
	int i;

	if (!rk312x_priv || !rk312x_priv->component) {
		DBG("%s : rk312x_priv or rk312x_priv->codec is NULL\n",
		    __func__);
		return -EINVAL;
	}
	component = rk312x_priv->component;

	DBG("%s : power up %s%s\n", __func__,
	    type == RK312x_CODEC_PLAYBACK ? "playback" : "",
	    type == RK312x_CODEC_CAPTURE ? "capture" : "");

	if (type == RK312x_CODEC_PLAYBACK) {
		for (i = 0; i < RK312x_CODEC_PLAYBACK_POWER_UP_LIST_LEN; i++) {
			snd_soc_component_write(component,
						playback_power_up_list[i].reg,
						playback_power_up_list[i].value);
			usleep_range(1000, 1100);
		}
	} else if (type == RK312x_CODEC_CAPTURE) {
		if (rk312x_priv->rk312x_for_mid == 1) {
			for (i = 0;
			     i < RK312x_CODEC_CAPTURE_POWER_UP_LIST_LEN;
			     i++) {
				snd_soc_component_write(component,
							capture_power_up_list[i].reg,
							capture_power_up_list[i].value);
			}
		} else {
			for (i = 0;
			     i < RK312x_CODEC_CAPTURE_POWER_UP_LIST_LEN - 4;
			     i++) {
				snd_soc_component_write(component,
							capture_power_up_list[i].reg,
							capture_power_up_list[i].value);
			}
		}
	} else if (type == RK312x_CODEC_INCALL) {
		snd_soc_component_update_bits(component, RK312x_ALC_MUNIN_CTL,
					      RK312x_MUXINL_F_MSK | RK312x_MUXINR_F_MSK,
					      RK312x_MUXINR_F_INR | RK312x_MUXINL_F_INL);
	}

	return 0;
}

static int rk312x_codec_power_down(int type)
{
	struct snd_soc_component *component;
	int i;

	if (!rk312x_priv || !rk312x_priv->component) {
		DBG("%s : rk312x_priv or rk312x_priv->component is NULL\n",
		    __func__);
		return -EINVAL;
	}
	component = rk312x_priv->component;

	DBG("%s : power down %s%s%s\n", __func__,
	    type == RK312x_CODEC_PLAYBACK ? "playback" : "",
	    type == RK312x_CODEC_CAPTURE ? "capture" : "",
	    type == RK312x_CODEC_ALL ? "all" : "");

	if ((type == RK312x_CODEC_CAPTURE) || (type == RK312x_CODEC_INCALL)) {
		for (i = 0; i < RK312x_CODEC_CAPTURE_POWER_DOWN_LIST_LEN; i++) {
			snd_soc_component_write(component,
						capture_power_down_list[i].reg,
						capture_power_down_list[i].value);
		}
	} else if (type == RK312x_CODEC_PLAYBACK) {
		for (i = 0;
		     i < RK312x_CODEC_PLAYBACK_POWER_DOWN_LIST_LEN;
		     i++) {
			snd_soc_component_write(component,
						playback_power_down_list[i].reg,
						playback_power_down_list[i].value);
		}

	} else if (type == RK312x_CODEC_ALL) {
		rk312x_reset(component);
	}

	return 0;
}

static void  rk312x_codec_capture_work(struct work_struct *work)
{
	DBG("%s : rk312x_codec_work_capture_type = %d\n", __func__,
	    rk312x_codec_work_capture_type);

	switch (rk312x_codec_work_capture_type) {
	case RK312x_CODEC_WORK_POWER_DOWN:
		rk312x_codec_power_down(RK312x_CODEC_CAPTURE);
		break;
	case RK312x_CODEC_WORK_POWER_UP:
		rk312x_codec_power_up(RK312x_CODEC_CAPTURE);
		snd_soc_component_write(rk312x_priv->component,
					0x10c, 0x20 | rk312x_priv->capture_volume);
		snd_soc_component_write(rk312x_priv->component,
					0x14c, 0x20 | rk312x_priv->capture_volume);
		break;
	default:
		break;
	}

	rk312x_codec_work_capture_type = RK312x_CODEC_WORK_NULL;
}

static int rk312x_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct rk312x_codec_priv *rk312x = rk312x_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running;
	bool is_codec_capture_running;

	if (rk312x_priv->rk312x_for_mid) {
		return 0;
	}
	if (!rk312x) {
		DBG("%s : rk312x is NULL\n", __func__);
		return -EINVAL;
	}
	is_codec_playback_running = rk312x->playback_active > 0;
	is_codec_capture_running = rk312x->capture_active > 0;

	if (playback)
		rk312x->playback_active++;
	else
		rk312x->capture_active++;

	if (playback) {
		if (rk312x->playback_active > 0)
			if (!is_codec_playback_running) {
				rk312x_codec_power_up(RK312x_CODEC_PLAYBACK);
				snd_soc_component_write(rk312x_priv->component,
							0xb4, rk312x_priv->spk_volume);
				snd_soc_component_write(rk312x_priv->component,
							0xb8, rk312x_priv->spk_volume);
			}
	} else {
		if (rk312x->capture_active > 0 && !is_codec_capture_running) {
			if (rk312x_codec_work_capture_type != RK312x_CODEC_WORK_POWER_UP) {
				//cancel_delayed_work_sync(&capture_delayed_work);
				if (rk312x_codec_work_capture_type == RK312x_CODEC_WORK_NULL)
					rk312x_codec_power_up(RK312x_CODEC_CAPTURE);
				else
					rk312x_codec_work_capture_type = RK312x_CODEC_WORK_NULL;
			}
		}
	}

	return 0;
}

static void rk312x_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct rk312x_codec_priv *rk312x = rk312x_priv;
	bool playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_codec_playback_running;
	bool is_codec_capture_running;

	if (rk312x_priv->rk312x_for_mid) {
		return;
	}

	if (!rk312x) {
		DBG("%s : rk312x is NULL\n", __func__);
		return;
	}
	is_codec_playback_running = rk312x->playback_active > 0;
	is_codec_capture_running = rk312x->capture_active > 0;

	if (playback)
		rk312x->playback_active--;
	else
		rk312x->capture_active--;

	if (playback) {
		if (rk312x->playback_active <= 0) {
			if (is_codec_playback_running)
				rk312x_codec_power_down(
					RK312x_CODEC_PLAYBACK);
			else
				DBG(" Warning:playback closed! return !\n");
		}
	} else {
		if (rk312x->capture_active <= 0) {
			if ((rk312x_codec_work_capture_type !=
			     RK312x_CODEC_WORK_POWER_DOWN) &&
			    is_codec_capture_running) {
				cancel_delayed_work_sync(&capture_delayed_work);
			/*
			 * If rk312x_codec_work_capture_type is NULL
			 * means codec already power down,
			 * so power up codec.
			 * If rk312x_codec_work_capture_type is
			 * RK312x_CODEC_WORK_POWER_UP it means
			 * codec haven't be powered up, so we don't
			 * need to power down codec.
			 * If is playback call power down,
			 * power down immediatly, because audioflinger
			 * already has delay 3s.
			 */
				if (rk312x_codec_work_capture_type ==
				    RK312x_CODEC_WORK_NULL) {
					rk312x_codec_work_capture_type =
						RK312x_CODEC_WORK_POWER_DOWN;
					queue_delayed_work(rk312x_codec_workq,
							&capture_delayed_work,
							msecs_to_jiffies(3000));
				} else {
					rk312x_codec_work_capture_type =
							RK312x_CODEC_WORK_NULL;
				}
			}
		}
	}
}

#define RK312x_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000 |	\
			      SNDRV_PCM_RATE_192000)

#define RK312x_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK312x_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk312x_dai_ops = {
	.hw_params	= rk312x_hw_params,
	.set_fmt	= rk312x_set_dai_fmt,
	.set_sysclk	= rk312x_set_dai_sysclk,
	.digital_mute	= rk312x_digital_mute,
	.startup	= rk312x_startup,
	.shutdown	= rk312x_shutdown,
};

static struct snd_soc_dai_driver rk312x_dai[] = {
	{
		.name = "rk312x-hifi",
		.id = RK312x_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK312x_PLAYBACK_RATES,
			.formats = RK312x_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK312x_CAPTURE_RATES,
			.formats = RK312x_FORMATS,
		},
		.ops = &rk312x_dai_ops,
	},
	{
		.name = "rk312x-voice",
		.id = RK312x_VOICE,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK312x_PLAYBACK_RATES,
			.formats = RK312x_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK312x_CAPTURE_RATES,
			.formats = RK312x_FORMATS,
		},
		.ops = &rk312x_dai_ops,
	},

};

static int rk312x_suspend(struct snd_soc_component *component)
{
	unsigned int val=0;
	DBG("%s\n", __func__);
	if (rk312x_priv->codec_hp_det) {
        /* disable hp det interrupt */
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, 0x1f0013);
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		cancel_delayed_work_sync(&rk312x_priv->hpdet_work);

	}
	if (rk312x_priv->rk312x_for_mid) {
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk312x_codec_work_capture_type != RK312x_CODEC_WORK_NULL)
			rk312x_codec_work_capture_type = RK312x_CODEC_WORK_NULL;

		rk312x_codec_power_down(RK312x_CODEC_PLAYBACK);
		rk312x_codec_power_down(RK312x_CODEC_ALL);
		snd_soc_component_write(component, RK312x_SELECT_CURRENT, 0x1e);
		snd_soc_component_write(component, RK312x_SELECT_CURRENT, 0x3e);
	} else {
		snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);
	}
	return 0;
}

static ssize_t gpio_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return 0;
}

static ssize_t gpio_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	const char *buftmp = buf;
	char cmd;
	int ret;
	struct rk312x_codec_priv *rk312x =
			snd_soc_component_get_drvdata(rk312x_priv->component);

	ret = sscanf(buftmp, "%c ", &cmd);
	if (ret == 0)
		return ret;
	switch (cmd) {
	case 'd':
		if (rk312x->spk_ctl_gpio) {
			gpiod_set_value(rk312x->spk_ctl_gpio, 0);
			DBG(KERN_INFO"%s : spk gpio disable\n",__func__);
		}

		if (rk312x->hp_ctl_gpio) {
			gpiod_set_value(rk312x->hp_ctl_gpio, 0);
			DBG(KERN_INFO"%s : disable hp gpio \n",__func__);
		}
		break;
	case 'e':
		if (rk312x->spk_ctl_gpio) {
			gpiod_set_value(rk312x->spk_ctl_gpio, 1);
			DBG(KERN_INFO"%s : spk gpio enable\n",__func__);
		}

		if (rk312x->hp_ctl_gpio) {
		gpiod_set_value(rk312x->hp_ctl_gpio, 1);
		DBG("%s : enable hp gpio \n",__func__);
	}
		break;
	default:
		DBG(KERN_ERR"--rk312x codec %s-- unknown cmd\n", __func__);
		break;
	}
	return n;
}
static struct kobject *gpio_kobj;
struct gpio_attribute {

	struct attribute    attr;

	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n);
};

static struct gpio_attribute gpio_attrs[] = {
	/*     node_name    permision       show_func   store_func */
	__ATTR(spk-ctl,  S_IRUGO | S_IWUSR,  gpio_show, gpio_store),
};

static int rk312x_resume(struct snd_soc_component *component)
{
	unsigned int val = 0;

	if (rk312x_priv->codec_hp_det) {
		/* enable hp det interrupt */
		snd_soc_component_write(component, RK312x_DAC_CTL, 0x08);
		snd_soc_component_read(component, RK312x_DAC_CTL, &val);
		printk("0xa0 -- 0x%x\n", val);
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, 0x1f001f);
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		printk("GRF_ACODEC_CON is 0x%x\n", val);
		schedule_delayed_work(&rk312x_priv->hpdet_work, msecs_to_jiffies(20));
	}
	if (!rk312x_priv->rk312x_for_mid)
		snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);

	return 0;
}

static irqreturn_t codec_hp_det_isr(int irq, void *data)
{
	unsigned int val = 0;
	regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
	DBG("%s GRF_ACODEC_CON -- 0x%x\n", __func__, val);
	if (val & 0x1) {
		DBG("%s hp det rising\n", __func__);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, val | 0x10001);
	} else if (val & 0x2) {
		DBG("%s hp det falling\n", __func__);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, val | 0x20002);
	}
	cancel_delayed_work(&rk312x_priv->hpdet_work);
	schedule_delayed_work(&rk312x_priv->hpdet_work, msecs_to_jiffies(20));
	return IRQ_HANDLED;
}
static void hpdet_work_func(struct work_struct *work)
{
	unsigned int val = 0;

	regmap_read(rk312x_priv->grf, GRF_SOC_STATUS0, &val);
	DBG("%s GRF_SOC_STATUS0 -- 0x%x\n", __func__, val);
	if (val & 0x80000000) {
		DBG("%s hp det high\n", __func__);
		DBG("%s no headset\n", __func__);
		extcon_set_state_sync(rk312x_priv->edev,
				      EXTCON_JACK_HEADPHONE, false);
	} else {
		DBG("%s hp det low\n", __func__);
		DBG("%s headset inserted\n", __func__);
		extcon_set_state_sync(rk312x_priv->edev,
				      EXTCON_JACK_HEADPHONE, true);
	}
	return;
}

static void rk312x_delay_workq(struct work_struct *work)
{

	int ret;
	unsigned int val;
	struct rk312x_codec_priv *rk312x_codec;
	struct snd_soc_component *component;

	printk("%s\n", __func__);
	if (!rk312x_priv || !rk312x_priv->component) {
		DBG("%s : rk312x_priv or rk312x_priv->component is NULL\n",
		    __func__);
		return;
	}
	rk312x_codec = snd_soc_component_get_drvdata(rk312x_priv->component);
	component = rk312x_codec->component;
	rk312x_reset(component);
	if (!rk312x_priv->rk312x_for_mid) {
		snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);
		snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);
	}
#ifdef WITH_CAP
	snd_soc_component_write(component, RK312x_SELECT_CURRENT, 0x1e);
	snd_soc_component_write(component, RK312x_SELECT_CURRENT, 0x3e);
#endif

	if (rk312x_codec->codec_hp_det) {
		/*init codec_hp_det interrupt only for rk3128 */
		ret = devm_request_irq(rk312x_priv->dev, rk312x_priv->irq, codec_hp_det_isr,
				       IRQF_TRIGGER_RISING, "codec_hp_det", NULL);
		if (ret < 0)
			DBG(" codec_hp_det request_irq failed %d\n", ret);

		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, 0x1f001f);
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		DBG("GRF_ACODEC_CON 3334is 0x%x\n", val);
		/* enable rk 3128 codec_hp_det */
		snd_soc_component_write(component, RK312x_DAC_CTL, 0x08);
		snd_soc_component_read(component, RK312x_DAC_CTL, &val);
		DBG("0xa0 -- 0x%x\n", val);
		/* codec hp det once */
		schedule_delayed_work(&rk312x_priv->hpdet_work, msecs_to_jiffies(100));
	}


}
static int rk312x_probe(struct snd_soc_component *component)
{
	struct rk312x_codec_priv *rk312x_codec =
				snd_soc_component_get_drvdata(component);
	unsigned int val;
	int ret;
	int i = 0;

	rk312x_codec->component = component;
	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);
	clk_prepare_enable(rk312x_codec->pclk);

	rk312x_codec->playback_active = 0;
	rk312x_codec->capture_active = 0;

	rk312x_codec_workq = create_freezable_workqueue("rk312x-codec");

	if (rk312x_codec_workq == NULL) {
		DBG("%s : rk312x_codec_workq is NULL!\n", __func__);
		ret = -ENOMEM;
		goto err__;
	}

	snd_soc_component_read(component, RK312x_RESET, &val);

	if (val != rk312x_reg_defaults[RK312x_RESET]) {
		DBG("%s : codec register 0: %x is not a 0x00000003\n",
		    __func__, val);
		ret = -ENODEV;
		goto err__;
	}

	snd_soc_add_component_controls(component, rk312x_snd_path_controls,
				       ARRAY_SIZE(rk312x_snd_path_controls));
	INIT_DELAYED_WORK(&rk312x_priv->init_delayed_work, rk312x_delay_workq);
	INIT_DELAYED_WORK(&rk312x_priv->mute_delayed_work, rk312x_codec_unpop);
	INIT_DELAYED_WORK(&rk312x_priv->hpdet_work, hpdet_work_func);

	schedule_delayed_work(&rk312x_priv->init_delayed_work, msecs_to_jiffies(3000));
	if (rk312x_codec->gpio_debug) {
		gpio_kobj = kobject_create_and_add("codec-spk-ctl", NULL);

		if (!gpio_kobj)
			return -ENOMEM;
		for (i = 0; i < ARRAY_SIZE(gpio_attrs); i++) {
			ret = sysfs_create_file(gpio_kobj, &gpio_attrs[i].attr);
			if (ret != 0) {
				DBG(KERN_ERR"create codec-spk-ctl sysfs %d error\n", i);
				/* return ret; */
			}
		}
	}
	return 0;

err__:
	dbg_codec(2, "%s err ret=%d\n", __func__, ret);
	return ret;
}

/* power down chip */
static void rk312x_remove(struct snd_soc_component *component)
{

	DBG("%s\n", __func__);
	if (!rk312x_priv) {
		DBG("%s : rk312x_priv is NULL\n", __func__);
		return;
	}

	if (rk312x_priv->spk_ctl_gpio)
		gpiod_set_value(rk312x_priv->spk_ctl_gpio, 0);

	if (rk312x_priv->hp_ctl_gpio)
		gpiod_set_value(rk312x_priv->hp_ctl_gpio, 0);

	mdelay(10);

	if (rk312x_priv->rk312x_for_mid) {
		cancel_delayed_work_sync(&capture_delayed_work);

		if (rk312x_codec_work_capture_type != RK312x_CODEC_WORK_NULL)
			rk312x_codec_work_capture_type = RK312x_CODEC_WORK_NULL;
	}
	snd_soc_component_write(component, RK312x_RESET, 0xfc);
	mdelay(10);
	snd_soc_component_write(component, RK312x_RESET, 0x3);
	mdelay(10);
}


static struct snd_soc_component_driver soc_codec_dev_rk312x = {
	.probe = rk312x_probe,
	.remove = rk312x_remove,
	.suspend = rk312x_suspend,
	.resume = rk312x_resume,
	.set_bias_level = rk312x_set_bias_level,
};

static const struct regmap_config rk312x_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = RK312x_PGAR_AGC_CTL5,
	.writeable_reg = rk312x_codec_register,
	.readable_reg = rk312x_codec_register,
	.volatile_reg = rk312x_volatile_register,
};

#define GRF_SOC_CON0		0x00140
#define GRF_ACODEC_SEL		(BIT(10) | BIT(16 + 10))

static int rk312x_platform_probe(struct platform_device *pdev)
{
	struct device_node *rk312x_np = pdev->dev.of_node;
	struct rk312x_codec_priv *rk312x;
	struct resource *res;
	int ret;

	rk312x = devm_kzalloc(&pdev->dev, sizeof(*rk312x), GFP_KERNEL);
	if (!rk312x) {
		dbg_codec(2, "%s : rk312x priv kzalloc failed!\n",
			  __func__);
		return -ENOMEM;
	}
	rk312x_priv = rk312x;
	platform_set_drvdata(pdev, rk312x);
	rk312x->dev = &pdev->dev;

#if 0
	rk312x->spk_hp_switch_gpio = of_get_named_gpio_flags(rk312x_np,
						 "spk_hp_switch_gpio", 0, &rk312x->spk_io);
	rk312x->spk_io = !rk312x->spk_io;
	if (!gpio_is_valid(rk312x->spk_hp_switch_gpio)) {
		dbg_codec(2, "invalid spk hp switch_gpio : %d\n",
			  rk312x->spk_hp_switch_gpio);
		rk312x->spk_hp_switch_gpio = INVALID_GPIO;
		/* ret = -ENOENT; */
		/* goto err__; */
	}
	DBG("%s : spk_hp_switch_gpio %d spk  active_level %d \n", __func__,
		rk312x->spk_hp_switch_gpio, rk312x->spk_io);

	if(rk312x->spk_hp_switch_gpio != INVALID_GPIO) {
		ret = devm_gpio_request(&pdev->dev, rk312x->spk_hp_switch_gpio, "spk_hp_switch");
		if (ret < 0) {
			dbg_codec(2, "rk312x_platform_probe spk_hp_switch_gpio fail\n");
			/* goto err__; */
			rk312x->spk_hp_switch_gpio = INVALID_GPIO;
		}
	}
#endif
	rk312x->edev = devm_extcon_dev_allocate(&pdev->dev, headset_extcon_cable);
	if (IS_ERR(rk312x->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	ret = devm_extcon_dev_register(&pdev->dev, rk312x->edev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		return ret;
	}

	rk312x->hp_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "hp-ctl",
						  GPIOD_OUT_LOW);
	if (!IS_ERR_OR_NULL(rk312x->hp_ctl_gpio)) {
		DBG("%s : hp-ctl-gpio %d\n", __func__,
		    desc_to_gpio(rk312x->hp_ctl_gpio));
	}

	rk312x->spk_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "spk-ctl",
						  GPIOD_OUT_LOW);
	if (!IS_ERR_OR_NULL(rk312x->spk_ctl_gpio)) {
		DBG(KERN_INFO "%s : spk-ctl-gpio %d\n", __func__,
		    desc_to_gpio(rk312x->spk_ctl_gpio));
	}

	ret = of_property_read_u32(rk312x_np, "spk-mute-delay",
				   &rk312x->spk_mute_delay);
	if (ret < 0) {
		DBG(KERN_ERR "%s() Can not read property spk-mute-delay\n",
			__func__);
		rk312x->spk_mute_delay = 0;
	}

	ret = of_property_read_u32(rk312x_np, "hp-mute-delay",
				   &rk312x->hp_mute_delay);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property hp-mute-delay\n",
		       __func__);
		rk312x->hp_mute_delay = 0;
	}
	DBG("spk mute delay %dms --- hp mute delay %dms\n",rk312x->spk_mute_delay,rk312x->hp_mute_delay);

	ret = of_property_read_u32(rk312x_np, "rk312x_for_mid",
				   &rk312x->rk312x_for_mid);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property rk312x_for_mid, default  for mid\n",
			__func__);
		rk312x->rk312x_for_mid = 1;
	}
	ret = of_property_read_u32(rk312x_np, "is_rk3128",
				   &rk312x->is_rk3128);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property is_rk3128, default rk3126\n",
			__func__);
		rk312x->is_rk3128 = 0;
	}
	ret = of_property_read_u32(rk312x_np, "spk_volume",
				   &rk312x->spk_volume);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property spk_volume, default 25\n",
			__func__);
		rk312x->spk_volume = 25;
	}
	ret = of_property_read_u32(rk312x_np, "hp_volume",
				   &rk312x->hp_volume);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property hp_volume, default 25\n",
		       __func__);
		rk312x->hp_volume = 25;
	}
	ret = of_property_read_u32(rk312x_np, "capture_volume",
        &rk312x->capture_volume);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property capture_volume, default 26\n",
			__func__);
		rk312x->capture_volume = 26;
	}
	ret = of_property_read_u32(rk312x_np, "gpio_debug", &rk312x->gpio_debug);
	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property gpio_debug\n", __func__);
		rk312x->gpio_debug = 0;
	}
	ret = of_property_read_u32(rk312x_np, "codec_hp_det", &rk312x->codec_hp_det);

	if (ret < 0) {
		DBG(KERN_ERR"%s() Can not read property gpio_debug\n", __func__);
		rk312x->codec_hp_det = 0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk312x->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rk312x->regbase))
		return PTR_ERR(rk312x->regbase);
	rk312x->regmap = devm_regmap_init_mmio(&pdev->dev, rk312x->regbase,
					       &rk312x_codec_regmap_config);
	if (IS_ERR(rk312x->regmap))
		return PTR_ERR(rk312x->regmap);

	rk312x->grf = syscon_regmap_lookup_by_phandle(rk312x_np, "rockchip,grf");
	if (IS_ERR(rk312x->grf)) {
		dev_err(&pdev->dev, "needs 'rockchip,grf' property\n");
		return PTR_ERR(rk312x->grf);
	}
	ret = regmap_write(rk312x->grf, GRF_SOC_CON0, GRF_ACODEC_SEL);
	if (ret) {
		dev_err(&pdev->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	if (rk312x->codec_hp_det)
		rk312x->irq = platform_get_irq(pdev, 0);

	rk312x->pclk = devm_clk_get(&pdev->dev, "g_pclk_acodec");
	if (IS_ERR(rk312x->pclk)) {
		dev_err(&pdev->dev, "Unable to get acodec pclk\n");
		ret = -ENXIO;
		goto err__;
	}
	rk312x->mclk = devm_clk_get(&pdev->dev, "i2s_clk");
	if (IS_ERR(rk312x->mclk)) {
		dev_err(&pdev->dev, "Unable to get mclk\n");
		ret = -ENXIO;
		goto err__;
	}

	clk_prepare_enable(rk312x->mclk);
	clk_set_rate(rk312x->mclk, 11289600);

	return devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rk312x,
					       rk312x_dai, ARRAY_SIZE(rk312x_dai));

err__:
	platform_set_drvdata(pdev, NULL);
	rk312x_priv = NULL;
	return ret;
}

static int rk312x_platform_remove(struct platform_device *pdev)
{
	DBG("%s\n", __func__);
	rk312x_priv = NULL;
	return 0;
}

void rk312x_platform_shutdown(struct platform_device *pdev)
{
	unsigned int val = 0;
	DBG("%s\n", __func__);
	if (!rk312x_priv || !rk312x_priv->component) {
		DBG("%s : rk312x_priv or rk312x_priv->component is NULL\n",
		    __func__);
		return;
	}
	if (rk312x_priv->codec_hp_det) {
		/* disable hp det interrupt */
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		regmap_write(rk312x_priv->grf, GRF_ACODEC_CON, 0x1f0013);
		regmap_read(rk312x_priv->grf, GRF_ACODEC_CON, &val);
		DBG("disable codec_hp_det GRF_ACODEC_CON is 0x%x\n", val);
		cancel_delayed_work_sync(&rk312x_priv->hpdet_work);
	}

	if (rk312x_priv->spk_ctl_gpio)
		gpiod_set_value(rk312x_priv->spk_ctl_gpio, 0);

	if (rk312x_priv->hp_ctl_gpio)
		gpiod_set_value(rk312x_priv->hp_ctl_gpio, 0);

	mdelay(10);

	if (rk312x_priv->rk312x_for_mid) {
		cancel_delayed_work_sync(&capture_delayed_work);
		if (rk312x_codec_work_capture_type !=
					RK312x_CODEC_WORK_NULL)
			rk312x_codec_work_capture_type =
					RK312x_CODEC_WORK_NULL;
	}

	regmap_write(rk312x_priv->regmap, RK312x_RESET, 0xfc);
	mdelay(10);
	regmap_write(rk312x_priv->regmap, RK312x_RESET, 0x03);

}

#ifdef CONFIG_OF
static const struct of_device_id rk312x_codec_of_match[] = {
	{ .compatible = "rockchip,rk3128-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, rk312x_codec_of_match);
#endif

static struct platform_driver rk312x_codec_driver = {
	.driver = {
		   .name = "rk312x-codec",
		   .of_match_table = of_match_ptr(rk312x_codec_of_match),
		   },
	.probe = rk312x_platform_probe,
	.remove = rk312x_platform_remove,
	.shutdown = rk312x_platform_shutdown,
};
module_platform_driver(rk312x_codec_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
