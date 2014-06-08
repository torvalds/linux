/*
 * Codec driver for ST STA32x 2.1-channel high-efficiency digital audio system
 *
 * Copyright: 2011 Raumfeld GmbH
 * Author: Johannes Stezenbach <js@sig21.net>
 *
 * based on code from:
 *	Wolfson Microelectronics PLC.
 *	  Mark Brown <broonie@opensource.wolfsonmicro.com>
 *	Freescale Semiconductor, Inc.
 *	  Timur Tabi <timur@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s:%d: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <sound/sta32x.h>
#include "sta32x.h"

#define STA32X_RATES (SNDRV_PCM_RATE_32000 | \
		      SNDRV_PCM_RATE_44100 | \
		      SNDRV_PCM_RATE_48000 | \
		      SNDRV_PCM_RATE_88200 | \
		      SNDRV_PCM_RATE_96000 | \
		      SNDRV_PCM_RATE_176400 | \
		      SNDRV_PCM_RATE_192000)

#define STA32X_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_S16_BE  | \
	 SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S18_3BE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE | \
	 SNDRV_PCM_FMTBIT_S24_LE  | SNDRV_PCM_FMTBIT_S24_BE  | \
	 SNDRV_PCM_FMTBIT_S32_LE  | SNDRV_PCM_FMTBIT_S32_BE)

/* Power-up register defaults */
static const struct reg_default sta32x_regs[] = {
	{  0x0, 0x63 },
	{  0x1, 0x80 },
	{  0x2, 0xc2 },
	{  0x3, 0x40 },
	{  0x4, 0xc2 },
	{  0x5, 0x5c },
	{  0x6, 0x10 },
	{  0x7, 0xff },
	{  0x8, 0x60 },
	{  0x9, 0x60 },
	{  0xa, 0x60 },
	{  0xb, 0x80 },
	{  0xc, 0x00 },
	{  0xd, 0x00 },
	{  0xe, 0x00 },
	{  0xf, 0x40 },
	{ 0x10, 0x80 },
	{ 0x11, 0x77 },
	{ 0x12, 0x6a },
	{ 0x13, 0x69 },
	{ 0x14, 0x6a },
	{ 0x15, 0x69 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
	{ 0x18, 0x00 },
	{ 0x19, 0x00 },
	{ 0x1a, 0x00 },
	{ 0x1b, 0x00 },
	{ 0x1c, 0x00 },
	{ 0x1d, 0x00 },
	{ 0x1e, 0x00 },
	{ 0x1f, 0x00 },
	{ 0x20, 0x00 },
	{ 0x21, 0x00 },
	{ 0x22, 0x00 },
	{ 0x23, 0x00 },
	{ 0x24, 0x00 },
	{ 0x25, 0x00 },
	{ 0x26, 0x00 },
	{ 0x27, 0x2d },
	{ 0x28, 0xc0 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x0c },
};

/* regulator power supply names */
static const char *sta32x_supply_names[] = {
	"Vdda",	/* analog supply, 3.3VV */
	"Vdd3",	/* digital supply, 3.3V */
	"Vcc"	/* power amp spply, 10V - 36V */
};

/* codec private data */
struct sta32x_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[ARRAY_SIZE(sta32x_supply_names)];
	struct snd_soc_codec *codec;
	struct sta32x_platform_data *pdata;

	unsigned int mclk;
	unsigned int format;

	u32 coef_shadow[STA32X_COEF_COUNT];
	struct delayed_work watchdog_work;
	int shutdown;
};

static const DECLARE_TLV_DB_SCALE(mvol_tlv, -12700, 50, 1);
static const DECLARE_TLV_DB_SCALE(chvol_tlv, -7950, 50, 1);
static const DECLARE_TLV_DB_SCALE(tone_tlv, -120, 200, 0);

static const char *sta32x_drc_ac[] = {
	"Anti-Clipping", "Dynamic Range Compression" };
static const char *sta32x_auto_eq_mode[] = {
	"User", "Preset", "Loudness" };
static const char *sta32x_auto_gc_mode[] = {
	"User", "AC no clipping", "AC limited clipping (10%)",
	"DRC nighttime listening mode" };
static const char *sta32x_auto_xo_mode[] = {
	"User", "80Hz", "100Hz", "120Hz", "140Hz", "160Hz", "180Hz", "200Hz",
	"220Hz", "240Hz", "260Hz", "280Hz", "300Hz", "320Hz", "340Hz", "360Hz" };
static const char *sta32x_preset_eq_mode[] = {
	"Flat", "Rock", "Soft Rock", "Jazz", "Classical", "Dance", "Pop", "Soft",
	"Hard", "Party", "Vocal", "Hip-Hop", "Dialog", "Bass-boost #1",
	"Bass-boost #2", "Bass-boost #3", "Loudness 1", "Loudness 2",
	"Loudness 3", "Loudness 4", "Loudness 5", "Loudness 6", "Loudness 7",
	"Loudness 8", "Loudness 9", "Loudness 10", "Loudness 11", "Loudness 12",
	"Loudness 13", "Loudness 14", "Loudness 15", "Loudness 16" };
