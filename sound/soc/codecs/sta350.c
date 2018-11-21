/*
 * Codec driver for ST STA350 2.1-channel high-efficiency digital audio system
 *
 * Copyright: 2014 Raumfeld GmbH
 * Author: Sven Brandau <info@brandau.biz>
 *
 * based on code from:
 *	Raumfeld GmbH
 *	  Johannes Stezenbach <js@sig21.net>
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
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <sound/sta350.h>
#include "sta350.h"

#define STA350_RATES (SNDRV_PCM_RATE_32000 | \
		      SNDRV_PCM_RATE_44100 | \
		      SNDRV_PCM_RATE_48000 | \
		      SNDRV_PCM_RATE_88200 | \
		      SNDRV_PCM_RATE_96000 | \
		      SNDRV_PCM_RATE_176400 | \
		      SNDRV_PCM_RATE_192000)

#define STA350_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE  | SNDRV_PCM_FMTBIT_S16_BE  | \
	 SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S18_3BE | \
	 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
	 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE | \
	 SNDRV_PCM_FMTBIT_S24_LE  | SNDRV_PCM_FMTBIT_S24_BE  | \
	 SNDRV_PCM_FMTBIT_S32_LE  | SNDRV_PCM_FMTBIT_S32_BE)

/* Power-up register defaults */
static const struct reg_default sta350_regs[] = {
	{  0x0, 0x63 },
	{  0x1, 0x80 },
	{  0x2, 0xdf },
	{  0x3, 0x40 },
	{  0x4, 0xc2 },
	{  0x5, 0x5c },
	{  0x6, 0x00 },
	{  0x7, 0xff },
	{  0x8, 0x60 },
	{  0x9, 0x60 },
	{  0xa, 0x60 },
	{  0xb, 0x00 },
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
	{ 0x27, 0x2a },
	{ 0x28, 0xc0 },
	{ 0x29, 0xf3 },
	{ 0x2a, 0x33 },
	{ 0x2b, 0x00 },
	{ 0x2c, 0x0c },
	{ 0x31, 0x00 },
	{ 0x36, 0x00 },
	{ 0x37, 0x00 },
	{ 0x38, 0x00 },
	{ 0x39, 0x01 },
	{ 0x3a, 0xee },
	{ 0x3b, 0xff },
	{ 0x3c, 0x7e },
	{ 0x3d, 0xc0 },
	{ 0x3e, 0x26 },
	{ 0x3f, 0x00 },
	{ 0x48, 0x00 },
	{ 0x49, 0x00 },
	{ 0x4a, 0x00 },
	{ 0x4b, 0x04 },
	{ 0x4c, 0x00 },
};

static const struct regmap_range sta350_write_regs_range[] = {
	regmap_reg_range(STA350_CONFA,  STA350_AUTO2),
	regmap_reg_range(STA350_C1CFG,  STA350_FDRC2),
	regmap_reg_range(STA350_EQCFG,  STA350_EVOLRES),
	regmap_reg_range(STA350_NSHAPE, STA350_MISC2),
};

static const struct regmap_range sta350_read_regs_range[] = {
	regmap_reg_range(STA350_CONFA,  STA350_AUTO2),
	regmap_reg_range(STA350_C1CFG,  STA350_STATUS),
	regmap_reg_range(STA350_EQCFG,  STA350_EVOLRES),
	regmap_reg_range(STA350_NSHAPE, STA350_MISC2),
};

static const struct regmap_range sta350_volatile_regs_range[] = {
	regmap_reg_range(STA350_CFADDR2, STA350_CFUD),
	regmap_reg_range(STA350_STATUS,  STA350_STATUS),
};

static const struct regmap_access_table sta350_write_regs = {
	.yes_ranges =	sta350_write_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta350_write_regs_range),
};

static const struct regmap_access_table sta350_read_regs = {
	.yes_ranges =	sta350_read_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta350_read_regs_range),
};

static const struct regmap_access_table sta350_volatile_regs = {
	.yes_ranges =	sta350_volatile_regs_range,
	.n_yes_ranges =	ARRAY_SIZE(sta350_volatile_regs_range),
};

/* regulator power supply names */
static const char * const sta350_supply_names[] = {
	"vdd-dig",	/* digital supply, 3.3V */
	"vdd-pll",	/* pll supply, 3.3V */
	"vcc"		/* power amp supply, 5V - 26V */
};

/* codec private data */
struct sta350_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[ARRAY_SIZE(sta350_supply_names)];
	struct sta350_platform_data *pdata;

	unsigned int mclk;
	unsigned int format;

	u32 coef_shadow[STA350_COEF_COUNT];
	int shutdown;

	struct gpio_desc *gpiod_nreset;
	struct gpio_desc *gpiod_power_down;

	struct mutex coeff_lock;
};

