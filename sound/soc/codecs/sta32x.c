// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s:%d: " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
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
	(SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_S18_3LE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE  | SNDRV_PCM_FMTBIT_S32_LE)

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

static const struct regmap_range sta32x_write_regs_range[] = {
	regmap_reg_range(STA32X_CONFA,  STA32X_FDRC2),
};

static const struct regmap_range sta32x_read_regs_range[] = {
	regmap_reg_range(STA32X_CONFA,  STA32X_FDRC2),
};

static const struct regmap_range sta32x_volatile_regs_range[] = {
	regmap_reg_range(STA32X_CFADDR2, STA32X_CFUD),
};

static const struct regmap_access_table sta32x_write_regs = {
	.yes_ranges =	sta32x_write_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta32x_write_regs_range),
};

static const struct regmap_access_table sta32x_read_regs = {
	.yes_ranges =	sta32x_read_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta32x_read_regs_range),
};

static const struct regmap_access_table sta32x_volatile_regs = {
	.yes_ranges =	sta32x_volatile_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta32x_volatile_regs_range),
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
	struct clk *xti_clk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(sta32x_supply_names)];
	struct snd_soc_component *component;
	struct sta32x_platform_data *pdata;

	unsigned int mclk;
	unsigned int format;

	u32 coef_shadow[STA32X_COEF_COUNT];
	struct delayed_work watchdog_work;
	int shutdown;
	struct gpio_desc *gpiod_nreset;
	struct mutex coeff_lock;
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
static DECLARE_TLV_DB_RANGE(sta32x_limiter_ac_attack_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(300, 100, 0),
);

static DECLARE_TLV_DB_RANGE(sta32x_limiter_ac_release_tlv,
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2900, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-2000, 0, 0),
	3, 8, TLV_DB_SCALE_ITEM(-1400, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(-700, 100, 0),
);

static DECLARE_TLV_DB_RANGE(sta32x_limiter_drc_attack_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-3100, 200, 0),
	8, 13, TLV_DB_SCALE_ITEM(-1600, 100, 0),
	14, 16, TLV_DB_SCALE_ITEM(-1000, 300, 0),
);

static DECLARE_TLV_DB_RANGE(sta32x_limiter_drc_release_tlv,
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 2, TLV_DB_SCALE_ITEM(-3800, 200, 0),
	3, 4, TLV_DB_SCALE_ITEM(-3300, 200, 0),
	5, 12, TLV_DB_SCALE_ITEM(-3000, 200, 0),
	13, 16, TLV_DB_SCALE_ITEM(-1500, 300, 0),
);

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
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud, val;
	int i, ret = 0;

	mutex_lock(&sta32x->coeff_lock);

	/* preserve reserved bits in STA32X_CFUD */
	regmap_read(sta32x->regmap, STA32X_CFUD, &cfud);
	cfud &= 0xf0;
	/*
	 * chip documentation does not say if the bits are self clearing,
	 * so do it explicitly
	 */
	regmap_write(sta32x->regmap, STA32X_CFUD, cfud);

	regmap_write(sta32x->regmap, STA32X_CFADDR2, index);
	if (numcoef == 1) {
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud | 0x04);
	} else if (numcoef == 5) {
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud | 0x08);
	} else {
		ret = -EINVAL;
		goto exit_unlock;
	}

	for (i = 0; i < 3 * numcoef; i++) {
		regmap_read(sta32x->regmap, STA32X_B1CF1 + i, &val);
		ucontrol->value.bytes.data[i] = val;
	}

exit_unlock:
	mutex_unlock(&sta32x->coeff_lock);

	return ret;
}

static int sta32x_coefficient_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA32X_CFUD */
	regmap_read(sta32x->regmap, STA32X_CFUD, &cfud);
	cfud &= 0xf0;
	/*
	 * chip documentation does not say if the bits are self clearing,
	 * so do it explicitly
	 */
	regmap_write(sta32x->regmap, STA32X_CFUD, cfud);

	regmap_write(sta32x->regmap, STA32X_CFADDR2, index);
	for (i = 0; i < numcoef && (index + i < STA32X_COEF_COUNT); i++)
		sta32x->coef_shadow[index + i] =
			  (ucontrol->value.bytes.data[3 * i] << 16)
			| (ucontrol->value.bytes.data[3 * i + 1] << 8)
			| (ucontrol->value.bytes.data[3 * i + 2]);
	for (i = 0; i < 3 * numcoef; i++)
		regmap_write(sta32x->regmap, STA32X_B1CF1 + i,
			     ucontrol->value.bytes.data[i]);
	if (numcoef == 1)
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud | 0x01);
	else if (numcoef == 5)
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud | 0x02);
	else
		return -EINVAL;

	return 0;
}