static const char *sta32x_limiter_select[] = {
	"Limiter Disabled", "Limiter #1", "Limiter #2" };
static const char *sta32x_limiter_attack_rate[] = {
	"3.1584", "2.7072", "2.2560", "1.8048", "1.3536", "0.9024",
	"0.4512", "0.2256", "0.1504", "0.1123", "0.0902", "0.0752",
	"0.0645", "0.0564", "0.0501", "0.0451" };
static const char *sta32x_limiter_release_rate[] = {
	"0.5116", "0.1370", "0.0744", "0.0499", "0.0360", "0.0299",
	"0.0264", "0.0208", "0.0198", "0.0172", "0.0147", "0.0137",
	"0.0134", "0.0117", "0.0110", "0.0104" };

static const unsigned int sta32x_limiter_ac_attack_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 7, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(300, 100, 0),
};

static const unsigned int sta32x_limiter_ac_release_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2900, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-2000, 0, 0),
	3, 8, TLV_DB_SCALE_ITEM(-1400, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(-700, 100, 0),
};

static const unsigned int sta32x_limiter_drc_attack_tlv[] = {
	TLV_DB_RANGE_HEAD(3),
	0, 7, TLV_DB_SCALE_ITEM(-3100, 200, 0),
	8, 13, TLV_DB_SCALE_ITEM(-1600, 100, 0),
	14, 16, TLV_DB_SCALE_ITEM(-1000, 300, 0),
};

static const unsigned int sta32x_limiter_drc_release_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 2, TLV_DB_SCALE_ITEM(-3800, 200, 0),
	3, 4, TLV_DB_SCALE_ITEM(-3300, 200, 0),
	5, 12, TLV_DB_SCALE_ITEM(-3000, 200, 0),
	13, 16, TLV_DB_SCALE_ITEM(-1500, 300, 0),
};

static SOC_ENUM_SINGLE_DECL(sta32x_drc_ac_enum,
			    STA32X_CONFD, STA32X_CONFD_DRC_SHIFT,
			    sta32x_drc_ac);
static SOC_ENUM_SINGLE_DECL(sta32x_auto_eq_enum,
			    STA32X_AUTO1, STA32X_AUTO1_AMEQ_SHIFT,
			    sta32x_auto_eq_mode);
static SOC_ENUM_SINGLE_DECL(sta32x_auto_gc_enum,
			    STA32X_AUTO1, STA32X_AUTO1_AMGC_SHIFT,
			    sta32x_auto_gc_mode);
static SOC_ENUM_SINGLE_DECL(sta32x_auto_xo_enum,
			    STA32X_AUTO2, STA32X_AUTO2_XO_SHIFT,
			    sta32x_auto_xo_mode);
static SOC_ENUM_SINGLE_DECL(sta32x_preset_eq_enum,
			    STA32X_AUTO3, STA32X_AUTO3_PEQ_SHIFT,
			    sta32x_preset_eq_mode);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter_ch1_enum,
			    STA32X_C1CFG, STA32X_CxCFG_LS_SHIFT,
			    sta32x_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter_ch2_enum,
			    STA32X_C2CFG, STA32X_CxCFG_LS_SHIFT,
			    sta32x_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter_ch3_enum,
			    STA32X_C3CFG, STA32X_CxCFG_LS_SHIFT,
			    sta32x_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter1_attack_rate_enum,
			    STA32X_L1AR, STA32X_LxA_SHIFT,
			    sta32x_limiter_attack_rate);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter2_attack_rate_enum,
			    STA32X_L2AR, STA32X_LxA_SHIFT,
			    sta32x_limiter_attack_rate);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter1_release_rate_enum,
			    STA32X_L1AR, STA32X_LxR_SHIFT,
			    sta32x_limiter_release_rate);
static SOC_ENUM_SINGLE_DECL(sta32x_limiter2_release_rate_enum,
			    STA32X_L2AR, STA32X_LxR_SHIFT,
			    sta32x_limiter_release_rate);

/* byte array controls for setting biquad, mixer, scaling coefficients;
 * for biquads all five coefficients need to be set in one go,
 * mixer and pre/postscale coefs can be set individually;
 * each coef is 24bit, the bytes are ordered in the same way
 * as given in the STA32x data sheet (big endian; b1, b2, a1, a2, b0)
 */

static int sta32x_coefficient_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	int numcoef = kcontrol->private_value >> 16;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 3 * numcoef;
	return 0;
}