static const DECLARE_TLV_DB_SCALE(mvol_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(chvol_tlv, -7950, 50, 1);
static const DECLARE_TLV_DB_SCALE(tone_tlv, -1200, 200, 0);

static const char * const sta350_drc_ac[] = {
	"Anti-Clipping", "Dynamic Range Compression"
};
static const char * const sta350_auto_gc_mode[] = {
	"User", "AC no clipping", "AC limited clipping (10%)",
	"DRC nighttime listening mode"
};
static const char * const sta350_auto_xo_mode[] = {
	"User", "80Hz", "100Hz", "120Hz", "140Hz", "160Hz", "180Hz",
	"200Hz", "220Hz", "240Hz", "260Hz", "280Hz", "300Hz", "320Hz",
	"340Hz", "360Hz"
};
static const char * const sta350_binary_output[] = {
	"FFX 3-state output - normal operation", "Binary output"
};
static const char * const sta350_limiter_select[] = {
	"Limiter Disabled", "Limiter #1", "Limiter #2"
};
static const char * const sta350_limiter_attack_rate[] = {
	"3.1584", "2.7072", "2.2560", "1.8048", "1.3536", "0.9024",
	"0.4512", "0.2256", "0.1504", "0.1123", "0.0902", "0.0752",
	"0.0645", "0.0564", "0.0501", "0.0451"
};
static const char * const sta350_limiter_release_rate[] = {
	"0.5116", "0.1370", "0.0744", "0.0499", "0.0360", "0.0299",
	"0.0264", "0.0208", "0.0198", "0.0172", "0.0147", "0.0137",
	"0.0134", "0.0117", "0.0110", "0.0104"
};
static const char * const sta350_noise_shaper_type[] = {
	"Third order", "Fourth order"
};

static DECLARE_TLV_DB_RANGE(sta350_limiter_ac_attack_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(300, 100, 0),
);

static DECLARE_TLV_DB_RANGE(sta350_limiter_ac_release_tlv,
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2900, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-2000, 0, 0),
	3, 8, TLV_DB_SCALE_ITEM(-1400, 200, 0),
	8, 16, TLV_DB_SCALE_ITEM(-700, 100, 0),
);

static DECLARE_TLV_DB_RANGE(sta350_limiter_drc_attack_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-3100, 200, 0),
	8, 13, TLV_DB_SCALE_ITEM(-1600, 100, 0),
	14, 16, TLV_DB_SCALE_ITEM(-1000, 300, 0),
);

static DECLARE_TLV_DB_RANGE(sta350_limiter_drc_release_tlv,
	0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 0),
	1, 2, TLV_DB_SCALE_ITEM(-3800, 200, 0),
	3, 4, TLV_DB_SCALE_ITEM(-3300, 200, 0),
	5, 12, TLV_DB_SCALE_ITEM(-3000, 200, 0),
	13, 16, TLV_DB_SCALE_ITEM(-1500, 300, 0),
);

static SOC_ENUM_SINGLE_DECL(sta350_drc_ac_enum,
			    STA350_CONFD, STA350_CONFD_DRC_SHIFT,
			    sta350_drc_ac);
static SOC_ENUM_SINGLE_DECL(sta350_noise_shaper_enum,
			    STA350_CONFE, STA350_CONFE_NSBW_SHIFT,
			    sta350_noise_shaper_type);
static SOC_ENUM_SINGLE_DECL(sta350_auto_gc_enum,
			    STA350_AUTO1, STA350_AUTO1_AMGC_SHIFT,
			    sta350_auto_gc_mode);
static SOC_ENUM_SINGLE_DECL(sta350_auto_xo_enum,
			    STA350_AUTO2, STA350_AUTO2_XO_SHIFT,
			    sta350_auto_xo_mode);
static SOC_ENUM_SINGLE_DECL(sta350_binary_output_ch1_enum,
			    STA350_C1CFG, STA350_CxCFG_BO_SHIFT,
			    sta350_binary_output);
static SOC_ENUM_SINGLE_DECL(sta350_binary_output_ch2_enum,
			    STA350_C2CFG, STA350_CxCFG_BO_SHIFT,
			    sta350_binary_output);
static SOC_ENUM_SINGLE_DECL(sta350_binary_output_ch3_enum,
			    STA350_C3CFG, STA350_CxCFG_BO_SHIFT,
			    sta350_binary_output);
static SOC_ENUM_SINGLE_DECL(sta350_limiter_ch1_enum,
			    STA350_C1CFG, STA350_CxCFG_LS_SHIFT,
			    sta350_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta350_limiter_ch2_enum,
			    STA350_C2CFG, STA350_CxCFG_LS_SHIFT,
			    sta350_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta350_limiter_ch3_enum,
			    STA350_C3CFG, STA350_CxCFG_LS_SHIFT,
			    sta350_limiter_select);
static SOC_ENUM_SINGLE_DECL(sta350_limiter1_attack_rate_enum,
			    STA350_L1AR, STA350_LxA_SHIFT,
			    sta350_limiter_attack_rate);
static SOC_ENUM_SINGLE_DECL(sta350_limiter2_attack_rate_enum,
			    STA350_L2AR, STA350_LxA_SHIFT,
			    sta350_limiter_attack_rate);
static SOC_ENUM_SINGLE_DECL(sta350_limiter1_release_rate_enum,
			    STA350_L1AR, STA350_LxR_SHIFT,
			    sta350_limiter_release_rate);
static SOC_ENUM_SINGLE_DECL(sta350_limiter2_release_rate_enum,
			    STA350_L2AR, STA350_LxR_SHIFT,
			    sta350_limiter_release_rate);

