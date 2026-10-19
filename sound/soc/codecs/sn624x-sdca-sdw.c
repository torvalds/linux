// SPDX-License-Identifier: GPL-2.0-only
//
// sn624x-sdw-sdca.c -- SN624X SDCA ALSA SoC SoundWire audio driver
//
// Copyright(c) 2025 Senary Semiconductor Corp.
//

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/find.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>

#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdca.h>
#include <sound/sdca_asoc.h>
#include <sound/sdca_function.h>
#include <sound/sdca_regmap.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "sn624x-sdca.h"

/* Resume wait after bus re-enumeration (rt722-style probe timeout) */
#define SN624X_PROBE_TIMEOUT_MS			5000U
#define SN624X_PWR_D0				SDCA_PDE_PS0
#define SN624X_PWR_D3				SDCA_PDE_PS3
#define SN624X_MUTE_ON				1
/*
 * Budget for ACTUAL_PS poll via sdca_asoc_pde_poll_actual_ps() (100 polls).
 * Matches previous ~2s hand-rolled loop for slow PDE transitions.
 */
#define SN624X_PDE_PS_POLL_BUDGET_US		2000000U

/*
 * Prefer ACPI mipi-sdca-function-initialization-table via
 * sdca_regmap_write_init(). If absent or DisCo parse is incomplete,
 * a temporary vendor-window OEM fallback is used until firmware tables land.
 *
 * Jack detection is poll-driven (UAJ pulse + GE35); SDCA INTMASK bits stay
 * masked so interrupt storms cannot schedule extra work.
 */

struct sn624x_sdca_priv;

static void sn624x_sdca_sdca_irq_mask_all(struct sn624x_sdca_priv *sn624x);
static int sn624x_uaj_ge35_apply_detected_mode(struct device *dev,
					       struct sn624x_sdca_priv *sn624x);
#define SN624X_DBG(dev, fmt, ...) dev_dbg(dev, "sn624x: " fmt, ##__VA_ARGS__)

/*
 * SDCA control addresses via SDW_SDCA_CTL(fun, ent, ctl, ch).
 * Pass ch=0 for single-value controls (power/rate/status); use CH_L/CH_R
 * only for multi-channel FU mute/volume. CH_ENABLE_* are not SDCA Control
 * Prefix addresses — keep as raw windows.
 */
/* Speaker amp (function 4) — PDE/FU power/mute/vol; rate on CS01 */
#define SN624X_REG_PWR_STATE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_ACTUAL_PS, 0)
#define SN624X_REG_PWR_STATE_WRITE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_REQUESTED_PS, 0)
#define SN624X_REG_RATE_SEL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_CS01, \
		     SN624X_SDCA_CTL_SAMPLE_FREQ_INDEX, 0)
#define SN624X_REG_CH_ENABLE_A			0x00000220u
#define SN624X_REG_CH_ENABLE_B			0x00000230u
#define SN624X_REG_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_USER_FU10, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_L)
#define SN624X_REG_CHR_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_USER_FU10, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_R)
#define SN624X_REG_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_USER_FU10, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_L)
#define SN624X_REG_RIGHT_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_SPEAKER_AMP, SN624X_SDCA_ENT_USER_FU10, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_R)

/* Jack playback (function 1) */
#define SN624X_REG_JACK_OUT_PWR_STATE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_ACTUAL_PS, 0)
#define SN624X_REG_JACK_OUT_PWR_STATE_WRITE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_REQUESTED_PS, 0)
#define SN624X_REG_JACK_OUT_RATE_SEL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_CS01, \
		     SN624X_SDCA_CTL_SAMPLE_FREQ_INDEX, 0)
#define SN624X_REG_JACK_OUT_CH_ENABLE_A		0x00000120u
#define SN624X_REG_JACK_OUT_CH_ENABLE_B		0x00000130u
#define SN624X_REG_JACK_OUT_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_L)
#define SN624X_REG_JACK_OUT_CHR_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_R)
#define SN624X_REG_JACK_OUT_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_L)
#define SN624X_REG_JACK_OUT_CLUSTER_INDEX \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_CRU05, \
		     SN624X_SDCA_CTL_CLUSTER_INDEX, 0)

/* Jack headset capture (function 1) */
#define SN624X_REG_JACK_CAP_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU1E, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_L)
#define SN624X_REG_JACK_CAP_CHR_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU1E, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_R)
#define SN624X_REG_JACK_CAP_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_USER_FU1E, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_L)
#define SN624X_REG_JACK_CAP_PWR_STATE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_PDE04, \
		     SDCA_CTL_PDE_ACTUAL_PS, 0)
#define SN624X_REG_JACK_CAP_PWR_STATE_WRITE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_PDE04, \
		     SDCA_CTL_PDE_REQUESTED_PS, 0)
#define SN624X_REG_JACK_CAP_RATE_SEL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_CS02, \
		     SN624X_SDCA_CTL_SAMPLE_FREQ_INDEX, 0)
#define SN624X_REG_JACK_CAP_CH_ENABLE_A		0x00000420u	/* low bits: 0x3=both */
#define SN624X_REG_JACK_CAP_CH_ENABLE_B		0x00000430u

#define SN624X_REG_JACK_FUN_STATUS_CTL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_ENTITY0, \
		     SN624X_SDCA_CTL_FUNCTION_STATUS, 0)
#define SN624X_JACK_FUN_NEEDS_INIT		BIT(5)
#define SN624X_JACK_FUN_HAS_BEEN_RESET		BIT(6)

/* DMIC capture (function 2) */
#define SN624X_REG_DMIC_CAP_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_L)
#define SN624X_REG_DMIC_CAP_CHR_VOL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_VOLUME, SN624X_SDCA_CH_R)
/* Q7.8 channel volume: integer in bits 15:8, fraction in bits 7:0 */
#define SN624X_VOL_Q78(int_part, frac_part) \
	((((unsigned int)(int_part) & 0xff) << 8) | ((unsigned int)(frac_part) & 0xff))
#define SN624X_REG_DMIC_CAP_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_L)
#define SN624X_REG_DMIC_CAP_CHR_MUTE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_USER_FU14, \
		     SN624X_SDCA_CTL_FU_MUTE, SN624X_SDCA_CH_R)
#define SN624X_REG_DMIC_CAP_RATE_SEL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_CS02, \
		     SN624X_SDCA_CTL_SAMPLE_FREQ_INDEX, 0)
/* OEM sheet used ctl=0; SDCA Actual PS is 0x10 — poll the standard selector */
#define SN624X_REG_DMIC_CAP_PWR_STATE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_ACTUAL_PS, 0)
#define SN624X_REG_DMIC_CAP_PWR_STATE_WRITE \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_PDE03, \
		     SDCA_CTL_PDE_REQUESTED_PS, 0)
#define SN624X_REG_DMIC_CAP_CH_ENABLE_A		0x00000520u	/* low bits: 0x3=both */
#define SN624X_REG_DMIC_CAP_CH_ENABLE_B		0x00000530u

#define SN624X_REG_DMIC_FUN_STATUS_CTL \
	SDW_SDCA_CTL(SN624X_FUNC_NUM_MIC_ARRAY, SN624X_SDCA_ENT_ENTITY0, \
		     SN624X_SDCA_CTL_FUNCTION_STATUS, 0)

/*
 * Write REQUESTED_PS then poll ACTUAL_PS with the shared SDCA helper
 * (sdca_asoc_pde_poll_actual_ps). Caller must not hand-roll ACTUAL_PS waits.
 */
static int sn624x_pde_request_ps(struct device *dev, struct regmap *regmap,
				 unsigned int fun, unsigned int ent,
				 unsigned int ps);

/*
 * DAPM PDE supplies (rt711/rt712 style): write REQUESTED_PS then poll ACTUAL_PS.
 * Mute-before-D3 is handled by FU widget PRE_PMD, which runs before SUPPLY PRE_PMD
 * when the FU lists the PDE as a dependency.
 */