static int sta32x_coefficient_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA32X_CFUD */
	cfud = snd_soc_read(codec, STA32X_CFUD) & 0xf0;
	/* chip documentation does not say if the bits are self clearing,
	 * so do it explicitly */
	snd_soc_write(codec, STA32X_CFUD, cfud);

	snd_soc_write(codec, STA32X_CFADDR2, index);
	if (numcoef == 1)
		snd_soc_write(codec, STA32X_CFUD, cfud | 0x04);
	else if (numcoef == 5)
		snd_soc_write(codec, STA32X_CFUD, cfud | 0x08);
	else
		return -EINVAL;
	for (i = 0; i < 3 * numcoef; i++)
		ucontrol->value.bytes.data[i] =
			snd_soc_read(codec, STA32X_B1CF1 + i);

	return 0;
}

static int sta32x_coefficient_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA32X_CFUD */
	cfud = snd_soc_read(codec, STA32X_CFUD) & 0xf0;
	/* chip documentation does not say if the bits are self clearing,
	 * so do it explicitly */
	snd_soc_write(codec, STA32X_CFUD, cfud);

	snd_soc_write(codec, STA32X_CFADDR2, index);
	for (i = 0; i < numcoef && (index + i < STA32X_COEF_COUNT); i++)
		sta32x->coef_shadow[index + i] =
			  (ucontrol->value.bytes.data[3 * i] << 16)
			| (ucontrol->value.bytes.data[3 * i + 1] << 8)
			| (ucontrol->value.bytes.data[3 * i + 2]);
	for (i = 0; i < 3 * numcoef; i++)
		snd_soc_write(codec, STA32X_B1CF1 + i,
			      ucontrol->value.bytes.data[i]);
	if (numcoef == 1)
		snd_soc_write(codec, STA32X_CFUD, cfud | 0x01);
	else if (numcoef == 5)
		snd_soc_write(codec, STA32X_CFUD, cfud | 0x02);
	else
		return -EINVAL;

	return 0;
}

static int sta32x_sync_coef_shadow(struct snd_soc_codec *codec)
{
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA32X_CFUD */
	cfud = snd_soc_read(codec, STA32X_CFUD) & 0xf0;

	for (i = 0; i < STA32X_COEF_COUNT; i++) {
		snd_soc_write(codec, STA32X_CFADDR2, i);
		snd_soc_write(codec, STA32X_B1CF1,
			      (sta32x->coef_shadow[i] >> 16) & 0xff);
		snd_soc_write(codec, STA32X_B1CF2,
			      (sta32x->coef_shadow[i] >> 8) & 0xff);
		snd_soc_write(codec, STA32X_B1CF3,
			      (sta32x->coef_shadow[i]) & 0xff);
		/* chip documentation does not say if the bits are
		 * self-clearing, so do it explicitly */
		snd_soc_write(codec, STA32X_CFUD, cfud);
		snd_soc_write(codec, STA32X_CFUD, cfud | 0x01);
	}
	return 0;
}

static int sta32x_cache_sync(struct snd_soc_codec *codec)
{
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	unsigned int mute;
	int rc;

	/* mute during register sync */
	mute = snd_soc_read(codec, STA32X_MMUTE);
	snd_soc_write(codec, STA32X_MMUTE, mute | STA32X_MMUTE_MMUTE);
	sta32x_sync_coef_shadow(codec);
	rc = regcache_sync(sta32x->regmap);
	snd_soc_write(codec, STA32X_MMUTE, mute);
	return rc;
}

/* work around ESD issue where sta32x resets and loses all configuration */
static void sta32x_watchdog(struct work_struct *work)
{
	struct sta32x_priv *sta32x = container_of(work, struct sta32x_priv,
						  watchdog_work.work);
	struct snd_soc_codec *codec = sta32x->codec;
	unsigned int confa, confa_cached;

	/* check if sta32x has reset itself */
	confa_cached = snd_soc_read(codec, STA32X_CONFA);
	regcache_cache_bypass(sta32x->regmap, true);
	confa = snd_soc_read(codec, STA32X_CONFA);
	regcache_cache_bypass(sta32x->regmap, false);
	if (confa != confa_cached) {
		regcache_mark_dirty(sta32x->regmap);
		sta32x_cache_sync(codec);
	}

	if (!sta32x->shutdown)
		queue_delayed_work(system_power_efficient_wq,
				   &sta32x->watchdog_work,
				   round_jiffies_relative(HZ));
}

static void sta32x_watchdog_start(struct sta32x_priv *sta32x)
{
	if (sta32x->pdata->needs_esd_watchdog) {
		sta32x->shutdown = 0;
		queue_delayed_work(system_power_efficient_wq,
				   &sta32x->watchdog_work,
				   round_jiffies_relative(HZ));
	}
}