/*
 * byte array controls for setting biquad, mixer, scaling coefficients;
 * for biquads all five coefficients need to be set in one go,
 * mixer and pre/postscale coefs can be set individually;
 * each coef is 24bit, the bytes are ordered in the same way
 * as given in the STA350 data sheet (big endian; b1, b2, a1, a2, b0)
 */

static int sta350_coefficient_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	int numcoef = kcontrol->private_value >> 16;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 3 * numcoef;
	return 0;
}

static int sta350_coefficient_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud, val;
	int i, ret = 0;

	mutex_lock(&sta350->coeff_lock);

	/* preserve reserved bits in STA350_CFUD */
	regmap_read(sta350->regmap, STA350_CFUD, &cfud);
	cfud &= 0xf0;
	/*
	 * chip documentation does not say if the bits are self clearing,
	 * so do it explicitly
	 */
	regmap_write(sta350->regmap, STA350_CFUD, cfud);

	regmap_write(sta350->regmap, STA350_CFADDR2, index);
	if (numcoef == 1) {
		regmap_write(sta350->regmap, STA350_CFUD, cfud | 0x04);
	} else if (numcoef == 5) {
		regmap_write(sta350->regmap, STA350_CFUD, cfud | 0x08);
	} else {
		ret = -EINVAL;
		goto exit_unlock;
	}

	for (i = 0; i < 3 * numcoef; i++) {
		regmap_read(sta350->regmap, STA350_B1CF1 + i, &val);
		ucontrol->value.bytes.data[i] = val;
	}

exit_unlock:
	mutex_unlock(&sta350->coeff_lock);

	return ret;
}

static int sta350_coefficient_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	int numcoef = kcontrol->private_value >> 16;
	int index = kcontrol->private_value & 0xffff;
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA350_CFUD */
	regmap_read(sta350->regmap, STA350_CFUD, &cfud);
	cfud &= 0xf0;
	/*
	 * chip documentation does not say if the bits are self clearing,
	 * so do it explicitly
	 */
	regmap_write(sta350->regmap, STA350_CFUD, cfud);

	regmap_write(sta350->regmap, STA350_CFADDR2, index);
	for (i = 0; i < numcoef && (index + i < STA350_COEF_COUNT); i++)
		sta350->coef_shadow[index + i] =
			  (ucontrol->value.bytes.data[3 * i] << 16)
			| (ucontrol->value.bytes.data[3 * i + 1] << 8)
			| (ucontrol->value.bytes.data[3 * i + 2]);
	for (i = 0; i < 3 * numcoef; i++)
		regmap_write(sta350->regmap, STA350_B1CF1 + i,
			     ucontrol->value.bytes.data[i]);
	if (numcoef == 1)
		regmap_write(sta350->regmap, STA350_CFUD, cfud | 0x01);
	else if (numcoef == 5)
		regmap_write(sta350->regmap, STA350_CFUD, cfud | 0x02);
	else
		return -EINVAL;

	return 0;
}

static int sta350_sync_coef_shadow(struct snd_soc_component *component)
{
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	unsigned int cfud;
	int i;

	/* preserve reserved bits in STA350_CFUD */
	regmap_read(sta350->regmap, STA350_CFUD, &cfud);
	cfud &= 0xf0;

	for (i = 0; i < STA350_COEF_COUNT; i++) {
		regmap_write(sta350->regmap, STA350_CFADDR2, i);
		regmap_write(sta350->regmap, STA350_B1CF1,
			     (sta350->coef_shadow[i] >> 16) & 0xff);
		regmap_write(sta350->regmap, STA350_B1CF2,
			     (sta350->coef_shadow[i] >> 8) & 0xff);
		regmap_write(sta350->regmap, STA350_B1CF3,
			     (sta350->coef_shadow[i]) & 0xff);
		/*
		 * chip documentation does not say if the bits are
		 * self-clearing, so do it explicitly
		 */
		regmap_write(sta350->regmap, STA350_CFUD, cfud);
		regmap_write(sta350->regmap, STA350_CFUD, cfud | 0x01);
	}
	return 0;
}

static int sta350_cache_sync(struct snd_soc_component *component)
{
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	unsigned int mute;
	int rc;

	/* mute during register sync */
	regmap_read(sta350->regmap, STA350_CFUD, &mute);
	regmap_write(sta350->regmap, STA350_MMUTE, mute | STA350_MMUTE_MMUTE);
	sta350_sync_coef_shadow(component);
	rc = regcache_sync(sta350->regmap);
	regmap_write(sta350->regmap, STA350_MMUTE, mute);
	return rc;
}

#define SINGLE_COEF(xname, index) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = sta350_coefficient_info, \
	.get = sta350_coefficient_get,\
	.put = sta350_coefficient_put, \
	.private_value = index | (1 << 16) }

#define BIQUAD_COEFS(xname, index) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = sta350_coefficient_info, \
	.get = sta350_coefficient_get,\
	.put = sta350_coefficient_put, \
	.private_value = index | (5 << 16) }