static int sn624x_dapm_pde_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event,
				 unsigned int fun, unsigned int ent)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sn624x_sdca_priv *sn624x =
		snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int ret;

	if (!sn624x || !sn624x->regmap)
		return -ENODEV;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		ret = sn624x_pde_request_ps(dev, sn624x->regmap, fun, ent,
					    SN624X_PWR_D0);
		if (ret < 0)
			dev_warn(dev,
				 "sn624x: DAPM %s PDE fun=%u ent=0x%x -> PS0 failed (%d)\n",
				 w->name, fun, ent, ret);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ret = sn624x_pde_request_ps(dev, sn624x->regmap, fun, ent,
					    SN624X_PWR_D3);
		if (ret < 0)
			dev_warn(dev,
				 "sn624x: DAPM %s PDE fun=%u ent=0x%x -> PS3 failed (%d)\n",
				 w->name, fun, ent, ret);
		break;
	default:
		return 0;
	}
	return ret;
}

static int sn624x_pde_hp_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_dapm_pde_event(w, kcontrol, event,
				     SN624X_FUNC_NUM_JACK_CODEC,
				     SN624X_SDCA_ENT_PDE03);
}

static int sn624x_pde_spk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_dapm_pde_event(w, kcontrol, event,
				     SN624X_FUNC_NUM_SPEAKER_AMP,
				     SN624X_SDCA_ENT_PDE03);
}

static int sn624x_pde_mic2_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_dapm_pde_event(w, kcontrol, event,
				     SN624X_FUNC_NUM_JACK_CODEC,
				     SN624X_SDCA_ENT_PDE04);
}

static int sn624x_pde_dmic_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_dapm_pde_event(w, kcontrol, event,
				     SN624X_FUNC_NUM_MIC_ARRAY,
				     SN624X_SDCA_ENT_PDE03);
}

static int sn624x_fu_mute_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event,
				unsigned int mute_l, unsigned int mute_r,
				unsigned int vol_l, unsigned int vol_r,
				unsigned int vol_val)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sn624x_sdca_priv *sn624x =
		snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int ret;

	if (!sn624x || !sn624x->regmap)
		return -ENODEV;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (vol_l) {
			ret = regmap_write(sn624x->regmap, vol_l, vol_val);
			if (ret < 0)
				dev_warn(dev, "sn624x: DAPM %s vol(L) failed (%d)\n",
					 w->name, ret);
		}
		if (vol_r) {
			ret = regmap_write(sn624x->regmap, vol_r, vol_val);
			if (ret < 0)
				dev_warn(dev, "sn624x: DAPM %s vol(R) failed (%d)\n",
					 w->name, ret);
		}
		ret = regmap_write(sn624x->regmap, mute_l, 0);
		if (ret < 0)
			dev_warn(dev, "sn624x: DAPM %s unmute(L) failed (%d)\n",
				 w->name, ret);
		if (mute_r) {
			ret = regmap_write(sn624x->regmap, mute_r, 0);
			if (ret < 0)
				dev_warn(dev, "sn624x: DAPM %s unmute(R) failed (%d)\n",
					 w->name, ret);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		/* Mute and settle before PDE SUPPLY PRE_PMD takes the domain to PS3 */
		ret = regmap_write(sn624x->regmap, mute_l, SN624X_MUTE_ON);
		if (ret < 0)
			dev_warn(dev, "sn624x: DAPM %s mute(L) failed (%d)\n",
				 w->name, ret);
		if (mute_r) {
			ret = regmap_write(sn624x->regmap, mute_r, SN624X_MUTE_ON);
			if (ret < 0)
				dev_warn(dev, "sn624x: DAPM %s mute(R) failed (%d)\n",
					 w->name, ret);
		}
		usleep_range(SN624X_ROUTE_AFTER_MUTE_US_MIN,
			     SN624X_ROUTE_AFTER_MUTE_US_MAX);
		break;
	default:
		break;
	}
	return 0;
}

static int sn624x_fu_hp_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_fu_mute_event(w, kcontrol, event,
				    SN624X_REG_JACK_OUT_MUTE, 0,
				    SN624X_REG_JACK_OUT_VOL,
				    SN624X_REG_JACK_OUT_CHR_VOL, 0);
}

static int sn624x_fu_spk_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_fu_mute_event(w, kcontrol, event,
				    SN624X_REG_MUTE, SN624X_REG_RIGHT_MUTE,
				    SN624X_REG_VOL, SN624X_REG_CHR_VOL, 0);
}

static int sn624x_fu_mic2_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct sn624x_sdca_priv *sn624x =
		snd_soc_component_get_drvdata(component);
	int ret;

	if (event == SND_SOC_DAPM_POST_PMU && sn624x)
		sn624x_uaj_ge35_apply_detected_mode(component->dev, sn624x);

	ret = sn624x_fu_mute_event(w, kcontrol, event,
				   SN624X_REG_JACK_CAP_MUTE, 0,
				   SN624X_REG_JACK_CAP_VOL,
				   SN624X_REG_JACK_CAP_CHR_VOL,
				   SN624X_VOL_Q78(6, 0));
	return ret;
}

static int sn624x_fu_dmic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return sn624x_fu_mute_event(w, kcontrol, event,
				    SN624X_REG_DMIC_CAP_MUTE,
				    SN624X_REG_DMIC_CAP_CHR_MUTE,
				    SN624X_REG_DMIC_CAP_VOL,
				    SN624X_REG_DMIC_CAP_CHR_VOL,
				    SN624X_VOL_Q78(6, 0));
}

