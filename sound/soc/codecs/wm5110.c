/*
 * wm5110.c  --  WM5110 ALSA SoC Audio driver
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"
#include "wm_adsp.h"
#include "wm5110.h"

#define WM5110_NUM_ADSP 4

struct wm5110_priv {
	struct arizona_priv core;
	struct arizona_fll fll[2];

	unsigned int in_value;
	int in_pre_pending;
	int in_post_pending;

	unsigned int in_pga_cache[6];
};

static const struct wm_adsp_region wm5110_dsp1_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x100000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x180000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x190000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x1a8000 },
};

static const struct wm_adsp_region wm5110_dsp2_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x200000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x280000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x290000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x2a8000 },
};

static const struct wm_adsp_region wm5110_dsp3_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x300000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x380000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x390000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x3a8000 },
};

static const struct wm_adsp_region wm5110_dsp4_regions[] = {
	{ .type = WMFW_ADSP2_PM, .base = 0x400000 },
	{ .type = WMFW_ADSP2_ZM, .base = 0x480000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x490000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x4a8000 },
};

static const struct wm_adsp_region *wm5110_dsp_regions[] = {
	wm5110_dsp1_regions,
	wm5110_dsp2_regions,
	wm5110_dsp3_regions,
	wm5110_dsp4_regions,
};

static const struct reg_default wm5110_sysclk_revd_patch[] = {
	{ 0x3093, 0x1001 },
	{ 0x30E3, 0x1301 },
	{ 0x3133, 0x1201 },
	{ 0x3183, 0x1501 },
	{ 0x31D3, 0x1401 },
	{ 0x0049, 0x01ea },
	{ 0x004a, 0x01f2 },
	{ 0x0057, 0x01e7 },
	{ 0x0058, 0x01fb },
	{ 0x33ce, 0xc4f5 },
	{ 0x33cf, 0x1361 },
	{ 0x33d0, 0x0402 },
	{ 0x33d1, 0x4700 },
	{ 0x33d2, 0x026d },
	{ 0x33d3, 0xff00 },
	{ 0x33d4, 0x026d },
	{ 0x33d5, 0x0101 },
	{ 0x33d6, 0xc4f5 },
	{ 0x33d7, 0x0361 },
	{ 0x33d8, 0x0402 },
	{ 0x33d9, 0x6701 },
	{ 0x33da, 0xc4f5 },
	{ 0x33db, 0x136f },
	{ 0x33dc, 0xc4f5 },
	{ 0x33dd, 0x134f },
	{ 0x33de, 0xc4f5 },
	{ 0x33df, 0x131f },
	{ 0x33e0, 0x026d },
	{ 0x33e1, 0x4f01 },
	{ 0x33e2, 0x026d },
	{ 0x33e3, 0xf100 },
	{ 0x33e4, 0x026d },
	{ 0x33e5, 0x0001 },
	{ 0x33e6, 0xc4f5 },
	{ 0x33e7, 0x0361 },
	{ 0x33e8, 0x0402 },
	{ 0x33e9, 0x6601 },
	{ 0x33ea, 0xc4f5 },
	{ 0x33eb, 0x136f },
	{ 0x33ec, 0xc4f5 },
	{ 0x33ed, 0x134f },
	{ 0x33ee, 0xc4f5 },
	{ 0x33ef, 0x131f },
	{ 0x33f0, 0x026d },
	{ 0x33f1, 0x4e01 },
	{ 0x33f2, 0x026d },
	{ 0x33f3, 0xf000 },
	{ 0x33f6, 0xc4f5 },
	{ 0x33f7, 0x1361 },
	{ 0x33f8, 0x0402 },
	{ 0x33f9, 0x4600 },
	{ 0x33fa, 0x026d },
	{ 0x33fb, 0xfe00 },
};

static const struct reg_default wm5110_sysclk_reve_patch[] = {
	{ 0x3270, 0xE410 },
	{ 0x3271, 0x3078 },
	{ 0x3272, 0xE410 },
	{ 0x3273, 0x3070 },
	{ 0x3274, 0xE410 },
	{ 0x3275, 0x3066 },
	{ 0x3276, 0xE410 },
	{ 0x3277, 0x3056 },
	{ 0x327A, 0xE414 },
	{ 0x327B, 0x3078 },
	{ 0x327C, 0xE414 },
	{ 0x327D, 0x3070 },
	{ 0x327E, 0xE414 },
	{ 0x327F, 0x3066 },
	{ 0x3280, 0xE414 },
	{ 0x3281, 0x3056 },
};

static int wm5110_sysclk_ev(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona *arizona = dev_get_drvdata(codec->dev->parent);
	struct regmap *regmap = arizona->regmap;
	const struct reg_default *patch = NULL;
	int i, patch_size;

	switch (arizona->rev) {
	case 3:
		patch = wm5110_sysclk_revd_patch;
		patch_size = ARRAY_SIZE(wm5110_sysclk_revd_patch);
		break;
	default:
		patch = wm5110_sysclk_reve_patch;
		patch_size = ARRAY_SIZE(wm5110_sysclk_reve_patch);
		break;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (patch)
			for (i = 0; i < patch_size; i++)
				regmap_write_async(regmap, patch[i].reg,
						   patch[i].def);
		break;
	case SND_SOC_DAPM_PRE_PMU:
	case SND_SOC_DAPM_POST_PMD:
		return arizona_clk_ev(w, kcontrol, event);
	default:
		break;
	}

	return 0;
}

static int wm5110_adsp_power_ev(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona *arizona = dev_get_drvdata(codec->dev->parent);
	unsigned int v;
	int ret;

	ret = regmap_read(arizona->regmap, ARIZONA_SYSTEM_CLOCK_1, &v);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to read SYSCLK state: %d\n", ret);
		return ret;
	}

	v = (v & ARIZONA_SYSCLK_FREQ_MASK) >> ARIZONA_SYSCLK_FREQ_SHIFT;

	return wm_adsp2_early_event(w, kcontrol, event, v);
}

static const struct reg_sequence wm5110_no_dre_left_enable[] = {
	{ 0x3024, 0xE410 },
	{ 0x3025, 0x0056 },
	{ 0x301B, 0x0224 },
	{ 0x301F, 0x4263 },
	{ 0x3021, 0x5291 },
	{ 0x3030, 0xE410 },
	{ 0x3031, 0x3066 },
	{ 0x3032, 0xE410 },
	{ 0x3033, 0x3070 },
	{ 0x3034, 0xE410 },
	{ 0x3035, 0x3078 },
	{ 0x3036, 0xE410 },
	{ 0x3037, 0x3080 },
	{ 0x3038, 0xE410 },
	{ 0x3039, 0x3080 },
};

static const struct reg_sequence wm5110_dre_left_enable[] = {
	{ 0x3024, 0x0231 },
	{ 0x3025, 0x0B00 },
	{ 0x301B, 0x0227 },
	{ 0x301F, 0x4266 },
	{ 0x3021, 0x5294 },
	{ 0x3030, 0xE231 },
	{ 0x3031, 0x0266 },
	{ 0x3032, 0x8231 },
	{ 0x3033, 0x4B15 },
	{ 0x3034, 0x8231 },
	{ 0x3035, 0x0B15 },
	{ 0x3036, 0xE231 },
	{ 0x3037, 0x5294 },
	{ 0x3038, 0x0231 },
	{ 0x3039, 0x0B00 },
};

static const struct reg_sequence wm5110_no_dre_right_enable[] = {
	{ 0x3074, 0xE414 },
	{ 0x3075, 0x0056 },
	{ 0x306B, 0x0224 },
	{ 0x306F, 0x4263 },
	{ 0x3071, 0x5291 },
	{ 0x3080, 0xE414 },
	{ 0x3081, 0x3066 },
	{ 0x3082, 0xE414 },
	{ 0x3083, 0x3070 },
	{ 0x3084, 0xE414 },
	{ 0x3085, 0x3078 },
	{ 0x3086, 0xE414 },
	{ 0x3087, 0x3080 },
	{ 0x3088, 0xE414 },
	{ 0x3089, 0x3080 },
};

static const struct reg_sequence wm5110_dre_right_enable[] = {
	{ 0x3074, 0x0231 },
	{ 0x3075, 0x0B00 },
	{ 0x306B, 0x0227 },
	{ 0x306F, 0x4266 },
	{ 0x3071, 0x5294 },
	{ 0x3080, 0xE231 },
	{ 0x3081, 0x0266 },
	{ 0x3082, 0x8231 },
	{ 0x3083, 0x4B17 },
	{ 0x3084, 0x8231 },
	{ 0x3085, 0x0B17 },
	{ 0x3086, 0xE231 },
	{ 0x3087, 0x5294 },
	{ 0x3088, 0x0231 },
	{ 0x3089, 0x0B00 },
};

static int wm5110_hp_pre_enable(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona *arizona = priv->arizona;
	unsigned int val = snd_soc_read(codec, ARIZONA_DRE_ENABLE);
	const struct reg_sequence *wseq;
	int nregs;

	switch (w->shift) {
	case ARIZONA_OUT1L_ENA_SHIFT:
		if (val & ARIZONA_DRE1L_ENA_MASK) {
			wseq = wm5110_dre_left_enable;
			nregs = ARRAY_SIZE(wm5110_dre_left_enable);
		} else {
			wseq = wm5110_no_dre_left_enable;
			nregs = ARRAY_SIZE(wm5110_no_dre_left_enable);
			priv->out_up_delay += 10;
		}
		break;
	case ARIZONA_OUT1R_ENA_SHIFT:
		if (val & ARIZONA_DRE1R_ENA_MASK) {
			wseq = wm5110_dre_right_enable;
			nregs = ARRAY_SIZE(wm5110_dre_right_enable);
		} else {
			wseq = wm5110_no_dre_right_enable;
			nregs = ARRAY_SIZE(wm5110_no_dre_right_enable);
			priv->out_up_delay += 10;
		}
		break;
	default:
		return 0;
	}

	return regmap_multi_reg_write(arizona->regmap, wseq, nregs);
}

static int wm5110_hp_pre_disable(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	unsigned int val = snd_soc_read(codec, ARIZONA_DRE_ENABLE);

	switch (w->shift) {
	case ARIZONA_OUT1L_ENA_SHIFT:
		if (!(val & ARIZONA_DRE1L_ENA_MASK)) {
			snd_soc_update_bits(codec, ARIZONA_SPARE_TRIGGERS,
					    ARIZONA_WS_TRG1, ARIZONA_WS_TRG1);
			snd_soc_update_bits(codec, ARIZONA_SPARE_TRIGGERS,
					    ARIZONA_WS_TRG1, 0);
			priv->out_down_delay += 27;
		}
		break;
	case ARIZONA_OUT1R_ENA_SHIFT:
		if (!(val & ARIZONA_DRE1R_ENA_MASK)) {
			snd_soc_update_bits(codec, ARIZONA_SPARE_TRIGGERS,
					    ARIZONA_WS_TRG2, ARIZONA_WS_TRG2);
			snd_soc_update_bits(codec, ARIZONA_SPARE_TRIGGERS,
					    ARIZONA_WS_TRG2, 0);
			priv->out_down_delay += 27;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int wm5110_hp_ev(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (priv->arizona->rev) {
	case 0 ... 3:
		break;
	default:
		switch (event) {
		case SND_SOC_DAPM_PRE_PMU:
			wm5110_hp_pre_enable(w);
			break;
		case SND_SOC_DAPM_PRE_PMD:
			wm5110_hp_pre_disable(w);
			break;
		default:
			break;
		}
		break;
	}

	return arizona_hp_ev(w, kcontrol, event);
}

static int wm5110_clear_pga_volume(struct arizona *arizona, int output)
{
	unsigned int reg = ARIZONA_OUTPUT_PATH_CONFIG_1L + output * 4;
	int ret;

	ret = regmap_write(arizona->regmap, reg, 0x80);
	if (ret)
		dev_err(arizona->dev, "Failed to clear PGA (0x%x): %d\n",
			reg, ret);

	return ret;
}

static int wm5110_put_dre(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct arizona *arizona = dev_get_drvdata(codec->dev->parent);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int ena, dre;
	unsigned int mask = (0x1 << mc->shift) | (0x1 << mc->rshift);
	unsigned int lnew = (!!ucontrol->value.integer.value[0]) << mc->shift;
	unsigned int rnew = (!!ucontrol->value.integer.value[1]) << mc->rshift;
	unsigned int lold, rold;
	unsigned int lena, rena;
	int ret;

	snd_soc_dapm_mutex_lock(dapm);

	ret = regmap_read(arizona->regmap, ARIZONA_OUTPUT_ENABLES_1, &ena);
	if (ret) {
		dev_err(arizona->dev, "Failed to read output state: %d\n", ret);
		goto err;
	}
	ret = regmap_read(arizona->regmap, ARIZONA_DRE_ENABLE, &dre);
	if (ret) {
		dev_err(arizona->dev, "Failed to read DRE state: %d\n", ret);
		goto err;
	}

	lold = dre & (1 << mc->shift);
	rold = dre & (1 << mc->rshift);
	/* Enables are channel wise swapped from the DRE enables */
	lena = ena & (1 << mc->rshift);
	rena = ena & (1 << mc->shift);

	if ((lena && lnew != lold) || (rena && rnew != rold)) {
		dev_err(arizona->dev, "Can't change DRE on active outputs\n");
		ret = -EBUSY;
		goto err;
	}

	ret = regmap_update_bits(arizona->regmap, ARIZONA_DRE_ENABLE,
				 mask, lnew | rnew);
	if (ret) {
		dev_err(arizona->dev, "Failed to set DRE: %d\n", ret);
		goto err;
	}

	/* Force reset of PGA volumes, if turning DRE off */
	if (!lnew && lold)
		wm5110_clear_pga_volume(arizona, mc->shift);

	if (!rnew && rold)
		wm5110_clear_pga_volume(arizona, mc->rshift);