static void sta32x_watchdog_stop(struct sta32x_priv *sta32x)
{
	if (sta32x->pdata->needs_esd_watchdog) {
		sta32x->shutdown = 1;
		cancel_delayed_work_sync(&sta32x->watchdog_work);
	}
}

#define SINGLE_COEF(xname, index) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = sta32x_coefficient_info, \
	.get = sta32x_coefficient_get,\
	.put = sta32x_coefficient_put, \
	.private_value = index | (1 << 16) }

#define BIQUAD_COEFS(xname, index) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = sta32x_coefficient_info, \
	.get = sta32x_coefficient_get,\
	.put = sta32x_coefficient_put, \
	.private_value = index | (5 << 16) }

static const struct snd_kcontrol_new sta32x_snd_controls[] = {
SOC_SINGLE_TLV("Master Volume", STA32X_MVOL, 0, 0xff, 1, mvol_tlv),
SOC_SINGLE("Master Switch", STA32X_MMUTE, 0, 1, 1),
SOC_SINGLE("Ch1 Switch", STA32X_MMUTE, 1, 1, 1),
SOC_SINGLE("Ch2 Switch", STA32X_MMUTE, 2, 1, 1),
SOC_SINGLE("Ch3 Switch", STA32X_MMUTE, 3, 1, 1),
SOC_SINGLE_TLV("Ch1 Volume", STA32X_C1VOL, 0, 0xff, 1, chvol_tlv),
SOC_SINGLE_TLV("Ch2 Volume", STA32X_C2VOL, 0, 0xff, 1, chvol_tlv),
SOC_SINGLE_TLV("Ch3 Volume", STA32X_C3VOL, 0, 0xff, 1, chvol_tlv),
SOC_SINGLE("De-emphasis Filter Switch", STA32X_CONFD, STA32X_CONFD_DEMP_SHIFT, 1, 0),
SOC_ENUM("Compressor/Limiter Switch", sta32x_drc_ac_enum),
SOC_SINGLE("Miami Mode Switch", STA32X_CONFD, STA32X_CONFD_MME_SHIFT, 1, 0),
SOC_SINGLE("Zero Cross Switch", STA32X_CONFE, STA32X_CONFE_ZCE_SHIFT, 1, 0),
SOC_SINGLE("Soft Ramp Switch", STA32X_CONFE, STA32X_CONFE_SVE_SHIFT, 1, 0),
SOC_SINGLE("Auto-Mute Switch", STA32X_CONFF, STA32X_CONFF_IDE_SHIFT, 1, 0),
SOC_ENUM("Automode EQ", sta32x_auto_eq_enum),
SOC_ENUM("Automode GC", sta32x_auto_gc_enum),
SOC_ENUM("Automode XO", sta32x_auto_xo_enum),
SOC_ENUM("Preset EQ", sta32x_preset_eq_enum),
SOC_SINGLE("Ch1 Tone Control Bypass Switch", STA32X_C1CFG, STA32X_CxCFG_TCB_SHIFT, 1, 0),
SOC_SINGLE("Ch2 Tone Control Bypass Switch", STA32X_C2CFG, STA32X_CxCFG_TCB_SHIFT, 1, 0),
SOC_SINGLE("Ch1 EQ Bypass Switch", STA32X_C1CFG, STA32X_CxCFG_EQBP_SHIFT, 1, 0),
SOC_SINGLE("Ch2 EQ Bypass Switch", STA32X_C2CFG, STA32X_CxCFG_EQBP_SHIFT, 1, 0),
SOC_SINGLE("Ch1 Master Volume Bypass Switch", STA32X_C1CFG, STA32X_CxCFG_VBP_SHIFT, 1, 0),
SOC_SINGLE("Ch2 Master Volume Bypass Switch", STA32X_C1CFG, STA32X_CxCFG_VBP_SHIFT, 1, 0),
SOC_SINGLE("Ch3 Master Volume Bypass Switch", STA32X_C1CFG, STA32X_CxCFG_VBP_SHIFT, 1, 0),
SOC_ENUM("Ch1 Limiter Select", sta32x_limiter_ch1_enum),
SOC_ENUM("Ch2 Limiter Select", sta32x_limiter_ch2_enum),
SOC_ENUM("Ch3 Limiter Select", sta32x_limiter_ch3_enum),
SOC_SINGLE_TLV("Bass Tone Control", STA32X_TONE, STA32X_TONE_BTC_SHIFT, 15, 0, tone_tlv),
SOC_SINGLE_TLV("Treble Tone Control", STA32X_TONE, STA32X_TONE_TTC_SHIFT, 15, 0, tone_tlv),
SOC_ENUM("Limiter1 Attack Rate (dB/ms)", sta32x_limiter1_attack_rate_enum),
SOC_ENUM("Limiter2 Attack Rate (dB/ms)", sta32x_limiter2_attack_rate_enum),
SOC_ENUM("Limiter1 Release Rate (dB/ms)", sta32x_limiter1_release_rate_enum),
SOC_ENUM("Limiter2 Release Rate (dB/ms)", sta32x_limiter2_release_rate_enum),

/* depending on mode, the attack/release thresholds have
 * two different enum definitions; provide both
 */
SOC_SINGLE_TLV("Limiter1 Attack Threshold (AC Mode)", STA32X_L1ATRT, STA32X_LxA_SHIFT,
	       16, 0, sta32x_limiter_ac_attack_tlv),
SOC_SINGLE_TLV("Limiter2 Attack Threshold (AC Mode)", STA32X_L2ATRT, STA32X_LxA_SHIFT,
	       16, 0, sta32x_limiter_ac_attack_tlv),
SOC_SINGLE_TLV("Limiter1 Release Threshold (AC Mode)", STA32X_L1ATRT, STA32X_LxR_SHIFT,
	       16, 0, sta32x_limiter_ac_release_tlv),
SOC_SINGLE_TLV("Limiter2 Release Threshold (AC Mode)", STA32X_L2ATRT, STA32X_LxR_SHIFT,
	       16, 0, sta32x_limiter_ac_release_tlv),
SOC_SINGLE_TLV("Limiter1 Attack Threshold (DRC Mode)", STA32X_L1ATRT, STA32X_LxA_SHIFT,
	       16, 0, sta32x_limiter_drc_attack_tlv),
SOC_SINGLE_TLV("Limiter2 Attack Threshold (DRC Mode)", STA32X_L2ATRT, STA32X_LxA_SHIFT,
	       16, 0, sta32x_limiter_drc_attack_tlv),
SOC_SINGLE_TLV("Limiter1 Release Threshold (DRC Mode)", STA32X_L1ATRT, STA32X_LxR_SHIFT,
	       16, 0, sta32x_limiter_drc_release_tlv),
SOC_SINGLE_TLV("Limiter2 Release Threshold (DRC Mode)", STA32X_L2ATRT, STA32X_LxR_SHIFT,
	       16, 0, sta32x_limiter_drc_release_tlv),

BIQUAD_COEFS("Ch1 - Biquad 1", 0),
BIQUAD_COEFS("Ch1 - Biquad 2", 5),
BIQUAD_COEFS("Ch1 - Biquad 3", 10),
BIQUAD_COEFS("Ch1 - Biquad 4", 15),
BIQUAD_COEFS("Ch2 - Biquad 1", 20),
BIQUAD_COEFS("Ch2 - Biquad 2", 25),
BIQUAD_COEFS("Ch2 - Biquad 3", 30),
BIQUAD_COEFS("Ch2 - Biquad 4", 35),
BIQUAD_COEFS("High-pass", 40),
BIQUAD_COEFS("Low-pass", 45),
SINGLE_COEF("Ch1 - Prescale", 50),
SINGLE_COEF("Ch2 - Prescale", 51),
SINGLE_COEF("Ch1 - Postscale", 52),
SINGLE_COEF("Ch2 - Postscale", 53),
SINGLE_COEF("Ch3 - Postscale", 54),
SINGLE_COEF("Thermal warning - Postscale", 55),
SINGLE_COEF("Ch1 - Mix 1", 56),
SINGLE_COEF("Ch1 - Mix 2", 57),
SINGLE_COEF("Ch2 - Mix 1", 58),
SINGLE_COEF("Ch2 - Mix 2", 59),
SINGLE_COEF("Ch3 - Mix 1", 60),
SINGLE_COEF("Ch3 - Mix 2", 61),
};