static int sn624x_pde_request_ps(struct device *dev, struct regmap *regmap,
				 unsigned int fun, unsigned int ent,
				 unsigned int ps)
{
	static const struct sdca_pde_delay delays[] = {
		{ .from_ps = SDCA_PDE_PS0, .to_ps = SDCA_PDE_PS3,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
		{ .from_ps = SDCA_PDE_PS3, .to_ps = SDCA_PDE_PS0,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
		{ .from_ps = SDCA_PDE_PS1, .to_ps = SDCA_PDE_PS0,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
		{ .from_ps = SDCA_PDE_PS2, .to_ps = SDCA_PDE_PS0,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
		{ .from_ps = SDCA_PDE_PS0, .to_ps = SDCA_PDE_PS0,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
		{ .from_ps = SDCA_PDE_PS3, .to_ps = SDCA_PDE_PS3,
		  .us = SN624X_PDE_PS_POLL_BUDGET_US },
	};
	unsigned int req = SDW_SDCA_CTL(fun, ent, SDCA_CTL_PDE_REQUESTED_PS, 0);
	unsigned int act = SDW_SDCA_CTL(fun, ent, SDCA_CTL_PDE_ACTUAL_PS, 0);
	unsigned int from_ps = SDCA_PDE_PS3;
	int ret;

	ret = regmap_read(regmap, act, &from_ps);
	if (ret < 0) {
		dev_warn(dev,
			 "sn624x: PDE fun=%u ent=0x%x ACTUAL_PS read failed (%d)\n",
			 fun, ent, ret);
		from_ps = (ps == SN624X_PWR_D0) ? SN624X_PWR_D3 : SN624X_PWR_D0;
	} else {
		from_ps &= 0xffu;
	}

	ret = regmap_write(regmap, req, ps);
	if (ret < 0) {
		dev_warn(dev,
			 "sn624x: PDE fun=%u ent=0x%x REQUESTED_PS=%u write failed (%d)\n",
			 fun, ent, ps, ret);
		return ret;
	}

	ret = sdca_asoc_pde_poll_actual_ps(regmap, fun, ent,
					   from_ps, ps,
					   delays, ARRAY_SIZE(delays));
	if (ret < 0)
		dev_warn(dev,
			 "sn624x: PDE fun=%u ent=0x%x ACTUAL_PS poll failed (%d; want %u)\n",
			 fun, ent, ret, ps);
	return ret;
}

static void sn624x_jack_type_from_det_mode(struct sn624x_sdca_priv *sn624x,
					   unsigned int det_mode)
{
	switch (det_mode) {
	case 0x00:
		sn624x->jack_type = 0;
		break;
	case 0x03:
		sn624x->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		sn624x->jack_type = SND_JACK_HEADSET;
		break;
	default:
		SN624X_DBG(&sn624x->slave->dev,
			   "unknown GE35 detected_mode 0x%x\n", det_mode);
		sn624x->jack_type = 0;
		break;
	}
}

static int sn624x_uaj_ge35_select_mode(struct device *dev,
				       struct sn624x_sdca_priv *sn624x,
				       unsigned int mode)
{
	int ret;

	ret = regmap_write(sn624x->regmap,
			   SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC,
					SN624X_SDCA_ENT_GE35,
					SN624X_SDCA_CTL_SELECTED_MODE, 0),
			   mode);
	if (ret < 0)
		dev_warn(dev,
			 "sn624x: jack_cap: GE35 SELECTED_MODE=0x%x failed (%d)\n",
			 mode, ret);
	else
		dev_dbg(dev,
			 "sn624x: jack_cap: GE35 SELECTED_MODE=0x%x\n",
			 mode);
	return ret;
}

/*
 * Follow DETECTED_MODE → SELECTED_MODE (rt711/rt722 pattern). Never force
 * headset: mic path is only valid when the jack hardware reports one.
 * Also updates sn624x->jack_type from DETECTED_MODE.
 */
static int sn624x_uaj_ge35_apply_detected_mode(struct device *dev,
					       struct sn624x_sdca_priv *sn624x)
{
	unsigned int det_mode = 0;
	int ret;

	ret = regmap_read(sn624x->regmap,
			  SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC,
				       SN624X_SDCA_ENT_GE35,
				       SN624X_SDCA_CTL_DETECTED_MODE, 0),
			  &det_mode);
	if (ret < 0) {
		dev_warn(dev,
			 "sn624x: jack_cap: GE35 DETECTED_MODE read failed (%d)\n",
			 ret);
		return ret;
	}

	dev_dbg(dev, "sn624x: jack_cap: GE35 DETECTED_MODE=0x%x\n", det_mode);
	sn624x_jack_type_from_det_mode(sn624x, det_mode);

	if (!det_mode) {
		dev_dbg(dev,
			"sn624x: jack_cap: no jack detected — skip SELECTED_MODE\n");
		return 0;
	}

	return sn624x_uaj_ge35_select_mode(dev, sn624x, det_mode);
}

static const char *sn624x_status_str(enum sdw_slave_status status)
{
	switch (status) {
	case SDW_SLAVE_UNATTACHED:
		return "UNATTACHED";
	case SDW_SLAVE_ATTACHED:
		return "ATTACHED";
	case SDW_SLAVE_ALERT:
		return "ALERT";
	case SDW_SLAVE_RESERVED:
		return "RESERVED";
	default:
		return "UNKNOWN";
	}
}

static int sn624x_sdca_mbq_size(struct device *dev, unsigned int reg)
{
	if (!SDW_SDCA_VALID_CTL(reg))
		return 1;

	/*
	 * FU_VOLUME and GE35 DETECTED_MODE share control selector 0x02.
	 * Only list known FU volume addresses as 16-bit MBQ — matching
	 * DETECTED_MODE by csel alone makes GE35 use SDW_SDCA_MBQ_CTL and
	 * fails with -ENODATA (-61), which breaks jack plug/unplug.
	 */
	switch (reg) {
	case SN624X_REG_VOL:
	case SN624X_REG_CHR_VOL:
	case SN624X_REG_JACK_OUT_VOL:
	case SN624X_REG_JACK_OUT_CHR_VOL:
	case SN624X_REG_JACK_CAP_VOL:
	case SN624X_REG_JACK_CAP_CHR_VOL:
	case SN624X_REG_DMIC_CAP_VOL:
	case SN624X_REG_DMIC_CAP_CHR_VOL:
		return 2;
	default:
		return 1;
	}
}

static bool sn624x_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	if (!SDW_SDCA_VALID_CTL(reg))
		return true;

	switch (reg) {
	case SN624X_REG_PWR_STATE:
	case SN624X_REG_JACK_OUT_PWR_STATE:
	case SN624X_REG_JACK_CAP_PWR_STATE:
	case SN624X_REG_DMIC_CAP_PWR_STATE:
	case SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_SDCA_ENT_GE35,
			  SN624X_SDCA_CTL_DETECTED_MODE, 0):
		return true;
	default:
		if (SDW_SDCA_CTL_CSEL(reg) == SDCA_CTL_PDE_ACTUAL_PS)
			return true;
		return false;
	}
}

static const struct regmap_sdw_mbq_cfg sn624x_sdca_mbq_cfg = {
	.mbq_size = sn624x_sdca_mbq_size,
};

static const struct regmap_config sn624x_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 16,
	.volatile_reg = sn624x_sdca_volatile_register,
	.max_register = 0xffffffff,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

/*
 * Prefer ACPI mipi-sdca-function-initialization-table (sdca_regmap_write_init).
 * sdca_parse_function() loads that table before entity parsing, so a later
 * DisCo parse failure can still leave fn->num_init_table usable.
 */
static int sn624x_write_sdca_init_table(struct device *dev,
					struct sn624x_sdca_priv *sn624x,
					struct sdca_function_data *fn,
					const char *label)
{
	int ret;

	if (!fn || !fn->num_init_table)
		return -ENOENT;

	ret = sdca_regmap_write_init(dev, sn624x->regmap, fn);
	if (ret < 0) {
		dev_warn(dev,
			 "sn624x: %s function-initialization-table write failed (%d)\n",
			 label, ret);
		return ret;
	}

	dev_info(dev,
		 "sn624x: applied %s function-initialization-table (%d writes)\n",
		 label, fn->num_init_table);
	return 0;
}

/*
 * Temporary vendor-window fallback when ACPI has no init table.
 * Validated on current platforms; remove once DisCo init tables are complete.
 */
static int sn624x_jack_oem_fallback_init(struct device *dev,
					 struct sn624x_sdca_priv *sn624x)
{
	static const struct reg_sequence jack_oem_table[] = {
		REG_SEQ0(SN624X_UAJ_CTL_IT33_MIC_BIAS, 0x06),
		REG_SEQ0(SN624X_UAJ_CTL_IT31_MIC_BIAS, 0x06),
		REG_SEQ0(SN624X_UAJ_CTL_FU41_CH1_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU41_CH2_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU31_CH0_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU32_CH0_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU33_CH0_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU36_CH1_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU36_CH2_MUTE, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU31_CH0_GAIN_HIGH, 0x12),
		REG_SEQ0(SN624X_UAJ_CTL_FU31_CH0_GAIN_LOW, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU32_CH0_GAIN_HIGH, 0x0C),
		REG_SEQ0(SN624X_UAJ_CTL_FU32_CH0_GAIN_LOW, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_FU33_CH0_GAIN_HIGH, 0x12),
		REG_SEQ0(SN624X_UAJ_CTL_FU33_CH0_GAIN_LOW, 0x00),
		REG_SEQ0(SN624X_UAJ_CTL_INPUT_DELAY_TIME, 0x0F),
		REG_SEQ0(SN624X_UAJ_CTL_PORTA_CHARGE_PUMP, 0x0B),
	};
	int ret;

	dev_warn_once(dev,
		      "sn624x: using temporary jack OEM init fallback (no ACPI init table)\n");
	ret = regmap_multi_reg_write_bypassed(sn624x->regmap, jack_oem_table,
					      ARRAY_SIZE(jack_oem_table));
	if (ret < 0) {
		dev_warn(dev, "sn624x: jack OEM fallback failed (%d)\n", ret);
		return ret;
	}
	usleep_range(1000, 1500);
	return 0;
}

static int sn624x_dmic_oem_fallback_init(struct device *dev,
					 struct sn624x_sdca_priv *sn624x)
{
	static const struct reg_sequence dmic_oem_table[] = {
		REG_SEQ0(0x00002041, 0x00),
		REG_SEQ0(SN624X_UAJ_DMIC_PPU11, 0x30),
		REG_SEQ0(0x00002105, 0x66),
		REG_SEQ0(0x00002107, 0x26),
		REG_SEQ0(0x00002109, 0x62),
		REG_SEQ0(0x40800a09, 0x00),
		REG_SEQ0(0x40800a0a, 0x00),
		REG_SEQ0(0x40802a11, 0x00),
		REG_SEQ0(0x40800a11, 0x00),
		REG_SEQ0(0x40802a12, 0x00),
		REG_SEQ0(0x40800a12, 0x00),
		REG_SEQ0(0x40802a13, 0x00),
		REG_SEQ0(0x40800a13, 0x00),
		REG_SEQ0(0x40802a14, 0x00),
		REG_SEQ0(0x40800a14, 0x00),
		REG_SEQ0(0x408029d8, 0x0c),
		REG_SEQ0(0x408009d8, 0x00),
		REG_SEQ0(0x408029d9, 0x0c),
		REG_SEQ0(0x408009d9, 0x00),
		REG_SEQ0(0x00002140, 0x18),
		REG_SEQ0(0x00002045, 0x01),
	};
	int ret;

	dev_warn_once(dev,
		      "sn624x: using temporary dmic OEM init fallback (no ACPI init table)\n");
	ret = regmap_multi_reg_write_bypassed(sn624x->regmap, dmic_oem_table,
					      ARRAY_SIZE(dmic_oem_table));
	if (ret < 0) {
		dev_warn(dev, "sn624x: dmic OEM fallback failed (%d)\n", ret);
		return ret;
	}
	usleep_range(1000, 1500);
	return 0;
}

static void sn624x_uaj_apply_io_defaults(struct sn624x_sdca_priv *sn624x)
{
	struct device *dev = &sn624x->slave->dev;
	int ret;

	ret = sn624x_write_sdca_init_table(dev, sn624x, sn624x->jack_func,
					   "jack");
	if (ret)
		sn624x_jack_oem_fallback_init(dev, sn624x);
}

static int sn624x_parse_sdca_functions(struct sn624x_sdca_priv *sn624x)
{
	struct sdw_slave *slave = sn624x->slave;
	struct device *dev = &slave->dev;
	int i, ret;

	if (!slave->sdca_data.num_functions) {
		dev_dbg(dev, "sn624x: no SDCA function descriptors from DisCo\n");
		return 0;
	}

	for (i = 0; i < slave->sdca_data.num_functions; i++) {
		struct sdca_function_desc *desc = &slave->sdca_data.function[i];
		struct sdca_function_data *fn;
		bool is_jack = false, is_mic = false, is_amp = false;

		switch (desc->type) {
		case SDCA_FUNCTION_TYPE_UAJ:
		case SDCA_FUNCTION_TYPE_SIMPLE_JACK:
		case SDCA_FUNCTION_TYPE_RJ:
			is_jack = true;
			break;
		case SDCA_FUNCTION_TYPE_SMART_MIC:
		case SDCA_FUNCTION_TYPE_SIMPLE_MIC:
			is_mic = true;
			break;
		case SDCA_FUNCTION_TYPE_SMART_AMP:
		case SDCA_FUNCTION_TYPE_SIMPLE_AMP:
		case SDCA_FUNCTION_TYPE_SPEAKER_MIC:
			is_amp = true;
			break;
		default:
			break;
		}

		if (desc->adr == SN624X_FUNC_NUM_JACK_CODEC)
			is_jack = true;
		else if (desc->adr == SN624X_FUNC_NUM_MIC_ARRAY)
			is_mic = true;
		else if (desc->adr == SN624X_FUNC_NUM_SPEAKER_AMP)
			is_amp = true;

		if (!is_jack && !is_mic && !is_amp)
			continue;

		fn = devm_kzalloc(dev, sizeof(*fn), GFP_KERNEL);
		if (!fn)
			return -ENOMEM;

		fn->desc = desc;
		ret = sdca_parse_function(dev, fn);
		if (ret) {
			/*
			 * sdca_parse_function() reads the ACPI
			 * mipi-sdca-function-initialization-table first,
			 * then parses entities/controls. On some platforms
			 * DisCo entity data is incomplete so the later
			 * steps fail (-EINVAL), but fn->init_table is
			 * already filled. Keep that partial function so
			 * io_init can still apply the ACPI table; if the
			 * table was never loaded, drop this function and
			 * let the OEM register fallback run instead.
			 */
			dev_warn(dev,
				 "sn624x: sdca_parse_function(%s adr=%u) failed (%d)%s\n",
				 desc->name ? desc->name : "?", desc->adr, ret,
				 fn->num_init_table ?
				 ", keeping ACPI init table" : "");
			if (!fn->num_init_table)
				continue;
		} else {
			dev_dbg(dev,
				"sn624x: parsed SDCA function %s type=%u adr=%u init_writes=%d\n",
				desc->name ? desc->name : "?", desc->type,
				desc->adr, fn->num_init_table);
		}

		if (is_jack && !sn624x->jack_func)
			sn624x->jack_func = fn;
		else if (is_mic && !sn624x->mic_func)
			sn624x->mic_func = fn;
		else if (is_amp && !sn624x->amp_func)
			sn624x->amp_func = fn;
	}

	return 0;
}

/*
 * Poll-driven jack detect: pulse UAJ jack-detect, then apply GE35
 * DETECTED_MODE → SELECTED_MODE via sn624x_uaj_ge35_apply_detected_mode().
 */
static int sn624x_sdca_headset_detect(struct sn624x_sdca_priv *sn624x)
{
	struct device *dev = &sn624x->slave->dev;
	int ret;

	SN624X_DBG(dev, "headset_detect: pulse UAJ jack-detect, read GE35\n");
	ret = regmap_write(sn624x->regmap,
			   SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC,
					SN624X_UAJ_ENT_BLOCK,
					SN624X_UAJ_CTL_JACK_DETECT, 0),
			   0x08);
	if (ret < 0)
		goto io_error;

	usleep_range(5000, 5500);

	ret = sn624x_uaj_ge35_apply_detected_mode(dev, sn624x);

	/* Clear the detect pulse even if GE35 apply failed. */
	regmap_write(sn624x->regmap,
		     SDW_SDCA_CTL(SN624X_FUNC_NUM_JACK_CODEC, SN624X_UAJ_ENT_BLOCK,
				  SN624X_UAJ_CTL_JACK_DETECT, 0),
		     0x00);

	if (ret < 0)
		goto io_error;

	SN624X_DBG(dev, "headset_detect: jack_type=0x%x\n", sn624x->jack_type);
	return 0;

io_error:
	if (ret == -ENODATA)
		SN624X_DBG(dev, "%s: UAJ/GE35 access ignored (-ENODATA)\n",
			   __func__);
	else
		dev_err_ratelimited(dev, "%s failed (%d)\n", __func__, ret);
	return ret;
}