static const struct snd_kcontrol_new sta350_snd_controls[] = {
SOC_SINGLE_TLV("Master Volume", STA350_MVOL, 0, 0xff, 1, mvol_tlv),
/* VOL */
SOC_SINGLE_TLV("Ch1 Volume", STA350_C1VOL, 0, 0xff, 1, chvol_tlv),
SOC_SINGLE_TLV("Ch2 Volume", STA350_C2VOL, 0, 0xff, 1, chvol_tlv),
SOC_SINGLE_TLV("Ch3 Volume", STA350_C3VOL, 0, 0xff, 1, chvol_tlv),
/* CONFD */
SOC_SINGLE("High Pass Filter Bypass Switch",
	   STA350_CONFD, STA350_CONFD_HPB_SHIFT, 1, 1),
SOC_SINGLE("De-emphasis Filter Switch",
	   STA350_CONFD, STA350_CONFD_DEMP_SHIFT, 1, 0),
SOC_SINGLE("DSP Bypass Switch",
	   STA350_CONFD, STA350_CONFD_DSPB_SHIFT, 1, 0),
SOC_SINGLE("Post-scale Link Switch",
	   STA350_CONFD, STA350_CONFD_PSL_SHIFT, 1, 0),
SOC_SINGLE("Biquad Coefficient Link Switch",
	   STA350_CONFD, STA350_CONFD_BQL_SHIFT, 1, 0),
SOC_ENUM("Compressor/Limiter Switch", sta350_drc_ac_enum),
SOC_ENUM("Noise Shaper Bandwidth", sta350_noise_shaper_enum),
SOC_SINGLE("Zero-detect Mute Enable Switch",
	   STA350_CONFD, STA350_CONFD_ZDE_SHIFT, 1, 0),
SOC_SINGLE("Submix Mode Switch",
	   STA350_CONFD, STA350_CONFD_SME_SHIFT, 1, 0),
/* CONFE */
SOC_SINGLE("Zero Cross Switch", STA350_CONFE, STA350_CONFE_ZCE_SHIFT, 1, 0),
SOC_SINGLE("Soft Ramp Switch", STA350_CONFE, STA350_CONFE_SVE_SHIFT, 1, 0),
/* MUTE */
SOC_SINGLE("Master Switch", STA350_MMUTE, STA350_MMUTE_MMUTE_SHIFT, 1, 1),
SOC_SINGLE("Ch1 Switch", STA350_MMUTE, STA350_MMUTE_C1M_SHIFT, 1, 1),
SOC_SINGLE("Ch2 Switch", STA350_MMUTE, STA350_MMUTE_C2M_SHIFT, 1, 1),
SOC_SINGLE("Ch3 Switch", STA350_MMUTE, STA350_MMUTE_C3M_SHIFT, 1, 1),
/* AUTOx */
SOC_ENUM("Automode GC", sta350_auto_gc_enum),
SOC_ENUM("Automode XO", sta350_auto_xo_enum),
/* CxCFG */
SOC_SINGLE("Ch1 Tone Control Bypass Switch",
	   STA350_C1CFG, STA350_CxCFG_TCB_SHIFT, 1, 0),
SOC_SINGLE("Ch2 Tone Control Bypass Switch",
	   STA350_C2CFG, STA350_CxCFG_TCB_SHIFT, 1, 0),
SOC_SINGLE("Ch1 EQ Bypass Switch",
	   STA350_C1CFG, STA350_CxCFG_EQBP_SHIFT, 1, 0),
SOC_SINGLE("Ch2 EQ Bypass Switch",
	   STA350_C2CFG, STA350_CxCFG_EQBP_SHIFT, 1, 0),
SOC_SINGLE("Ch1 Master Volume Bypass Switch",
	   STA350_C1CFG, STA350_CxCFG_VBP_SHIFT, 1, 0),
SOC_SINGLE("Ch2 Master Volume Bypass Switch",
	   STA350_C1CFG, STA350_CxCFG_VBP_SHIFT, 1, 0),
SOC_SINGLE("Ch3 Master Volume Bypass Switch",
	   STA350_C1CFG, STA350_CxCFG_VBP_SHIFT, 1, 0),
SOC_ENUM("Ch1 Binary Output Select", sta350_binary_output_ch1_enum),
SOC_ENUM("Ch2 Binary Output Select", sta350_binary_output_ch2_enum),
SOC_ENUM("Ch3 Binary Output Select", sta350_binary_output_ch3_enum),
SOC_ENUM("Ch1 Limiter Select", sta350_limiter_ch1_enum),
SOC_ENUM("Ch2 Limiter Select", sta350_limiter_ch2_enum),
SOC_ENUM("Ch3 Limiter Select", sta350_limiter_ch3_enum),
/* TONE */
SOC_SINGLE_RANGE_TLV("Bass Tone Control Volume",
		     STA350_TONE, STA350_TONE_BTC_SHIFT, 1, 13, 0, tone_tlv),
SOC_SINGLE_RANGE_TLV("Treble Tone Control Volume",
		     STA350_TONE, STA350_TONE_TTC_SHIFT, 1, 13, 0, tone_tlv),
SOC_ENUM("Limiter1 Attack Rate (dB/ms)", sta350_limiter1_attack_rate_enum),
SOC_ENUM("Limiter2 Attack Rate (dB/ms)", sta350_limiter2_attack_rate_enum),
SOC_ENUM("Limiter1 Release Rate (dB/ms)", sta350_limiter1_release_rate_enum),
SOC_ENUM("Limiter2 Release Rate (dB/ms)", sta350_limiter2_release_rate_enum),

/*
 * depending on mode, the attack/release thresholds have
 * two different enum definitions; provide both
 */
SOC_SINGLE_TLV("Limiter1 Attack Threshold (AC Mode)",
	       STA350_L1ATRT, STA350_LxA_SHIFT,
	       16, 0, sta350_limiter_ac_attack_tlv),
SOC_SINGLE_TLV("Limiter2 Attack Threshold (AC Mode)",
	       STA350_L2ATRT, STA350_LxA_SHIFT,
	       16, 0, sta350_limiter_ac_attack_tlv),
SOC_SINGLE_TLV("Limiter1 Release Threshold (AC Mode)",
	       STA350_L1ATRT, STA350_LxR_SHIFT,
	       16, 0, sta350_limiter_ac_release_tlv),
SOC_SINGLE_TLV("Limiter2 Release Threshold (AC Mode)",
	       STA350_L2ATRT, STA350_LxR_SHIFT,
	       16, 0, sta350_limiter_ac_release_tlv),
SOC_SINGLE_TLV("Limiter1 Attack Threshold (DRC Mode)",
	       STA350_L1ATRT, STA350_LxA_SHIFT,
	       16, 0, sta350_limiter_drc_attack_tlv),
SOC_SINGLE_TLV("Limiter2 Attack Threshold (DRC Mode)",
	       STA350_L2ATRT, STA350_LxA_SHIFT,
	       16, 0, sta350_limiter_drc_attack_tlv),
SOC_SINGLE_TLV("Limiter1 Release Threshold (DRC Mode)",
	       STA350_L1ATRT, STA350_LxR_SHIFT,
	       16, 0, sta350_limiter_drc_release_tlv),
SOC_SINGLE_TLV("Limiter2 Release Threshold (DRC Mode)",
	       STA350_L2ATRT, STA350_LxR_SHIFT,
	       16, 0, sta350_limiter_drc_release_tlv),

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

static const struct snd_soc_dapm_widget sta350_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LEFT"),
SND_SOC_DAPM_OUTPUT("RIGHT"),
SND_SOC_DAPM_OUTPUT("SUB"),
};