static const struct snd_soc_dapm_widget sta32x_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LEFT"),
SND_SOC_DAPM_OUTPUT("RIGHT"),
SND_SOC_DAPM_OUTPUT("SUB"),
};

static const struct snd_soc_dapm_route sta32x_dapm_routes[] = {
	{ "LEFT", NULL, "DAC" },
	{ "RIGHT", NULL, "DAC" },
	{ "SUB", NULL, "DAC" },
};

/* MCLK interpolation ratio per fs */
static struct {
	int fs;
	int ir;
} interpolation_ratios[] = {
	{ 32000, 0 },
	{ 44100, 0 },
	{ 48000, 0 },
	{ 88200, 1 },
	{ 96000, 1 },
	{ 176400, 2 },
	{ 192000, 2 },
};

/* MCLK to fs clock ratios */
static struct {
	int ratio;
	int mcs;
} mclk_ratios[3][7] = {
	{ { 768, 0 }, { 512, 1 }, { 384, 2 }, { 256, 3 },
	  { 128, 4 }, { 576, 5 }, { 0, 0 } },
	{ { 384, 2 }, { 256, 3 }, { 192, 4 }, { 128, 5 }, {64, 0 }, { 0, 0 } },
	{ { 384, 2 }, { 256, 3 }, { 192, 4 }, { 128, 5 }, {64, 0 }, { 0, 0 } },
};