static void sn624x_sdca_sdca_irq_mask_all(struct sn624x_sdca_priv *sn624x)
{
	if (!sn624x->slave)
		return;

	sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INTMASK1, 0);
	sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INTMASK2, 0);
	sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INTMASK3, 0);
	sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INTMASK4, 0);
	SN624X_DBG(&sn624x->slave->dev,
		   "SDCA INTMASK1-4 = 0 (all SDCA sources masked)\n");
}

static void sn624x_sdca_jack_irq_unmask(struct sn624x_sdca_priv *sn624x)
{
	/* Jack detection is poll-driven; SDCA jack IRQs remain masked. */
	(void)sn624x;
}

static void sn624x_sdca_jack_irq_mask(struct sn624x_sdca_priv *sn624x)
{
	if (!sn624x->slave)
		return;

	sn624x_sdca_sdca_irq_mask_all(sn624x);
}

/*
 * Hold a runtime PM reference for the lifetime of jack detection so the
 * device cannot autosuspend while the poll worker needs register access.
 */
static int sn624x_jack_rpm_get(struct sn624x_sdca_priv *sn624x)
{
	struct device *dev;
	int ret;

	if (!sn624x)
		return -EINVAL;
	if (sn624x->jack_rpm)
		return 0;
	if (!sn624x->component)
		return -ENODEV;

	dev = sn624x->component->dev;
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		if (ret != -EACCES) {
			dev_err(dev, "sn624x: jack rpm get failed (%d)\n", ret);
			return ret;
		}
		/* pm_runtime not enabled yet (before first ATTACHED io_init) */
		return 0;
	}

	sn624x->jack_rpm = true;
	return 0;
}