static int sta32x_sync_coef_shadow(struct snd_soc_component *component)
{
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA32X_CFUD */
	regmap_read(sta32x->regmap, STA32X_CFUD, &cfud);
	cfud &= 0xf0;

	for (i = 0; i < STA32X_COEF_COUNT; i++) {
		regmap_write(sta32x->regmap, STA32X_CFADDR2, i);
		regmap_write(sta32x->regmap, STA32X_B1CF1,
			     (sta32x->coef_shadow[i] >> 16) & 0xff);
		regmap_write(sta32x->regmap, STA32X_B1CF2,
			     (sta32x->coef_shadow[i] >> 8) & 0xff);
		regmap_write(sta32x->regmap, STA32X_B1CF3,
			     (sta32x->coef_shadow[i]) & 0xff);
		/*
		 * chip documentation does not say if the bits are
		 * self-clearing, so do it explicitly
		 */
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud);
		regmap_write(sta32x->regmap, STA32X_CFUD, cfud | 0x01);
	}
	return 0;
}

static int sta32x_cache_sync(struct snd_soc_component *component)
{
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	unsigned int mute;
	int rc;

	/* mute during register sync */
	regmap_read(sta32x->regmap, STA32X_MMUTE, &mute);
	regmap_write(sta32x->regmap, STA32X_MMUTE, mute | STA32X_MMUTE_MMUTE);
	sta32x_sync_coef_shadow(component);
	rc = regcache_sync(sta32x->regmap);
	regmap_write(sta32x->regmap, STA32X_MMUTE, mute);
	return rc;
}