/**
 * sta32x_set_dai_sysclk - configure MCLK
 * @codec_dai: the codec DAI
 * @clk_id: the clock ID (ignored)
 * @freq: the MCLK input frequency
 * @dir: the clock direction (ignored)
 *
 * The value of MCLK is used to determine which sample rates are supported
 * by the STA32X, based on the mclk_ratios table.
 *
 * This function must be called by the machine driver's 'startup' function,
 * otherwise the list of supported sample rates will not be available in
 * time for ALSA.
 *
 * For setups with variable MCLKs, pass 0 as 'freq' argument. This will cause
 * theoretically possible sample rates to be enabled. Call it again with a
 * proper value set one the external clock is set (most probably you would do
 * that from a machine's driver 'hw_param' hook.
 */
static int sta32x_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	int i, j, ir, fs;
	unsigned int rates = 0;
	unsigned int rate_min = -1;
	unsigned int rate_max = 0;

	pr_debug("mclk=%u\n", freq);
	sta32x->mclk = freq;

	if (sta32x->mclk) {
		for (i = 0; i < ARRAY_SIZE(interpolation_ratios); i++) {
			ir = interpolation_ratios[i].ir;
			fs = interpolation_ratios[i].fs;
			for (j = 0; mclk_ratios[ir][j].ratio; j++) {
				if (mclk_ratios[ir][j].ratio * fs == freq) {
					rates |= snd_pcm_rate_to_rate_bit(fs);
					if (fs < rate_min)
						rate_min = fs;
					if (fs > rate_max)
						rate_max = fs;
					break;
				}
			}
		}
		/* FIXME: soc should support a rate list */
		rates &= ~SNDRV_PCM_RATE_KNOT;

		if (!rates) {
			dev_err(codec->dev, "could not find a valid sample rate\n");
			return -EINVAL;
		}
	} else {
		/* enable all possible rates */
		rates = STA32X_RATES;
		rate_min = 32000;
		rate_max = 192000;
	}

	codec_dai->driver->playback.rates = rates;
	codec_dai->driver->playback.rate_min = rate_min;
	codec_dai->driver->playback.rate_max = rate_max;
	return 0;
}

/**
 * sta32x_set_dai_fmt - configure the codec for the selected audio format
 * @codec_dai: the codec DAI
 * @fmt: a SND_SOC_DAIFMT_x value indicating the data format
 *
 * This function takes a bitmask of SND_SOC_DAIFMT_x bits and programs the
 * codec accordingly.
 */
static int sta32x_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	u8 confb = snd_soc_read(codec, STA32X_CONFB);

	pr_debug("\n");
	confb &= ~(STA32X_CONFB_C1IM | STA32X_CONFB_C2IM);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		sta32x->format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		confb |= STA32X_CONFB_C2IM;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		confb |= STA32X_CONFB_C1IM;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, STA32X_CONFB, confb);
	return 0;
}

/**
 * sta32x_hw_params - program the STA32X with the given hardware parameters.
 * @substream: the audio stream
 * @params: the hardware parameters to set
 * @dai: the SOC DAI (ignored)
 *
 * This function programs the hardware with the values provided.
 * Specifically, the sample rate and the data format.
 */
static int sta32x_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	unsigned int rate;
	int i, mcs = -1, ir = -1;
	u8 confa, confb;

	rate = params_rate(params);
	pr_debug("rate: %u\n", rate);
	for (i = 0; i < ARRAY_SIZE(interpolation_ratios); i++)
		if (interpolation_ratios[i].fs == rate) {
			ir = interpolation_ratios[i].ir;
			break;
		}
	if (ir < 0)
		return -EINVAL;
	for (i = 0; mclk_ratios[ir][i].ratio; i++)
		if (mclk_ratios[ir][i].ratio * rate == sta32x->mclk) {
			mcs = mclk_ratios[ir][i].mcs;
			break;
		}
	if (mcs < 0)
		return -EINVAL;

	confa = snd_soc_read(codec, STA32X_CONFA);
	confa &= ~(STA32X_CONFA_MCS_MASK | STA32X_CONFA_IR_MASK);
	confa |= (ir << STA32X_CONFA_IR_SHIFT) | (mcs << STA32X_CONFA_MCS_SHIFT);

	confb = snd_soc_read(codec, STA32X_CONFB);
	confb &= ~(STA32X_CONFB_SAI_MASK | STA32X_CONFB_SAIFB);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
		pr_debug("24bit\n");
		/* fall through */
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
		pr_debug("24bit or 32bit\n");
		switch (sta32x->format) {
		case SND_SOC_DAIFMT_I2S:
			confb |= 0x0;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			confb |= 0x1;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			confb |= 0x2;
			break;
		}

		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
		pr_debug("20bit\n");
		switch (sta32x->format) {
		case SND_SOC_DAIFMT_I2S:
			confb |= 0x4;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			confb |= 0x5;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			confb |= 0x6;
			break;
		}

		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
		pr_debug("18bit\n");
		switch (sta32x->format) {
		case SND_SOC_DAIFMT_I2S:
			confb |= 0x8;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			confb |= 0x9;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			confb |= 0xa;
			break;
		}

		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		pr_debug("16bit\n");
		switch (sta32x->format) {
		case SND_SOC_DAIFMT_I2S:
			confb |= 0x0;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			confb |= 0xd;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			confb |= 0xe;
			break;
		}

		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, STA32X_CONFA, confa);
	snd_soc_write(codec, STA32X_CONFB, confb);
	return 0;
}