err:
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int wm5110_in_pga_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int ret;

	/*
	 * PGA Volume is also used as part of the enable sequence, so
	 * usage of it should be avoided whilst that is running.
	 */
	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_get_volsw_range(kcontrol, ucontrol);

	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int wm5110_in_pga_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int ret;

	/*
	 * PGA Volume is also used as part of the enable sequence, so
	 * usage of it should be avoided whilst that is running.
	 */
	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_put_volsw_range(kcontrol, ucontrol);

	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static int wm5110_in_analog_ev(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct wm5110_priv *wm5110 = snd_soc_codec_get_drvdata(codec);
	struct arizona *arizona = priv->arizona;
	unsigned int reg, mask;
	struct reg_sequence analog_seq[] = {
		{ 0x80, 0x3 },
		{ 0x35d, 0 },
		{ 0x80, 0x0 },
	};

	reg = ARIZONA_IN1L_CONTROL + ((w->shift ^ 0x1) * 4);
	mask = ARIZONA_IN1L_PGA_VOL_MASK;

	switch (event) {
	case SND_SOC_DAPM_WILL_PMU:
		wm5110->in_value |= 0x3 << ((w->shift ^ 0x1) * 2);
		wm5110->in_pre_pending++;
		wm5110->in_post_pending++;
		return 0;
	case SND_SOC_DAPM_PRE_PMU:
		wm5110->in_pga_cache[w->shift] = snd_soc_read(codec, reg);

		snd_soc_update_bits(codec, reg, mask,
				    0x40 << ARIZONA_IN1L_PGA_VOL_SHIFT);

		wm5110->in_pre_pending--;
		if (wm5110->in_pre_pending == 0) {
			analog_seq[1].def = wm5110->in_value;
			regmap_multi_reg_write_bypassed(arizona->regmap,
							analog_seq,
							ARRAY_SIZE(analog_seq));

			msleep(55);

			wm5110->in_value = 0;
		}

		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, reg, mask,
				    wm5110->in_pga_cache[w->shift]);

		wm5110->in_post_pending--;
		if (wm5110->in_post_pending == 0)
			regmap_multi_reg_write_bypassed(arizona->regmap,
							analog_seq,
							ARRAY_SIZE(analog_seq));
		break;
	default:
		break;
	}

	return 0;
}

static int wm5110_in_ev(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct arizona_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct arizona *arizona = priv->arizona;

	switch (arizona->rev) {
	case 0 ... 4:
		if (arizona_input_analog(codec, w->shift))
			wm5110_in_analog_ev(w, kcontrol, event);

		break;
	default:
		break;
	}

	return arizona_in_ev(w, kcontrol, event);
}

static DECLARE_TLV_DB_SCALE(ana_tlv, 0, 100, 0);
static DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static DECLARE_TLV_DB_SCALE(digital_tlv, -6400, 50, 0);
static DECLARE_TLV_DB_SCALE(noise_tlv, -13200, 600, 0);
static DECLARE_TLV_DB_SCALE(ng_tlv, -10200, 600, 0);

#define WM5110_NG_SRC(name, base) \
	SOC_SINGLE(name " NG HPOUT1L Switch",  base,  0, 1, 0), \
	SOC_SINGLE(name " NG HPOUT1R Switch",  base,  1, 1, 0), \
	SOC_SINGLE(name " NG HPOUT2L Switch",  base,  2, 1, 0), \
	SOC_SINGLE(name " NG HPOUT2R Switch",  base,  3, 1, 0), \
	SOC_SINGLE(name " NG HPOUT3L Switch",  base,  4, 1, 0), \
	SOC_SINGLE(name " NG HPOUT3R Switch",  base,  5, 1, 0), \
	SOC_SINGLE(name " NG SPKOUTL Switch",  base,  6, 1, 0), \
	SOC_SINGLE(name " NG SPKOUTR Switch",  base,  7, 1, 0), \
	SOC_SINGLE(name " NG SPKDAT1L Switch", base,  8, 1, 0), \
	SOC_SINGLE(name " NG SPKDAT1R Switch", base,  9, 1, 0), \
	SOC_SINGLE(name " NG SPKDAT2L Switch", base, 10, 1, 0), \
	SOC_SINGLE(name " NG SPKDAT2R Switch", base, 11, 1, 0)

#define WM5110_RXANC_INPUT_ROUTES(widget, name) \
	{ widget, NULL, name " NG Mux" }, \
	{ name " NG Internal", NULL, "RXANC NG Clock" }, \
	{ name " NG Internal", NULL, name " Channel" }, \
	{ name " NG External", NULL, "RXANC NG External Clock" }, \
	{ name " NG External", NULL, name " Channel" }, \
	{ name " NG Mux", "None", name " Channel" }, \
	{ name " NG Mux", "Internal", name " NG Internal" }, \
	{ name " NG Mux", "External", name " NG External" }, \
	{ name " Channel", "Left", name " Left Input" }, \
	{ name " Channel", "Combine", name " Left Input" }, \
	{ name " Channel", "Right", name " Right Input" }, \
	{ name " Channel", "Combine", name " Right Input" }, \
	{ name " Left Input", "IN1", "IN1L PGA" }, \
	{ name " Right Input", "IN1", "IN1R PGA" }, \
	{ name " Left Input", "IN2", "IN2L PGA" }, \
	{ name " Right Input", "IN2", "IN2R PGA" }, \
	{ name " Left Input", "IN3", "IN3L PGA" }, \
	{ name " Right Input", "IN3", "IN3R PGA" }, \
	{ name " Left Input", "IN4", "IN4L PGA" }, \
	{ name " Right Input", "IN4", "IN4R PGA" }

#define WM5110_RXANC_OUTPUT_ROUTES(widget, name) \
	{ widget, NULL, name " ANC Source" }, \
	{ name " ANC Source", "RXANCL", "RXANCL" }, \
	{ name " ANC Source", "RXANCR", "RXANCR" }