/* work around ESD issue where sta32x resets and loses all configuration */
static void sta32x_watchdog(struct work_struct *work)
{
	struct sta32x_priv *sta32x = container_of(work, struct sta32x_priv,
						  watchdog_work.work);
	struct snd_soc_component *component = sta32x->component;
	unsigned int confa, confa_cached;

	/* check if sta32x has reset itself */
	confa_cached = snd_soc_component_read(component, STA32X_CONFA);
	regcache_cache_bypass(sta32x->regmap, true);
	confa = snd_soc_component_read(component, STA32X_CONFA);
	regcache_cache_bypass(sta32x->regmap, false);
	if (confa != confa_cached) {
		regcache_mark_dirty(sta32x->regmap);
		sta32x_cache_sync(component);
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
static int mcs_ratio_table[3][7] = {
	{ 768, 512, 384, 256, 128, 576, 0 },
	{ 384, 256, 192, 128,  64,   0 },
	{ 384, 256, 192, 128,  64,   0 },
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
	struct snd_soc_component *component = codec_dai->component;
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "mclk=%u\n", freq);
	sta32x->mclk = freq;

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
	struct snd_soc_component *component = codec_dai->component;
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	u8 confb = 0;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
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

	return regmap_update_bits(sta32x->regmap, STA32X_CONFB,
				  STA32X_CONFB_C1IM | STA32X_CONFB_C2IM, confb);
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
	struct snd_soc_component *component = dai->component;
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	int i, mcs = -EINVAL, ir = -EINVAL;
	unsigned int confa, confb;
	unsigned int rate, ratio;
	int ret;

	if (!sta32x->mclk) {
		dev_err(component->dev,
			"sta32x->mclk is unset. Unable to determine ratio\n");
		return -EIO;
	}

	rate = params_rate(params);
	ratio = sta32x->mclk / rate;
	dev_dbg(component->dev, "rate: %u, ratio: %u\n", rate, ratio);

	for (i = 0; i < ARRAY_SIZE(interpolation_ratios); i++) {
		if (interpolation_ratios[i].fs == rate) {
			ir = interpolation_ratios[i].ir;
			break;
		}
	}

	if (ir < 0) {
		dev_err(component->dev, "Unsupported samplerate: %u\n", rate);
		return -EINVAL;
	}

	for (i = 0; i < 6; i++) {
		if (mcs_ratio_table[ir][i] == ratio) {
			mcs = i;
			break;
		}
	}

	if (mcs < 0) {
		dev_err(component->dev, "Unresolvable ratio: %u\n", ratio);
		return -EINVAL;
	}

	confa = (ir << STA32X_CONFA_IR_SHIFT) |
		(mcs << STA32X_CONFA_MCS_SHIFT);
	confb = 0;

	switch (params_width(params)) {
	case 24:
		dev_dbg(component->dev, "24bit\n");
		fallthrough;
	case 32:
		dev_dbg(component->dev, "24bit or 32bit\n");
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
	case 20:
		dev_dbg(component->dev, "20bit\n");
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
	case 18:
		dev_dbg(component->dev, "18bit\n");
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
	case 16:
		dev_dbg(component->dev, "16bit\n");
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

	ret = regmap_update_bits(sta32x->regmap, STA32X_CONFA,
				 STA32X_CONFA_MCS_MASK | STA32X_CONFA_IR_MASK,
				 confa);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(sta32x->regmap, STA32X_CONFB,
				 STA32X_CONFB_SAI_MASK | STA32X_CONFB_SAIFB,
				 confb);
	if (ret < 0)
		return ret;

	return 0;
}

static int sta32x_startup_sequence(struct sta32x_priv *sta32x)
{
	if (sta32x->gpiod_nreset) {
		gpiod_set_value(sta32x->gpiod_nreset, 0);
		mdelay(1);
		gpiod_set_value(sta32x->gpiod_nreset, 1);
		mdelay(1);
	}

	return 0;
}

/**
 * sta32x_set_bias_level - DAPM callback
 * @component: the component device
 * @level: DAPM power level
 *
 * This is called by ALSA to put the component into low power mode
 * or to wake it up.  If the component is powered off completely
 * all registers must be restored after power on.
 */
static int sta32x_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	int ret;
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "level = %d\n", level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Full power on */
		regmap_update_bits(sta32x->regmap, STA32X_CONFF,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD,
				    STA32X_CONFF_PWDN | STA32X_CONFF_EAPD);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(sta32x->supplies),
						    sta32x->supplies);
			if (ret != 0) {
				dev_err(component->dev,
					"Failed to enable supplies: %d\n", ret);
				return ret;
			}

			sta32x_startup_sequence(sta32x);
			sta32x_cache_sync(component);
			sta32x_watchdog_start(sta32x);
		}

		/* Power down */
		regmap_update_bits(sta32x->regmap, STA32X_CONFF,
				   STA32X_CONFF_PWDN | STA32X_CONFF_EAPD,
				   0);

		break;

	case SND_SOC_BIAS_OFF:
		/* The chip runs through the power down sequence for us. */
		regmap_update_bits(sta32x->regmap, STA32X_CONFF,
				   STA32X_CONFF_PWDN | STA32X_CONFF_EAPD, 0);
		msleep(300);
		sta32x_watchdog_stop(sta32x);

		gpiod_set_value(sta32x->gpiod_nreset, 0);

		regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies),
				       sta32x->supplies);
		break;
	}
	return 0;
}

static const struct snd_soc_dai_ops sta32x_dai_ops = {
	.hw_params	= sta32x_hw_params,
	.set_sysclk	= sta32x_set_dai_sysclk,
	.set_fmt	= sta32x_set_dai_fmt,
};

static struct snd_soc_dai_driver sta32x_dai = {
	.name = "sta32x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = STA32X_RATES,
		.formats = STA32X_FORMATS,
	},
	.ops = &sta32x_dai_ops,
};