/**
 * sta32x_set_bias_level - DAPM callback
 * @codec: the codec device
 * @level: DAPM power level
 *
 * This is called by ALSA to put the codec into low power mode
 * or to wake it up.  If the codec is powered off completely
 * all registers must be restored after power on.
 */
static int sta32x_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	int ret;
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);

	pr_debug("level = %d\n", level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Full power on */
		snd_soc_update_bits(codec, STA32X_CONFF,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(sta32x->supplies),
						    sta32x->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n", ret);
				return ret;
			}

			sta32x_cache_sync(codec);
			sta32x_watchdog_start(sta32x);
		}

		/* Power up to mute */
		/* FIXME */
		snd_soc_update_bits(codec, STA32X_CONFF,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD);

		break;

	case SND_SOC_BIAS_OFF:
		/* The chip runs through the power down sequence for us. */
		snd_soc_update_bits(codec, STA32X_CONFF,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD,
				    STA32X_CONFF_PWDN);
		msleep(300);
		sta32x_watchdog_stop(sta32x);
		regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies),
				       sta32x->supplies);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static const struct snd_soc_dai_ops sta32x_dai_ops = {
	.hw_params	= sta32x_hw_params,
	.set_sysclk	= sta32x_set_dai_sysclk,
	.set_fmt	= sta32x_set_dai_fmt,
};

static struct snd_soc_dai_driver sta32x_dai = {
	.name = "STA32X",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = STA32X_RATES,
		.formats = STA32X_FORMATS,
	},
	.ops = &sta32x_dai_ops,
};

#ifdef CONFIG_PM
static int sta32x_suspend(struct snd_soc_codec *codec)
{
	sta32x_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int sta32x_resume(struct snd_soc_codec *codec)
{
	sta32x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define sta32x_suspend NULL
#define sta32x_resume NULL
#endif

static int sta32x_probe(struct snd_soc_codec *codec)
{
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);
	int i, ret = 0, thermal = 0;

	sta32x->codec = codec;
	sta32x->pdata = dev_get_platdata(codec->dev);

	ret = regulator_bulk_enable(ARRAY_SIZE(sta32x->supplies),
				    sta32x->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Chip documentation explicitly requires that the reset values
	 * of reserved register bits are left untouched.
	 * Write the register default value to cache for reserved registers,
	 * so the write to the these registers are suppressed by the cache
	 * restore code when it skips writes of default registers.
	 */
	regcache_cache_only(sta32x->regmap, true);
	snd_soc_write(codec, STA32X_CONFC, 0xc2);
	snd_soc_write(codec, STA32X_CONFE, 0xc2);
	snd_soc_write(codec, STA32X_CONFF, 0x5c);
	snd_soc_write(codec, STA32X_MMUTE, 0x10);
	snd_soc_write(codec, STA32X_AUTO1, 0x60);
	snd_soc_write(codec, STA32X_AUTO3, 0x00);
	snd_soc_write(codec, STA32X_C3CFG, 0x40);
	regcache_cache_only(sta32x->regmap, false);

	/* set thermal warning adjustment and recovery */
	if (!(sta32x->pdata->thermal_conf & STA32X_THERMAL_ADJUSTMENT_ENABLE))
		thermal |= STA32X_CONFA_TWAB;
	if (!(sta32x->pdata->thermal_conf & STA32X_THERMAL_RECOVERY_ENABLE))
		thermal |= STA32X_CONFA_TWRB;
	snd_soc_update_bits(codec, STA32X_CONFA,
			    STA32X_CONFA_TWAB | STA32X_CONFA_TWRB,
			    thermal);

	/* select output configuration  */
	snd_soc_update_bits(codec, STA32X_CONFF,
			    STA32X_CONFF_OCFG_MASK,
			    sta32x->pdata->output_conf
			    << STA32X_CONFF_OCFG_SHIFT);

	/* channel to output mapping */
	snd_soc_update_bits(codec, STA32X_C1CFG,
			    STA32X_CxCFG_OM_MASK,
			    sta32x->pdata->ch1_output_mapping
			    << STA32X_CxCFG_OM_SHIFT);
	snd_soc_update_bits(codec, STA32X_C2CFG,
			    STA32X_CxCFG_OM_MASK,
			    sta32x->pdata->ch2_output_mapping
			    << STA32X_CxCFG_OM_SHIFT);
	snd_soc_update_bits(codec, STA32X_C3CFG,
			    STA32X_CxCFG_OM_MASK,
			    sta32x->pdata->ch3_output_mapping
			    << STA32X_CxCFG_OM_SHIFT);

	/* initialize coefficient shadow RAM with reset values */
	for (i = 4; i <= 49; i += 5)
		sta32x->coef_shadow[i] = 0x400000;
	for (i = 50; i <= 54; i++)
		sta32x->coef_shadow[i] = 0x7fffff;
	sta32x->coef_shadow[55] = 0x5a9df7;
	sta32x->coef_shadow[56] = 0x7fffff;
	sta32x->coef_shadow[59] = 0x7fffff;
	sta32x->coef_shadow[60] = 0x400000;
	sta32x->coef_shadow[61] = 0x400000;

	if (sta32x->pdata->needs_esd_watchdog)
		INIT_DELAYED_WORK(&sta32x->watchdog_work, sta32x_watchdog);

	sta32x_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies), sta32x->supplies);

	return 0;
}