static void sn624x_jack_rpm_put(struct sn624x_sdca_priv *sn624x)
{
	if (!sn624x || !sn624x->jack_rpm || !sn624x->component)
		return;

	pm_runtime_mark_last_busy(sn624x->component->dev);
	pm_runtime_put_autosuspend(sn624x->component->dev);
	sn624x->jack_rpm = false;
}

static void sn624x_jack_schedule_poll(struct sn624x_sdca_priv *sn624x)
{
	if (!sn624x || !sn624x->hs_jack || !sn624x->slave)
		return;

	mod_delayed_work(system_power_efficient_wq, &sn624x->jack_detect_work,
			 msecs_to_jiffies(SN624X_JACK_POLL_MS));
}

static void sn624x_sdca_jack_poll_and_report(struct sn624x_sdca_priv *sn624x)
{
	struct device *dev;

	if (!sn624x->hs_jack || !sn624x->slave)
		return;

	dev = &sn624x->slave->dev;
	if (sn624x_sdca_headset_detect(sn624x) < 0)
		return;

	snd_soc_jack_report(sn624x->hs_jack, sn624x->jack_type,
			    SND_JACK_HEADSET | SND_JACK_HEADPHONE);
	sn624x->jack_type_last = sn624x->jack_type;
	dev_dbg(dev, "sn624x: jack polled: detected_mode -> jack_type=0x%x\n",
		 sn624x->jack_type);
}

static void sn624x_sdca_jack_detect_handler(struct work_struct *work)
{
	struct sn624x_sdca_priv *sn624x =
		container_of(work, struct sn624x_sdca_priv, jack_detect_work.work);
	struct device *dev;

	if (!sn624x->slave) {
		sn624x_jack_schedule_poll(sn624x);
		return;
	}

	dev = &sn624x->slave->dev;

	if (!sn624x->hs_jack) {
		SN624X_DBG(dev, "jack_work: skip (no hs_jack)\n");
		goto out_reschedule;
	}

	/* Registers are only valid after ATTACHED → io_init (hw_init). */
	if (!sn624x->hw_init) {
		SN624X_DBG(dev, "jack_work: skip (hw_init not ready)\n");
		goto out_reschedule;
	}

	/* Poll GE35 every interval; do not rely on SDCA_0 IRQ or PCM activity */
	sn624x_sdca_headset_detect(sn624x);

	if (sn624x->jack_type != sn624x->jack_type_last) {
		dev_dbg(dev,
			 "sn624x: jack event: %s (jack_type=0x%x; HP=0x%x HS=0x%x)\n",
			 sn624x->jack_type ? "plug" : "unplug",
			 sn624x->jack_type, SND_JACK_HEADPHONE, SND_JACK_HEADSET);
		sn624x->jack_type_last = sn624x->jack_type;

		snd_soc_jack_report(sn624x->hs_jack, sn624x->jack_type,
				    SND_JACK_HEADSET | SND_JACK_HEADPHONE);
	}

out_reschedule:
	sn624x_jack_schedule_poll(sn624x);
}

static void sn624x_sdca_jack_init(struct sn624x_sdca_priv *sn624x)
{
	/* No machine set_jack() yet — not "jack unsupported". */
	if (!sn624x->hs_jack)
		return;

	sn624x_sdca_jack_irq_unmask(sn624x);
	sn624x_sdca_jack_poll_and_report(sn624x);
	sn624x_jack_schedule_poll(sn624x);

	dev_dbg(&sn624x->slave->dev,
		 "sn624x: jack_init: poll scheduled every %ums\n", SN624X_JACK_POLL_MS);
}

static int sn624x_sdca_set_jack_detect(struct snd_soc_component *component,
				       struct snd_soc_jack *hs_jack, void *data)
{
	struct sn624x_sdca_priv *sn624x = snd_soc_component_get_drvdata(component);
	int ret;

	dev_dbg(component->dev,
		 "sn624x: set_jack: hs_jack=%p first_hw_init=%d hw_init=%d\n",
		 hs_jack, sn624x->first_hw_init, sn624x->hw_init);

	if (!hs_jack) {
		cancel_delayed_work_sync(&sn624x->jack_detect_work);
		sn624x->hs_jack = NULL;
		sn624x_jack_rpm_put(sn624x);
		return 0;
	}

	/*
	 * Component registration happens before runtime PM is enabled (RPM is
	 * enabled from io_init on first ATTACHED). Do not touch runtime PM
	 * until then, or get/put counts can go out of sync.
	 */
	if (!sn624x->first_hw_init) {
		sn624x->hs_jack = hs_jack;
		sn624x->jack_type_last = -1;
		sn624x_jack_schedule_poll(sn624x);
		return 0;
	}

	/*
	 * Take the runtime PM reference before publishing hs_jack, so a
	 * failed get cannot leave jack detection marked as enabled.
	 */
	ret = sn624x_jack_rpm_get(sn624x);
	if (ret < 0)
		return ret;

	sn624x->hs_jack = hs_jack;
	sn624x->jack_type_last = -1;
	sn624x_sdca_jack_init(sn624x);
	return 0;
}

/* Prefer ACPI init table; OEM vendor-window fallback only if absent. */
static int sn624x_jack_apply_defaults(struct device *dev,
				      struct sn624x_sdca_priv *sn624x)
{
	int ret;

	if (!sn624x || !sn624x->regmap || !sn624x->slave)
		return -ENODEV;

	ret = sn624x_write_sdca_init_table(dev, sn624x, sn624x->jack_func,
					   "jack");
	if (ret == 0) {
		usleep_range(1000, 1500);
		return 0;
	}

	return sn624x_jack_oem_fallback_init(dev, sn624x);
}

static int sn624x_dmic_apply_defaults(struct device *dev,
				      struct sn624x_sdca_priv *sn624x)
{
	int ret;

	if (!sn624x || !sn624x->regmap || !sn624x->slave)
		return -ENODEV;

	ret = sn624x_write_sdca_init_table(dev, sn624x, sn624x->mic_func,
					   "dmic");
	if (ret == 0) {
		usleep_range(1000, 1500);
		return 0;
	}

	return sn624x_dmic_oem_fallback_init(dev, sn624x);
}

static int sn624x_sdca_jack_function_init(struct device *dev,
					  struct sn624x_sdca_priv *sn624x)
{
	unsigned int jack_func_status;
	const struct reg_sequence clear_status =
		REG_SEQ0(SN624X_REG_JACK_FUN_STATUS_CTL, 0x61);
	int ret;

	ret = regmap_read_bypassed(sn624x->regmap,
				   SN624X_REG_JACK_FUN_STATUS_CTL,
				   &jack_func_status);
	if (ret < 0)
		return ret;

	if (!(jack_func_status & (SN624X_JACK_FUN_NEEDS_INIT |
				  SN624X_JACK_FUN_HAS_BEEN_RESET)))
		return 0;