static const struct snd_soc_dapm_route sta350_dapm_routes[] = {
	{ "LEFT", NULL, "DAC" },
	{ "RIGHT", NULL, "DAC" },
	{ "SUB", NULL, "DAC" },
	{ "DAC", NULL, "Playback" },
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
static int mcs_ratio_table[3][6] = {
	{ 768, 512, 384, 256, 128, 576 },
	{ 384, 256, 192, 128,  64,   0 },
	{ 192, 128,  96,  64,  32,   0 },
};

/**
 * sta350_set_dai_sysclk - configure MCLK
 * @codec_dai: the codec DAI
 * @clk_id: the clock ID (ignored)
 * @freq: the MCLK input frequency
 * @dir: the clock direction (ignored)
 *
 * The value of MCLK is used to determine which sample rates are supported
 * by the STA350, based on the mcs_ratio_table.
 *
 * This function must be called by the machine driver's 'startup' function,
 * otherwise the list of supported sample rates will not be available in
 * time for ALSA.
 */
static int sta350_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "mclk=%u\n", freq);
	sta350->mclk = freq;

	return 0;
}

/**
 * sta350_set_dai_fmt - configure the codec for the selected audio format
 * @codec_dai: the codec DAI
 * @fmt: a SND_SOC_DAIFMT_x value indicating the data format
 *
 * This function takes a bitmask of SND_SOC_DAIFMT_x bits and programs the
 * codec accordingly.
 */
static int sta350_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	unsigned int confb = 0;

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
		sta350->format = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		confb |= STA350_CONFB_C2IM;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		confb |= STA350_CONFB_C1IM;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(sta350->regmap, STA350_CONFB,
				  STA350_CONFB_C1IM | STA350_CONFB_C2IM, confb);
}

/**
 * sta350_hw_params - program the STA350 with the given hardware parameters.
 * @substream: the audio stream
 * @params: the hardware parameters to set
 * @dai: the SOC DAI (ignored)
 *
 * This function programs the hardware with the values provided.
 * Specifically, the sample rate and the data format.
 */