static int sta32x_remove(struct snd_soc_codec *codec)
{
	struct sta32x_priv *sta32x = snd_soc_codec_get_drvdata(codec);

	sta32x_watchdog_stop(sta32x);
	sta32x_set_bias_level(codec, SND_SOC_BIAS_OFF);
	regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies), sta32x->supplies);

	return 0;
}

static bool sta32x_reg_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STA32X_CONFA ... STA32X_L2ATRT:
	case STA32X_MPCC1 ... STA32X_FDRC2:
		return 0;
	}
	return 1;
}

static const struct snd_soc_codec_driver sta32x_codec = {
	.probe =		sta32x_probe,
	.remove =		sta32x_remove,
	.suspend =		sta32x_suspend,
	.resume =		sta32x_resume,
	.set_bias_level =	sta32x_set_bias_level,
	.controls =		sta32x_snd_controls,
	.num_controls =		ARRAY_SIZE(sta32x_snd_controls),
	.dapm_widgets =		sta32x_dapm_widgets,
	.num_dapm_widgets =	ARRAY_SIZE(sta32x_dapm_widgets),
	.dapm_routes =		sta32x_dapm_routes,
	.num_dapm_routes =	ARRAY_SIZE(sta32x_dapm_routes),
};

static const struct regmap_config sta32x_regmap = {
	.reg_bits =		8,
	.val_bits =		8,
	.max_register =		STA32X_FDRC2,
	.reg_defaults =		sta32x_regs,
	.num_reg_defaults =	ARRAY_SIZE(sta32x_regs),
	.cache_type =		REGCACHE_RBTREE,
	.volatile_reg =		sta32x_reg_is_volatile,
};

static int sta32x_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct sta32x_priv *sta32x;
	int ret, i;

	sta32x = devm_kzalloc(&i2c->dev, sizeof(struct sta32x_priv),
			      GFP_KERNEL);
	if (!sta32x)
		return -ENOMEM;

	/* regulators */
	for (i = 0; i < ARRAY_SIZE(sta32x->supplies); i++)
		sta32x->supplies[i].supply = sta32x_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(sta32x->supplies),
				      sta32x->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	sta32x->regmap = devm_regmap_init_i2c(i2c, &sta32x_regmap);
	if (IS_ERR(sta32x->regmap)) {
		ret = PTR_ERR(sta32x->regmap);
		dev_err(&i2c->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, sta32x);

	ret = snd_soc_register_codec(&i2c->dev, &sta32x_codec, &sta32x_dai, 1);
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to register codec (%d)\n", ret);

	return ret;
}

static int sta32x_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id sta32x_i2c_id[] = {
	{ "sta326", 0 },
	{ "sta328", 0 },
	{ "sta329", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sta32x_i2c_id);

static struct i2c_driver sta32x_i2c_driver = {
	.driver = {
		.name = "sta32x",
		.owner = THIS_MODULE,
	},
	.probe =    sta32x_i2c_probe,
	.remove =   sta32x_i2c_remove,
	.id_table = sta32x_i2c_id,
};

module_i2c_driver(sta32x_i2c_driver);

MODULE_DESCRIPTION("ASoC STA32X driver");
MODULE_AUTHOR("Johannes Stezenbach <js@sig21.net>");
MODULE_LICENSE("GPL");