	ret = sn624x_jack_apply_defaults(dev, sn624x);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write_bypassed(sn624x->regmap, &clear_status, 1);
	if (ret < 0)
		return ret;

	usleep_range(1000, 1500);
	return 0;
}

static int sn624x_sdca_dmic_function_init(struct device *dev,
					  struct sn624x_sdca_priv *sn624x)
{
	unsigned int dmic_func_status;
	const struct reg_sequence clear_status =
		REG_SEQ0(SN624X_REG_DMIC_FUN_STATUS_CTL, 0x61);
	int ret;

	ret = regmap_read_bypassed(sn624x->regmap,
				   SN624X_REG_DMIC_FUN_STATUS_CTL,
				   &dmic_func_status);
	if (ret < 0)
		return ret;

	if (!(dmic_func_status & (SN624X_JACK_FUN_NEEDS_INIT |
				  SN624X_JACK_FUN_HAS_BEEN_RESET)))
		return 0;

	ret = sn624x_dmic_apply_defaults(dev, sn624x);
	if (ret < 0)
		return ret;

	ret = regmap_multi_reg_write_bypassed(sn624x->regmap, &clear_status, 1);
	if (ret < 0)
		return ret;

	usleep_range(1000, 1500);
	return 0;
}

static int sn624x_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(dev);
	int ret;

	if (sn624x->hw_init) {
		SN624X_DBG(dev, "io_init: skip (hw_init already true)\n");
		return 0;
	}

	SN624X_DBG(dev, "io_init: start (first hardware bring-up)\n");
	sn624x->disable_irq = false;

	sn624x_sdca_sdca_irq_mask_all(sn624x);

	regcache_cache_only(sn624x->regmap, false);
	if (!sn624x->first_hw_init) {
		/*
		 * PM runtime is only enabled when a Slave reports as Attached
		 * (same pattern as rt722).
		 */
		pm_runtime_set_autosuspend_delay(dev, 3000);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_set_active(dev);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_enable(dev);
	}

	pm_runtime_get_noresume(dev);

	sn624x_uaj_apply_io_defaults(sn624x);

	/*
	 * set_jack() may have run before RPM was enabled (-EACCES). Take the
	 * long-lived jack PM ref now that runtime PM is active.
	 */
	if (sn624x->hs_jack) {
		sn624x_jack_rpm_get(sn624x);
		sn624x_sdca_jack_init(sn624x);
	}

	ret = sn624x_sdca_jack_function_init(dev, sn624x);
	if (ret < 0) {
		dev_err(dev, "sn624x: jack function init failed (%d)\n", ret);
		goto err;
	}

	ret = sn624x_sdca_dmic_function_init(dev, sn624x);
	if (ret < 0) {
		dev_err(dev, "sn624x: dmic function init failed (%d)\n", ret);
		goto err;
	}

	/*
	 * Re-attach after bus reset: force a full cache write-back on the
	 * following resume/sync path (same idea as rt722).
	 */
	if (sn624x->first_hw_init)
		regcache_mark_dirty(sn624x->regmap);

	sn624x->hw_init = true;
	sn624x->first_hw_init = true;

	pm_runtime_put_autosuspend(dev);

	SN624X_DBG(dev, "io_init: complete (hw_init set), jack %s\n",
		   sn624x->hs_jack ? "registered" : "not registered yet");
	return 0;

err:
	pm_runtime_put_noidle(dev);
	return ret;
}

/*
 * Bus lifecycle (same as rt722): enumerate → assign dev_num → ATTACHED →
 * io_init(). Do not poll enumeration/init around each register access; after
 * unattach, resume uses sdw_slave_wait_for_init() then regcache_sync.
 */
static int sn624x_sdca_update_status(struct sdw_slave *slave,
				     enum sdw_slave_status status)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(&slave->dev);
	bool need_io_init;
	int ret;

	SN624X_DBG(&slave->dev, "update_status: %s hw_init=%d dev_num=%u\n",
		   sn624x_status_str(status), sn624x->hw_init, slave->dev_num);

	if (status == SDW_SLAVE_UNATTACHED) {
		cancel_delayed_work_sync(&sn624x->jack_detect_work);
		sn624x->hw_init = false;
		SN624X_DBG(&slave->dev,
			   "slave UNATTACHED: cleared hw_init, jack work cancelled\n");
	}

	if (status != SDW_SLAVE_ATTACHED)
		return 0;

	sn624x_sdca_sdca_irq_mask_all(sn624x);

	need_io_init = !sn624x->hw_init;
	if (need_io_init) {
		dev_notice(&slave->dev, "sn624x: update_status ATTACHED -> io_init()\n");
		ret = sn624x_sdca_io_init(&slave->dev, slave);
		if (ret < 0)
			return ret;
	} else {
		dev_dbg(&slave->dev,
			"sn624x: update_status ATTACHED: io_init already done\n");
	}

	/* Re-ATTACHED without io_init: restore jack detect after bus is up. */
	if (sn624x->hs_jack && sn624x->hw_init && !need_io_init) {
		sn624x_sdca_jack_irq_unmask(sn624x);
		sn624x_sdca_jack_poll_and_report(sn624x);
		sn624x_jack_schedule_poll(sn624x);
	}

	return 0;
}

static int sn624x_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct sdw_dpn_prop *dpn;
	unsigned long addr;
	unsigned int bit;
	int nval;
	int i, j;

	sdw_slave_read_lane_mapping(slave);
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->paging_support = true;
	prop->use_domain_irq = true;
	prop->wake_capable = true;

	prop->source_ports = BIT(SN624X_PORT_JACK_CAPTURE) |
			     BIT(SN624X_PORT_DMIC_CAPTURE);
	prop->sink_ports = BIT(SN624X_PORT_JACK_PLAYBACK) |
			   BIT(SN624X_PORT_SPEAKER_PLAYBACK);

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->src_dpn_prop), GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;
	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop), GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;
	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = true;
		dpn[j].ch_prep_timeout = 10;
		j++;
	}
	prop->clk_stop_timeout = 900;
	prop->simple_clk_stop_capable = true;
	prop->lane_control_support = true;
	SN624X_DBG(&slave->dev,
		   "read_prop: src_ports=0x%x sink_ports=0x%x lane irq paging\n",
		   prop->source_ports, prop->sink_ports);

	return 0;
}

/*
 * Clear SDCA INT1/2 only. DP0_INT standard fields (PORT_READY, etc.) are
 * owned by the SoundWire bus core; cascade clears when SDCA status is clear.
 * Pending SDCA IRQs can block clock-stop prepare (CLK_STP_NF -> -ETIMEDOUT).
 */
static int sn624x_sdca_flush_pending_irqs(struct sn624x_sdca_priv *sn624x,
					  struct device *dev)
{
	int ret, stat = 0;
	int count = 0;
	const int retry = 3;
	unsigned int scp_sdca_stat1, scp_sdca_stat2 = 0;

	if (!sn624x || !sn624x->slave)
		return 0;

	ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT1);
	if (ret < 0)
		return ret;
	sn624x->scp_sdca_stat1 = ret;

	ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT2);
	if (ret < 0)
		return ret;
	sn624x->scp_sdca_stat2 = ret;

	do {
		ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			return ret;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_0) {
			ret = sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INT1,
					      SDW_SCP_SDCA_INTMASK_SDCA_0);
			if (ret < 0)
				return ret;
		}

		ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			return ret;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_8) {
			ret = sdw_write_no_pm(sn624x->slave, SDW_SCP_SDCA_INT2,
					      SDW_SCP_SDCA_INTMASK_SDCA_8);
			if (ret < 0)
				return ret;
		}

		ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			return ret;
		scp_sdca_stat1 = ret & SDW_SCP_SDCA_INTMASK_SDCA_0;

		ret = sdw_read_no_pm(sn624x->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			return ret;
		scp_sdca_stat2 = ret & SDW_SCP_SDCA_INTMASK_SDCA_8;

		stat = scp_sdca_stat1 || scp_sdca_stat2;
		count++;
	} while (stat != 0 && count < retry);

	if (stat && dev)
		dev_warn(dev,
			 "sn624x: SDCA IRQ flush incomplete (stat=%u)\n", stat);

	return stat ? -EBUSY : 0;
}