static int sta350_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	int i, mcs = -EINVAL, ir = -EINVAL;
	unsigned int confa, confb;
	unsigned int rate, ratio;
	int ret;

	if (!sta350->mclk) {
		dev_err(component->dev,
			"sta350->mclk is unset. Unable to determine ratio\n");
		return -EIO;
	}

	rate = params_rate(params);
	ratio = sta350->mclk / rate;
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

	confa = (ir << STA350_CONFA_IR_SHIFT) |
		(mcs << STA350_CONFA_MCS_SHIFT);
	confb = 0;

	switch (params_width(params)) {
	case 24:
		dev_dbg(component->dev, "24bit\n");
		/* fall through */
	case 32:
		dev_dbg(component->dev, "24bit or 32bit\n");
		switch (sta350->format) {
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
		switch (sta350->format) {
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
		switch (sta350->format) {
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
		switch (sta350->format) {
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

	ret = regmap_update_bits(sta350->regmap, STA350_CONFA,
				 STA350_CONFA_MCS_MASK | STA350_CONFA_IR_MASK,
				 confa);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(sta350->regmap, STA350_CONFB,
				 STA350_CONFB_SAI_MASK | STA350_CONFB_SAIFB,
				 confb);
	if (ret < 0)
		return ret;

	return 0;
}

static int sta350_startup_sequence(struct sta350_priv *sta350)
{
	if (sta350->gpiod_power_down)
		gpiod_set_value(sta350->gpiod_power_down, 1);

	if (sta350->gpiod_nreset) {
		gpiod_set_value(sta350->gpiod_nreset, 0);
		mdelay(1);
		gpiod_set_value(sta350->gpiod_nreset, 1);
		mdelay(1);
	}

	return 0;
}

/**
 * sta350_set_bias_level - DAPM callback
 * @component: the component device
 * @level: DAPM power level
 *
 * This is called by ALSA to put the component into low power mode
 * or to wake it up.  If the component is powered off completely
 * all registers must be restored after power on.
 */
static int sta350_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	int ret;

	dev_dbg(component->dev, "level = %d\n", level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Full power on */
		regmap_update_bits(sta350->regmap, STA350_CONFF,
				   STA350_CONFF_PWDN | STA350_CONFF_EAPD,
				   STA350_CONFF_PWDN | STA350_CONFF_EAPD);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(
				ARRAY_SIZE(sta350->supplies),
				sta350->supplies);
			if (ret < 0) {
				dev_err(component->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}
			sta350_startup_sequence(sta350);
			sta350_cache_sync(component);
		}

		/* Power down */
		regmap_update_bits(sta350->regmap, STA350_CONFF,
				   STA350_CONFF_PWDN | STA350_CONFF_EAPD,
				   0);

		break;

	case SND_SOC_BIAS_OFF:
		/* The chip runs through the power down sequence for us */
		regmap_update_bits(sta350->regmap, STA350_CONFF,
				   STA350_CONFF_PWDN | STA350_CONFF_EAPD, 0);

		/* power down: low */
		if (sta350->gpiod_power_down)
			gpiod_set_value(sta350->gpiod_power_down, 0);

		if (sta350->gpiod_nreset)
			gpiod_set_value(sta350->gpiod_nreset, 0);

		regulator_bulk_disable(ARRAY_SIZE(sta350->supplies),
				       sta350->supplies);
		break;
	}
	return 0;
}

static const struct snd_soc_dai_ops sta350_dai_ops = {
	.hw_params	= sta350_hw_params,
	.set_sysclk	= sta350_set_dai_sysclk,
	.set_fmt	= sta350_set_dai_fmt,
};

static struct snd_soc_dai_driver sta350_dai = {
	.name = "sta350-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = STA350_RATES,
		.formats = STA350_FORMATS,
	},
	.ops = &sta350_dai_ops,
};

static int sta350_probe(struct snd_soc_component *component)
{
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);
	struct sta350_platform_data *pdata = sta350->pdata;
	int i, ret = 0, thermal = 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(sta350->supplies),
				    sta350->supplies);
	if (ret < 0) {
		dev_err(component->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = sta350_startup_sequence(sta350);
	if (ret < 0) {
		dev_err(component->dev, "Failed to startup device\n");
		return ret;
	}

	/* CONFA */
	if (!pdata->thermal_warning_recovery)
		thermal |= STA350_CONFA_TWAB;
	if (!pdata->thermal_warning_adjustment)
		thermal |= STA350_CONFA_TWRB;
	if (!pdata->fault_detect_recovery)
		thermal |= STA350_CONFA_FDRB;
	regmap_update_bits(sta350->regmap, STA350_CONFA,
			   STA350_CONFA_TWAB | STA350_CONFA_TWRB |
			   STA350_CONFA_FDRB,
			   thermal);

	/* CONFC */
	regmap_update_bits(sta350->regmap, STA350_CONFC,
			   STA350_CONFC_OM_MASK,
			   pdata->ffx_power_output_mode
				<< STA350_CONFC_OM_SHIFT);
	regmap_update_bits(sta350->regmap, STA350_CONFC,
			   STA350_CONFC_CSZ_MASK,
			   pdata->drop_compensation_ns
				<< STA350_CONFC_CSZ_SHIFT);
	regmap_update_bits(sta350->regmap,
			   STA350_CONFC,
			   STA350_CONFC_OCRB,
			   pdata->oc_warning_adjustment ?
				STA350_CONFC_OCRB : 0);

	/* CONFE */
	regmap_update_bits(sta350->regmap, STA350_CONFE,
			   STA350_CONFE_MPCV,
			   pdata->max_power_use_mpcc ?
				STA350_CONFE_MPCV : 0);
	regmap_update_bits(sta350->regmap, STA350_CONFE,
			   STA350_CONFE_MPC,
			   pdata->max_power_correction ?
				STA350_CONFE_MPC : 0);
	regmap_update_bits(sta350->regmap, STA350_CONFE,
			   STA350_CONFE_AME,
			   pdata->am_reduction_mode ?
				STA350_CONFE_AME : 0);
	regmap_update_bits(sta350->regmap, STA350_CONFE,
			   STA350_CONFE_PWMS,
			   pdata->odd_pwm_speed_mode ?
				STA350_CONFE_PWMS : 0);
	regmap_update_bits(sta350->regmap, STA350_CONFE,
			   STA350_CONFE_DCCV,
			   pdata->distortion_compensation ?
				STA350_CONFE_DCCV : 0);
	/*  CONFF */
	regmap_update_bits(sta350->regmap, STA350_CONFF,
			   STA350_CONFF_IDE,
			   pdata->invalid_input_detect_mute ?
				STA350_CONFF_IDE : 0);
	regmap_update_bits(sta350->regmap, STA350_CONFF,
			   STA350_CONFF_OCFG_MASK,
			   pdata->output_conf
				<< STA350_CONFF_OCFG_SHIFT);

	/* channel to output mapping */
	regmap_update_bits(sta350->regmap, STA350_C1CFG,
			   STA350_CxCFG_OM_MASK,
			   pdata->ch1_output_mapping
				<< STA350_CxCFG_OM_SHIFT);
	regmap_update_bits(sta350->regmap, STA350_C2CFG,
			   STA350_CxCFG_OM_MASK,
			   pdata->ch2_output_mapping
				<< STA350_CxCFG_OM_SHIFT);
	regmap_update_bits(sta350->regmap, STA350_C3CFG,
			   STA350_CxCFG_OM_MASK,
			   pdata->ch3_output_mapping
				<< STA350_CxCFG_OM_SHIFT);

	/* miscellaneous registers */
	regmap_update_bits(sta350->regmap, STA350_MISC1,
			   STA350_MISC1_CPWMEN,
			   pdata->activate_mute_output ?
				STA350_MISC1_CPWMEN : 0);
	regmap_update_bits(sta350->regmap, STA350_MISC1,
			   STA350_MISC1_BRIDGOFF,
			   pdata->bridge_immediate_off ?
				STA350_MISC1_BRIDGOFF : 0);
	regmap_update_bits(sta350->regmap, STA350_MISC1,
			   STA350_MISC1_NSHHPEN,
			   pdata->noise_shape_dc_cut ?
				STA350_MISC1_NSHHPEN : 0);
	regmap_update_bits(sta350->regmap, STA350_MISC1,
			   STA350_MISC1_RPDNEN,
			   pdata->powerdown_master_vol ?
				STA350_MISC1_RPDNEN: 0);

	regmap_update_bits(sta350->regmap, STA350_MISC2,
			   STA350_MISC2_PNDLSL_MASK,
			   pdata->powerdown_delay_divider
				<< STA350_MISC2_PNDLSL_SHIFT);

	/* initialize coefficient shadow RAM with reset values */
	for (i = 4; i <= 49; i += 5)
		sta350->coef_shadow[i] = 0x400000;
	for (i = 50; i <= 54; i++)
		sta350->coef_shadow[i] = 0x7fffff;
	sta350->coef_shadow[55] = 0x5a9df7;
	sta350->coef_shadow[56] = 0x7fffff;
	sta350->coef_shadow[59] = 0x7fffff;
	sta350->coef_shadow[60] = 0x400000;
	sta350->coef_shadow[61] = 0x400000;

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);
	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(sta350->supplies), sta350->supplies);

	return 0;
}