static int sta32x_probe(struct snd_soc_component *component)
{
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);
	struct sta32x_platform_data *pdata = sta32x->pdata;
	int i, ret = 0, thermal = 0;

	sta32x->component = component;

	if (sta32x->xti_clk) {
		ret = clk_prepare_enable(sta32x->xti_clk);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to enable clock: %d\n", ret);
			return ret;
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(sta32x->supplies),
				    sta32x->supplies);
	if (ret != 0) {
		dev_err(component->dev, "Failed to enable supplies: %d\n", ret);
		goto err_clk_disable_unprepare;
	}

	ret = sta32x_startup_sequence(sta32x);
	if (ret < 0) {
		dev_err(component->dev, "Failed to startup device\n");
		goto err_regulator_bulk_disable;
	}

	/* CONFA */
	if (!pdata->thermal_warning_recovery)
		thermal |= STA32X_CONFA_TWAB;
	if (!pdata->thermal_warning_adjustment)
		thermal |= STA32X_CONFA_TWRB;
	if (!pdata->fault_detect_recovery)
		thermal |= STA32X_CONFA_FDRB;
	regmap_update_bits(sta32x->regmap, STA32X_CONFA,
			   STA32X_CONFA_TWAB | STA32X_CONFA_TWRB |
			   STA32X_CONFA_FDRB,
			   thermal);

	/* CONFC */
	regmap_update_bits(sta32x->regmap, STA32X_CONFC,
			   STA32X_CONFC_CSZ_MASK,
			   pdata->drop_compensation_ns
				<< STA32X_CONFC_CSZ_SHIFT);

	/* CONFE */
	regmap_update_bits(sta32x->regmap, STA32X_CONFE,
			   STA32X_CONFE_MPCV,
			   pdata->max_power_use_mpcc ?
				STA32X_CONFE_MPCV : 0);
	regmap_update_bits(sta32x->regmap, STA32X_CONFE,
			   STA32X_CONFE_MPC,
			   pdata->max_power_correction ?
				STA32X_CONFE_MPC : 0);
	regmap_update_bits(sta32x->regmap, STA32X_CONFE,
			   STA32X_CONFE_AME,
			   pdata->am_reduction_mode ?
				STA32X_CONFE_AME : 0);
	regmap_update_bits(sta32x->regmap, STA32X_CONFE,
			   STA32X_CONFE_PWMS,
			   pdata->odd_pwm_speed_mode ?
				STA32X_CONFE_PWMS : 0);

	/*  CONFF */
	regmap_update_bits(sta32x->regmap, STA32X_CONFF,
			   STA32X_CONFF_IDE,
			   pdata->invalid_input_detect_mute ?
				STA32X_CONFF_IDE : 0);

	/* select output configuration  */
	regmap_update_bits(sta32x->regmap, STA32X_CONFF,
			   STA32X_CONFF_OCFG_MASK,
			   pdata->output_conf
				<< STA32X_CONFF_OCFG_SHIFT);

	/* channel to output mapping */
	regmap_update_bits(sta32x->regmap, STA32X_C1CFG,
			   STA32X_CxCFG_OM_MASK,
			   pdata->ch1_output_mapping
				<< STA32X_CxCFG_OM_SHIFT);
	regmap_update_bits(sta32x->regmap, STA32X_C2CFG,
			   STA32X_CxCFG_OM_MASK,
			   pdata->ch2_output_mapping
				<< STA32X_CxCFG_OM_SHIFT);
	regmap_update_bits(sta32x->regmap, STA32X_C3CFG,
			   STA32X_CxCFG_OM_MASK,
			   pdata->ch3_output_mapping
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

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);
	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies), sta32x->supplies);

	return 0;

err_regulator_bulk_disable:
	regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies), sta32x->supplies);
err_clk_disable_unprepare:
	if (sta32x->xti_clk)
		clk_disable_unprepare(sta32x->xti_clk);
	return ret;
}

static void sta32x_remove(struct snd_soc_component *component)
{
	struct sta32x_priv *sta32x = snd_soc_component_get_drvdata(component);

	sta32x_watchdog_stop(sta32x);
	regulator_bulk_disable(ARRAY_SIZE(sta32x->supplies), sta32x->supplies);

	if (sta32x->xti_clk)
		clk_disable_unprepare(sta32x->xti_clk);
}

static const struct snd_soc_component_driver sta32x_component = {
	.probe			= sta32x_probe,
	.remove			= sta32x_remove,
	.set_bias_level		= sta32x_set_bias_level,
	.controls		= sta32x_snd_controls,
	.num_controls		= ARRAY_SIZE(sta32x_snd_controls),
	.dapm_widgets		= sta32x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sta32x_dapm_widgets),
	.dapm_routes		= sta32x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sta32x_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config sta32x_regmap = {
	.reg_bits =		8,
	.val_bits =		8,
	.max_register =		STA32X_FDRC2,
	.reg_defaults =		sta32x_regs,
	.num_reg_defaults =	ARRAY_SIZE(sta32x_regs),
	.cache_type =		REGCACHE_RBTREE,
	.wr_table =		&sta32x_write_regs,
	.rd_table =		&sta32x_read_regs,
	.volatile_table =	&sta32x_volatile_regs,
};