static int sn624x_sdca_interrupt_callback(struct sdw_slave *slave,
					  struct sdw_slave_intr_status *status)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(&slave->dev);
	int ret;

	if (!sn624x)
		return 0;

	/*
	 * Bus expects the codec to clear SCP SDCA INT1/2 when cascade is set.
	 * Leaving them set can stall alert handling.
	 */

	SN624X_DBG(&slave->dev,
		   "irq: control_port=%#x sdca_cascade=%u disable_irq=%d\n",
		   status->control_port, status->sdca_cascade, sn624x->disable_irq);

	cancel_delayed_work_sync(&sn624x->jack_detect_work);

	mutex_lock(&sn624x->disable_irq_lock);

	ret = sn624x_sdca_flush_pending_irqs(sn624x, &slave->dev);
	if (ret < 0 && ret != -EBUSY)
		goto io_error;

	SN624X_DBG(&slave->dev, "irq: INT1=0x%x INT2=0x%x\n",
		   sn624x->scp_sdca_stat1, sn624x->scp_sdca_stat2);

	/*
	 * Always restart the jack poll after IRQ handling so a cancel above
	 * cannot stop detection. SDCA INTMASK stays masked; any residual SCP
	 * IRQ still triggers an early poll. The poll itself does the UAJ
	 * pulse + GE35 read.
	 */
	if (!sn624x->disable_irq)
		sn624x_jack_schedule_poll(sn624x);

	mutex_unlock(&sn624x->disable_irq_lock);
	return 0;

io_error:
	mutex_unlock(&sn624x->disable_irq_lock);
	dev_err_ratelimited(&slave->dev, "%s: IO error (%d)\n", __func__, ret);
	return ret;
}

static const struct sdw_slave_ops sn624x_sdca_slave_ops = {
	.read_prop = sn624x_sdca_read_prop,
	.interrupt_callback = sn624x_sdca_interrupt_callback,
	.update_status = sn624x_sdca_update_status,
};

static int sn624x_sdca_probe(struct snd_soc_component *component)
{
	struct sn624x_sdca_priv *sn624x = snd_soc_component_get_drvdata(component);
	int ret;

	sn624x->component = component;

	if (!sn624x->first_hw_init)
		return 0;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static int sn624x_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct sn624x_sdca_priv *sn624x = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {};
	struct sdw_port_config port_config = {};
	struct sdw_stream_runtime *sdw_stream;
	unsigned int sampling_rate;
	int port;
	int ret;

	dev_dbg(dai->dev,
		 "sn624x: hw_params: entered dai=%s id=%d stream=%s\n",
		 dai->name, dai->id, snd_pcm_stream_str(substream));

	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	if (!sdw_stream) {
		dev_warn(dai->dev,
			 "sn624x: hw_params: no SDW stream (set_stream not run yet?)\n");
		return -EINVAL;
	}
	if (!sn624x->slave) {
		dev_warn(dai->dev, "sn624x: hw_params: slave NULL\n");
		return -EINVAL;
	}

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err(dai->dev,
			"sn624x: hw_params: pm_runtime_resume failed (%d)\n", ret);
		return ret;
	}

	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (dai->id == SN624X_DAI_JACK)
			port = SN624X_PORT_JACK_PLAYBACK;
		else if (dai->id == SN624X_DAI_SPEAKER)
			port = SN624X_PORT_SPEAKER_PLAYBACK;
		else
			return -EINVAL;
	} else {
		if (dai->id == SN624X_DAI_JACK)
			port = SN624X_PORT_JACK_CAPTURE;
		else if (dai->id == SN624X_DAI_DMIC)
			port = SN624X_PORT_DMIC_CAPTURE;
		else
			return -EINVAL;
	}
	port_config.num = port;

	ret = sdw_stream_add_slave(sn624x->slave, &stream_config,
				   &port_config, 1, sdw_stream);
	if (ret) {
		dev_err(dai->dev, "%s: sdw_stream_add_slave port %d failed: %d\n",
			__func__, port, ret);
		return ret;
	}

	/* SDCA SampleFreqIndex (same encoding as other SDCA codecs) */
	switch (params_rate(params)) {
	case 44100:
		sampling_rate = SN624X_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = SN624X_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = SN624X_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = SN624X_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(dai->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	if (dai->id == SN624X_DAI_JACK) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = regmap_write(sn624x->regmap,
					   SN624X_REG_JACK_OUT_RATE_SEL,
					   sampling_rate);
		else
			ret = regmap_write(sn624x->regmap,
					   SN624X_REG_JACK_CAP_RATE_SEL,
					   sampling_rate);
	} else if (dai->id == SN624X_DAI_SPEAKER) {
		ret = regmap_write(sn624x->regmap, SN624X_REG_RATE_SEL,
				   sampling_rate);
	} else if (dai->id == SN624X_DAI_DMIC) {
		ret = regmap_write(sn624x->regmap, SN624X_REG_DMIC_CAP_RATE_SEL,
				   sampling_rate);
	} else {
		return -EINVAL;
	}

	return ret;
}

static int sn624x_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct sn624x_sdca_priv *sn624x = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!sn624x->slave)
		return -EINVAL;

	return sdw_stream_remove_slave(sn624x->slave, sdw_stream);
}

static int sn624x_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				      int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);
	return 0;
}

static void sn624x_sdca_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static const struct snd_soc_dapm_widget sn624x_sdca_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("TX", "Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SPK RX", "Speaker Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DMIC TX", "DMic Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("PDE HP", SND_SOC_NOPM, 0, 0,
			    sn624x_pde_hp_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE SPK", SND_SOC_NOPM, 0, 0,
			    sn624x_pde_spk_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE MIC2", SND_SOC_NOPM, 0, 0,
			    sn624x_pde_mic2_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE DMIC", SND_SOC_NOPM, 0, 0,
			    sn624x_pde_dmic_event,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_DAC_E("FU HP", NULL, SND_SOC_NOPM, 0, 0,
			   sn624x_fu_hp_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU SPK", NULL, SND_SOC_NOPM, 0, 0,
			   sn624x_fu_spk_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU MIC2", NULL, SND_SOC_NOPM, 0, 0,
			   sn624x_fu_mic2_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU DMIC", NULL, SND_SOC_NOPM, 0, 0,
			   sn624x_fu_dmic_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("DMIC"),
};

static const struct snd_soc_dapm_route sn624x_sdca_audio_map[] = {
	{ "FU HP", NULL, "RX" },
	{ "FU HP", NULL, "PDE HP" },
	{ "HP", NULL, "FU HP" },

	{ "FU SPK", NULL, "SPK RX" },
	{ "FU SPK", NULL, "PDE SPK" },
	{ "SPK", NULL, "FU SPK" },

	{ "FU MIC2", NULL, "MIC2" },
	{ "FU MIC2", NULL, "PDE MIC2" },
	/* UAJ headset capture also needs jack PDE03 (shared with HP) */
	{ "FU MIC2", NULL, "PDE HP" },
	{ "TX", NULL, "FU MIC2" },

	{ "FU DMIC", NULL, "DMIC" },
	{ "FU DMIC", NULL, "PDE DMIC" },
	{ "DMIC TX", NULL, "FU DMIC" },
};

static const struct snd_soc_dai_ops sn624x_sdca_ops = {
	.hw_params	= sn624x_sdca_pcm_hw_params,
	.hw_free	= sn624x_sdca_pcm_hw_free,
	.set_stream	= sn624x_sdca_set_sdw_stream,
	.shutdown	= sn624x_sdca_shutdown,
};

static struct snd_soc_dai_driver sn624x_sdca_dai[] = {
	{
		.name = "sn624x-sdca-aif",
		.id = 0,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SN624X_STEREO_RATES,
			.formats = SN624X_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SN624X_STEREO_RATES,
			.formats = SN624X_FORMATS,
		},
		.ops = &sn624x_sdca_ops,
	},
	{
		.name = "sn624x-sdca-aif2",
		.id = 1,
		.playback = {
			.stream_name = "Speaker Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SN624X_STEREO_RATES,
			.formats = SN624X_FORMATS,
		},
		.ops = &sn624x_sdca_ops,
	},
	{
		.name = "sn624x-sdca-aif3",
		.id = 2,
		.capture = {
			.stream_name = "DMic Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = SN624X_STEREO_RATES,
			.formats = SN624X_FORMATS,
		},
		.ops = &sn624x_sdca_ops,
	},
};