static const struct snd_kcontrol_new wm5110_snd_controls[] = {
SOC_ENUM("IN1 OSR", arizona_in_dmic_osr[0]),
SOC_ENUM("IN2 OSR", arizona_in_dmic_osr[1]),
SOC_ENUM("IN3 OSR", arizona_in_dmic_osr[2]),
SOC_ENUM("IN4 OSR", arizona_in_dmic_osr[3]),

SOC_SINGLE_RANGE_EXT_TLV("IN1L Volume", ARIZONA_IN1L_CONTROL,
			 ARIZONA_IN1L_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),
SOC_SINGLE_RANGE_EXT_TLV("IN1R Volume", ARIZONA_IN1R_CONTROL,
			 ARIZONA_IN1R_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),
SOC_SINGLE_RANGE_EXT_TLV("IN2L Volume", ARIZONA_IN2L_CONTROL,
			 ARIZONA_IN2L_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),
SOC_SINGLE_RANGE_EXT_TLV("IN2R Volume", ARIZONA_IN2R_CONTROL,
			 ARIZONA_IN2R_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),
SOC_SINGLE_RANGE_EXT_TLV("IN3L Volume", ARIZONA_IN3L_CONTROL,
			 ARIZONA_IN3L_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),
SOC_SINGLE_RANGE_EXT_TLV("IN3R Volume", ARIZONA_IN3R_CONTROL,
			 ARIZONA_IN3R_PGA_VOL_SHIFT, 0x40, 0x5f, 0,
			 wm5110_in_pga_get, wm5110_in_pga_put, ana_tlv),

SOC_ENUM("IN HPF Cutoff Frequency", arizona_in_hpf_cut_enum),

SOC_SINGLE("IN1L HPF Switch", ARIZONA_IN1L_CONTROL,
	   ARIZONA_IN1L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN1R HPF Switch", ARIZONA_IN1R_CONTROL,
	   ARIZONA_IN1R_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2L HPF Switch", ARIZONA_IN2L_CONTROL,
	   ARIZONA_IN2L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2R HPF Switch", ARIZONA_IN2R_CONTROL,
	   ARIZONA_IN2R_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN3L HPF Switch", ARIZONA_IN3L_CONTROL,
	   ARIZONA_IN3L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN3R HPF Switch", ARIZONA_IN3R_CONTROL,
	   ARIZONA_IN3R_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN4L HPF Switch", ARIZONA_IN4L_CONTROL,
	   ARIZONA_IN4L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN4R HPF Switch", ARIZONA_IN4R_CONTROL,
	   ARIZONA_IN4R_HPF_SHIFT, 1, 0),

SOC_SINGLE_TLV("IN1L Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_1L,
	       ARIZONA_IN1L_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN1R Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_1R,
	       ARIZONA_IN1R_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN2L Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_2L,
	       ARIZONA_IN2L_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN2R Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_2R,
	       ARIZONA_IN2R_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN3L Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_3L,
	       ARIZONA_IN3L_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN3R Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_3R,
	       ARIZONA_IN3R_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN4L Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_4L,
	       ARIZONA_IN4L_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),
SOC_SINGLE_TLV("IN4R Digital Volume", ARIZONA_ADC_DIGITAL_VOLUME_4R,
	       ARIZONA_IN4R_DIG_VOL_SHIFT, 0xbf, 0, digital_tlv),

SOC_ENUM("Input Ramp Up", arizona_in_vi_ramp),
SOC_ENUM("Input Ramp Down", arizona_in_vd_ramp),

SND_SOC_BYTES("RXANC Coefficients", ARIZONA_ANC_COEFF_START,
	      ARIZONA_ANC_COEFF_END - ARIZONA_ANC_COEFF_START + 1),
SND_SOC_BYTES("RXANCL Config", ARIZONA_FCL_FILTER_CONTROL, 1),
SND_SOC_BYTES("RXANCL Coefficients", ARIZONA_FCL_COEFF_START,
	      ARIZONA_FCL_COEFF_END - ARIZONA_FCL_COEFF_START + 1),
SND_SOC_BYTES("RXANCR Config", ARIZONA_FCR_FILTER_CONTROL, 1),
SND_SOC_BYTES("RXANCR Coefficients", ARIZONA_FCR_COEFF_START,
	      ARIZONA_FCR_COEFF_END - ARIZONA_FCR_COEFF_START + 1),

ARIZONA_MIXER_CONTROLS("EQ1", ARIZONA_EQ1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("EQ2", ARIZONA_EQ2MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("EQ3", ARIZONA_EQ3MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("EQ4", ARIZONA_EQ4MIX_INPUT_1_SOURCE),

ARIZONA_EQ_CONTROL("EQ1 Coefficients", ARIZONA_EQ1_2),
SOC_SINGLE_TLV("EQ1 B1 Volume", ARIZONA_EQ1_1, ARIZONA_EQ1_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 B2 Volume", ARIZONA_EQ1_1, ARIZONA_EQ1_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 B3 Volume", ARIZONA_EQ1_1, ARIZONA_EQ1_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 B4 Volume", ARIZONA_EQ1_2, ARIZONA_EQ1_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ1 B5 Volume", ARIZONA_EQ1_2, ARIZONA_EQ1_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

ARIZONA_EQ_CONTROL("EQ2 Coefficients", ARIZONA_EQ2_2),
SOC_SINGLE_TLV("EQ2 B1 Volume", ARIZONA_EQ2_1, ARIZONA_EQ2_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 B2 Volume", ARIZONA_EQ2_1, ARIZONA_EQ2_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 B3 Volume", ARIZONA_EQ2_1, ARIZONA_EQ2_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 B4 Volume", ARIZONA_EQ2_2, ARIZONA_EQ2_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 B5 Volume", ARIZONA_EQ2_2, ARIZONA_EQ2_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

ARIZONA_EQ_CONTROL("EQ3 Coefficients", ARIZONA_EQ3_2),
SOC_SINGLE_TLV("EQ3 B1 Volume", ARIZONA_EQ3_1, ARIZONA_EQ3_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 B2 Volume", ARIZONA_EQ3_1, ARIZONA_EQ3_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 B3 Volume", ARIZONA_EQ3_1, ARIZONA_EQ3_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 B4 Volume", ARIZONA_EQ3_2, ARIZONA_EQ3_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 B5 Volume", ARIZONA_EQ3_2, ARIZONA_EQ3_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

ARIZONA_EQ_CONTROL("EQ4 Coefficients", ARIZONA_EQ4_2),
SOC_SINGLE_TLV("EQ4 B1 Volume", ARIZONA_EQ4_1, ARIZONA_EQ4_B1_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 B2 Volume", ARIZONA_EQ4_1, ARIZONA_EQ4_B2_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 B3 Volume", ARIZONA_EQ4_1, ARIZONA_EQ4_B3_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 B4 Volume", ARIZONA_EQ4_2, ARIZONA_EQ4_B4_GAIN_SHIFT,
	       24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 B5 Volume", ARIZONA_EQ4_2, ARIZONA_EQ4_B5_GAIN_SHIFT,
	       24, 0, eq_tlv),

ARIZONA_MIXER_CONTROLS("DRC1L", ARIZONA_DRC1LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DRC1R", ARIZONA_DRC1RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DRC2L", ARIZONA_DRC2LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DRC2R", ARIZONA_DRC2RMIX_INPUT_1_SOURCE),

SND_SOC_BYTES_MASK("DRC1", ARIZONA_DRC1_CTRL1, 5,
		   ARIZONA_DRC1R_ENA | ARIZONA_DRC1L_ENA),
SND_SOC_BYTES_MASK("DRC2", ARIZONA_DRC2_CTRL1, 5,
		   ARIZONA_DRC2R_ENA | ARIZONA_DRC2L_ENA),

ARIZONA_MIXER_CONTROLS("LHPF1", ARIZONA_HPLP1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("LHPF2", ARIZONA_HPLP2MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("LHPF3", ARIZONA_HPLP3MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("LHPF4", ARIZONA_HPLP4MIX_INPUT_1_SOURCE),

ARIZONA_LHPF_CONTROL("LHPF1 Coefficients", ARIZONA_HPLPF1_2),
ARIZONA_LHPF_CONTROL("LHPF2 Coefficients", ARIZONA_HPLPF2_2),
ARIZONA_LHPF_CONTROL("LHPF3 Coefficients", ARIZONA_HPLPF3_2),
ARIZONA_LHPF_CONTROL("LHPF4 Coefficients", ARIZONA_HPLPF4_2),

SOC_ENUM("LHPF1 Mode", arizona_lhpf1_mode),
SOC_ENUM("LHPF2 Mode", arizona_lhpf2_mode),
SOC_ENUM("LHPF3 Mode", arizona_lhpf3_mode),
SOC_ENUM("LHPF4 Mode", arizona_lhpf4_mode),

SOC_ENUM("ISRC1 FSL", arizona_isrc_fsl[0]),
SOC_ENUM("ISRC2 FSL", arizona_isrc_fsl[1]),
SOC_ENUM("ISRC3 FSL", arizona_isrc_fsl[2]),
SOC_ENUM("ISRC1 FSH", arizona_isrc_fsh[0]),
SOC_ENUM("ISRC2 FSH", arizona_isrc_fsh[1]),
SOC_ENUM("ISRC3 FSH", arizona_isrc_fsh[2]),
SOC_ENUM("ASRC RATE 1", arizona_asrc_rate1),

ARIZONA_MIXER_CONTROLS("DSP1L", ARIZONA_DSP1LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP1R", ARIZONA_DSP1RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP2L", ARIZONA_DSP2LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP2R", ARIZONA_DSP2RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP3L", ARIZONA_DSP3LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP3R", ARIZONA_DSP3RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP4L", ARIZONA_DSP4LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("DSP4R", ARIZONA_DSP4RMIX_INPUT_1_SOURCE),

ARIZONA_MIXER_CONTROLS("Mic", ARIZONA_MICMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("Noise", ARIZONA_NOISEMIX_INPUT_1_SOURCE),

SOC_SINGLE_TLV("Noise Generator Volume", ARIZONA_COMFORT_NOISE_GENERATOR,
	       ARIZONA_NOISE_GEN_GAIN_SHIFT, 0x16, 0, noise_tlv),

ARIZONA_MIXER_CONTROLS("HPOUT1L", ARIZONA_OUT1LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("HPOUT1R", ARIZONA_OUT1RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("HPOUT2L", ARIZONA_OUT2LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("HPOUT2R", ARIZONA_OUT2RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("HPOUT3L", ARIZONA_OUT3LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("HPOUT3R", ARIZONA_OUT3RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKOUTL", ARIZONA_OUT4LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKOUTR", ARIZONA_OUT4RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKDAT1L", ARIZONA_OUT5LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKDAT1R", ARIZONA_OUT5RMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKDAT2L", ARIZONA_OUT6LMIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SPKDAT2R", ARIZONA_OUT6RMIX_INPUT_1_SOURCE),

SOC_SINGLE("HPOUT1 SC Protect Switch", ARIZONA_HP1_SHORT_CIRCUIT_CTRL,
	   ARIZONA_HP1_SC_ENA_SHIFT, 1, 0),
SOC_SINGLE("HPOUT2 SC Protect Switch", ARIZONA_HP2_SHORT_CIRCUIT_CTRL,
	   ARIZONA_HP2_SC_ENA_SHIFT, 1, 0),
SOC_SINGLE("HPOUT3 SC Protect Switch", ARIZONA_HP3_SHORT_CIRCUIT_CTRL,
	   ARIZONA_HP3_SC_ENA_SHIFT, 1, 0),

SOC_SINGLE("SPKDAT1 High Performance Switch", ARIZONA_OUTPUT_PATH_CONFIG_5L,
	   ARIZONA_OUT5_OSR_SHIFT, 1, 0),
SOC_SINGLE("SPKDAT2 High Performance Switch", ARIZONA_OUTPUT_PATH_CONFIG_6L,
	   ARIZONA_OUT6_OSR_SHIFT, 1, 0),

SOC_DOUBLE_R("HPOUT1 Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_1L,
	     ARIZONA_DAC_DIGITAL_VOLUME_1R, ARIZONA_OUT1L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("HPOUT2 Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_2L,
	     ARIZONA_DAC_DIGITAL_VOLUME_2R, ARIZONA_OUT2L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("HPOUT3 Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_3L,
	     ARIZONA_DAC_DIGITAL_VOLUME_3R, ARIZONA_OUT3L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("Speaker Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_4L,
	     ARIZONA_DAC_DIGITAL_VOLUME_4R, ARIZONA_OUT4L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("SPKDAT1 Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_5L,
	     ARIZONA_DAC_DIGITAL_VOLUME_5R, ARIZONA_OUT5L_MUTE_SHIFT, 1, 1),
SOC_DOUBLE_R("SPKDAT2 Digital Switch", ARIZONA_DAC_DIGITAL_VOLUME_6L,
	     ARIZONA_DAC_DIGITAL_VOLUME_6R, ARIZONA_OUT6L_MUTE_SHIFT, 1, 1),

SOC_DOUBLE_R_TLV("HPOUT1 Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_1L,
		 ARIZONA_DAC_DIGITAL_VOLUME_1R, ARIZONA_OUT1L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("HPOUT2 Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_2L,
		 ARIZONA_DAC_DIGITAL_VOLUME_2R, ARIZONA_OUT2L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("HPOUT3 Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_3L,
		 ARIZONA_DAC_DIGITAL_VOLUME_3R, ARIZONA_OUT3L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("Speaker Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_4L,
		 ARIZONA_DAC_DIGITAL_VOLUME_4R, ARIZONA_OUT4L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("SPKDAT1 Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_5L,
		 ARIZONA_DAC_DIGITAL_VOLUME_5R, ARIZONA_OUT5L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),
SOC_DOUBLE_R_TLV("SPKDAT2 Digital Volume", ARIZONA_DAC_DIGITAL_VOLUME_6L,
		 ARIZONA_DAC_DIGITAL_VOLUME_6R, ARIZONA_OUT6L_VOL_SHIFT,
		 0xbf, 0, digital_tlv),

SOC_DOUBLE("SPKDAT1 Switch", ARIZONA_PDM_SPK1_CTRL_1, ARIZONA_SPK1L_MUTE_SHIFT,
	   ARIZONA_SPK1R_MUTE_SHIFT, 1, 1),
SOC_DOUBLE("SPKDAT2 Switch", ARIZONA_PDM_SPK2_CTRL_1, ARIZONA_SPK2L_MUTE_SHIFT,
	   ARIZONA_SPK2R_MUTE_SHIFT, 1, 1),

SOC_DOUBLE_EXT("HPOUT1 DRE Switch", ARIZONA_DRE_ENABLE,
	   ARIZONA_DRE1L_ENA_SHIFT, ARIZONA_DRE1R_ENA_SHIFT, 1, 0,
	   snd_soc_get_volsw, wm5110_put_dre),
SOC_DOUBLE_EXT("HPOUT2 DRE Switch", ARIZONA_DRE_ENABLE,
	   ARIZONA_DRE2L_ENA_SHIFT, ARIZONA_DRE2R_ENA_SHIFT, 1, 0,
	   snd_soc_get_volsw, wm5110_put_dre),
SOC_DOUBLE_EXT("HPOUT3 DRE Switch", ARIZONA_DRE_ENABLE,
	   ARIZONA_DRE3L_ENA_SHIFT, ARIZONA_DRE3R_ENA_SHIFT, 1, 0,
	   snd_soc_get_volsw, wm5110_put_dre),

SOC_ENUM("Output Ramp Up", arizona_out_vi_ramp),
SOC_ENUM("Output Ramp Down", arizona_out_vd_ramp),

SOC_SINGLE("Noise Gate Switch", ARIZONA_NOISE_GATE_CONTROL,
	   ARIZONA_NGATE_ENA_SHIFT, 1, 0),
SOC_SINGLE_TLV("Noise Gate Threshold Volume", ARIZONA_NOISE_GATE_CONTROL,
	       ARIZONA_NGATE_THR_SHIFT, 7, 1, ng_tlv),
SOC_ENUM("Noise Gate Hold", arizona_ng_hold),

WM5110_NG_SRC("HPOUT1L", ARIZONA_NOISE_GATE_SELECT_1L),
WM5110_NG_SRC("HPOUT1R", ARIZONA_NOISE_GATE_SELECT_1R),
WM5110_NG_SRC("HPOUT2L", ARIZONA_NOISE_GATE_SELECT_2L),
WM5110_NG_SRC("HPOUT2R", ARIZONA_NOISE_GATE_SELECT_2R),
WM5110_NG_SRC("HPOUT3L", ARIZONA_NOISE_GATE_SELECT_3L),
WM5110_NG_SRC("HPOUT3R", ARIZONA_NOISE_GATE_SELECT_3R),
WM5110_NG_SRC("SPKOUTL", ARIZONA_NOISE_GATE_SELECT_4L),
WM5110_NG_SRC("SPKOUTR", ARIZONA_NOISE_GATE_SELECT_4R),
WM5110_NG_SRC("SPKDAT1L", ARIZONA_NOISE_GATE_SELECT_5L),
WM5110_NG_SRC("SPKDAT1R", ARIZONA_NOISE_GATE_SELECT_5R),
WM5110_NG_SRC("SPKDAT2L", ARIZONA_NOISE_GATE_SELECT_6L),
WM5110_NG_SRC("SPKDAT2R", ARIZONA_NOISE_GATE_SELECT_6R),

ARIZONA_MIXER_CONTROLS("AIF1TX1", ARIZONA_AIF1TX1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX2", ARIZONA_AIF1TX2MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX3", ARIZONA_AIF1TX3MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX4", ARIZONA_AIF1TX4MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX5", ARIZONA_AIF1TX5MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX6", ARIZONA_AIF1TX6MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX7", ARIZONA_AIF1TX7MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF1TX8", ARIZONA_AIF1TX8MIX_INPUT_1_SOURCE),

ARIZONA_MIXER_CONTROLS("AIF2TX1", ARIZONA_AIF2TX1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF2TX2", ARIZONA_AIF2TX2MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF2TX3", ARIZONA_AIF2TX3MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF2TX4", ARIZONA_AIF2TX4MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF2TX5", ARIZONA_AIF2TX5MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF2TX6", ARIZONA_AIF2TX6MIX_INPUT_1_SOURCE),

ARIZONA_MIXER_CONTROLS("AIF3TX1", ARIZONA_AIF3TX1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("AIF3TX2", ARIZONA_AIF3TX2MIX_INPUT_1_SOURCE),

ARIZONA_MIXER_CONTROLS("SLIMTX1", ARIZONA_SLIMTX1MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX2", ARIZONA_SLIMTX2MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX3", ARIZONA_SLIMTX3MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX4", ARIZONA_SLIMTX4MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX5", ARIZONA_SLIMTX5MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX6", ARIZONA_SLIMTX6MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX7", ARIZONA_SLIMTX7MIX_INPUT_1_SOURCE),
ARIZONA_MIXER_CONTROLS("SLIMTX8", ARIZONA_SLIMTX8MIX_INPUT_1_SOURCE),
};

ARIZONA_MIXER_ENUMS(EQ1, ARIZONA_EQ1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(EQ2, ARIZONA_EQ2MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(EQ3, ARIZONA_EQ3MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(EQ4, ARIZONA_EQ4MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(DRC1L, ARIZONA_DRC1LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DRC1R, ARIZONA_DRC1RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DRC2L, ARIZONA_DRC2LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DRC2R, ARIZONA_DRC2RMIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(LHPF1, ARIZONA_HPLP1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(LHPF2, ARIZONA_HPLP2MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(LHPF3, ARIZONA_HPLP3MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(LHPF4, ARIZONA_HPLP4MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(DSP1L, ARIZONA_DSP1LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DSP1R, ARIZONA_DSP1RMIX_INPUT_1_SOURCE);
ARIZONA_DSP_AUX_ENUMS(DSP1, ARIZONA_DSP1AUX1MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(DSP2L, ARIZONA_DSP2LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DSP2R, ARIZONA_DSP2RMIX_INPUT_1_SOURCE);
ARIZONA_DSP_AUX_ENUMS(DSP2, ARIZONA_DSP2AUX1MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(DSP3L, ARIZONA_DSP3LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DSP3R, ARIZONA_DSP3RMIX_INPUT_1_SOURCE);
ARIZONA_DSP_AUX_ENUMS(DSP3, ARIZONA_DSP3AUX1MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(DSP4L, ARIZONA_DSP4LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(DSP4R, ARIZONA_DSP4RMIX_INPUT_1_SOURCE);
ARIZONA_DSP_AUX_ENUMS(DSP4, ARIZONA_DSP4AUX1MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(Mic, ARIZONA_MICMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(Noise, ARIZONA_NOISEMIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(PWM1, ARIZONA_PWM1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(PWM2, ARIZONA_PWM2MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(OUT1L, ARIZONA_OUT1LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(OUT1R, ARIZONA_OUT1RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(OUT2L, ARIZONA_OUT2LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(OUT2R, ARIZONA_OUT2RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(OUT3L, ARIZONA_OUT3LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(OUT3R, ARIZONA_OUT3RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKOUTL, ARIZONA_OUT4LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKOUTR, ARIZONA_OUT4RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKDAT1L, ARIZONA_OUT5LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKDAT1R, ARIZONA_OUT5RMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKDAT2L, ARIZONA_OUT6LMIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SPKDAT2R, ARIZONA_OUT6RMIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(AIF1TX1, ARIZONA_AIF1TX1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX2, ARIZONA_AIF1TX2MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX3, ARIZONA_AIF1TX3MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX4, ARIZONA_AIF1TX4MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX5, ARIZONA_AIF1TX5MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX6, ARIZONA_AIF1TX6MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX7, ARIZONA_AIF1TX7MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF1TX8, ARIZONA_AIF1TX8MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(AIF2TX1, ARIZONA_AIF2TX1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF2TX2, ARIZONA_AIF2TX2MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF2TX3, ARIZONA_AIF2TX3MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF2TX4, ARIZONA_AIF2TX4MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF2TX5, ARIZONA_AIF2TX5MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF2TX6, ARIZONA_AIF2TX6MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(AIF3TX1, ARIZONA_AIF3TX1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(AIF3TX2, ARIZONA_AIF3TX2MIX_INPUT_1_SOURCE);

ARIZONA_MIXER_ENUMS(SLIMTX1, ARIZONA_SLIMTX1MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX2, ARIZONA_SLIMTX2MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX3, ARIZONA_SLIMTX3MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX4, ARIZONA_SLIMTX4MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX5, ARIZONA_SLIMTX5MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX6, ARIZONA_SLIMTX6MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX7, ARIZONA_SLIMTX7MIX_INPUT_1_SOURCE);
ARIZONA_MIXER_ENUMS(SLIMTX8, ARIZONA_SLIMTX8MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ASRC1L, ARIZONA_ASRC1LMIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ASRC1R, ARIZONA_ASRC1RMIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ASRC2L, ARIZONA_ASRC2LMIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ASRC2R, ARIZONA_ASRC2RMIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC1INT1, ARIZONA_ISRC1INT1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1INT2, ARIZONA_ISRC1INT2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1INT3, ARIZONA_ISRC1INT3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1INT4, ARIZONA_ISRC1INT4MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC1DEC1, ARIZONA_ISRC1DEC1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1DEC2, ARIZONA_ISRC1DEC2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1DEC3, ARIZONA_ISRC1DEC3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC1DEC4, ARIZONA_ISRC1DEC4MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC2INT1, ARIZONA_ISRC2INT1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2INT2, ARIZONA_ISRC2INT2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2INT3, ARIZONA_ISRC2INT3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2INT4, ARIZONA_ISRC2INT4MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC2DEC1, ARIZONA_ISRC2DEC1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2DEC2, ARIZONA_ISRC2DEC2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2DEC3, ARIZONA_ISRC2DEC3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC2DEC4, ARIZONA_ISRC2DEC4MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC3INT1, ARIZONA_ISRC3INT1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3INT2, ARIZONA_ISRC3INT2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3INT3, ARIZONA_ISRC3INT3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3INT4, ARIZONA_ISRC3INT4MIX_INPUT_1_SOURCE);

ARIZONA_MUX_ENUMS(ISRC3DEC1, ARIZONA_ISRC3DEC1MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3DEC2, ARIZONA_ISRC3DEC2MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3DEC3, ARIZONA_ISRC3DEC3MIX_INPUT_1_SOURCE);
ARIZONA_MUX_ENUMS(ISRC3DEC4, ARIZONA_ISRC3DEC4MIX_INPUT_1_SOURCE);

static const char *wm5110_aec_loopback_texts[] = {
	"HPOUT1L", "HPOUT1R", "HPOUT2L", "HPOUT2R", "HPOUT3L", "HPOUT3R",
	"SPKOUTL", "SPKOUTR", "SPKDAT1L", "SPKDAT1R", "SPKDAT2L", "SPKDAT2R",
};

static const unsigned int wm5110_aec_loopback_values[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
};

static const struct soc_enum wm5110_aec_loopback =
	SOC_VALUE_ENUM_SINGLE(ARIZONA_DAC_AEC_CONTROL_1,
			      ARIZONA_AEC_LOOPBACK_SRC_SHIFT, 0xf,
			      ARRAY_SIZE(wm5110_aec_loopback_texts),
			      wm5110_aec_loopback_texts,
			      wm5110_aec_loopback_values);

static const struct snd_kcontrol_new wm5110_aec_loopback_mux =
	SOC_DAPM_ENUM("AEC Loopback", wm5110_aec_loopback);

static const struct snd_kcontrol_new wm5110_anc_input_mux[] = {
	SOC_DAPM_ENUM("RXANCL Input", arizona_anc_input_src[0]),
	SOC_DAPM_ENUM("RXANCL Channel", arizona_anc_input_src[1]),
	SOC_DAPM_ENUM("RXANCR Input", arizona_anc_input_src[2]),
	SOC_DAPM_ENUM("RXANCR Channel", arizona_anc_input_src[3]),
};

static const struct snd_kcontrol_new wm5110_anc_ng_mux =
	SOC_DAPM_ENUM("RXANC NG Source", arizona_anc_ng_enum);

static const struct snd_kcontrol_new wm5110_output_anc_src[] = {
	SOC_DAPM_ENUM("HPOUT1L ANC Source", arizona_output_anc_src[0]),
	SOC_DAPM_ENUM("HPOUT1R ANC Source", arizona_output_anc_src[1]),
	SOC_DAPM_ENUM("HPOUT2L ANC Source", arizona_output_anc_src[2]),
	SOC_DAPM_ENUM("HPOUT2R ANC Source", arizona_output_anc_src[3]),
	SOC_DAPM_ENUM("HPOUT3L ANC Source", arizona_output_anc_src[4]),
	SOC_DAPM_ENUM("HPOUT3R ANC Source", arizona_output_anc_src[5]),
	SOC_DAPM_ENUM("SPKOUTL ANC Source", arizona_output_anc_src[6]),
	SOC_DAPM_ENUM("SPKOUTR ANC Source", arizona_output_anc_src[7]),
	SOC_DAPM_ENUM("SPKDAT1L ANC Source", arizona_output_anc_src[8]),
	SOC_DAPM_ENUM("SPKDAT1R ANC Source", arizona_output_anc_src[9]),
	SOC_DAPM_ENUM("SPKDAT2L ANC Source", arizona_output_anc_src[10]),
	SOC_DAPM_ENUM("SPKDAT2R ANC Source", arizona_output_anc_src[11]),
};

static const struct snd_soc_dapm_widget wm5110_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", ARIZONA_SYSTEM_CLOCK_1, ARIZONA_SYSCLK_ENA_SHIFT,
		    0, wm5110_sysclk_ev, SND_SOC_DAPM_POST_PMU |
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("ASYNCCLK", ARIZONA_ASYNC_CLOCK_1,
		    ARIZONA_ASYNC_CLK_ENA_SHIFT, 0, arizona_clk_ev,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("OPCLK", ARIZONA_OUTPUT_SYSTEM_CLOCK,
		    ARIZONA_OPCLK_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("ASYNCOPCLK", ARIZONA_OUTPUT_ASYNC_CLOCK,
		    ARIZONA_OPCLK_ASYNC_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_REGULATOR_SUPPLY("DBVDD2", 0, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("DBVDD3", 0, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("CPVDD", 20, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("MICVDD", 0, SND_SOC_DAPM_REGULATOR_BYPASS),
SND_SOC_DAPM_REGULATOR_SUPPLY("SPKVDDL", 0, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("SPKVDDR", 0, 0),

SND_SOC_DAPM_SIGGEN("TONE"),
SND_SOC_DAPM_SIGGEN("NOISE"),
SND_SOC_DAPM_SIGGEN("HAPTICS"),

SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("IN4L"),
SND_SOC_DAPM_INPUT("IN4R"),

SND_SOC_DAPM_OUTPUT("DRC1 Signal Activity"),
SND_SOC_DAPM_OUTPUT("DRC2 Signal Activity"),

SND_SOC_DAPM_OUTPUT("DSP Voice Trigger"),

SND_SOC_DAPM_SWITCH("DSP3 Voice Trigger", SND_SOC_NOPM, 2, 0,
		    &arizona_voice_trigger_switch[2]),

SND_SOC_DAPM_PGA_E("IN1L PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN1L_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN1R PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN1R_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN2L PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN2L_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN2R PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN2R_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN3L PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN3L_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN3R PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN3R_ENA_SHIFT,
		   0, NULL, 0, wm5110_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_WILL_PMU),
SND_SOC_DAPM_PGA_E("IN4L PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN4L_ENA_SHIFT,
		   0, NULL, 0, arizona_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN4R PGA", ARIZONA_INPUT_ENABLES, ARIZONA_IN4R_ENA_SHIFT,
		   0, NULL, 0, arizona_in_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_SUPPLY("MICBIAS1", ARIZONA_MIC_BIAS_CTRL_1,
		    ARIZONA_MICB1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS2", ARIZONA_MIC_BIAS_CTRL_2,
		    ARIZONA_MICB1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS3", ARIZONA_MIC_BIAS_CTRL_3,
		    ARIZONA_MICB1_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("Noise Generator", ARIZONA_COMFORT_NOISE_GENERATOR,
		 ARIZONA_NOISE_GEN_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("Tone Generator 1", ARIZONA_TONE_GENERATOR_1,
		 ARIZONA_TONE1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("Tone Generator 2", ARIZONA_TONE_GENERATOR_1,
		 ARIZONA_TONE2_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("Mic Mute Mixer", ARIZONA_MIC_NOISE_MIX_CONTROL_1,
		 ARIZONA_MICMUTE_MIX_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("EQ1", ARIZONA_EQ1_1, ARIZONA_EQ1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ2", ARIZONA_EQ2_1, ARIZONA_EQ2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ3", ARIZONA_EQ3_1, ARIZONA_EQ3_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("EQ4", ARIZONA_EQ4_1, ARIZONA_EQ4_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("DRC1L", ARIZONA_DRC1_CTRL1, ARIZONA_DRC1L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC1R", ARIZONA_DRC1_CTRL1, ARIZONA_DRC1R_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC2L", ARIZONA_DRC2_CTRL1, ARIZONA_DRC2L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC2R", ARIZONA_DRC2_CTRL1, ARIZONA_DRC2R_ENA_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_PGA("LHPF1", ARIZONA_HPLPF1_1, ARIZONA_LHPF1_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF2", ARIZONA_HPLPF2_1, ARIZONA_LHPF2_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF3", ARIZONA_HPLPF3_1, ARIZONA_LHPF3_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF4", ARIZONA_HPLPF4_1, ARIZONA_LHPF4_ENA_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_PGA("PWM1 Driver", ARIZONA_PWM_DRIVE_1, ARIZONA_PWM1_ENA_SHIFT,
		 0, NULL, 0),
SND_SOC_DAPM_PGA("PWM2 Driver", ARIZONA_PWM_DRIVE_1, ARIZONA_PWM2_ENA_SHIFT,
		 0, NULL, 0),

SND_SOC_DAPM_PGA("ASRC1L", ARIZONA_ASRC_ENABLE, ARIZONA_ASRC1L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("ASRC1R", ARIZONA_ASRC_ENABLE, ARIZONA_ASRC1R_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("ASRC2L", ARIZONA_ASRC_ENABLE, ARIZONA_ASRC2L_ENA_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("ASRC2R", ARIZONA_ASRC_ENABLE, ARIZONA_ASRC2R_ENA_SHIFT, 0,
		 NULL, 0),

WM_ADSP2("DSP1", 0, wm5110_adsp_power_ev),
WM_ADSP2("DSP2", 1, wm5110_adsp_power_ev),
WM_ADSP2("DSP3", 2, wm5110_adsp_power_ev),
WM_ADSP2("DSP4", 3, wm5110_adsp_power_ev),

SND_SOC_DAPM_PGA("ISRC1INT1", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_INT0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT2", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_INT1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT3", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_INT2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT4", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_INT3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC1DEC1", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_DEC0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC2", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_DEC1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC3", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_DEC2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC4", ARIZONA_ISRC_1_CTRL_3,
		 ARIZONA_ISRC1_DEC3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2INT1", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_INT0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2INT2", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_INT1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2INT3", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_INT2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2INT4", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_INT3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2DEC1", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_DEC0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2DEC2", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_DEC1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2DEC3", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_DEC2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2DEC4", ARIZONA_ISRC_2_CTRL_3,
		 ARIZONA_ISRC2_DEC3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3INT1", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_INT0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3INT2", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_INT1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3INT3", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_INT2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3INT4", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_INT3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3DEC1", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_DEC0_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3DEC2", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_DEC1_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3DEC3", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_DEC2_ENA_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3DEC4", ARIZONA_ISRC_3_CTRL_3,
		 ARIZONA_ISRC3_DEC3_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_MUX("AEC Loopback", ARIZONA_DAC_AEC_CONTROL_1,
		       ARIZONA_AEC_LOOPBACK_ENA_SHIFT, 0,
		       &wm5110_aec_loopback_mux),

SND_SOC_DAPM_SUPPLY("RXANC NG External Clock", SND_SOC_NOPM,
		   ARIZONA_EXT_NG_SEL_SET_SHIFT, 0, arizona_anc_ev,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA("RXANCL NG External", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("RXANCR NG External", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("RXANC NG Clock", SND_SOC_NOPM,
		   ARIZONA_CLK_NG_ENA_SET_SHIFT, 0, arizona_anc_ev,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA("RXANCL NG Internal", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("RXANCR NG Internal", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("RXANCL Left Input", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[0]),
SND_SOC_DAPM_MUX("RXANCL Right Input", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[0]),
SND_SOC_DAPM_MUX("RXANCL Channel", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[1]),
SND_SOC_DAPM_MUX("RXANCL NG Mux", SND_SOC_NOPM, 0, 0, &wm5110_anc_ng_mux),
SND_SOC_DAPM_MUX("RXANCR Left Input", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[2]),
SND_SOC_DAPM_MUX("RXANCR Right Input", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[2]),
SND_SOC_DAPM_MUX("RXANCR Channel", SND_SOC_NOPM, 0, 0,
		 &wm5110_anc_input_mux[3]),
SND_SOC_DAPM_MUX("RXANCR NG Mux", SND_SOC_NOPM, 0, 0, &wm5110_anc_ng_mux),

SND_SOC_DAPM_PGA_E("RXANCL", SND_SOC_NOPM, ARIZONA_CLK_L_ENA_SET_SHIFT,
		   0, NULL, 0, arizona_anc_ev,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_E("RXANCR", SND_SOC_NOPM, ARIZONA_CLK_R_ENA_SET_SHIFT,
		   0, NULL, 0, arizona_anc_ev,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_MUX("HPOUT1L ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[0]),
SND_SOC_DAPM_MUX("HPOUT1R ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[1]),
SND_SOC_DAPM_MUX("HPOUT2L ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[2]),
SND_SOC_DAPM_MUX("HPOUT2R ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[3]),
SND_SOC_DAPM_MUX("HPOUT3L ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[4]),
SND_SOC_DAPM_MUX("HPOUT3R ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[5]),
SND_SOC_DAPM_MUX("SPKOUTL ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[6]),
SND_SOC_DAPM_MUX("SPKOUTR ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[7]),
SND_SOC_DAPM_MUX("SPKDAT1L ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[8]),
SND_SOC_DAPM_MUX("SPKDAT1R ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[9]),
SND_SOC_DAPM_MUX("SPKDAT2L ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[10]),
SND_SOC_DAPM_MUX("SPKDAT2R ANC Source", SND_SOC_NOPM, 0, 0,
		 &wm5110_output_anc_src[11]),

SND_SOC_DAPM_AIF_OUT("AIF1TX1", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX2", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX3", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX4", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX5", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX6", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX7", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX8", NULL, 0,
		     ARIZONA_AIF1_TX_ENABLES, ARIZONA_AIF1TX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("AIF1RX1", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX2", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX3", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX4", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX5", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX6", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX7", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX8", NULL, 0,
		    ARIZONA_AIF1_RX_ENABLES, ARIZONA_AIF1RX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("AIF2TX1", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX2", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX3", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX4", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX5", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX6", NULL, 0,
		     ARIZONA_AIF2_TX_ENABLES, ARIZONA_AIF2TX6_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("AIF2RX1", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX2", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX3", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX4", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX5", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX6", NULL, 0,
		    ARIZONA_AIF2_RX_ENABLES, ARIZONA_AIF2RX6_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("SLIMRX1", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX2", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX3", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX4", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX5", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX6", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX7", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("SLIMRX8", NULL, 0,
		    ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE,
		    ARIZONA_SLIMRX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("SLIMTX1", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX2", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX2_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX3", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX3_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX4", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX4_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX5", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX5_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX6", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX6_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX7", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX7_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("SLIMTX8", NULL, 0,
		     ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE,
		     ARIZONA_SLIMTX8_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("AIF3TX1", NULL, 0,
		     ARIZONA_AIF3_TX_ENABLES, ARIZONA_AIF3TX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("AIF3TX2", NULL, 0,
		     ARIZONA_AIF3_TX_ENABLES, ARIZONA_AIF3TX2_ENA_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("AIF3RX1", NULL, 0,
		    ARIZONA_AIF3_RX_ENABLES, ARIZONA_AIF3RX1_ENA_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("AIF3RX2", NULL, 0,
		    ARIZONA_AIF3_RX_ENABLES, ARIZONA_AIF3RX2_ENA_SHIFT, 0),

SND_SOC_DAPM_PGA_E("OUT1L", SND_SOC_NOPM,
		   ARIZONA_OUT1L_ENA_SHIFT, 0, NULL, 0, wm5110_hp_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT1R", SND_SOC_NOPM,
		   ARIZONA_OUT1R_ENA_SHIFT, 0, NULL, 0, wm5110_hp_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT2L", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT2L_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT2R", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT2R_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT3L", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT3L_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT3R", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT3R_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT5L", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT5L_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT5R", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT5R_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT6L", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT6L_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("OUT6R", ARIZONA_OUTPUT_ENABLES_1,
		   ARIZONA_OUT6R_ENA_SHIFT, 0, NULL, 0, arizona_out_ev,
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

ARIZONA_MIXER_WIDGETS(EQ1, "EQ1"),
ARIZONA_MIXER_WIDGETS(EQ2, "EQ2"),
ARIZONA_MIXER_WIDGETS(EQ3, "EQ3"),
ARIZONA_MIXER_WIDGETS(EQ4, "EQ4"),

ARIZONA_MIXER_WIDGETS(DRC1L, "DRC1L"),
ARIZONA_MIXER_WIDGETS(DRC1R, "DRC1R"),
ARIZONA_MIXER_WIDGETS(DRC2L, "DRC2L"),
ARIZONA_MIXER_WIDGETS(DRC2R, "DRC2R"),

ARIZONA_MIXER_WIDGETS(LHPF1, "LHPF1"),
ARIZONA_MIXER_WIDGETS(LHPF2, "LHPF2"),
ARIZONA_MIXER_WIDGETS(LHPF3, "LHPF3"),
ARIZONA_MIXER_WIDGETS(LHPF4, "LHPF4"),

ARIZONA_MIXER_WIDGETS(Mic, "Mic"),
ARIZONA_MIXER_WIDGETS(Noise, "Noise"),

ARIZONA_MIXER_WIDGETS(PWM1, "PWM1"),
ARIZONA_MIXER_WIDGETS(PWM2, "PWM2"),

ARIZONA_MIXER_WIDGETS(OUT1L, "HPOUT1L"),
ARIZONA_MIXER_WIDGETS(OUT1R, "HPOUT1R"),
ARIZONA_MIXER_WIDGETS(OUT2L, "HPOUT2L"),
ARIZONA_MIXER_WIDGETS(OUT2R, "HPOUT2R"),
ARIZONA_MIXER_WIDGETS(OUT3L, "HPOUT3L"),
ARIZONA_MIXER_WIDGETS(OUT3R, "HPOUT3R"),
ARIZONA_MIXER_WIDGETS(SPKOUTL, "SPKOUTL"),
ARIZONA_MIXER_WIDGETS(SPKOUTR, "SPKOUTR"),
ARIZONA_MIXER_WIDGETS(SPKDAT1L, "SPKDAT1L"),
ARIZONA_MIXER_WIDGETS(SPKDAT1R, "SPKDAT1R"),
ARIZONA_MIXER_WIDGETS(SPKDAT2L, "SPKDAT2L"),
ARIZONA_MIXER_WIDGETS(SPKDAT2R, "SPKDAT2R"),

ARIZONA_MIXER_WIDGETS(AIF1TX1, "AIF1TX1"),
ARIZONA_MIXER_WIDGETS(AIF1TX2, "AIF1TX2"),
ARIZONA_MIXER_WIDGETS(AIF1TX3, "AIF1TX3"),
ARIZONA_MIXER_WIDGETS(AIF1TX4, "AIF1TX4"),
ARIZONA_MIXER_WIDGETS(AIF1TX5, "AIF1TX5"),
ARIZONA_MIXER_WIDGETS(AIF1TX6, "AIF1TX6"),
ARIZONA_MIXER_WIDGETS(AIF1TX7, "AIF1TX7"),
ARIZONA_MIXER_WIDGETS(AIF1TX8, "AIF1TX8"),

ARIZONA_MIXER_WIDGETS(AIF2TX1, "AIF2TX1"),
ARIZONA_MIXER_WIDGETS(AIF2TX2, "AIF2TX2"),
ARIZONA_MIXER_WIDGETS(AIF2TX3, "AIF2TX3"),
ARIZONA_MIXER_WIDGETS(AIF2TX4, "AIF2TX4"),
ARIZONA_MIXER_WIDGETS(AIF2TX5, "AIF2TX5"),
ARIZONA_MIXER_WIDGETS(AIF2TX6, "AIF2TX6"),

ARIZONA_MIXER_WIDGETS(AIF3TX1, "AIF3TX1"),
ARIZONA_MIXER_WIDGETS(AIF3TX2, "AIF3TX2"),

ARIZONA_MIXER_WIDGETS(SLIMTX1, "SLIMTX1"),
ARIZONA_MIXER_WIDGETS(SLIMTX2, "SLIMTX2"),
ARIZONA_MIXER_WIDGETS(SLIMTX3, "SLIMTX3"),
ARIZONA_MIXER_WIDGETS(SLIMTX4, "SLIMTX4"),
ARIZONA_MIXER_WIDGETS(SLIMTX5, "SLIMTX5"),
ARIZONA_MIXER_WIDGETS(SLIMTX6, "SLIMTX6"),
ARIZONA_MIXER_WIDGETS(SLIMTX7, "SLIMTX7"),
ARIZONA_MIXER_WIDGETS(SLIMTX8, "SLIMTX8"),

ARIZONA_MUX_WIDGETS(ASRC1L, "ASRC1L"),
ARIZONA_MUX_WIDGETS(ASRC1R, "ASRC1R"),
ARIZONA_MUX_WIDGETS(ASRC2L, "ASRC2L"),
ARIZONA_MUX_WIDGETS(ASRC2R, "ASRC2R"),

ARIZONA_DSP_WIDGETS(DSP1, "DSP1"),
ARIZONA_DSP_WIDGETS(DSP2, "DSP2"),
ARIZONA_DSP_WIDGETS(DSP3, "DSP3"),
ARIZONA_DSP_WIDGETS(DSP4, "DSP4"),

ARIZONA_MUX_WIDGETS(ISRC1DEC1, "ISRC1DEC1"),
ARIZONA_MUX_WIDGETS(ISRC1DEC2, "ISRC1DEC2"),
ARIZONA_MUX_WIDGETS(ISRC1DEC3, "ISRC1DEC3"),
ARIZONA_MUX_WIDGETS(ISRC1DEC4, "ISRC1DEC4"),

ARIZONA_MUX_WIDGETS(ISRC1INT1, "ISRC1INT1"),
ARIZONA_MUX_WIDGETS(ISRC1INT2, "ISRC1INT2"),
ARIZONA_MUX_WIDGETS(ISRC1INT3, "ISRC1INT3"),
ARIZONA_MUX_WIDGETS(ISRC1INT4, "ISRC1INT4"),

ARIZONA_MUX_WIDGETS(ISRC2DEC1, "ISRC2DEC1"),
ARIZONA_MUX_WIDGETS(ISRC2DEC2, "ISRC2DEC2"),
ARIZONA_MUX_WIDGETS(ISRC2DEC3, "ISRC2DEC3"),
ARIZONA_MUX_WIDGETS(ISRC2DEC4, "ISRC2DEC4"),

ARIZONA_MUX_WIDGETS(ISRC2INT1, "ISRC2INT1"),
ARIZONA_MUX_WIDGETS(ISRC2INT2, "ISRC2INT2"),
ARIZONA_MUX_WIDGETS(ISRC2INT3, "ISRC2INT3"),
ARIZONA_MUX_WIDGETS(ISRC2INT4, "ISRC2INT4"),

ARIZONA_MUX_WIDGETS(ISRC3DEC1, "ISRC3DEC1"),
ARIZONA_MUX_WIDGETS(ISRC3DEC2, "ISRC3DEC2"),
ARIZONA_MUX_WIDGETS(ISRC3DEC3, "ISRC3DEC3"),
ARIZONA_MUX_WIDGETS(ISRC3DEC4, "ISRC3DEC4"),

ARIZONA_MUX_WIDGETS(ISRC3INT1, "ISRC3INT1"),
ARIZONA_MUX_WIDGETS(ISRC3INT2, "ISRC3INT2"),
ARIZONA_MUX_WIDGETS(ISRC3INT3, "ISRC3INT3"),
ARIZONA_MUX_WIDGETS(ISRC3INT4, "ISRC3INT4"),

SND_SOC_DAPM_OUTPUT("HPOUT1L"),
SND_SOC_DAPM_OUTPUT("HPOUT1R"),
SND_SOC_DAPM_OUTPUT("HPOUT2L"),
SND_SOC_DAPM_OUTPUT("HPOUT2R"),
SND_SOC_DAPM_OUTPUT("HPOUT3L"),
SND_SOC_DAPM_OUTPUT("HPOUT3R"),
SND_SOC_DAPM_OUTPUT("SPKOUTLN"),
SND_SOC_DAPM_OUTPUT("SPKOUTLP"),
SND_SOC_DAPM_OUTPUT("SPKOUTRN"),
SND_SOC_DAPM_OUTPUT("SPKOUTRP"),
SND_SOC_DAPM_OUTPUT("SPKDAT1L"),
SND_SOC_DAPM_OUTPUT("SPKDAT1R"),
SND_SOC_DAPM_OUTPUT("SPKDAT2L"),
SND_SOC_DAPM_OUTPUT("SPKDAT2R"),

SND_SOC_DAPM_OUTPUT("MICSUPP"),
};

#define ARIZONA_MIXER_INPUT_ROUTES(name)	\
	{ name, "Noise Generator", "Noise Generator" }, \
	{ name, "Tone Generator 1", "Tone Generator 1" }, \
	{ name, "Tone Generator 2", "Tone Generator 2" }, \
	{ name, "Haptics", "HAPTICS" }, \
	{ name, "AEC", "AEC Loopback" }, \
	{ name, "IN1L", "IN1L PGA" }, \
	{ name, "IN1R", "IN1R PGA" }, \
	{ name, "IN2L", "IN2L PGA" }, \
	{ name, "IN2R", "IN2R PGA" }, \
	{ name, "IN3L", "IN3L PGA" }, \
	{ name, "IN3R", "IN3R PGA" }, \
	{ name, "IN4L", "IN4L PGA" }, \
	{ name, "IN4R", "IN4R PGA" }, \
	{ name, "Mic Mute Mixer", "Mic Mute Mixer" }, \
	{ name, "AIF1RX1", "AIF1RX1" }, \
	{ name, "AIF1RX2", "AIF1RX2" }, \
	{ name, "AIF1RX3", "AIF1RX3" }, \
	{ name, "AIF1RX4", "AIF1RX4" }, \
	{ name, "AIF1RX5", "AIF1RX5" }, \
	{ name, "AIF1RX6", "AIF1RX6" }, \
	{ name, "AIF1RX7", "AIF1RX7" }, \
	{ name, "AIF1RX8", "AIF1RX8" }, \
	{ name, "AIF2RX1", "AIF2RX1" }, \
	{ name, "AIF2RX2", "AIF2RX2" }, \
	{ name, "AIF2RX3", "AIF2RX3" }, \
	{ name, "AIF2RX4", "AIF2RX4" }, \
	{ name, "AIF2RX5", "AIF2RX5" }, \
	{ name, "AIF2RX6", "AIF2RX6" }, \
	{ name, "AIF3RX1", "AIF3RX1" }, \
	{ name, "AIF3RX2", "AIF3RX2" }, \
	{ name, "SLIMRX1", "SLIMRX1" }, \
	{ name, "SLIMRX2", "SLIMRX2" }, \
	{ name, "SLIMRX3", "SLIMRX3" }, \
	{ name, "SLIMRX4", "SLIMRX4" }, \
	{ name, "SLIMRX5", "SLIMRX5" }, \
	{ name, "SLIMRX6", "SLIMRX6" }, \
	{ name, "SLIMRX7", "SLIMRX7" }, \
	{ name, "SLIMRX8", "SLIMRX8" }, \
	{ name, "EQ1", "EQ1" }, \
	{ name, "EQ2", "EQ2" }, \
	{ name, "EQ3", "EQ3" }, \
	{ name, "EQ4", "EQ4" }, \
	{ name, "DRC1L", "DRC1L" }, \
	{ name, "DRC1R", "DRC1R" }, \
	{ name, "DRC2L", "DRC2L" }, \
	{ name, "DRC2R", "DRC2R" }, \
	{ name, "LHPF1", "LHPF1" }, \
	{ name, "LHPF2", "LHPF2" }, \
	{ name, "LHPF3", "LHPF3" }, \
	{ name, "LHPF4", "LHPF4" }, \
	{ name, "ASRC1L", "ASRC1L" }, \
	{ name, "ASRC1R", "ASRC1R" }, \
	{ name, "ASRC2L", "ASRC2L" }, \
	{ name, "ASRC2R", "ASRC2R" }, \
	{ name, "ISRC1DEC1", "ISRC1DEC1" }, \
	{ name, "ISRC1DEC2", "ISRC1DEC2" }, \
	{ name, "ISRC1DEC3", "ISRC1DEC3" }, \
	{ name, "ISRC1DEC4", "ISRC1DEC4" }, \
	{ name, "ISRC1INT1", "ISRC1INT1" }, \
	{ name, "ISRC1INT2", "ISRC1INT2" }, \
	{ name, "ISRC1INT3", "ISRC1INT3" }, \
	{ name, "ISRC1INT4", "ISRC1INT4" }, \
	{ name, "ISRC2DEC1", "ISRC2DEC1" }, \
	{ name, "ISRC2DEC2", "ISRC2DEC2" }, \
	{ name, "ISRC2DEC3", "ISRC2DEC3" }, \
	{ name, "ISRC2DEC4", "ISRC2DEC4" }, \
	{ name, "ISRC2INT1", "ISRC2INT1" }, \
	{ name, "ISRC2INT2", "ISRC2INT2" }, \
	{ name, "ISRC2INT3", "ISRC2INT3" }, \
	{ name, "ISRC2INT4", "ISRC2INT4" }, \
	{ name, "ISRC3DEC1", "ISRC3DEC1" }, \
	{ name, "ISRC3DEC2", "ISRC3DEC2" }, \
	{ name, "ISRC3DEC3", "ISRC3DEC3" }, \
	{ name, "ISRC3DEC4", "ISRC3DEC4" }, \
	{ name, "ISRC3INT1", "ISRC3INT1" }, \
	{ name, "ISRC3INT2", "ISRC3INT2" }, \
	{ name, "ISRC3INT3", "ISRC3INT3" }, \
	{ name, "ISRC3INT4", "ISRC3INT4" }, \
	{ name, "DSP1.1", "DSP1" }, \
	{ name, "DSP1.2", "DSP1" }, \
	{ name, "DSP1.3", "DSP1" }, \
	{ name, "DSP1.4", "DSP1" }, \
	{ name, "DSP1.5", "DSP1" }, \
	{ name, "DSP1.6", "DSP1" }, \
	{ name, "DSP2.1", "DSP2" }, \
	{ name, "DSP2.2", "DSP2" }, \
	{ name, "DSP2.3", "DSP2" }, \
	{ name, "DSP2.4", "DSP2" }, \
	{ name, "DSP2.5", "DSP2" }, \
	{ name, "DSP2.6", "DSP2" }, \
	{ name, "DSP3.1", "DSP3" }, \
	{ name, "DSP3.2", "DSP3" }, \
	{ name, "DSP3.3", "DSP3" }, \
	{ name, "DSP3.4", "DSP3" }, \
	{ name, "DSP3.5", "DSP3" }, \
	{ name, "DSP3.6", "DSP3" }, \
	{ name, "DSP4.1", "DSP4" }, \
	{ name, "DSP4.2", "DSP4" }, \
	{ name, "DSP4.3", "DSP4" }, \
	{ name, "DSP4.4", "DSP4" }, \
	{ name, "DSP4.5", "DSP4" }, \
	{ name, "DSP4.6", "DSP4" }

static const struct snd_soc_dapm_route wm5110_dapm_routes[] = {
	{ "AIF2 Capture", NULL, "DBVDD2" },
	{ "AIF2 Playback", NULL, "DBVDD2" },

	{ "AIF3 Capture", NULL, "DBVDD3" },
	{ "AIF3 Playback", NULL, "DBVDD3" },

	{ "OUT1L", NULL, "CPVDD" },
	{ "OUT1R", NULL, "CPVDD" },
	{ "OUT2L", NULL, "CPVDD" },
	{ "OUT2R", NULL, "CPVDD" },
	{ "OUT3L", NULL, "CPVDD" },
	{ "OUT3R", NULL, "CPVDD" },

	{ "OUT4L", NULL, "SPKVDDL" },
	{ "OUT4R", NULL, "SPKVDDR" },

	{ "OUT1L", NULL, "SYSCLK" },
	{ "OUT1R", NULL, "SYSCLK" },
	{ "OUT2L", NULL, "SYSCLK" },
	{ "OUT2R", NULL, "SYSCLK" },
	{ "OUT3L", NULL, "SYSCLK" },
	{ "OUT3R", NULL, "SYSCLK" },
	{ "OUT4L", NULL, "SYSCLK" },
	{ "OUT4R", NULL, "SYSCLK" },
	{ "OUT5L", NULL, "SYSCLK" },
	{ "OUT5R", NULL, "SYSCLK" },
	{ "OUT6L", NULL, "SYSCLK" },
	{ "OUT6R", NULL, "SYSCLK" },

	{ "IN1L", NULL, "SYSCLK" },
	{ "IN1R", NULL, "SYSCLK" },
	{ "IN2L", NULL, "SYSCLK" },
	{ "IN2R", NULL, "SYSCLK" },
	{ "IN3L", NULL, "SYSCLK" },
	{ "IN3R", NULL, "SYSCLK" },
	{ "IN4L", NULL, "SYSCLK" },
	{ "IN4R", NULL, "SYSCLK" },

	{ "ASRC1L", NULL, "SYSCLK" },
	{ "ASRC1R", NULL, "SYSCLK" },
	{ "ASRC2L", NULL, "SYSCLK" },
	{ "ASRC2R", NULL, "SYSCLK" },

	{ "ASRC1L", NULL, "ASYNCCLK" },
	{ "ASRC1R", NULL, "ASYNCCLK" },
	{ "ASRC2L", NULL, "ASYNCCLK" },
	{ "ASRC2R", NULL, "ASYNCCLK" },

	{ "MICBIAS1", NULL, "MICVDD" },
	{ "MICBIAS2", NULL, "MICVDD" },
	{ "MICBIAS3", NULL, "MICVDD" },

	{ "Noise Generator", NULL, "SYSCLK" },
	{ "Tone Generator 1", NULL, "SYSCLK" },
	{ "Tone Generator 2", NULL, "SYSCLK" },

	{ "Noise Generator", NULL, "NOISE" },
	{ "Tone Generator 1", NULL, "TONE" },
	{ "Tone Generator 2", NULL, "TONE" },

	{ "AIF1 Capture", NULL, "AIF1TX1" },
	{ "AIF1 Capture", NULL, "AIF1TX2" },
	{ "AIF1 Capture", NULL, "AIF1TX3" },
	{ "AIF1 Capture", NULL, "AIF1TX4" },
	{ "AIF1 Capture", NULL, "AIF1TX5" },
	{ "AIF1 Capture", NULL, "AIF1TX6" },
	{ "AIF1 Capture", NULL, "AIF1TX7" },
	{ "AIF1 Capture", NULL, "AIF1TX8" },

	{ "AIF1RX1", NULL, "AIF1 Playback" },
	{ "AIF1RX2", NULL, "AIF1 Playback" },
	{ "AIF1RX3", NULL, "AIF1 Playback" },
	{ "AIF1RX4", NULL, "AIF1 Playback" },
	{ "AIF1RX5", NULL, "AIF1 Playback" },
	{ "AIF1RX6", NULL, "AIF1 Playback" },
	{ "AIF1RX7", NULL, "AIF1 Playback" },
	{ "AIF1RX8", NULL, "AIF1 Playback" },

	{ "AIF2 Capture", NULL, "AIF2TX1" },
	{ "AIF2 Capture", NULL, "AIF2TX2" },
	{ "AIF2 Capture", NULL, "AIF2TX3" },
	{ "AIF2 Capture", NULL, "AIF2TX4" },
	{ "AIF2 Capture", NULL, "AIF2TX5" },
	{ "AIF2 Capture", NULL, "AIF2TX6" },

	{ "AIF2RX1", NULL, "AIF2 Playback" },
	{ "AIF2RX2", NULL, "AIF2 Playback" },
	{ "AIF2RX3", NULL, "AIF2 Playback" },
	{ "AIF2RX4", NULL, "AIF2 Playback" },
	{ "AIF2RX5", NULL, "AIF2 Playback" },
	{ "AIF2RX6", NULL, "AIF2 Playback" },

	{ "AIF3 Capture", NULL, "AIF3TX1" },
	{ "AIF3 Capture", NULL, "AIF3TX2" },

	{ "AIF3RX1", NULL, "AIF3 Playback" },
	{ "AIF3RX2", NULL, "AIF3 Playback" },

	{ "Slim1 Capture", NULL, "SLIMTX1" },
	{ "Slim1 Capture", NULL, "SLIMTX2" },
	{ "Slim1 Capture", NULL, "SLIMTX3" },
	{ "Slim1 Capture", NULL, "SLIMTX4" },

	{ "SLIMRX1", NULL, "Slim1 Playback" },
	{ "SLIMRX2", NULL, "Slim1 Playback" },
	{ "SLIMRX3", NULL, "Slim1 Playback" },
	{ "SLIMRX4", NULL, "Slim1 Playback" },

	{ "Slim2 Capture", NULL, "SLIMTX5" },
	{ "Slim2 Capture", NULL, "SLIMTX6" },

	{ "SLIMRX5", NULL, "Slim2 Playback" },
	{ "SLIMRX6", NULL, "Slim2 Playback" },

	{ "Slim3 Capture", NULL, "SLIMTX7" },
	{ "Slim3 Capture", NULL, "SLIMTX8" },

	{ "SLIMRX7", NULL, "Slim3 Playback" },
	{ "SLIMRX8", NULL, "Slim3 Playback" },

	{ "AIF1 Playback", NULL, "SYSCLK" },
	{ "AIF2 Playback", NULL, "SYSCLK" },
	{ "AIF3 Playback", NULL, "SYSCLK" },
	{ "Slim1 Playback", NULL, "SYSCLK" },
	{ "Slim2 Playback", NULL, "SYSCLK" },
	{ "Slim3 Playback", NULL, "SYSCLK" },

	{ "AIF1 Capture", NULL, "SYSCLK" },
	{ "AIF2 Capture", NULL, "SYSCLK" },
	{ "AIF3 Capture", NULL, "SYSCLK" },
	{ "Slim1 Capture", NULL, "SYSCLK" },
	{ "Slim2 Capture", NULL, "SYSCLK" },
	{ "Slim3 Capture", NULL, "SYSCLK" },

	{ "Voice Control DSP", NULL, "DSP3" },

	{ "Audio Trace DSP", NULL, "DSP1" },

	{ "IN1L PGA", NULL, "IN1L" },
	{ "IN1R PGA", NULL, "IN1R" },

	{ "IN2L PGA", NULL, "IN2L" },
	{ "IN2R PGA", NULL, "IN2R" },

	{ "IN3L PGA", NULL, "IN3L" },
	{ "IN3R PGA", NULL, "IN3R" },

	{ "IN4L PGA", NULL, "IN4L" },
	{ "IN4R PGA", NULL, "IN4R" },

	ARIZONA_MIXER_ROUTES("OUT1L", "HPOUT1L"),
	ARIZONA_MIXER_ROUTES("OUT1R", "HPOUT1R"),
	ARIZONA_MIXER_ROUTES("OUT2L", "HPOUT2L"),
	ARIZONA_MIXER_ROUTES("OUT2R", "HPOUT2R"),
	ARIZONA_MIXER_ROUTES("OUT3L", "HPOUT3L"),
	ARIZONA_MIXER_ROUTES("OUT3R", "HPOUT3R"),

	ARIZONA_MIXER_ROUTES("OUT4L", "SPKOUTL"),
	ARIZONA_MIXER_ROUTES("OUT4R", "SPKOUTR"),
	ARIZONA_MIXER_ROUTES("OUT5L", "SPKDAT1L"),
	ARIZONA_MIXER_ROUTES("OUT5R", "SPKDAT1R"),
	ARIZONA_MIXER_ROUTES("OUT6L", "SPKDAT2L"),
	ARIZONA_MIXER_ROUTES("OUT6R", "SPKDAT2R"),

	ARIZONA_MIXER_ROUTES("PWM1 Driver", "PWM1"),
	ARIZONA_MIXER_ROUTES("PWM2 Driver", "PWM2"),

	ARIZONA_MIXER_ROUTES("AIF1TX1", "AIF1TX1"),
	ARIZONA_MIXER_ROUTES("AIF1TX2", "AIF1TX2"),
	ARIZONA_MIXER_ROUTES("AIF1TX3", "AIF1TX3"),
	ARIZONA_MIXER_ROUTES("AIF1TX4", "AIF1TX4"),
	ARIZONA_MIXER_ROUTES("AIF1TX5", "AIF1TX5"),
	ARIZONA_MIXER_ROUTES("AIF1TX6", "AIF1TX6"),
	ARIZONA_MIXER_ROUTES("AIF1TX7", "AIF1TX7"),
	ARIZONA_MIXER_ROUTES("AIF1TX8", "AIF1TX8"),

	ARIZONA_MIXER_ROUTES("AIF2TX1", "AIF2TX1"),
	ARIZONA_MIXER_ROUTES("AIF2TX2", "AIF2TX2"),
	ARIZONA_MIXER_ROUTES("AIF2TX3", "AIF2TX3"),
	ARIZONA_MIXER_ROUTES("AIF2TX4", "AIF2TX4"),
	ARIZONA_MIXER_ROUTES("AIF2TX5", "AIF2TX5"),
	ARIZONA_MIXER_ROUTES("AIF2TX6", "AIF2TX6"),

	ARIZONA_MIXER_ROUTES("AIF3TX1", "AIF3TX1"),
	ARIZONA_MIXER_ROUTES("AIF3TX2", "AIF3TX2"),

	ARIZONA_MIXER_ROUTES("SLIMTX1", "SLIMTX1"),
	ARIZONA_MIXER_ROUTES("SLIMTX2", "SLIMTX2"),
	ARIZONA_MIXER_ROUTES("SLIMTX3", "SLIMTX3"),
	ARIZONA_MIXER_ROUTES("SLIMTX4", "SLIMTX4"),
	ARIZONA_MIXER_ROUTES("SLIMTX5", "SLIMTX5"),
	ARIZONA_MIXER_ROUTES("SLIMTX6", "SLIMTX6"),
	ARIZONA_MIXER_ROUTES("SLIMTX7", "SLIMTX7"),
	ARIZONA_MIXER_ROUTES("SLIMTX8", "SLIMTX8"),

	ARIZONA_MIXER_ROUTES("EQ1", "EQ1"),
	ARIZONA_MIXER_ROUTES("EQ2", "EQ2"),
	ARIZONA_MIXER_ROUTES("EQ3", "EQ3"),
	ARIZONA_MIXER_ROUTES("EQ4", "EQ4"),

	ARIZONA_MIXER_ROUTES("DRC1L", "DRC1L"),
	ARIZONA_MIXER_ROUTES("DRC1R", "DRC1R"),
	ARIZONA_MIXER_ROUTES("DRC2L", "DRC2L"),
	ARIZONA_MIXER_ROUTES("DRC2R", "DRC2R"),

	ARIZONA_MIXER_ROUTES("LHPF1", "LHPF1"),
	ARIZONA_MIXER_ROUTES("LHPF2", "LHPF2"),
	ARIZONA_MIXER_ROUTES("LHPF3", "LHPF3"),
	ARIZONA_MIXER_ROUTES("LHPF4", "LHPF4"),

	ARIZONA_MIXER_ROUTES("Mic Mute Mixer", "Noise"),
	ARIZONA_MIXER_ROUTES("Mic Mute Mixer", "Mic"),

	ARIZONA_MUX_ROUTES("ASRC1L", "ASRC1L"),
	ARIZONA_MUX_ROUTES("ASRC1R", "ASRC1R"),
	ARIZONA_MUX_ROUTES("ASRC2L", "ASRC2L"),
	ARIZONA_MUX_ROUTES("ASRC2R", "ASRC2R"),

	ARIZONA_DSP_ROUTES("DSP1"),
	ARIZONA_DSP_ROUTES("DSP2"),
	ARIZONA_DSP_ROUTES("DSP3"),
	ARIZONA_DSP_ROUTES("DSP4"),

	ARIZONA_MUX_ROUTES("ISRC1INT1", "ISRC1INT1"),
	ARIZONA_MUX_ROUTES("ISRC1INT2", "ISRC1INT2"),
	ARIZONA_MUX_ROUTES("ISRC1INT3", "ISRC1INT3"),
	ARIZONA_MUX_ROUTES("ISRC1INT4", "ISRC1INT4"),

	ARIZONA_MUX_ROUTES("ISRC1DEC1", "ISRC1DEC1"),
	ARIZONA_MUX_ROUTES("ISRC1DEC2", "ISRC1DEC2"),
	ARIZONA_MUX_ROUTES("ISRC1DEC3", "ISRC1DEC3"),
	ARIZONA_MUX_ROUTES("ISRC1DEC4", "ISRC1DEC4"),

	ARIZONA_MUX_ROUTES("ISRC2INT1", "ISRC2INT1"),
	ARIZONA_MUX_ROUTES("ISRC2INT2", "ISRC2INT2"),
	ARIZONA_MUX_ROUTES("ISRC2INT3", "ISRC2INT3"),
	ARIZONA_MUX_ROUTES("ISRC2INT4", "ISRC2INT4"),

	ARIZONA_MUX_ROUTES("ISRC2DEC1", "ISRC2DEC1"),
	ARIZONA_MUX_ROUTES("ISRC2DEC2", "ISRC2DEC2"),
	ARIZONA_MUX_ROUTES("ISRC2DEC3", "ISRC2DEC3"),
	ARIZONA_MUX_ROUTES("ISRC2DEC4", "ISRC2DEC4"),

	ARIZONA_MUX_ROUTES("ISRC3INT1", "ISRC3INT1"),
	ARIZONA_MUX_ROUTES("ISRC3INT2", "ISRC3INT2"),
	ARIZONA_MUX_ROUTES("ISRC3INT3", "ISRC3INT3"),
	ARIZONA_MUX_ROUTES("ISRC3INT4", "ISRC3INT4"),

	ARIZONA_MUX_ROUTES("ISRC3DEC1", "ISRC3DEC1"),
	ARIZONA_MUX_ROUTES("ISRC3DEC2", "ISRC3DEC2"),
	ARIZONA_MUX_ROUTES("ISRC3DEC3", "ISRC3DEC3"),
	ARIZONA_MUX_ROUTES("ISRC3DEC4", "ISRC3DEC4"),

	{ "AEC Loopback", "HPOUT1L", "OUT1L" },
	{ "AEC Loopback", "HPOUT1R", "OUT1R" },
	{ "HPOUT1L", NULL, "OUT1L" },
	{ "HPOUT1R", NULL, "OUT1R" },

	{ "AEC Loopback", "HPOUT2L", "OUT2L" },
	{ "AEC Loopback", "HPOUT2R", "OUT2R" },
	{ "HPOUT2L", NULL, "OUT2L" },
	{ "HPOUT2R", NULL, "OUT2R" },

	{ "AEC Loopback", "HPOUT3L", "OUT3L" },
	{ "AEC Loopback", "HPOUT3R", "OUT3R" },
	{ "HPOUT3L", NULL, "OUT3L" },
	{ "HPOUT3R", NULL, "OUT3R" },

	{ "AEC Loopback", "SPKOUTL", "OUT4L" },
	{ "SPKOUTLN", NULL, "OUT4L" },
	{ "SPKOUTLP", NULL, "OUT4L" },

	{ "AEC Loopback", "SPKOUTR", "OUT4R" },
	{ "SPKOUTRN", NULL, "OUT4R" },
	{ "SPKOUTRP", NULL, "OUT4R" },

	{ "AEC Loopback", "SPKDAT1L", "OUT5L" },
	{ "AEC Loopback", "SPKDAT1R", "OUT5R" },
	{ "SPKDAT1L", NULL, "OUT5L" },
	{ "SPKDAT1R", NULL, "OUT5R" },

	{ "AEC Loopback", "SPKDAT2L", "OUT6L" },
	{ "AEC Loopback", "SPKDAT2R", "OUT6R" },
	{ "SPKDAT2L", NULL, "OUT6L" },
	{ "SPKDAT2R", NULL, "OUT6R" },

	WM5110_RXANC_INPUT_ROUTES("RXANCL", "RXANCL"),
	WM5110_RXANC_INPUT_ROUTES("RXANCR", "RXANCR"),

	WM5110_RXANC_OUTPUT_ROUTES("OUT1L", "HPOUT1L"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT1R", "HPOUT1R"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT2L", "HPOUT2L"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT2R", "HPOUT2R"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT3L", "HPOUT3L"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT3R", "HPOUT3R"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT4L", "SPKOUTL"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT4R", "SPKOUTR"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT5L", "SPKDAT1L"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT5R", "SPKDAT1R"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT6L", "SPKDAT2L"),
	WM5110_RXANC_OUTPUT_ROUTES("OUT6R", "SPKDAT2R"),

	{ "MICSUPP", NULL, "SYSCLK" },

	{ "DRC1 Signal Activity", NULL, "SYSCLK" },
	{ "DRC2 Signal Activity", NULL, "SYSCLK" },
	{ "DRC1 Signal Activity", NULL, "DRC1L" },
	{ "DRC1 Signal Activity", NULL, "DRC1R" },
	{ "DRC2 Signal Activity", NULL, "DRC2L" },
	{ "DRC2 Signal Activity", NULL, "DRC2R" },

	{ "DSP Voice Trigger", NULL, "SYSCLK" },
	{ "DSP Voice Trigger", NULL, "DSP3 Voice Trigger" },
	{ "DSP3 Voice Trigger", "Switch", "DSP3" },
};

static int wm5110_set_fll(struct snd_soc_codec *codec, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm5110_priv *wm5110 = snd_soc_codec_get_drvdata(codec);

	switch (fll_id) {
	case WM5110_FLL1:
		return arizona_set_fll(&wm5110->fll[0], source, Fref, Fout);
	case WM5110_FLL2:
		return arizona_set_fll(&wm5110->fll[1], source, Fref, Fout);
	case WM5110_FLL1_REFCLK:
		return arizona_set_fll_refclk(&wm5110->fll[0], source, Fref,
					      Fout);
	case WM5110_FLL2_REFCLK:
		return arizona_set_fll_refclk(&wm5110->fll[1], source, Fref,
					      Fout);
	default:
		return -EINVAL;
	}
}

#define WM5110_RATES SNDRV_PCM_RATE_KNOT

#define WM5110_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm5110_dai[] = {
	{
		.name = "wm5110-aif1",
		.id = 1,
		.base = ARIZONA_AIF1_BCLK_CTRL,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF1 Capture",
			 .channels_min = 1,
			 .channels_max = 8,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_dai_ops,
		.symmetric_rates = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "wm5110-aif2",
		.id = 2,
		.base = ARIZONA_AIF2_BCLK_CTRL,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 6,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF2 Capture",
			 .channels_min = 1,
			 .channels_max = 6,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_dai_ops,
		.symmetric_rates = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "wm5110-aif3",
		.id = 3,
		.base = ARIZONA_AIF3_BCLK_CTRL,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "AIF3 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_dai_ops,
		.symmetric_rates = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "wm5110-slim1",
		.id = 4,
		.playback = {
			.stream_name = "Slim1 Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "Slim1 Capture",
			 .channels_min = 1,
			 .channels_max = 4,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_simple_dai_ops,
	},
	{
		.name = "wm5110-slim2",
		.id = 5,
		.playback = {
			.stream_name = "Slim2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "Slim2 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_simple_dai_ops,
	},
	{
		.name = "wm5110-slim3",
		.id = 6,
		.playback = {
			.stream_name = "Slim3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.capture = {
			 .stream_name = "Slim3 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM5110_RATES,
			 .formats = WM5110_FORMATS,
		 },
		.ops = &arizona_simple_dai_ops,
	},
	{
		.name = "wm5110-cpu-voicectrl",
		.capture = {
			.stream_name = "Voice Control CPU",
			.channels_min = 1,
			.channels_max = 1,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.compress_new = snd_soc_new_compress,
	},
	{
		.name = "wm5110-dsp-voicectrl",
		.capture = {
			.stream_name = "Voice Control DSP",
			.channels_min = 1,
			.channels_max = 1,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
	},
	{
		.name = "wm5110-cpu-trace",
		.capture = {
			.stream_name = "Audio Trace CPU",
			.channels_min = 1,
			.channels_max = 6,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
		.compress_new = snd_soc_new_compress,
	},
	{
		.name = "wm5110-dsp-trace",
		.capture = {
			.stream_name = "Audio Trace DSP",
			.channels_min = 1,
			.channels_max = 6,
			.rates = WM5110_RATES,
			.formats = WM5110_FORMATS,
		},
	},
};

static int wm5110_open(struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct wm5110_priv *priv = snd_soc_platform_get_drvdata(rtd->platform);
	struct arizona *arizona = priv->core.arizona;
	int n_adsp;

	if (strcmp(rtd->codec_dai->name, "wm5110-dsp-voicectrl") == 0) {
		n_adsp = 2;
	} else if (strcmp(rtd->codec_dai->name, "wm5110-dsp-trace") == 0) {
		n_adsp = 0;
	} else {
		dev_err(arizona->dev,
			"No suitable compressed stream for DAI '%s'\n",
			rtd->codec_dai->name);
		return -EINVAL;
	}

	return wm_adsp_compr_open(&priv->core.adsp[n_adsp], stream);
}

static irqreturn_t wm5110_adsp2_irq(int irq, void *data)
{
	struct wm5110_priv *priv = data;
	struct arizona *arizona = priv->core.arizona;
	struct arizona_voice_trigger_info info;
	int serviced = 0;
	int i, ret;

	for (i = 0; i < WM5110_NUM_ADSP; ++i) {
		ret = wm_adsp_compr_handle_irq(&priv->core.adsp[i]);
		if (ret != -ENODEV)
			serviced++;
		if (ret == WM_ADSP_COMPR_VOICE_TRIGGER) {
			info.core = i;
			arizona_call_notifiers(arizona,
					       ARIZONA_NOTIFY_VOICE_TRIGGER,
					       &info);
		}
	}

	if (!serviced) {
		dev_err(arizona->dev, "Spurious compressed data IRQ\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int wm5110_codec_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct wm5110_priv *priv = snd_soc_codec_get_drvdata(codec);
	int i, ret;

	priv->core.arizona->dapm = dapm;

	arizona_init_spk(codec);
	arizona_init_gpio(codec);
	arizona_init_mono(codec);
	arizona_init_notifiers(codec);

	for (i = 0; i < WM5110_NUM_ADSP; ++i) {
		ret = wm_adsp2_codec_probe(&priv->core.adsp[i], codec);
		if (ret)
			goto err_adsp2_codec_probe;
	}

	ret = snd_soc_add_codec_controls(codec,
					 arizona_adsp2_rate_controls,
					 WM5110_NUM_ADSP);
	if (ret)
		goto err_adsp2_codec_probe;

	snd_soc_dapm_disable_pin(dapm, "HAPTICS");

	return 0;

err_adsp2_codec_probe:
	for (--i; i >= 0; --i)
		wm_adsp2_codec_remove(&priv->core.adsp[i], codec);

	return ret;
}

static int wm5110_codec_remove(struct snd_soc_codec *codec)
{
	struct wm5110_priv *priv = snd_soc_codec_get_drvdata(codec);
	int i;

	for (i = 0; i < WM5110_NUM_ADSP; ++i)
		wm_adsp2_codec_remove(&priv->core.adsp[i], codec);

	priv->core.arizona->dapm = NULL;

	return 0;
}

#define WM5110_DIG_VU 0x0200

static unsigned int wm5110_digital_vu[] = {
	ARIZONA_DAC_DIGITAL_VOLUME_1L,
	ARIZONA_DAC_DIGITAL_VOLUME_1R,
	ARIZONA_DAC_DIGITAL_VOLUME_2L,
	ARIZONA_DAC_DIGITAL_VOLUME_2R,
	ARIZONA_DAC_DIGITAL_VOLUME_3L,
	ARIZONA_DAC_DIGITAL_VOLUME_3R,
	ARIZONA_DAC_DIGITAL_VOLUME_4L,
	ARIZONA_DAC_DIGITAL_VOLUME_4R,
	ARIZONA_DAC_DIGITAL_VOLUME_5L,
	ARIZONA_DAC_DIGITAL_VOLUME_5R,
	ARIZONA_DAC_DIGITAL_VOLUME_6L,
	ARIZONA_DAC_DIGITAL_VOLUME_6R,
};

static struct regmap *wm5110_get_regmap(struct device *dev)
{
	struct wm5110_priv *priv = dev_get_drvdata(dev);

	return priv->core.arizona->regmap;
}

static const struct snd_soc_codec_driver soc_codec_dev_wm5110 = {
	.probe = wm5110_codec_probe,
	.remove = wm5110_codec_remove,
	.get_regmap = wm5110_get_regmap,

	.idle_bias_off = true,

	.set_sysclk = arizona_set_sysclk,
	.set_pll = wm5110_set_fll,

	.component_driver = {
		.controls		= wm5110_snd_controls,
		.num_controls		= ARRAY_SIZE(wm5110_snd_controls),
		.dapm_widgets		= wm5110_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(wm5110_dapm_widgets),
		.dapm_routes		= wm5110_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(wm5110_dapm_routes),
	},
};

static struct snd_compr_ops wm5110_compr_ops = {
	.open = wm5110_open,
	.free = wm_adsp_compr_free,
	.set_params = wm_adsp_compr_set_params,
	.get_caps = wm_adsp_compr_get_caps,
	.trigger = wm_adsp_compr_trigger,
	.pointer = wm_adsp_compr_pointer,
	.copy = wm_adsp_compr_copy,
};

static struct snd_soc_platform_driver wm5110_compr_platform = {
	.compr_ops = &wm5110_compr_ops,
};

static int wm5110_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct wm5110_priv *wm5110;
	int i, ret;

	wm5110 = devm_kzalloc(&pdev->dev, sizeof(struct wm5110_priv),
			      GFP_KERNEL);
	if (wm5110 == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, wm5110);

	wm5110->core.arizona = arizona;
	wm5110->core.num_inputs = 8;

	for (i = 0; i < WM5110_NUM_ADSP; i++) {
		wm5110->core.adsp[i].part = "wm5110";
		wm5110->core.adsp[i].num = i + 1;
		wm5110->core.adsp[i].type = WMFW_ADSP2;
		wm5110->core.adsp[i].dev = arizona->dev;
		wm5110->core.adsp[i].regmap = arizona->regmap;

		wm5110->core.adsp[i].base = ARIZONA_DSP1_CONTROL_1
			+ (0x100 * i);
		wm5110->core.adsp[i].mem = wm5110_dsp_regions[i];
		wm5110->core.adsp[i].num_mems
			= ARRAY_SIZE(wm5110_dsp1_regions);

		ret = wm_adsp2_init(&wm5110->core.adsp[i]);
		if (ret != 0)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm5110->fll); i++)
		wm5110->fll[i].vco_mult = 3;

	arizona_init_fll(arizona, 1, ARIZONA_FLL1_CONTROL_1 - 1,
			 ARIZONA_IRQ_FLL1_LOCK, ARIZONA_IRQ_FLL1_CLOCK_OK,
			 &wm5110->fll[0]);
	arizona_init_fll(arizona, 2, ARIZONA_FLL2_CONTROL_1 - 1,
			 ARIZONA_IRQ_FLL2_LOCK, ARIZONA_IRQ_FLL2_CLOCK_OK,
			 &wm5110->fll[1]);

	/* SR2 fixed at 8kHz, SR3 fixed at 16kHz */
	regmap_update_bits(arizona->regmap, ARIZONA_SAMPLE_RATE_2,
			   ARIZONA_SAMPLE_RATE_2_MASK, 0x11);
	regmap_update_bits(arizona->regmap, ARIZONA_SAMPLE_RATE_3,
			   ARIZONA_SAMPLE_RATE_3_MASK, 0x12);

	for (i = 0; i < ARRAY_SIZE(wm5110_dai); i++)
		arizona_init_dai(&wm5110->core, i);

	/* Latch volume update bits */
	for (i = 0; i < ARRAY_SIZE(wm5110_digital_vu); i++)
		regmap_update_bits(arizona->regmap, wm5110_digital_vu[i],
				   WM5110_DIG_VU, WM5110_DIG_VU);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);

	ret = arizona_request_irq(arizona, ARIZONA_IRQ_DSP_IRQ1,
				  "ADSP2 Compressed IRQ", wm5110_adsp2_irq,
				  wm5110);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request DSP IRQ: %d\n", ret);
		return ret;
	}

	ret = arizona_init_spk_irqs(arizona);
	if (ret < 0)
		goto err_dsp_irq;

	ret = snd_soc_register_platform(&pdev->dev, &wm5110_compr_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register platform: %d\n", ret);
		goto err_spk_irqs;
	}

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wm5110,
				      wm5110_dai, ARRAY_SIZE(wm5110_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);
		goto err_platform;
	}

	return ret;

err_platform:
	snd_soc_unregister_platform(&pdev->dev);
err_spk_irqs:
	arizona_free_spk_irqs(arizona);
err_dsp_irq:
	arizona_free_irq(arizona, ARIZONA_IRQ_DSP_IRQ1, wm5110);

	return ret;
}

static int wm5110_remove(struct platform_device *pdev)
{
	struct wm5110_priv *wm5110 = platform_get_drvdata(pdev);
	struct arizona *arizona = wm5110->core.arizona;
	int i;

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_codec(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	for (i = 0; i < WM5110_NUM_ADSP; i++)
		wm_adsp2_remove(&wm5110->core.adsp[i]);

	arizona_free_spk_irqs(arizona);

	arizona_free_irq(arizona, ARIZONA_IRQ_DSP_IRQ1, wm5110);

	return 0;
}

static struct platform_driver wm5110_codec_driver = {
	.driver = {
		.name = "wm5110-codec",
	},
	.probe = wm5110_probe,
	.remove = wm5110_remove,
};

module_platform_driver(wm5110_codec_driver);

MODULE_DESCRIPTION("ASoC WM5110 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm5110-codec");