#ifdef CONFIG_OF
static const struct of_device_id st32x_dt_ids[] = {
	{ .compatible = "st,sta32x", },
	{ }
};
MODULE_DEVICE_TABLE(of, st32x_dt_ids);

static int sta32x_probe_dt(struct device *dev, struct sta32x_priv *sta32x)
{
	struct device_node *np = dev->of_node;
	struct sta32x_platform_data *pdata;
	u16 tmp;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	of_property_read_u8(np, "st,output-conf",
			    &pdata->output_conf);
	of_property_read_u8(np, "st,ch1-output-mapping",
			    &pdata->ch1_output_mapping);
	of_property_read_u8(np, "st,ch2-output-mapping",
			    &pdata->ch2_output_mapping);
	of_property_read_u8(np, "st,ch3-output-mapping",
			    &pdata->ch3_output_mapping);

	pdata->fault_detect_recovery =
		of_property_read_bool(np, "st,fault-detect-recovery");
	pdata->thermal_warning_recovery =
		of_property_read_bool(np, "st,thermal-warning-recovery");
	pdata->thermal_warning_adjustment =
		of_property_read_bool(np, "st,thermal-warning-adjustment");
	pdata->needs_esd_watchdog =
		of_property_read_bool(np, "st,needs_esd_watchdog");

	tmp = 140;
	of_property_read_u16(np, "st,drop-compensation-ns", &tmp);
	pdata->drop_compensation_ns = clamp_t(u16, tmp, 0, 300) / 20;

	/* CONFE */
	pdata->max_power_use_mpcc =
		of_property_read_bool(np, "st,max-power-use-mpcc");
	pdata->max_power_correction =
		of_property_read_bool(np, "st,max-power-correction");
	pdata->am_reduction_mode =
		of_property_read_bool(np, "st,am-reduction-mode");
	pdata->odd_pwm_speed_mode =
		of_property_read_bool(np, "st,odd-pwm-speed-mode");

	/* CONFF */
	pdata->invalid_input_detect_mute =
		of_property_read_bool(np, "st,invalid-input-detect-mute");

	sta32x->pdata = pdata;

	return 0;
}
#endif

static int sta32x_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct sta32x_priv *sta32x;
	int ret, i;

	sta32x = devm_kzalloc(&i2c->dev, sizeof(struct sta32x_priv),
			      GFP_KERNEL);
	if (!sta32x)
		return -ENOMEM;

	mutex_init(&sta32x->coeff_lock);
	sta32x->pdata = dev_get_platdata(dev);

#ifdef CONFIG_OF
	if (dev->of_node) {
		ret = sta32x_probe_dt(dev, sta32x);
		if (ret < 0)
			return ret;
	}
#endif

	/* Clock */
	sta32x->xti_clk = devm_clk_get(dev, "xti");
	if (IS_ERR(sta32x->xti_clk)) {
		ret = PTR_ERR(sta32x->xti_clk);

		if (ret == -EPROBE_DEFER)
			return ret;

		sta32x->xti_clk = NULL;
	}

	/* GPIOs */
	sta32x->gpiod_nreset = devm_gpiod_get_optional(dev, "reset",
						       GPIOD_OUT_LOW);
	if (IS_ERR(sta32x->gpiod_nreset))
		return PTR_ERR(sta32x->gpiod_nreset);

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
		dev_err(dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, sta32x);

	ret = devm_snd_soc_register_component(dev, &sta32x_component,
					      &sta32x_dai, 1);
	if (ret < 0)
		dev_err(dev, "Failed to register component (%d)\n", ret);

	return ret;
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
		.of_match_table = of_match_ptr(st32x_dt_ids),
	},
	.probe = sta32x_i2c_probe,
	.id_table = sta32x_i2c_id,
};

module_i2c_driver(sta32x_i2c_driver);

MODULE_DESCRIPTION("ASoC STA32X driver");
MODULE_AUTHOR("Johannes Stezenbach <js@sig21.net>");
MODULE_LICENSE("GPL");