static void sta350_remove(struct snd_soc_component *component)
{
	struct sta350_priv *sta350 = snd_soc_component_get_drvdata(component);

	regulator_bulk_disable(ARRAY_SIZE(sta350->supplies), sta350->supplies);
}

static const struct snd_soc_component_driver sta350_component = {
	.probe			= sta350_probe,
	.remove			= sta350_remove,
	.set_bias_level		= sta350_set_bias_level,
	.controls		= sta350_snd_controls,
	.num_controls		= ARRAY_SIZE(sta350_snd_controls),
	.dapm_widgets		= sta350_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sta350_dapm_widgets),
	.dapm_routes		= sta350_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(sta350_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config sta350_regmap = {
	.reg_bits =		8,
	.val_bits =		8,
	.max_register =		STA350_MISC2,
	.reg_defaults =		sta350_regs,
	.num_reg_defaults =	ARRAY_SIZE(sta350_regs),
	.cache_type =		REGCACHE_RBTREE,
	.wr_table =		&sta350_write_regs,
	.rd_table =		&sta350_read_regs,
	.volatile_table =	&sta350_volatile_regs,
};

#ifdef CONFIG_OF
static const struct of_device_id st350_dt_ids[] = {
	{ .compatible = "st,sta350", },
	{ }
};
MODULE_DEVICE_TABLE(of, st350_dt_ids);

static const char * const sta350_ffx_modes[] = {
	[STA350_FFX_PM_DROP_COMP]		= "drop-compensation",
	[STA350_FFX_PM_TAPERED_COMP]		= "tapered-compensation",
	[STA350_FFX_PM_FULL_POWER]		= "full-power-mode",
	[STA350_FFX_PM_VARIABLE_DROP_COMP]	= "variable-drop-compensation",
};

static int sta350_probe_dt(struct device *dev, struct sta350_priv *sta350)
{
	struct device_node *np = dev->of_node;
	struct sta350_platform_data *pdata;
	const char *ffx_power_mode;
	u16 tmp;
	u8 tmp8;

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

	if (of_get_property(np, "st,thermal-warning-recovery", NULL))
		pdata->thermal_warning_recovery = 1;
	if (of_get_property(np, "st,thermal-warning-adjustment", NULL))
		pdata->thermal_warning_adjustment = 1;
	if (of_get_property(np, "st,fault-detect-recovery", NULL))
		pdata->fault_detect_recovery = 1;

	pdata->ffx_power_output_mode = STA350_FFX_PM_VARIABLE_DROP_COMP;
	if (!of_property_read_string(np, "st,ffx-power-output-mode",
				     &ffx_power_mode)) {
		int i, mode = -EINVAL;

		for (i = 0; i < ARRAY_SIZE(sta350_ffx_modes); i++)
			if (!strcasecmp(ffx_power_mode, sta350_ffx_modes[i]))
				mode = i;

		if (mode < 0)
			dev_warn(dev, "Unsupported ffx output mode: %s\n",
				 ffx_power_mode);
		else
			pdata->ffx_power_output_mode = mode;
	}

	tmp = 140;
	of_property_read_u16(np, "st,drop-compensation-ns", &tmp);
	pdata->drop_compensation_ns = clamp_t(u16, tmp, 0, 300) / 20;

	if (of_get_property(np, "st,overcurrent-warning-adjustment", NULL))
		pdata->oc_warning_adjustment = 1;

	/* CONFE */
	if (of_get_property(np, "st,max-power-use-mpcc", NULL))
		pdata->max_power_use_mpcc = 1;

	if (of_get_property(np, "st,max-power-correction", NULL))
		pdata->max_power_correction = 1;

	if (of_get_property(np, "st,am-reduction-mode", NULL))
		pdata->am_reduction_mode = 1;

	if (of_get_property(np, "st,odd-pwm-speed-mode", NULL))
		pdata->odd_pwm_speed_mode = 1;

	if (of_get_property(np, "st,distortion-compensation", NULL))
		pdata->distortion_compensation = 1;

	/* CONFF */
	if (of_get_property(np, "st,invalid-input-detect-mute", NULL))
		pdata->invalid_input_detect_mute = 1;

	/* MISC */
	if (of_get_property(np, "st,activate-mute-output", NULL))
		pdata->activate_mute_output = 1;

	if (of_get_property(np, "st,bridge-immediate-off", NULL))
		pdata->bridge_immediate_off = 1;

	if (of_get_property(np, "st,noise-shape-dc-cut", NULL))
		pdata->noise_shape_dc_cut = 1;

	if (of_get_property(np, "st,powerdown-master-volume", NULL))
		pdata->powerdown_master_vol = 1;

	if (!of_property_read_u8(np, "st,powerdown-delay-divider", &tmp8)) {
		if (is_power_of_2(tmp8) && tmp8 >= 1 && tmp8 <= 128)
			pdata->powerdown_delay_divider = ilog2(tmp8);
		else
			dev_warn(dev, "Unsupported powerdown delay divider %d\n",
				 tmp8);
	}

	sta350->pdata = pdata;

	return 0;
}
#endif

static int sta350_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct sta350_priv *sta350;
	int ret, i;

	sta350 = devm_kzalloc(dev, sizeof(struct sta350_priv), GFP_KERNEL);
	if (!sta350)
		return -ENOMEM;

	mutex_init(&sta350->coeff_lock);
	sta350->pdata = dev_get_platdata(dev);

#ifdef CONFIG_OF
	if (dev->of_node) {
		ret = sta350_probe_dt(dev, sta350);
		if (ret < 0)
			return ret;
	}
#endif

	/* GPIOs */
	sta350->gpiod_nreset = devm_gpiod_get_optional(dev, "reset",
						       GPIOD_OUT_LOW);
	if (IS_ERR(sta350->gpiod_nreset))
		return PTR_ERR(sta350->gpiod_nreset);

	sta350->gpiod_power_down = devm_gpiod_get_optional(dev, "power-down",
							   GPIOD_OUT_LOW);
	if (IS_ERR(sta350->gpiod_power_down))
		return PTR_ERR(sta350->gpiod_power_down);

	/* regulators */
	for (i = 0; i < ARRAY_SIZE(sta350->supplies); i++)
		sta350->supplies[i].supply = sta350_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sta350->supplies),
				      sta350->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	sta350->regmap = devm_regmap_init_i2c(i2c, &sta350_regmap);
	if (IS_ERR(sta350->regmap)) {
		ret = PTR_ERR(sta350->regmap);
		dev_err(dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, sta350);

	ret = devm_snd_soc_register_component(dev, &sta350_component, &sta350_dai, 1);
	if (ret < 0)
		dev_err(dev, "Failed to register component (%d)\n", ret);

	return ret;
}

static int sta350_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id sta350_i2c_id[] = {
	{ "sta350", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sta350_i2c_id);

static struct i2c_driver sta350_i2c_driver = {
	.driver = {
		.name = "sta350",
		.of_match_table = of_match_ptr(st350_dt_ids),
	},
	.probe =    sta350_i2c_probe,
	.remove =   sta350_i2c_remove,
	.id_table = sta350_i2c_id,
};

module_i2c_driver(sta350_i2c_driver);

MODULE_DESCRIPTION("ASoC STA350 driver");
MODULE_AUTHOR("Sven Brandau <info@brandau.biz>");
MODULE_LICENSE("GPL");