static const struct snd_soc_component_driver soc_sdca_dev_sn624x = {
	.probe = sn624x_sdca_probe,
	.dapm_widgets = sn624x_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sn624x_sdca_dapm_widgets),
	.dapm_routes = sn624x_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sn624x_sdca_audio_map),
	.set_jack = sn624x_sdca_set_jack_detect,
	.endianness = 1,
};

int sn624x_sdca_init(struct device *dev, struct regmap *regmap,
		      struct sdw_slave *slave)
{
	struct sn624x_sdca_priv *sn624x;
	int ret;

	sn624x = devm_kzalloc(dev, sizeof(*sn624x), GFP_KERNEL);
	if (!sn624x)
		return -ENOMEM;

	dev_set_drvdata(dev, sn624x);
	sn624x->slave = slave;
	sn624x->regmap = regmap;

	ret = sn624x_parse_sdca_functions(sn624x);
	if (ret < 0)
		return ret;

	mutex_init(&sn624x->disable_irq_lock);
	INIT_DELAYED_WORK(&sn624x->jack_detect_work, sn624x_sdca_jack_detect_handler);

	regcache_cache_only(sn624x->regmap, true);

	ret = devm_snd_soc_register_component(dev,
					      &soc_sdca_dev_sn624x, sn624x_sdca_dai,
					      ARRAY_SIZE(sn624x_sdca_dai));
	if (ret < 0)
		return ret;

	/*
	 * Do not enable runtime PM here. SoundWire slaves are only powered /
	 * accessible after ATTACHED; enable RPM from io_init() then.
	 */
	SN624X_DBG(dev, "snd_soc_register_component OK (DAI sn624x-sdca-aif)\n");
	return 0;
}

static int sn624x_sdca_sdw_probe(struct sdw_slave *slave,
				 const struct sdw_device_id *id)
{
	struct regmap *regmap;
	int ret;

	SN624X_DBG(&slave->dev, "sdw_probe: dev_num=%u status=%s\n",
		   slave->dev_num, sn624x_status_str(slave->status));

	regmap = devm_regmap_init_sdw_mbq_cfg(&slave->dev, slave,
					      &sn624x_sdca_regmap,
					      &sn624x_sdca_mbq_cfg);
	if (IS_ERR(regmap)) {
		dev_err(&slave->dev, "sn624x: devm_regmap_init_sdw_mbq_cfg failed (%ld)\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	ret = sn624x_sdca_init(&slave->dev, regmap, slave);
	if (ret < 0)
		dev_err(&slave->dev, "sn624x: sn624x_sdca_init failed (%d)\n", ret);
	else {
		struct sn624x_sdca_priv *sn624x = dev_get_drvdata(&slave->dev);

		if (sn624x)
			sn624x_sdca_sdca_irq_mask_all(sn624x);
		SN624X_DBG(&slave->dev, "SoundWire probe finished OK\n");
	}

	return ret;
}

static void sn624x_sdca_sdw_remove(struct sdw_slave *slave)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(&slave->dev);

	SN624X_DBG(&slave->dev, "SoundWire driver remove\n");
	cancel_delayed_work_sync(&sn624x->jack_detect_work);
	sn624x_jack_rpm_put(sn624x);
	if (sn624x->first_hw_init)
		pm_runtime_disable(&slave->dev);
	mutex_destroy(&sn624x->disable_irq_lock);
}

static const struct sdw_device_id sn624x_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x0496, 0x6242, 0x3, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x0496, 0x6244, 0x3, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x0496, 0x6247, 0x3, 0x1, 0),
	{ }
};
MODULE_DEVICE_TABLE(sdw, sn624x_sdca_id);

static int sn624x_sdca_dev_suspend(struct device *dev)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(dev);

	if (!sn624x->first_hw_init)
		return 0;

	regcache_cache_only(sn624x->regmap, true);
	/*
	 * Suspend / clock-stop may reset the peripheral. Mark the cache dirty
	 * so resume's regcache_sync restores software state to hardware.
	 */
	regcache_mark_dirty(sn624x->regmap);

	return 0;
}

static int sn624x_sdca_dev_system_suspend(struct device *dev)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(dev);

	if (!sn624x->first_hw_init)
		return 0;

	/* Mask first so IRQ cannot re-schedule work between cancel and disable. */
	mutex_lock(&sn624x->disable_irq_lock);
	sn624x->disable_irq = true;
	sn624x_sdca_jack_irq_mask(sn624x);
	mutex_unlock(&sn624x->disable_irq_lock);

	cancel_delayed_work_sync(&sn624x->jack_detect_work);

	return sn624x_sdca_dev_suspend(dev);
}

static int sn624x_sdca_regmap_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(dev);
	int ret;

	if (!sn624x->first_hw_init)
		return 0;

	SN624X_DBG(dev, "resume: unattach_request=%u\n", slave->unattach_request);
	if (slave->unattach_request) {
		ret = sdw_slave_wait_for_init(slave, SN624X_PROBE_TIMEOUT_MS);
		if (ret) {
			sdw_show_ping_status(slave->bus, true);
			return ret;
		}
	}

	regcache_cache_only(sn624x->regmap, false);
	regcache_sync(sn624x->regmap);

	return 0;
}

static int sn624x_sdca_dev_resume(struct device *dev)
{
	return sn624x_sdca_regmap_resume(dev);
}

static int sn624x_sdca_dev_system_resume(struct device *dev)
{
	struct sn624x_sdca_priv *sn624x = dev_get_drvdata(dev);
	int ret;

	ret = sn624x_sdca_regmap_resume(dev);
	if (ret < 0)
		return ret;

	mutex_lock(&sn624x->disable_irq_lock);
	if (sn624x->disable_irq) {
		sn624x_sdca_jack_irq_unmask(sn624x);
		sn624x->disable_irq = false;
	}
	mutex_unlock(&sn624x->disable_irq_lock);

	if (sn624x->hs_jack && sn624x->hw_init) {
		sn624x_sdca_jack_poll_and_report(sn624x);
		sn624x_jack_schedule_poll(sn624x);
	}

	return 0;
}

static const struct dev_pm_ops sn624x_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(sn624x_sdca_dev_system_suspend, sn624x_sdca_dev_system_resume)
	RUNTIME_PM_OPS(sn624x_sdca_dev_suspend, sn624x_sdca_dev_resume, NULL)
};

static struct sdw_driver sn624x_sdca_sdw_driver = {
	.driver = {
		.name = "sn624x-sdca",
		.pm = pm_ptr(&sn624x_sdca_pm),
	},
	.probe = sn624x_sdca_sdw_probe,
	.remove = sn624x_sdca_sdw_remove,
	.ops = &sn624x_sdca_slave_ops,
	.id_table = sn624x_sdca_id,
};
module_sdw_driver(sn624x_sdca_sdw_driver);

MODULE_DESCRIPTION("ASoC SN624X SDCA SoundWire driver with UAJ jack support");
MODULE_AUTHOR("Senary Semiconductor Corp.");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA");
MODULE_SOFTDEP("pre: snd-soc-sdca");
