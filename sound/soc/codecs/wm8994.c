/*
 * wm8994.c  --  WM8994 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
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
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>

#include "wm8994.h"
#include "wm_hubs.h"

struct fll_config {
	int src;
	int in;
	int out;
};

#define WM8994_NUM_DRC 3
#define WM8994_NUM_EQ  3

static int wm8994_drc_base[] = {
	WM8994_AIF1_DRC1_1,
	WM8994_AIF1_DRC2_1,
	WM8994_AIF2_DRC_1,
};

static int wm8994_retune_mobile_base[] = {
	WM8994_AIF1_DAC1_EQ_GAINS_1,
	WM8994_AIF1_DAC2_EQ_GAINS_1,
	WM8994_AIF2_EQ_GAINS_1,
};

struct wm8994_micdet {
	struct snd_soc_jack *jack;
	int det;
	int shrt;
};

/* codec private data */
struct wm8994_priv {
	struct wm_hubs_data hubs;
	enum snd_soc_control_type control_type;
	void *control_data;
	struct snd_soc_codec *codec;
	int sysclk[2];
	int sysclk_rate[2];
	int mclk[2];
	int aifclk[2];
	struct fll_config fll[2], fll_suspend[2];

	int dac_rates[2];
	int lrclk_shared[2];

	int mbc_ena[3];

	/* Platform dependant DRC configuration */
	const char **drc_texts;
	int drc_cfg[WM8994_NUM_DRC];
	struct soc_enum drc_enum;

	/* Platform dependant ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg[WM8994_NUM_EQ];
	struct soc_enum retune_mobile_enum;

	/* Platform dependant MBC configuration */
	int mbc_cfg;
	const char **mbc_texts;
	struct soc_enum mbc_enum;

	struct wm8994_micdet micdet[2];

	wm8958_micdet_cb jack_cb;
	void *jack_cb_data;
	bool jack_is_mic;
	bool jack_is_video;

	int revision;
	struct wm8994_pdata *pdata;
};

static int wm8994_readable(unsigned int reg)
{
	switch (reg) {
	case WM8994_GPIO_1:
	case WM8994_GPIO_2:
	case WM8994_GPIO_3:
	case WM8994_GPIO_4:
	case WM8994_GPIO_5:
	case WM8994_GPIO_6:
	case WM8994_GPIO_7:
	case WM8994_GPIO_8:
	case WM8994_GPIO_9:
	case WM8994_GPIO_10:
	case WM8994_GPIO_11:
	case WM8994_INTERRUPT_STATUS_1:
	case WM8994_INTERRUPT_STATUS_2:
	case WM8994_INTERRUPT_RAW_STATUS_2:
		return 1;
	default:
		break;
	}

	if (reg >= WM8994_CACHE_SIZE)
		return 0;
	return wm8994_access_masks[reg].readable != 0;
}

static int wm8994_volatile(unsigned int reg)
{
	if (reg >= WM8994_CACHE_SIZE)
		return 1;

	switch (reg) {
	case WM8994_SOFTWARE_RESET:
	case WM8994_CHIP_REVISION:
	case WM8994_DC_SERVO_1:
	case WM8994_DC_SERVO_READBACK:
	case WM8994_RATE_STATUS:
	case WM8994_LDO_1:
	case WM8994_LDO_2:
	case WM8958_DSP2_EXECCONTROL:
	case WM8958_MIC_DETECT_3:
		return 1;
	default:
		return 0;
	}
}

static int wm8994_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	int ret;

	BUG_ON(reg > WM8994_MAX_REGISTER);

	if (!wm8994_volatile(reg)) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret != 0)
			dev_err(codec->dev, "Cache write to %x failed: %d\n",
				reg, ret);
	}

	return wm8994_reg_write(codec->control_data, reg, value);
}

static unsigned int wm8994_read(struct snd_soc_codec *codec,
				unsigned int reg)
{
	unsigned int val;
	int ret;

	BUG_ON(reg > WM8994_MAX_REGISTER);

	if (!wm8994_volatile(reg) && wm8994_readable(reg) &&
	    reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_read(codec, reg, &val);
		if (ret >= 0)
			return val;
		else
			dev_err(codec->dev, "Cache read from %x failed: %d\n",
				reg, ret);
	}

	return wm8994_reg_read(codec->control_data, reg);
}

static int configure_aif_clock(struct snd_soc_codec *codec, int aif)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int rate;
	int reg1 = 0;
	int offset;

	if (aif)
		offset = 4;
	else
		offset = 0;

	switch (wm8994->sysclk[aif]) {
	case WM8994_SYSCLK_MCLK1:
		rate = wm8994->mclk[0];
		break;

	case WM8994_SYSCLK_MCLK2:
		reg1 |= 0x8;
		rate = wm8994->mclk[1];
		break;

	case WM8994_SYSCLK_FLL1:
		reg1 |= 0x10;
		rate = wm8994->fll[0].out;
		break;

	case WM8994_SYSCLK_FLL2:
		reg1 |= 0x18;
		rate = wm8994->fll[1].out;
		break;

	default:
		return -EINVAL;
	}

	if (rate >= 13500000) {
		rate /= 2;
		reg1 |= WM8994_AIF1CLK_DIV;

		dev_dbg(codec->dev, "Dividing AIF%d clock to %dHz\n",
			aif + 1, rate);
	}

	if (rate && rate < 3000000)
		dev_warn(codec->dev, "AIF%dCLK is %dHz, should be >=3MHz for optimal performance\n",
			 aif + 1, rate);

	wm8994->aifclk[aif] = rate;

	snd_soc_update_bits(codec, WM8994_AIF1_CLOCKING_1 + offset,
			    WM8994_AIF1CLK_SRC_MASK | WM8994_AIF1CLK_DIV,
			    reg1);

	return 0;
}

static int configure_clock(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int old, new;

	/* Bring up the AIF clocks first */
	configure_aif_clock(codec, 0);
	configure_aif_clock(codec, 1);

	/* Then switch CLK_SYS over to the higher of them; a change
	 * can only happen as a result of a clocking change which can
	 * only be made outside of DAPM so we can safely redo the
	 * clocking.
	 */

	/* If they're equal it doesn't matter which is used */
	if (wm8994->aifclk[0] == wm8994->aifclk[1])
		return 0;

	if (wm8994->aifclk[0] < wm8994->aifclk[1])
		new = WM8994_SYSCLK_SRC;
	else
		new = 0;

	old = snd_soc_read(codec, WM8994_CLOCKING_1) & WM8994_SYSCLK_SRC;

	/* If there's no change then we're done. */
	if (old == new)
		return 0;

	snd_soc_update_bits(codec, WM8994_CLOCKING_1, WM8994_SYSCLK_SRC, new);

	snd_soc_dapm_sync(&codec->dapm);

	return 0;
}

static int check_clk_sys(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	int reg = snd_soc_read(source->codec, WM8994_CLOCKING_1);
	const char *clk;

	/* Check what we're currently using for CLK_SYS */
	if (reg & WM8994_SYSCLK_SRC)
		clk = "AIF2CLK";
	else
		clk = "AIF1CLK";

	return strcmp(source->name, clk) == 0;
}

static const char *sidetone_hpf_text[] = {
	"2.7kHz", "1.35kHz", "675Hz", "370Hz", "180Hz", "90Hz", "45Hz"
};

static const struct soc_enum sidetone_hpf =
	SOC_ENUM_SINGLE(WM8994_SIDETONE, 7, 7, sidetone_hpf_text);

static const DECLARE_TLV_DB_SCALE(aif_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(st_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(wm8994_3d_tlv, -1600, 183, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);

#define WM8994_DRC_SWITCH(xname, reg, shift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, .get = snd_soc_get_volsw,\
	.put = wm8994_put_drc_sw, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, 1, 0) }

static int wm8994_put_drc_sw(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int mask, ret;

	/* Can't enable both ADC and DAC paths simultaneously */
	if (mc->shift == WM8994_AIF1DAC1_DRC_ENA_SHIFT)
		mask = WM8994_AIF1ADC1L_DRC_ENA_MASK |
			WM8994_AIF1ADC1R_DRC_ENA_MASK;
	else
		mask = WM8994_AIF1DAC1_DRC_ENA_MASK;

	ret = snd_soc_read(codec, mc->reg);
	if (ret < 0)
		return ret;
	if (ret & mask)
		return -EINVAL;

	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static void wm8994_set_drc(struct snd_soc_codec *codec, int drc)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int base = wm8994_drc_base[drc];
	int cfg = wm8994->drc_cfg[drc];
	int save, i;

	/* Save any enables; the configuration should clear them. */
	save = snd_soc_read(codec, base);
	save &= WM8994_AIF1DAC1_DRC_ENA | WM8994_AIF1ADC1L_DRC_ENA |
		WM8994_AIF1ADC1R_DRC_ENA;

	for (i = 0; i < WM8994_DRC_REGS; i++)
		snd_soc_update_bits(codec, base + i, 0xffff,
				    pdata->drc_cfgs[cfg].regs[i]);

	snd_soc_update_bits(codec, base, WM8994_AIF1DAC1_DRC_ENA |
			     WM8994_AIF1ADC1L_DRC_ENA |
			     WM8994_AIF1ADC1R_DRC_ENA, save);
}

/* Icky as hell but saves code duplication */
static int wm8994_get_drc(const char *name)
{
	if (strcmp(name, "AIF1DRC1 Mode") == 0)
		return 0;
	if (strcmp(name, "AIF1DRC2 Mode") == 0)
		return 1;
	if (strcmp(name, "AIF2DRC Mode") == 0)
		return 2;
	return -EINVAL;
}

static int wm8994_put_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int drc = wm8994_get_drc(kcontrol->id.name);
	int value = ucontrol->value.integer.value[0];

	if (drc < 0)
		return drc;

	if (value >= pdata->num_drc_cfgs)
		return -EINVAL;

	wm8994->drc_cfg[drc] = value;

	wm8994_set_drc(codec, drc);

	return 0;
}

static int wm8994_get_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int drc = wm8994_get_drc(kcontrol->id.name);

	ucontrol->value.enumerated.item[0] = wm8994->drc_cfg[drc];

	return 0;
}

static void wm8994_set_retune_mobile(struct snd_soc_codec *codec, int block)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int base = wm8994_retune_mobile_base[block];
	int iface, best, best_val, save, i, cfg;

	if (!pdata || !wm8994->num_retune_mobile_texts)
		return;

	switch (block) {
	case 0:
	case 1:
		iface = 0;
		break;
	case 2:
		iface = 1;
		break;
	default:
		return;
	}

	/* Find the version of the currently selected configuration
	 * with the nearest sample rate. */
	cfg = wm8994->retune_mobile_cfg[block];
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		if (strcmp(pdata->retune_mobile_cfgs[i].name,
			   wm8994->retune_mobile_texts[cfg]) == 0 &&
		    abs(pdata->retune_mobile_cfgs[i].rate
			- wm8994->dac_rates[iface]) < best_val) {
			best = i;
			best_val = abs(pdata->retune_mobile_cfgs[i].rate
				       - wm8994->dac_rates[iface]);
		}
	}

	dev_dbg(codec->dev, "ReTune Mobile %d %s/%dHz for %dHz sample rate\n",
		block,
		pdata->retune_mobile_cfgs[best].name,
		pdata->retune_mobile_cfgs[best].rate,
		wm8994->dac_rates[iface]);

	/* The EQ will be disabled while reconfiguring it, remember the
	 * current configuration. 
	 */
	save = snd_soc_read(codec, base);
	save &= WM8994_AIF1DAC1_EQ_ENA;

	for (i = 0; i < WM8994_EQ_REGS; i++)
		snd_soc_update_bits(codec, base + i, 0xffff,
				pdata->retune_mobile_cfgs[best].regs[i]);

	snd_soc_update_bits(codec, base, WM8994_AIF1DAC1_EQ_ENA, save);
}

/* Icky as hell but saves code duplication */
static int wm8994_get_retune_mobile_block(const char *name)
{
	if (strcmp(name, "AIF1.1 EQ Mode") == 0)
		return 0;
	if (strcmp(name, "AIF1.2 EQ Mode") == 0)
		return 1;
	if (strcmp(name, "AIF2 EQ Mode") == 0)
		return 2;
	return -EINVAL;
}

static int wm8994_put_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int block = wm8994_get_retune_mobile_block(kcontrol->id.name);
	int value = ucontrol->value.integer.value[0];

	if (block < 0)
		return block;

	if (value >= pdata->num_retune_mobile_cfgs)
		return -EINVAL;

	wm8994->retune_mobile_cfg[block] = value;

	wm8994_set_retune_mobile(codec, block);

	return 0;
}

static int wm8994_get_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 =snd_soc_codec_get_drvdata(codec);
	int block = wm8994_get_retune_mobile_block(kcontrol->id.name);

	ucontrol->value.enumerated.item[0] = wm8994->retune_mobile_cfg[block];

	return 0;
}

static const char *aif_chan_src_text[] = {
	"Left", "Right"
};

static const struct soc_enum aif1adcl_src =
	SOC_ENUM_SINGLE(WM8994_AIF1_CONTROL_1, 15, 2, aif_chan_src_text);

static const struct soc_enum aif1adcr_src =
	SOC_ENUM_SINGLE(WM8994_AIF1_CONTROL_1, 14, 2, aif_chan_src_text);

static const struct soc_enum aif2adcl_src =
	SOC_ENUM_SINGLE(WM8994_AIF2_CONTROL_1, 15, 2, aif_chan_src_text);

static const struct soc_enum aif2adcr_src =
	SOC_ENUM_SINGLE(WM8994_AIF2_CONTROL_1, 14, 2, aif_chan_src_text);

static const struct soc_enum aif1dacl_src =
	SOC_ENUM_SINGLE(WM8994_AIF1_CONTROL_2, 15, 2, aif_chan_src_text);

static const struct soc_enum aif1dacr_src =
	SOC_ENUM_SINGLE(WM8994_AIF1_CONTROL_2, 14, 2, aif_chan_src_text);

static const struct soc_enum aif2dacl_src =
	SOC_ENUM_SINGLE(WM8994_AIF2_CONTROL_2, 15, 2, aif_chan_src_text);

static const struct soc_enum aif2dacr_src =
	SOC_ENUM_SINGLE(WM8994_AIF2_CONTROL_2, 14, 2, aif_chan_src_text);

static void wm8958_mbc_apply(struct snd_soc_codec *codec, int mbc, int start)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int pwr_reg = snd_soc_read(codec, WM8994_POWER_MANAGEMENT_5);
	int ena, reg, aif, i;

	switch (mbc) {
	case 0:
		pwr_reg &= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA);
		aif = 0;
		break;
	case 1:
		pwr_reg &= (WM8994_AIF1DAC2L_ENA | WM8994_AIF1DAC2R_ENA);
		aif = 0;
		break;
	case 2:
		pwr_reg &= (WM8994_AIF2DACL_ENA | WM8994_AIF2DACR_ENA);
		aif = 1;
		break;
	default:
		BUG();
		return;
	}

	/* We can only enable the MBC if the AIF is enabled and we
	 * want it to be enabled. */
	ena = pwr_reg && wm8994->mbc_ena[mbc];

	reg = snd_soc_read(codec, WM8958_DSP2_PROGRAM);

	dev_dbg(codec->dev, "MBC %d startup: %d, power: %x, DSP: %x\n",
		mbc, start, pwr_reg, reg);

	if (start && ena) {
		/* If the DSP is already running then noop */
		if (reg & WM8958_DSP2_ENA)
			return;

		/* Switch the clock over to the appropriate AIF */
		snd_soc_update_bits(codec, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_SRC | WM8958_DSP2CLK_ENA,
				    aif << WM8958_DSP2CLK_SRC_SHIFT |
				    WM8958_DSP2CLK_ENA);

		snd_soc_update_bits(codec, WM8958_DSP2_PROGRAM,
				    WM8958_DSP2_ENA, WM8958_DSP2_ENA);

		/* If we've got user supplied MBC settings use them */
		if (pdata && pdata->num_mbc_cfgs) {
			struct wm8958_mbc_cfg *cfg
				= &pdata->mbc_cfgs[wm8994->mbc_cfg];

			for (i = 0; i < ARRAY_SIZE(cfg->coeff_regs); i++)
				snd_soc_write(codec, i + WM8958_MBC_BAND_1_K_1,
					      cfg->coeff_regs[i]);

			for (i = 0; i < ARRAY_SIZE(cfg->cutoff_regs); i++)
				snd_soc_write(codec,
					      i + WM8958_MBC_BAND_2_LOWER_CUTOFF_C1_1,
					      cfg->cutoff_regs[i]);
		}

		/* Run the DSP */
		snd_soc_write(codec, WM8958_DSP2_EXECCONTROL,
			      WM8958_DSP2_RUNR);

		/* And we're off! */
		snd_soc_update_bits(codec, WM8958_DSP2_CONFIG,
				    WM8958_MBC_ENA | WM8958_MBC_SEL_MASK,
				    mbc << WM8958_MBC_SEL_SHIFT |
				    WM8958_MBC_ENA);
	} else {
		/* If the DSP is already stopped then noop */
		if (!(reg & WM8958_DSP2_ENA))
			return;

		snd_soc_update_bits(codec, WM8958_DSP2_CONFIG,
				    WM8958_MBC_ENA, 0);	
		snd_soc_update_bits(codec, WM8958_DSP2_PROGRAM,
				    WM8958_DSP2_ENA, 0);
		snd_soc_update_bits(codec, WM8994_CLOCKING_1,
				    WM8958_DSP2CLK_ENA, 0);
	}
}

static int wm8958_aif_ev(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int mbc;

	switch (w->shift) {
	case 13:
	case 12:
		mbc = 2;
		break;
	case 11:
	case 10:
		mbc = 1;
		break;
	case 9:
	case 8:
		mbc = 0;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wm8958_mbc_apply(codec, mbc, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wm8958_mbc_apply(codec, mbc, 0);
		break;
	}

	return 0;
}

static int wm8958_put_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int value = ucontrol->value.integer.value[0];
	int reg;

	/* Don't allow on the fly reconfiguration */
	reg = snd_soc_read(codec, WM8994_CLOCKING_1);
	if (reg < 0 || reg & WM8958_DSP2CLK_ENA)
		return -EBUSY;

	if (value >= pdata->num_mbc_cfgs)
		return -EINVAL;

	wm8994->mbc_cfg = value;

	return 0;
}

static int wm8958_get_mbc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8994->mbc_cfg;

	return 0;
}

static int wm8958_mbc_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm8958_mbc_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int mbc = kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wm8994->mbc_ena[mbc];

	return 0;
}

static int wm8958_mbc_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	int mbc = kcontrol->private_value;
	int i;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	if (ucontrol->value.integer.value[0] > 1)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(wm8994->mbc_ena); i++) {
		if (mbc != i && wm8994->mbc_ena[i]) {
			dev_dbg(codec->dev, "MBC %d active already\n", mbc);
			return -EBUSY;
		}
	}

	wm8994->mbc_ena[mbc] = ucontrol->value.integer.value[0];

	wm8958_mbc_apply(codec, mbc, wm8994->mbc_ena[mbc]);

	return 0;
}

#define WM8958_MBC_SWITCH(xname, xval) {\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,\
	.info = wm8958_mbc_info, \
	.get = wm8958_mbc_get, .put = wm8958_mbc_put, \
	.private_value = xval }

static const struct snd_kcontrol_new wm8994_snd_controls[] = {
SOC_DOUBLE_R_TLV("AIF1ADC1 Volume", WM8994_AIF1_ADC1_LEFT_VOLUME,
		 WM8994_AIF1_ADC1_RIGHT_VOLUME,
		 1, 119, 0, digital_tlv),
SOC_DOUBLE_R_TLV("AIF1ADC2 Volume", WM8994_AIF1_ADC2_LEFT_VOLUME,
		 WM8994_AIF1_ADC2_RIGHT_VOLUME,
		 1, 119, 0, digital_tlv),
SOC_DOUBLE_R_TLV("AIF2ADC Volume", WM8994_AIF2_ADC_LEFT_VOLUME,
		 WM8994_AIF2_ADC_RIGHT_VOLUME,
		 1, 119, 0, digital_tlv),

SOC_ENUM("AIF1ADCL Source", aif1adcl_src),
SOC_ENUM("AIF1ADCR Source", aif1adcr_src),
SOC_ENUM("AIF2ADCL Source", aif1adcl_src),
SOC_ENUM("AIF2ADCR Source", aif1adcr_src),

SOC_ENUM("AIF1DACL Source", aif1dacl_src),
SOC_ENUM("AIF1DACR Source", aif1dacr_src),
SOC_ENUM("AIF2DACL Source", aif1dacl_src),
SOC_ENUM("AIF2DACR Source", aif1dacr_src),

SOC_DOUBLE_R_TLV("AIF1DAC1 Volume", WM8994_AIF1_DAC1_LEFT_VOLUME,
		 WM8994_AIF1_DAC1_RIGHT_VOLUME, 1, 96, 0, digital_tlv),
SOC_DOUBLE_R_TLV("AIF1DAC2 Volume", WM8994_AIF1_DAC2_LEFT_VOLUME,
		 WM8994_AIF1_DAC2_RIGHT_VOLUME, 1, 96, 0, digital_tlv),
SOC_DOUBLE_R_TLV("AIF2DAC Volume", WM8994_AIF2_DAC_LEFT_VOLUME,
		 WM8994_AIF2_DAC_RIGHT_VOLUME, 1, 96, 0, digital_tlv),

SOC_SINGLE_TLV("AIF1 Boost Volume", WM8994_AIF1_CONTROL_2, 10, 3, 0, aif_tlv),
SOC_SINGLE_TLV("AIF2 Boost Volume", WM8994_AIF2_CONTROL_2, 10, 3, 0, aif_tlv),

SOC_SINGLE("AIF1DAC1 EQ Switch", WM8994_AIF1_DAC1_EQ_GAINS_1, 0, 1, 0),
SOC_SINGLE("AIF1DAC2 EQ Switch", WM8994_AIF1_DAC2_EQ_GAINS_1, 0, 1, 0),
SOC_SINGLE("AIF2 EQ Switch", WM8994_AIF2_EQ_GAINS_1, 0, 1, 0),

WM8994_DRC_SWITCH("AIF1DAC1 DRC Switch", WM8994_AIF1_DRC1_1, 2),
WM8994_DRC_SWITCH("AIF1ADC1L DRC Switch", WM8994_AIF1_DRC1_1, 1),
WM8994_DRC_SWITCH("AIF1ADC1R DRC Switch", WM8994_AIF1_DRC1_1, 0),

WM8994_DRC_SWITCH("AIF1DAC2 DRC Switch", WM8994_AIF1_DRC2_1, 2),
WM8994_DRC_SWITCH("AIF1ADC2L DRC Switch", WM8994_AIF1_DRC2_1, 1),
WM8994_DRC_SWITCH("AIF1ADC2R DRC Switch", WM8994_AIF1_DRC2_1, 0),

WM8994_DRC_SWITCH("AIF2DAC DRC Switch", WM8994_AIF2_DRC_1, 2),
WM8994_DRC_SWITCH("AIF2ADCL DRC Switch", WM8994_AIF2_DRC_1, 1),
WM8994_DRC_SWITCH("AIF2ADCR DRC Switch", WM8994_AIF2_DRC_1, 0),

SOC_SINGLE_TLV("DAC1 Right Sidetone Volume", WM8994_DAC1_MIXER_VOLUMES,
	       5, 12, 0, st_tlv),
SOC_SINGLE_TLV("DAC1 Left Sidetone Volume", WM8994_DAC1_MIXER_VOLUMES,
	       0, 12, 0, st_tlv),
SOC_SINGLE_TLV("DAC2 Right Sidetone Volume", WM8994_DAC2_MIXER_VOLUMES,
	       5, 12, 0, st_tlv),
SOC_SINGLE_TLV("DAC2 Left Sidetone Volume", WM8994_DAC2_MIXER_VOLUMES,
	       0, 12, 0, st_tlv),
SOC_ENUM("Sidetone HPF Mux", sidetone_hpf),
SOC_SINGLE("Sidetone HPF Switch", WM8994_SIDETONE, 6, 1, 0),

SOC_DOUBLE_R_TLV("DAC1 Volume", WM8994_DAC1_LEFT_VOLUME,
		 WM8994_DAC1_RIGHT_VOLUME, 1, 96, 0, digital_tlv),
SOC_DOUBLE_R("DAC1 Switch", WM8994_DAC1_LEFT_VOLUME,
	     WM8994_DAC1_RIGHT_VOLUME, 9, 1, 1),

SOC_DOUBLE_R_TLV("DAC2 Volume", WM8994_DAC2_LEFT_VOLUME,
		 WM8994_DAC2_RIGHT_VOLUME, 1, 96, 0, digital_tlv),
SOC_DOUBLE_R("DAC2 Switch", WM8994_DAC2_LEFT_VOLUME,
	     WM8994_DAC2_RIGHT_VOLUME, 9, 1, 1),

SOC_SINGLE_TLV("SPKL DAC2 Volume", WM8994_SPKMIXL_ATTENUATION,
	       6, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKL DAC1 Volume", WM8994_SPKMIXL_ATTENUATION,
	       2, 1, 1, wm_hubs_spkmix_tlv),

SOC_SINGLE_TLV("SPKR DAC2 Volume", WM8994_SPKMIXR_ATTENUATION,
	       6, 1, 1, wm_hubs_spkmix_tlv),
SOC_SINGLE_TLV("SPKR DAC1 Volume", WM8994_SPKMIXR_ATTENUATION,
	       2, 1, 1, wm_hubs_spkmix_tlv),

SOC_SINGLE_TLV("AIF1DAC1 3D Stereo Volume", WM8994_AIF1_DAC1_FILTERS_2,
	       10, 15, 0, wm8994_3d_tlv),
SOC_SINGLE("AIF1DAC1 3D Stereo Switch", WM8994_AIF1_DAC2_FILTERS_2,
	   8, 1, 0),
SOC_SINGLE_TLV("AIF1DAC2 3D Stereo Volume", WM8994_AIF1_DAC2_FILTERS_2,
	       10, 15, 0, wm8994_3d_tlv),
SOC_SINGLE("AIF1DAC2 3D Stereo Switch", WM8994_AIF1_DAC2_FILTERS_2,
	   8, 1, 0),
SOC_SINGLE_TLV("AIF2DAC 3D Stereo Volume", WM8994_AIF1_DAC1_FILTERS_2,
	       10, 15, 0, wm8994_3d_tlv),
SOC_SINGLE("AIF2DAC 3D Stereo Switch", WM8994_AIF1_DAC2_FILTERS_2,
	   8, 1, 0),
};

static const struct snd_kcontrol_new wm8994_eq_controls[] = {
SOC_SINGLE_TLV("AIF1DAC1 EQ1 Volume", WM8994_AIF1_DAC1_EQ_GAINS_1, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC1 EQ2 Volume", WM8994_AIF1_DAC1_EQ_GAINS_1, 6, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC1 EQ3 Volume", WM8994_AIF1_DAC1_EQ_GAINS_1, 1, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC1 EQ4 Volume", WM8994_AIF1_DAC1_EQ_GAINS_2, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC1 EQ5 Volume", WM8994_AIF1_DAC1_EQ_GAINS_2, 6, 31, 0,
	       eq_tlv),

SOC_SINGLE_TLV("AIF1DAC2 EQ1 Volume", WM8994_AIF1_DAC2_EQ_GAINS_1, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC2 EQ2 Volume", WM8994_AIF1_DAC2_EQ_GAINS_1, 6, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC2 EQ3 Volume", WM8994_AIF1_DAC2_EQ_GAINS_1, 1, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC2 EQ4 Volume", WM8994_AIF1_DAC2_EQ_GAINS_2, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF1DAC2 EQ5 Volume", WM8994_AIF1_DAC2_EQ_GAINS_2, 6, 31, 0,
	       eq_tlv),

SOC_SINGLE_TLV("AIF2 EQ1 Volume", WM8994_AIF2_EQ_GAINS_1, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF2 EQ2 Volume", WM8994_AIF2_EQ_GAINS_1, 6, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF2 EQ3 Volume", WM8994_AIF2_EQ_GAINS_1, 1, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF2 EQ4 Volume", WM8994_AIF2_EQ_GAINS_2, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("AIF2 EQ5 Volume", WM8994_AIF2_EQ_GAINS_2, 6, 31, 0,
	       eq_tlv),
};

static const struct snd_kcontrol_new wm8958_snd_controls[] = {
SOC_SINGLE_TLV("AIF3 Boost Volume", WM8958_AIF3_CONTROL_2, 10, 3, 0, aif_tlv),
WM8958_MBC_SWITCH("AIF1DAC1 MBC Switch", 0),
WM8958_MBC_SWITCH("AIF1DAC2 MBC Switch", 1),
WM8958_MBC_SWITCH("AIF2DAC MBC Switch", 2),
};

static int clk_sys_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return configure_clock(codec);

	case SND_SOC_DAPM_POST_PMD:
		configure_clock(codec);
		break;
	}

	return 0;
}

static void wm8994_update_class_w(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int enable = 1;
	int source = 0;  /* GCC flow analysis can't track enable */
	int reg, reg_r;

	/* Only support direct DAC->headphone paths */
	reg = snd_soc_read(codec, WM8994_OUTPUT_MIXER_1);
	if (!(reg & WM8994_DAC1L_TO_HPOUT1L)) {
		dev_vdbg(codec->dev, "HPL connected to output mixer\n");
		enable = 0;
	}

	reg = snd_soc_read(codec, WM8994_OUTPUT_MIXER_2);
	if (!(reg & WM8994_DAC1R_TO_HPOUT1R)) {
		dev_vdbg(codec->dev, "HPR connected to output mixer\n");
		enable = 0;
	}

	/* We also need the same setting for L/R and only one path */
	reg = snd_soc_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	switch (reg) {
	case WM8994_AIF2DACL_TO_DAC1L:
		dev_vdbg(codec->dev, "Class W source AIF2DAC\n");
		source = 2 << WM8994_CP_DYN_SRC_SEL_SHIFT;
		break;
	case WM8994_AIF1DAC2L_TO_DAC1L:
		dev_vdbg(codec->dev, "Class W source AIF1DAC2\n");
		source = 1 << WM8994_CP_DYN_SRC_SEL_SHIFT;
		break;
	case WM8994_AIF1DAC1L_TO_DAC1L:
		dev_vdbg(codec->dev, "Class W source AIF1DAC1\n");
		source = 0 << WM8994_CP_DYN_SRC_SEL_SHIFT;
		break;
	default:
		dev_vdbg(codec->dev, "DAC mixer setting: %x\n", reg);
		enable = 0;
		break;
	}

	reg_r = snd_soc_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	if (reg_r != reg) {
		dev_vdbg(codec->dev, "Left and right DAC mixers different\n");
		enable = 0;
	}

	if (enable) {
		dev_dbg(codec->dev, "Class W enabled\n");
		snd_soc_update_bits(codec, WM8994_CLASS_W_1,
				    WM8994_CP_DYN_PWR |
				    WM8994_CP_DYN_SRC_SEL_MASK,
				    source | WM8994_CP_DYN_PWR);
		wm8994->hubs.class_w = true;
		
	} else {
		dev_dbg(codec->dev, "Class W disabled\n");
		snd_soc_update_bits(codec, WM8994_CLASS_W_1,
				    WM8994_CP_DYN_PWR, 0);
		wm8994->hubs.class_w = false;
	}
}

static const char *hp_mux_text[] = {
	"Mixer",
	"DAC",
};

#define WM8994_HP_ENUM(xname, xenum) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
 	.get = snd_soc_dapm_get_enum_double, \
 	.put = wm8994_put_hp_enum, \
  	.private_value = (unsigned long)&xenum }

static int wm8994_put_hp_enum(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = w->codec;
	int ret;

	ret = snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	wm8994_update_class_w(codec);

	return ret;
}

static const struct soc_enum hpl_enum =
	SOC_ENUM_SINGLE(WM8994_OUTPUT_MIXER_1, 8, 2, hp_mux_text);

static const struct snd_kcontrol_new hpl_mux =
	WM8994_HP_ENUM("Left Headphone Mux", hpl_enum);

static const struct soc_enum hpr_enum =
	SOC_ENUM_SINGLE(WM8994_OUTPUT_MIXER_2, 8, 2, hp_mux_text);

static const struct snd_kcontrol_new hpr_mux =
	WM8994_HP_ENUM("Right Headphone Mux", hpr_enum);

static const char *adc_mux_text[] = {
	"ADC",
	"DMIC",
};

static const struct soc_enum adc_enum =
	SOC_ENUM_SINGLE(0, 0, 2, adc_mux_text);

static const struct snd_kcontrol_new adcl_mux =
	SOC_DAPM_ENUM_VIRT("ADCL Mux", adc_enum);

static const struct snd_kcontrol_new adcr_mux =
	SOC_DAPM_ENUM_VIRT("ADCR Mux", adc_enum);

static const struct snd_kcontrol_new left_speaker_mixer[] = {
SOC_DAPM_SINGLE("DAC2 Switch", WM8994_SPEAKER_MIXER, 9, 1, 0),
SOC_DAPM_SINGLE("Input Switch", WM8994_SPEAKER_MIXER, 7, 1, 0),
SOC_DAPM_SINGLE("IN1LP Switch", WM8994_SPEAKER_MIXER, 5, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8994_SPEAKER_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("DAC1 Switch", WM8994_SPEAKER_MIXER, 1, 1, 0),
};

static const struct snd_kcontrol_new right_speaker_mixer[] = {
SOC_DAPM_SINGLE("DAC2 Switch", WM8994_SPEAKER_MIXER, 8, 1, 0),
SOC_DAPM_SINGLE("Input Switch", WM8994_SPEAKER_MIXER, 6, 1, 0),
SOC_DAPM_SINGLE("IN1RP Switch", WM8994_SPEAKER_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("Output Switch", WM8994_SPEAKER_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("DAC1 Switch", WM8994_SPEAKER_MIXER, 0, 1, 0),
};

/* Debugging; dump chip status after DAPM transitions */
static int post_ev(struct snd_soc_dapm_widget *w,
	    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	dev_dbg(codec->dev, "SRC status: %x\n",
		snd_soc_read(codec,
			     WM8994_RATE_STATUS));
	return 0;
}

static const struct snd_kcontrol_new aif1adc1l_mix[] = {
SOC_DAPM_SINGLE("ADC/DMIC Switch", WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc1r_mix[] = {
SOC_DAPM_SINGLE("ADC/DMIC Switch", WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc2l_mix[] = {
SOC_DAPM_SINGLE("DMIC Switch", WM8994_AIF1_ADC2_LEFT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_AIF1_ADC2_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif1adc2r_mix[] = {
SOC_DAPM_SINGLE("DMIC Switch", WM8994_AIF1_ADC2_RIGHT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_AIF1_ADC2_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif2dac2l_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8994_DAC2_LEFT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8994_DAC2_LEFT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_DAC2_LEFT_MIXER_ROUTING,
		2, 1, 0),
SOC_DAPM_SINGLE("AIF1.2 Switch", WM8994_DAC2_LEFT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF1.1 Switch", WM8994_DAC2_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new aif2dac2r_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8994_DAC2_RIGHT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8994_DAC2_RIGHT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("AIF2 Switch", WM8994_DAC2_RIGHT_MIXER_ROUTING,
		2, 1, 0),
SOC_DAPM_SINGLE("AIF1.2 Switch", WM8994_DAC2_RIGHT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("AIF1.1 Switch", WM8994_DAC2_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

#define WM8994_CLASS_W_SWITCH(xname, reg, shift, max, invert) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = snd_soc_dapm_get_volsw, .put = wm8994_put_class_w, \
	.private_value =  SOC_SINGLE_VALUE(reg, shift, max, invert) }

static int wm8994_put_class_w(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = w->codec;
	int ret;

	ret = snd_soc_dapm_put_volsw(kcontrol, ucontrol);

	wm8994_update_class_w(codec);

	return ret;
}

static const struct snd_kcontrol_new dac1l_mix[] = {
WM8994_CLASS_W_SWITCH("Right Sidetone Switch", WM8994_DAC1_LEFT_MIXER_ROUTING,
		      5, 1, 0),
WM8994_CLASS_W_SWITCH("Left Sidetone Switch", WM8994_DAC1_LEFT_MIXER_ROUTING,
		      4, 1, 0),
WM8994_CLASS_W_SWITCH("AIF2 Switch", WM8994_DAC1_LEFT_MIXER_ROUTING,
		      2, 1, 0),
WM8994_CLASS_W_SWITCH("AIF1.2 Switch", WM8994_DAC1_LEFT_MIXER_ROUTING,
		      1, 1, 0),
WM8994_CLASS_W_SWITCH("AIF1.1 Switch", WM8994_DAC1_LEFT_MIXER_ROUTING,
		      0, 1, 0),
};

static const struct snd_kcontrol_new dac1r_mix[] = {
WM8994_CLASS_W_SWITCH("Right Sidetone Switch", WM8994_DAC1_RIGHT_MIXER_ROUTING,
		      5, 1, 0),
WM8994_CLASS_W_SWITCH("Left Sidetone Switch", WM8994_DAC1_RIGHT_MIXER_ROUTING,
		      4, 1, 0),
WM8994_CLASS_W_SWITCH("AIF2 Switch", WM8994_DAC1_RIGHT_MIXER_ROUTING,
		      2, 1, 0),
WM8994_CLASS_W_SWITCH("AIF1.2 Switch", WM8994_DAC1_RIGHT_MIXER_ROUTING,
		      1, 1, 0),
WM8994_CLASS_W_SWITCH("AIF1.1 Switch", WM8994_DAC1_RIGHT_MIXER_ROUTING,
		      0, 1, 0),
};

static const char *sidetone_text[] = {
	"ADC/DMIC1", "DMIC2",
};

static const struct soc_enum sidetone1_enum =
	SOC_ENUM_SINGLE(WM8994_SIDETONE, 0, 2, sidetone_text);

static const struct snd_kcontrol_new sidetone1_mux =
	SOC_DAPM_ENUM("Left Sidetone Mux", sidetone1_enum);

static const struct soc_enum sidetone2_enum =
	SOC_ENUM_SINGLE(WM8994_SIDETONE, 1, 2, sidetone_text);

static const struct snd_kcontrol_new sidetone2_mux =
	SOC_DAPM_ENUM("Right Sidetone Mux", sidetone2_enum);

static const char *aif1dac_text[] = {
	"AIF1DACDAT", "AIF3DACDAT",
};

static const struct soc_enum aif1dac_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 0, 2, aif1dac_text);

static const struct snd_kcontrol_new aif1dac_mux =
	SOC_DAPM_ENUM("AIF1DAC Mux", aif1dac_enum);

static const char *aif2dac_text[] = {
	"AIF2DACDAT", "AIF3DACDAT",
};

static const struct soc_enum aif2dac_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 1, 2, aif2dac_text);

static const struct snd_kcontrol_new aif2dac_mux =
	SOC_DAPM_ENUM("AIF2DAC Mux", aif2dac_enum);

static const char *aif2adc_text[] = {
	"AIF2ADCDAT", "AIF3DACDAT",
};

static const struct soc_enum aif2adc_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 2, 2, aif2adc_text);

static const struct snd_kcontrol_new aif2adc_mux =
	SOC_DAPM_ENUM("AIF2ADC Mux", aif2adc_enum);

static const char *aif3adc_text[] = {
	"AIF1ADCDAT", "AIF2ADCDAT", "AIF2DACDAT", "Mono PCM",
};

static const struct soc_enum wm8994_aif3adc_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 3, 3, aif3adc_text);

static const struct snd_kcontrol_new wm8994_aif3adc_mux =
	SOC_DAPM_ENUM("AIF3ADC Mux", wm8994_aif3adc_enum);

static const struct soc_enum wm8958_aif3adc_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 3, 4, aif3adc_text);

static const struct snd_kcontrol_new wm8958_aif3adc_mux =
	SOC_DAPM_ENUM("AIF3ADC Mux", wm8958_aif3adc_enum);

static const char *mono_pcm_out_text[] = {
	"None", "AIF2ADCL", "AIF2ADCR", 
};

static const struct soc_enum mono_pcm_out_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 9, 3, mono_pcm_out_text);

static const struct snd_kcontrol_new mono_pcm_out_mux =
	SOC_DAPM_ENUM("Mono PCM Out Mux", mono_pcm_out_enum);

static const char *aif2dac_src_text[] = {
	"AIF2", "AIF3",
};

/* Note that these two control shouldn't be simultaneously switched to AIF3 */
static const struct soc_enum aif2dacl_src_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 7, 2, aif2dac_src_text);

static const struct snd_kcontrol_new aif2dacl_src_mux =
	SOC_DAPM_ENUM("AIF2DACL Mux", aif2dacl_src_enum);

static const struct soc_enum aif2dacr_src_enum =
	SOC_ENUM_SINGLE(WM8994_POWER_MANAGEMENT_6, 8, 2, aif2dac_src_text);

static const struct snd_kcontrol_new aif2dacr_src_mux =
	SOC_DAPM_ENUM("AIF2DACR Mux", aif2dacr_src_enum);

static const struct snd_soc_dapm_widget wm8994_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("DMIC1DAT"),
SND_SOC_DAPM_INPUT("DMIC2DAT"),
SND_SOC_DAPM_INPUT("Clock"),

SND_SOC_DAPM_SUPPLY("CLK_SYS", SND_SOC_NOPM, 0, 0, clk_sys_event,
		    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_SUPPLY("DSP1CLK", WM8994_CLOCKING_1, 3, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("DSP2CLK", WM8994_CLOCKING_1, 2, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("DSPINTCLK", WM8994_CLOCKING_1, 1, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("AIF1CLK", WM8994_AIF1_CLOCKING_1, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("AIF2CLK", WM8994_AIF2_CLOCKING_1, 0, 0, NULL, 0),

SND_SOC_DAPM_AIF_OUT("AIF1ADC1L", "AIF1 Capture",
		     0, WM8994_POWER_MANAGEMENT_4, 9, 0),
SND_SOC_DAPM_AIF_OUT("AIF1ADC1R", "AIF1 Capture",
		     0, WM8994_POWER_MANAGEMENT_4, 8, 0),
SND_SOC_DAPM_AIF_IN_E("AIF1DAC1L", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 9, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_AIF_IN_E("AIF1DAC1R", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 8, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_AIF_OUT("AIF1ADC2L", "AIF1 Capture",
		     0, WM8994_POWER_MANAGEMENT_4, 11, 0),
SND_SOC_DAPM_AIF_OUT("AIF1ADC2R", "AIF1 Capture",
		     0, WM8994_POWER_MANAGEMENT_4, 10, 0),
SND_SOC_DAPM_AIF_IN_E("AIF1DAC2L", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 11, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_AIF_IN_E("AIF1DAC2R", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 10, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_MIXER("AIF1ADC1L Mixer", SND_SOC_NOPM, 0, 0,
		   aif1adc1l_mix, ARRAY_SIZE(aif1adc1l_mix)),
SND_SOC_DAPM_MIXER("AIF1ADC1R Mixer", SND_SOC_NOPM, 0, 0,
		   aif1adc1r_mix, ARRAY_SIZE(aif1adc1r_mix)),

SND_SOC_DAPM_MIXER("AIF1ADC2L Mixer", SND_SOC_NOPM, 0, 0,
		   aif1adc2l_mix, ARRAY_SIZE(aif1adc2l_mix)),
SND_SOC_DAPM_MIXER("AIF1ADC2R Mixer", SND_SOC_NOPM, 0, 0,
		   aif1adc2r_mix, ARRAY_SIZE(aif1adc2r_mix)),

SND_SOC_DAPM_MIXER("AIF2DAC2L Mixer", SND_SOC_NOPM, 0, 0,
		   aif2dac2l_mix, ARRAY_SIZE(aif2dac2l_mix)),
SND_SOC_DAPM_MIXER("AIF2DAC2R Mixer", SND_SOC_NOPM, 0, 0,
		   aif2dac2r_mix, ARRAY_SIZE(aif2dac2r_mix)),

SND_SOC_DAPM_MUX("Left Sidetone", SND_SOC_NOPM, 0, 0, &sidetone1_mux),
SND_SOC_DAPM_MUX("Right Sidetone", SND_SOC_NOPM, 0, 0, &sidetone2_mux),

SND_SOC_DAPM_MIXER("DAC1L Mixer", SND_SOC_NOPM, 0, 0,
		   dac1l_mix, ARRAY_SIZE(dac1l_mix)),
SND_SOC_DAPM_MIXER("DAC1R Mixer", SND_SOC_NOPM, 0, 0,
		   dac1r_mix, ARRAY_SIZE(dac1r_mix)),

SND_SOC_DAPM_AIF_OUT("AIF2ADCL", NULL, 0,
		     WM8994_POWER_MANAGEMENT_4, 13, 0),
SND_SOC_DAPM_AIF_OUT("AIF2ADCR", NULL, 0,
		     WM8994_POWER_MANAGEMENT_4, 12, 0),
SND_SOC_DAPM_AIF_IN_E("AIF2DACL", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 13, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_AIF_IN_E("AIF2DACR", NULL, 0,
		      WM8994_POWER_MANAGEMENT_5, 12, 0, wm8958_aif_ev,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_AIF_IN("AIF1DACDAT", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIF2DACDAT", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIF2ADCDAT", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("AIF1DAC Mux", SND_SOC_NOPM, 0, 0, &aif1dac_mux),
SND_SOC_DAPM_MUX("AIF2DAC Mux", SND_SOC_NOPM, 0, 0, &aif2dac_mux),
SND_SOC_DAPM_MUX("AIF2ADC Mux", SND_SOC_NOPM, 0, 0, &aif2adc_mux),

SND_SOC_DAPM_AIF_IN("AIF3DACDAT", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIF3ADCDAT", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_SUPPLY("TOCLK", WM8994_CLOCKING_1, 4, 0, NULL, 0),

SND_SOC_DAPM_ADC("DMIC2L", NULL, WM8994_POWER_MANAGEMENT_4, 5, 0),
SND_SOC_DAPM_ADC("DMIC2R", NULL, WM8994_POWER_MANAGEMENT_4, 4, 0),
SND_SOC_DAPM_ADC("DMIC1L", NULL, WM8994_POWER_MANAGEMENT_4, 3, 0),
SND_SOC_DAPM_ADC("DMIC1R", NULL, WM8994_POWER_MANAGEMENT_4, 2, 0),

/* Power is done with the muxes since the ADC power also controls the
 * downsampling chain, the chip will automatically manage the analogue
 * specific portions.
 */
SND_SOC_DAPM_ADC("ADCL", NULL, SND_SOC_NOPM, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("ADCL Mux", WM8994_POWER_MANAGEMENT_4, 1, 0, &adcl_mux),
SND_SOC_DAPM_MUX("ADCR Mux", WM8994_POWER_MANAGEMENT_4, 0, 0, &adcr_mux),

SND_SOC_DAPM_DAC("DAC2L", NULL, WM8994_POWER_MANAGEMENT_5, 3, 0),
SND_SOC_DAPM_DAC("DAC2R", NULL, WM8994_POWER_MANAGEMENT_5, 2, 0),
SND_SOC_DAPM_DAC("DAC1L", NULL, WM8994_POWER_MANAGEMENT_5, 1, 0),
SND_SOC_DAPM_DAC("DAC1R", NULL, WM8994_POWER_MANAGEMENT_5, 0, 0),

SND_SOC_DAPM_MUX("Left Headphone Mux", SND_SOC_NOPM, 0, 0, &hpl_mux),
SND_SOC_DAPM_MUX("Right Headphone Mux", SND_SOC_NOPM, 0, 0, &hpr_mux),

SND_SOC_DAPM_MIXER("SPKL", WM8994_POWER_MANAGEMENT_3, 8, 0,
		   left_speaker_mixer, ARRAY_SIZE(left_speaker_mixer)),
SND_SOC_DAPM_MIXER("SPKR", WM8994_POWER_MANAGEMENT_3, 9, 0,
		   right_speaker_mixer, ARRAY_SIZE(right_speaker_mixer)),

SND_SOC_DAPM_POST("Debug log", post_ev),
};

static const struct snd_soc_dapm_widget wm8994_specific_dapm_widgets[] = {
SND_SOC_DAPM_MUX("AIF3ADC Mux", SND_SOC_NOPM, 0, 0, &wm8994_aif3adc_mux),
};

static const struct snd_soc_dapm_widget wm8958_dapm_widgets[] = {
SND_SOC_DAPM_MUX("Mono PCM Out Mux", SND_SOC_NOPM, 0, 0, &mono_pcm_out_mux),
SND_SOC_DAPM_MUX("AIF2DACL Mux", SND_SOC_NOPM, 0, 0, &aif2dacl_src_mux),
SND_SOC_DAPM_MUX("AIF2DACR Mux", SND_SOC_NOPM, 0, 0, &aif2dacr_src_mux),
SND_SOC_DAPM_MUX("AIF3ADC Mux", SND_SOC_NOPM, 0, 0, &wm8958_aif3adc_mux),
};

static const struct snd_soc_dapm_route intercon[] = {
	{ "CLK_SYS", NULL, "AIF1CLK", check_clk_sys },
	{ "CLK_SYS", NULL, "AIF2CLK", check_clk_sys },

	{ "DSP1CLK", NULL, "CLK_SYS" },
	{ "DSP2CLK", NULL, "CLK_SYS" },
	{ "DSPINTCLK", NULL, "CLK_SYS" },

	{ "AIF1ADC1L", NULL, "AIF1CLK" },
	{ "AIF1ADC1L", NULL, "DSP1CLK" },
	{ "AIF1ADC1R", NULL, "AIF1CLK" },
	{ "AIF1ADC1R", NULL, "DSP1CLK" },
	{ "AIF1ADC1R", NULL, "DSPINTCLK" },

	{ "AIF1DAC1L", NULL, "AIF1CLK" },
	{ "AIF1DAC1L", NULL, "DSP1CLK" },
	{ "AIF1DAC1R", NULL, "AIF1CLK" },
	{ "AIF1DAC1R", NULL, "DSP1CLK" },
	{ "AIF1DAC1R", NULL, "DSPINTCLK" },

	{ "AIF1ADC2L", NULL, "AIF1CLK" },
	{ "AIF1ADC2L", NULL, "DSP1CLK" },
	{ "AIF1ADC2R", NULL, "AIF1CLK" },
	{ "AIF1ADC2R", NULL, "DSP1CLK" },
	{ "AIF1ADC2R", NULL, "DSPINTCLK" },

	{ "AIF1DAC2L", NULL, "AIF1CLK" },
	{ "AIF1DAC2L", NULL, "DSP1CLK" },
	{ "AIF1DAC2R", NULL, "AIF1CLK" },
	{ "AIF1DAC2R", NULL, "DSP1CLK" },
	{ "AIF1DAC2R", NULL, "DSPINTCLK" },

	{ "AIF2ADCL", NULL, "AIF2CLK" },
	{ "AIF2ADCL", NULL, "DSP2CLK" },
	{ "AIF2ADCR", NULL, "AIF2CLK" },
	{ "AIF2ADCR", NULL, "DSP2CLK" },
	{ "AIF2ADCR", NULL, "DSPINTCLK" },

	{ "AIF2DACL", NULL, "AIF2CLK" },
	{ "AIF2DACL", NULL, "DSP2CLK" },
	{ "AIF2DACR", NULL, "AIF2CLK" },
	{ "AIF2DACR", NULL, "DSP2CLK" },
	{ "AIF2DACR", NULL, "DSPINTCLK" },

	{ "DMIC1L", NULL, "DMIC1DAT" },
	{ "DMIC1L", NULL, "CLK_SYS" },
	{ "DMIC1R", NULL, "DMIC1DAT" },
	{ "DMIC1R", NULL, "CLK_SYS" },
	{ "DMIC2L", NULL, "DMIC2DAT" },
	{ "DMIC2L", NULL, "CLK_SYS" },
	{ "DMIC2R", NULL, "DMIC2DAT" },
	{ "DMIC2R", NULL, "CLK_SYS" },

	{ "ADCL", NULL, "AIF1CLK" },
	{ "ADCL", NULL, "DSP1CLK" },
	{ "ADCL", NULL, "DSPINTCLK" },

	{ "ADCR", NULL, "AIF1CLK" },
	{ "ADCR", NULL, "DSP1CLK" },
	{ "ADCR", NULL, "DSPINTCLK" },

	{ "ADCL Mux", "ADC", "ADCL" },
	{ "ADCL Mux", "DMIC", "DMIC1L" },
	{ "ADCR Mux", "ADC", "ADCR" },
	{ "ADCR Mux", "DMIC", "DMIC1R" },

	{ "DAC1L", NULL, "AIF1CLK" },
	{ "DAC1L", NULL, "DSP1CLK" },
	{ "DAC1L", NULL, "DSPINTCLK" },

	{ "DAC1R", NULL, "AIF1CLK" },
	{ "DAC1R", NULL, "DSP1CLK" },
	{ "DAC1R", NULL, "DSPINTCLK" },

	{ "DAC2L", NULL, "AIF2CLK" },
	{ "DAC2L", NULL, "DSP2CLK" },
	{ "DAC2L", NULL, "DSPINTCLK" },

	{ "DAC2R", NULL, "AIF2DACR" },
	{ "DAC2R", NULL, "AIF2CLK" },
	{ "DAC2R", NULL, "DSP2CLK" },
	{ "DAC2R", NULL, "DSPINTCLK" },

	{ "TOCLK", NULL, "CLK_SYS" },

	/* AIF1 outputs */
	{ "AIF1ADC1L", NULL, "AIF1ADC1L Mixer" },
	{ "AIF1ADC1L Mixer", "ADC/DMIC Switch", "ADCL Mux" },
	{ "AIF1ADC1L Mixer", "AIF2 Switch", "AIF2DACL" },

	{ "AIF1ADC1R", NULL, "AIF1ADC1R Mixer" },
	{ "AIF1ADC1R Mixer", "ADC/DMIC Switch", "ADCR Mux" },
	{ "AIF1ADC1R Mixer", "AIF2 Switch", "AIF2DACR" },

	{ "AIF1ADC2L", NULL, "AIF1ADC2L Mixer" },
	{ "AIF1ADC2L Mixer", "DMIC Switch", "DMIC2L" },
	{ "AIF1ADC2L Mixer", "AIF2 Switch", "AIF2DACL" },

	{ "AIF1ADC2R", NULL, "AIF1ADC2R Mixer" },
	{ "AIF1ADC2R Mixer", "DMIC Switch", "DMIC2R" },
	{ "AIF1ADC2R Mixer", "AIF2 Switch", "AIF2DACR" },

	/* Pin level routing for AIF3 */
	{ "AIF1DAC1L", NULL, "AIF1DAC Mux" },
	{ "AIF1DAC1R", NULL, "AIF1DAC Mux" },
	{ "AIF1DAC2L", NULL, "AIF1DAC Mux" },
	{ "AIF1DAC2R", NULL, "AIF1DAC Mux" },

	{ "AIF1DAC Mux", "AIF1DACDAT", "AIF1DACDAT" },
	{ "AIF1DAC Mux", "AIF3DACDAT", "AIF3DACDAT" },
	{ "AIF2DAC Mux", "AIF2DACDAT", "AIF2DACDAT" },
	{ "AIF2DAC Mux", "AIF3DACDAT", "AIF3DACDAT" },
	{ "AIF2ADC Mux", "AIF2ADCDAT", "AIF2ADCL" },
	{ "AIF2ADC Mux", "AIF2ADCDAT", "AIF2ADCR" },
	{ "AIF2ADC Mux", "AIF3DACDAT", "AIF3ADCDAT" },

	/* DAC1 inputs */
	{ "DAC1L", NULL, "DAC1L Mixer" },
	{ "DAC1L Mixer", "AIF2 Switch", "AIF2DACL" },
	{ "DAC1L Mixer", "AIF1.2 Switch", "AIF1DAC2L" },
	{ "DAC1L Mixer", "AIF1.1 Switch", "AIF1DAC1L" },
	{ "DAC1L Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "DAC1L Mixer", "Right Sidetone Switch", "Right Sidetone" },

	{ "DAC1R", NULL, "DAC1R Mixer" },
	{ "DAC1R Mixer", "AIF2 Switch", "AIF2DACR" },
	{ "DAC1R Mixer", "AIF1.2 Switch", "AIF1DAC2R" },
	{ "DAC1R Mixer", "AIF1.1 Switch", "AIF1DAC1R" },
	{ "DAC1R Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "DAC1R Mixer", "Right Sidetone Switch", "Right Sidetone" },

	/* DAC2/AIF2 outputs  */
	{ "AIF2ADCL", NULL, "AIF2DAC2L Mixer" },
	{ "DAC2L", NULL, "AIF2DAC2L Mixer" },
	{ "AIF2DAC2L Mixer", "AIF2 Switch", "AIF2DACL" },
	{ "AIF2DAC2L Mixer", "AIF1.2 Switch", "AIF1DAC2L" },
	{ "AIF2DAC2L Mixer", "AIF1.1 Switch", "AIF1DAC1L" },
	{ "AIF2DAC2L Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "AIF2DAC2L Mixer", "Right Sidetone Switch", "Right Sidetone" },

	{ "AIF2ADCR", NULL, "AIF2DAC2R Mixer" },
	{ "DAC2R", NULL, "AIF2DAC2R Mixer" },
	{ "AIF2DAC2R Mixer", "AIF2 Switch", "AIF2DACR" },
	{ "AIF2DAC2R Mixer", "AIF1.2 Switch", "AIF1DAC2R" },
	{ "AIF2DAC2R Mixer", "AIF1.1 Switch", "AIF1DAC1R" },
	{ "AIF2DAC2R Mixer", "Left Sidetone Switch", "Left Sidetone" },
	{ "AIF2DAC2R Mixer", "Right Sidetone Switch", "Right Sidetone" },

	{ "AIF2ADCDAT", NULL, "AIF2ADC Mux" },

	/* AIF3 output */
	{ "AIF3ADCDAT", "AIF1ADCDAT", "AIF1ADC1L" },
	{ "AIF3ADCDAT", "AIF1ADCDAT", "AIF1ADC1R" },
	{ "AIF3ADCDAT", "AIF1ADCDAT", "AIF1ADC2L" },
	{ "AIF3ADCDAT", "AIF1ADCDAT", "AIF1ADC2R" },
	{ "AIF3ADCDAT", "AIF2ADCDAT", "AIF2ADCL" },
	{ "AIF3ADCDAT", "AIF2ADCDAT", "AIF2ADCR" },
	{ "AIF3ADCDAT", "AIF2DACDAT", "AIF2DACL" },
	{ "AIF3ADCDAT", "AIF2DACDAT", "AIF2DACR" },

	/* Sidetone */
	{ "Left Sidetone", "ADC/DMIC1", "ADCL Mux" },
	{ "Left Sidetone", "DMIC2", "DMIC2L" },
	{ "Right Sidetone", "ADC/DMIC1", "ADCR Mux" },
	{ "Right Sidetone", "DMIC2", "DMIC2R" },

	/* Output stages */
	{ "Left Output Mixer", "DAC Switch", "DAC1L" },
	{ "Right Output Mixer", "DAC Switch", "DAC1R" },

	{ "SPKL", "DAC1 Switch", "DAC1L" },
	{ "SPKL", "DAC2 Switch", "DAC2L" },

	{ "SPKR", "DAC1 Switch", "DAC1R" },
	{ "SPKR", "DAC2 Switch", "DAC2R" },

	{ "Left Headphone Mux", "DAC", "DAC1L" },
	{ "Right Headphone Mux", "DAC", "DAC1R" },
};

static const struct snd_soc_dapm_route wm8994_intercon[] = {
	{ "AIF2DACL", NULL, "AIF2DAC Mux" },
	{ "AIF2DACR", NULL, "AIF2DAC Mux" },
};

static const struct snd_soc_dapm_route wm8958_intercon[] = {
	{ "AIF2DACL", NULL, "AIF2DACL Mux" },
	{ "AIF2DACR", NULL, "AIF2DACR Mux" },

	{ "AIF2DACL Mux", "AIF2", "AIF2DAC Mux" },
	{ "AIF2DACL Mux", "AIF3", "AIF3DACDAT" },
	{ "AIF2DACR Mux", "AIF2", "AIF2DAC Mux" },
	{ "AIF2DACR Mux", "AIF3", "AIF3DACDAT" },

	{ "Mono PCM Out Mux", "AIF2ADCL", "AIF2ADCL" },
	{ "Mono PCM Out Mux", "AIF2ADCR", "AIF2ADCR" },

	{ "AIF3ADC Mux", "Mono PCM", "Mono PCM Out Mux" },
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

struct fll_div {
	u16 outdiv;
	u16 n;
	u16 k;
	u16 clk_ref_div;
	u16 fll_fratio;
};

static int wm8994_get_fll_config(struct fll_div *fll,
				 int freq_in, int freq_out)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;

	pr_debug("FLL input=%dHz, output=%dHz\n", freq_in, freq_out);

	/* Scale the input frequency down to <= 13.5MHz */
	fll->clk_ref_div = 0;
	while (freq_in > 13500000) {
		fll->clk_ref_div++;
		freq_in /= 2;

		if (fll->clk_ref_div > 3)
			return -EINVAL;
	}
	pr_debug("CLK_REF_DIV=%d, Fref=%dHz\n", fll->clk_ref_div, freq_in);

	/* Scale the output to give 90MHz<=Fvco<=100MHz */
	fll->outdiv = 3;
	while (freq_out * (fll->outdiv + 1) < 90000000) {
		fll->outdiv++;
		if (fll->outdiv > 63)
			return -EINVAL;
	}
	freq_out *= fll->outdiv + 1;
	pr_debug("OUTDIV=%d, Fvco=%dHz\n", fll->outdiv, freq_out);

	if (freq_in > 1000000) {
		fll->fll_fratio = 0;
	} else if (freq_in > 256000) {
		fll->fll_fratio = 1;
		freq_in *= 2;
	} else if (freq_in > 128000) {
		fll->fll_fratio = 2;
		freq_in *= 4;
	} else if (freq_in > 64000) {
		fll->fll_fratio = 3;
		freq_in *= 8;
	} else {
		fll->fll_fratio = 4;
		freq_in *= 16;
	}
	pr_debug("FLL_FRATIO=%d, Fref=%dHz\n", fll->fll_fratio, freq_in);

	/* Now, calculate N.K */
	Ndiv = freq_out / freq_in;

	fll->n = Ndiv;
	Nmod = freq_out % freq_in;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, freq_in);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll->k = K / 10;

	pr_debug("N=%x K=%x\n", fll->n, fll->k);

	return 0;
}

static int _wm8994_set_fll(struct snd_soc_codec *codec, int id, int src,
			  unsigned int freq_in, unsigned int freq_out)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int reg_offset, ret;
	struct fll_div fll;
	u16 reg, aif1, aif2;

	aif1 = snd_soc_read(codec, WM8994_AIF1_CLOCKING_1)
		& WM8994_AIF1CLK_ENA;

	aif2 = snd_soc_read(codec, WM8994_AIF2_CLOCKING_1)
		& WM8994_AIF2CLK_ENA;

	switch (id) {
	case WM8994_FLL1:
		reg_offset = 0;
		id = 0;
		break;
	case WM8994_FLL2:
		reg_offset = 0x20;
		id = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (src) {
	case 0:
		/* Allow no source specification when stopping */
		if (freq_out)
			return -EINVAL;
		break;
	case WM8994_FLL_SRC_MCLK1:
	case WM8994_FLL_SRC_MCLK2:
	case WM8994_FLL_SRC_LRCLK:
	case WM8994_FLL_SRC_BCLK:
		break;
	default:
		return -EINVAL;
	}

	/* Are we changing anything? */
	if (wm8994->fll[id].src == src &&
	    wm8994->fll[id].in == freq_in && wm8994->fll[id].out == freq_out)
		return 0;

	/* If we're stopping the FLL redo the old config - no
	 * registers will actually be written but we avoid GCC flow
	 * analysis bugs spewing warnings.
	 */
	if (freq_out)
		ret = wm8994_get_fll_config(&fll, freq_in, freq_out);
	else
		ret = wm8994_get_fll_config(&fll, wm8994->fll[id].in,
					    wm8994->fll[id].out);
	if (ret < 0)
		return ret;

	/* Gate the AIF clocks while we reclock */
	snd_soc_update_bits(codec, WM8994_AIF1_CLOCKING_1,
			    WM8994_AIF1CLK_ENA, 0);
	snd_soc_update_bits(codec, WM8994_AIF2_CLOCKING_1,
			    WM8994_AIF2CLK_ENA, 0);

	/* We always need to disable the FLL while reconfiguring */
	snd_soc_update_bits(codec, WM8994_FLL1_CONTROL_1 + reg_offset,
			    WM8994_FLL1_ENA, 0);

	reg = (fll.outdiv << WM8994_FLL1_OUTDIV_SHIFT) |
		(fll.fll_fratio << WM8994_FLL1_FRATIO_SHIFT);
	snd_soc_update_bits(codec, WM8994_FLL1_CONTROL_2 + reg_offset,
			    WM8994_FLL1_OUTDIV_MASK |
			    WM8994_FLL1_FRATIO_MASK, reg);

	snd_soc_write(codec, WM8994_FLL1_CONTROL_3 + reg_offset, fll.k);

	snd_soc_update_bits(codec, WM8994_FLL1_CONTROL_4 + reg_offset,
			    WM8994_FLL1_N_MASK,
				    fll.n << WM8994_FLL1_N_SHIFT);

	snd_soc_update_bits(codec, WM8994_FLL1_CONTROL_5 + reg_offset,
			    WM8994_FLL1_REFCLK_DIV_MASK |
			    WM8994_FLL1_REFCLK_SRC_MASK,
			    (fll.clk_ref_div << WM8994_FLL1_REFCLK_DIV_SHIFT) |
			    (src - 1));

	/* Enable (with fractional mode if required) */
	if (freq_out) {
		if (fll.k)
			reg = WM8994_FLL1_ENA | WM8994_FLL1_FRAC;
		else
			reg = WM8994_FLL1_ENA;
		snd_soc_update_bits(codec, WM8994_FLL1_CONTROL_1 + reg_offset,
				    WM8994_FLL1_ENA | WM8994_FLL1_FRAC,
				    reg);
	}

	wm8994->fll[id].in = freq_in;
	wm8994->fll[id].out = freq_out;
	wm8994->fll[id].src = src;

	/* Enable any gated AIF clocks */
	snd_soc_update_bits(codec, WM8994_AIF1_CLOCKING_1,
			    WM8994_AIF1CLK_ENA, aif1);
	snd_soc_update_bits(codec, WM8994_AIF2_CLOCKING_1,
			    WM8994_AIF2CLK_ENA, aif2);

	configure_clock(codec);

	return 0;
}


static int opclk_divs[] = { 10, 20, 30, 40, 55, 60, 80, 120, 160 };

static int wm8994_set_fll(struct snd_soc_dai *dai, int id, int src,
			  unsigned int freq_in, unsigned int freq_out)
{
	return _wm8994_set_fll(dai->codec, id, src, freq_in, freq_out);
}

static int wm8994_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int i;

	switch (dai->id) {
	case 1:
	case 2:
		break;

	default:
		/* AIF3 shares clocking with AIF1/2 */
		return -EINVAL;
	}

	switch (clk_id) {
	case WM8994_SYSCLK_MCLK1:
		wm8994->sysclk[dai->id - 1] = WM8994_SYSCLK_MCLK1;
		wm8994->mclk[0] = freq;
		dev_dbg(dai->dev, "AIF%d using MCLK1 at %uHz\n",
			dai->id, freq);
		break;

	case WM8994_SYSCLK_MCLK2:
		/* TODO: Set GPIO AF */
		wm8994->sysclk[dai->id - 1] = WM8994_SYSCLK_MCLK2;
		wm8994->mclk[1] = freq;
		dev_dbg(dai->dev, "AIF%d using MCLK2 at %uHz\n",
			dai->id, freq);
		break;

	case WM8994_SYSCLK_FLL1:
		wm8994->sysclk[dai->id - 1] = WM8994_SYSCLK_FLL1;
		dev_dbg(dai->dev, "AIF%d using FLL1\n", dai->id);
		break;

	case WM8994_SYSCLK_FLL2:
		wm8994->sysclk[dai->id - 1] = WM8994_SYSCLK_FLL2;
		dev_dbg(dai->dev, "AIF%d using FLL2\n", dai->id);
		break;

	case WM8994_SYSCLK_OPCLK:
		/* Special case - a division (times 10) is given and
		 * no effect on main clocking. 
		 */
		if (freq) {
			for (i = 0; i < ARRAY_SIZE(opclk_divs); i++)
				if (opclk_divs[i] == freq)
					break;
			if (i == ARRAY_SIZE(opclk_divs))
				return -EINVAL;
			snd_soc_update_bits(codec, WM8994_CLOCKING_2,
					    WM8994_OPCLK_DIV_MASK, i);
			snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_2,
					    WM8994_OPCLK_ENA, WM8994_OPCLK_ENA);
		} else {
			snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_2,
					    WM8994_OPCLK_ENA, 0);
		}

	default:
		return -EINVAL;
	}

	configure_clock(codec);

	return 0;
}

static int wm8994_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8994 *control = codec->control_data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID=2x40k */
		snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
				    WM8994_VMID_SEL_MASK, 0x2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			pm_runtime_get_sync(codec->dev);

			/* Tweak DC servo and DSP configuration for
			 * improved performance. */
			if (control->type == WM8994 && wm8994->revision < 4) {
				/* Tweak DC servo and DSP configuration for
				 * improved performance. */
				snd_soc_write(codec, 0x102, 0x3);
				snd_soc_write(codec, 0x56, 0x3);
				snd_soc_write(codec, 0x817, 0);
				snd_soc_write(codec, 0x102, 0);
			}

			/* Discharge LINEOUT1 & 2 */
			snd_soc_update_bits(codec, WM8994_ANTIPOP_1,
					    WM8994_LINEOUT1_DISCH |
					    WM8994_LINEOUT2_DISCH,
					    WM8994_LINEOUT1_DISCH |
					    WM8994_LINEOUT2_DISCH);

			/* Startup bias, VMID ramp & buffer */
			snd_soc_update_bits(codec, WM8994_ANTIPOP_2,
					    WM8994_STARTUP_BIAS_ENA |
					    WM8994_VMID_BUF_ENA |
					    WM8994_VMID_RAMP_MASK,
					    WM8994_STARTUP_BIAS_ENA |
					    WM8994_VMID_BUF_ENA |
					    (0x11 << WM8994_VMID_RAMP_SHIFT));

			/* Main bias enable, VMID=2x40k */
			snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
					    WM8994_BIAS_ENA |
					    WM8994_VMID_SEL_MASK,
					    WM8994_BIAS_ENA | 0x2);

			msleep(20);
		}

		/* VMID=2x500k */
		snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
				    WM8994_VMID_SEL_MASK, 0x4);

		break;

	case SND_SOC_BIAS_OFF:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
			/* Switch over to startup biases */
			snd_soc_update_bits(codec, WM8994_ANTIPOP_2,
					    WM8994_BIAS_SRC |
					    WM8994_STARTUP_BIAS_ENA |
					    WM8994_VMID_BUF_ENA |
					    WM8994_VMID_RAMP_MASK,
					    WM8994_BIAS_SRC |
					    WM8994_STARTUP_BIAS_ENA |
					    WM8994_VMID_BUF_ENA |
					    (1 << WM8994_VMID_RAMP_SHIFT));

			/* Disable main biases */
			snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
					    WM8994_BIAS_ENA |
					    WM8994_VMID_SEL_MASK, 0);

			/* Discharge line */
			snd_soc_update_bits(codec, WM8994_ANTIPOP_1,
					    WM8994_LINEOUT1_DISCH |
					    WM8994_LINEOUT2_DISCH,
					    WM8994_LINEOUT1_DISCH |
					    WM8994_LINEOUT2_DISCH);

			msleep(5);

			/* Switch off startup biases */
			snd_soc_update_bits(codec, WM8994_ANTIPOP_2,
					    WM8994_BIAS_SRC |
					    WM8994_STARTUP_BIAS_ENA |
					    WM8994_VMID_BUF_ENA |
					    WM8994_VMID_RAMP_MASK, 0);

			pm_runtime_put(codec->dev);
		}
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int wm8994_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994 *control = codec->control_data;
	int ms_reg;
	int aif1_reg;
	int ms = 0;
	int aif1 = 0;

	switch (dai->id) {
	case 1:
		ms_reg = WM8994_AIF1_MASTER_SLAVE;
		aif1_reg = WM8994_AIF1_CONTROL_1;
		break;
	case 2:
		ms_reg = WM8994_AIF2_MASTER_SLAVE;
		aif1_reg = WM8994_AIF2_CONTROL_1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		ms = WM8994_AIF1_MSTR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= WM8994_AIF1_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x18;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x10;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif1 |= 0x8;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8994_AIF1_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8994_AIF1_BCLK_INV | WM8994_AIF1_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8994_AIF1_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8994_AIF1_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	/* The AIF2 format configuration needs to be mirrored to AIF3
	 * on WM8958 if it's in use so just do it all the time. */
	if (control->type == WM8958 && dai->id == 2)
		snd_soc_update_bits(codec, WM8958_AIF3_CONTROL_1,
				    WM8994_AIF1_LRCLK_INV |
				    WM8958_AIF3_FMT_MASK, aif1);

	snd_soc_update_bits(codec, aif1_reg,
			    WM8994_AIF1_BCLK_INV | WM8994_AIF1_LRCLK_INV |
			    WM8994_AIF1_FMT_MASK,
			    aif1);
	snd_soc_update_bits(codec, ms_reg, WM8994_AIF1_MSTR,
			    ms);

	return 0;
}

static struct {
	int val, rate;
} srs[] = {
	{ 0,   8000 },
	{ 1,  11025 },
	{ 2,  12000 },
	{ 3,  16000 },
	{ 4,  22050 },
	{ 5,  24000 },
	{ 6,  32000 },
	{ 7,  44100 },
	{ 8,  48000 },
	{ 9,  88200 },
	{ 10, 96000 },
};

static int fs_ratios[] = {
	64, 128, 192, 256, 348, 512, 768, 1024, 1408, 1536
};

static int bclk_divs[] = {
	10, 15, 20, 30, 40, 50, 60, 80, 110, 120, 160, 220, 240, 320, 440, 480,
	640, 880, 960, 1280, 1760, 1920
};

static int wm8994_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994 *control = codec->control_data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int aif1_reg;
	int bclk_reg;
	int lrclk_reg;
	int rate_reg;
	int aif1 = 0;
	int bclk = 0;
	int lrclk = 0;
	int rate_val = 0;
	int id = dai->id - 1;

	int i, cur_val, best_val, bclk_rate, best;

	switch (dai->id) {
	case 1:
		aif1_reg = WM8994_AIF1_CONTROL_1;
		bclk_reg = WM8994_AIF1_BCLK;
		rate_reg = WM8994_AIF1_RATE;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
		    wm8994->lrclk_shared[0]) {
			lrclk_reg = WM8994_AIF1DAC_LRCLK;
		} else {
			lrclk_reg = WM8994_AIF1ADC_LRCLK;
			dev_dbg(codec->dev, "AIF1 using split LRCLK\n");
		}
		break;
	case 2:
		aif1_reg = WM8994_AIF2_CONTROL_1;
		bclk_reg = WM8994_AIF2_BCLK;
		rate_reg = WM8994_AIF2_RATE;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
		    wm8994->lrclk_shared[1]) {
			lrclk_reg = WM8994_AIF2DAC_LRCLK;
		} else {
			lrclk_reg = WM8994_AIF2ADC_LRCLK;
			dev_dbg(codec->dev, "AIF2 using split LRCLK\n");
		}
		break;
	case 3:
		switch (control->type) {
		case WM8958:
			aif1_reg = WM8958_AIF3_CONTROL_1;
			break;
		default:
			return 0;
		}
	default:
		return -EINVAL;
	}

	bclk_rate = params_rate(params) * 2;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		bclk_rate *= 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		bclk_rate *= 20;
		aif1 |= 0x20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bclk_rate *= 24;
		aif1 |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bclk_rate *= 32;
		aif1 |= 0x60;
		break;
	default:
		return -EINVAL;
	}

	/* Try to find an appropriate sample rate; look for an exact match. */
	for (i = 0; i < ARRAY_SIZE(srs); i++)
		if (srs[i].rate == params_rate(params))
			break;
	if (i == ARRAY_SIZE(srs))
		return -EINVAL;
	rate_val |= srs[i].val << WM8994_AIF1_SR_SHIFT;

	dev_dbg(dai->dev, "Sample rate is %dHz\n", srs[i].rate);
	dev_dbg(dai->dev, "AIF%dCLK is %dHz, target BCLK %dHz\n",
		dai->id, wm8994->aifclk[id], bclk_rate);

	if (wm8994->aifclk[id] == 0) {
		dev_err(dai->dev, "AIF%dCLK not configured\n", dai->id);
		return -EINVAL;
	}

	/* AIFCLK/fs ratio; look for a close match in either direction */
	best = 0;
	best_val = abs((fs_ratios[0] * params_rate(params))
		       - wm8994->aifclk[id]);
	for (i = 1; i < ARRAY_SIZE(fs_ratios); i++) {
		cur_val = abs((fs_ratios[i] * params_rate(params))
			      - wm8994->aifclk[id]);
		if (cur_val >= best_val)
			continue;
		best = i;
		best_val = cur_val;
	}
	dev_dbg(dai->dev, "Selected AIF%dCLK/fs = %d\n",
		dai->id, fs_ratios[best]);
	rate_val |= best;

	/* We may not get quite the right frequency if using
	 * approximate clocks so look for the closest match that is
	 * higher than the target (we need to ensure that there enough
	 * BCLKs to clock out the samples).
	 */
	best = 0;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = (wm8994->aifclk[id] * 10 / bclk_divs[i]) - bclk_rate;
		if (cur_val < 0) /* BCLK table is sorted */
			break;
		best = i;
	}
	bclk_rate = wm8994->aifclk[id] * 10 / bclk_divs[best];
	dev_dbg(dai->dev, "Using BCLK_DIV %d for actual BCLK %dHz\n",
		bclk_divs[best], bclk_rate);
	bclk |= best << WM8994_AIF1_BCLK_DIV_SHIFT;

	lrclk = bclk_rate / params_rate(params);
	dev_dbg(dai->dev, "Using LRCLK rate %d for actual LRCLK %dHz\n",
		lrclk, bclk_rate / lrclk);

	snd_soc_update_bits(codec, aif1_reg, WM8994_AIF1_WL_MASK, aif1);
	snd_soc_update_bits(codec, bclk_reg, WM8994_AIF1_BCLK_DIV_MASK, bclk);
	snd_soc_update_bits(codec, lrclk_reg, WM8994_AIF1DAC_RATE_MASK,
			    lrclk);
	snd_soc_update_bits(codec, rate_reg, WM8994_AIF1_SR_MASK |
			    WM8994_AIF1CLK_RATE_MASK, rate_val);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		switch (dai->id) {
		case 1:
			wm8994->dac_rates[0] = params_rate(params);
			wm8994_set_retune_mobile(codec, 0);
			wm8994_set_retune_mobile(codec, 1);
			break;
		case 2:
			wm8994->dac_rates[1] = params_rate(params);
			wm8994_set_retune_mobile(codec, 2);
			break;
		}
	}

	return 0;
}

static int wm8994_aif3_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8994 *control = codec->control_data;
	int aif1_reg;
	int aif1 = 0;

	switch (dai->id) {
	case 3:
		switch (control->type) {
		case WM8958:
			aif1_reg = WM8958_AIF3_CONTROL_1;
			break;
		default:
			return 0;
		}
	default:
		return 0;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		aif1 |= 0x20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		aif1 |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		aif1 |= 0x60;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_update_bits(codec, aif1_reg, WM8994_AIF1_WL_MASK, aif1);
}

static int wm8994_aif_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int mute_reg;
	int reg;

	switch (codec_dai->id) {
	case 1:
		mute_reg = WM8994_AIF1_DAC1_FILTERS_1;
		break;
	case 2:
		mute_reg = WM8994_AIF2_DAC_FILTERS_1;
		break;
	default:
		return -EINVAL;
	}

	if (mute)
		reg = WM8994_AIF1DAC1_MUTE;
	else
		reg = 0;

	snd_soc_update_bits(codec, mute_reg, WM8994_AIF1DAC1_MUTE, reg);

	return 0;
}

static int wm8994_set_tristate(struct snd_soc_dai *codec_dai, int tristate)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int reg, val, mask;

	switch (codec_dai->id) {
	case 1:
		reg = WM8994_AIF1_MASTER_SLAVE;
		mask = WM8994_AIF1_TRI;
		break;
	case 2:
		reg = WM8994_AIF2_MASTER_SLAVE;
		mask = WM8994_AIF2_TRI;
		break;
	case 3:
		reg = WM8994_POWER_MANAGEMENT_6;
		mask = WM8994_AIF3_TRI;
		break;
	default:
		return -EINVAL;
	}

	if (tristate)
		val = mask;
	else
		val = 0;

	return snd_soc_update_bits(codec, reg, mask, reg);
}

#define WM8994_RATES SNDRV_PCM_RATE_8000_96000

#define WM8994_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8994_aif1_dai_ops = {
	.set_sysclk	= wm8994_set_dai_sysclk,
	.set_fmt	= wm8994_set_dai_fmt,
	.hw_params	= wm8994_hw_params,
	.digital_mute	= wm8994_aif_mute,
	.set_pll	= wm8994_set_fll,
	.set_tristate	= wm8994_set_tristate,
};

static struct snd_soc_dai_ops wm8994_aif2_dai_ops = {
	.set_sysclk	= wm8994_set_dai_sysclk,
	.set_fmt	= wm8994_set_dai_fmt,
	.hw_params	= wm8994_hw_params,
	.digital_mute   = wm8994_aif_mute,
	.set_pll	= wm8994_set_fll,
	.set_tristate	= wm8994_set_tristate,
};

static struct snd_soc_dai_ops wm8994_aif3_dai_ops = {
	.hw_params	= wm8994_aif3_hw_params,
	.set_tristate	= wm8994_set_tristate,
};

static struct snd_soc_dai_driver wm8994_dai[] = {
	{
		.name = "wm8994-aif1",
		.id = 1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		 },
		.ops = &wm8994_aif1_dai_ops,
	},
	{
		.name = "wm8994-aif2",
		.id = 2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		},
		.ops = &wm8994_aif2_dai_ops,
	},
	{
		.name = "wm8994-aif3",
		.id = 3,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = WM8994_RATES,
			.formats = WM8994_FORMATS,
		},
		.ops = &wm8994_aif3_dai_ops,
	}
};

#ifdef CONFIG_PM
static int wm8994_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(wm8994->fll); i++) {
		memcpy(&wm8994->fll_suspend[i], &wm8994->fll[i],
		       sizeof(struct fll_config));
		ret = _wm8994_set_fll(codec, i + 1, 0, 0, 0);
		if (ret < 0)
			dev_warn(codec->dev, "Failed to stop FLL%d: %d\n",
				 i + 1, ret);
	}

	wm8994_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8994_resume(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int i, ret;

	/* Restore the registers */
	ret = snd_soc_cache_sync(codec);
	if (ret != 0)
		dev_err(codec->dev, "Failed to sync cache: %d\n", ret);

	wm8994_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	for (i = 0; i < ARRAY_SIZE(wm8994->fll); i++) {
		if (!wm8994->fll_suspend[i].out)
			continue;

		ret = _wm8994_set_fll(codec, i + 1,
				     wm8994->fll_suspend[i].src,
				     wm8994->fll_suspend[i].in,
				     wm8994->fll_suspend[i].out);
		if (ret < 0)
			dev_warn(codec->dev, "Failed to restore FLL%d: %d\n",
				 i + 1, ret);
	}

	return 0;
}
#else
#define wm8994_suspend NULL
#define wm8994_resume NULL
#endif

static void wm8994_handle_retune_mobile_pdata(struct wm8994_priv *wm8994)
{
	struct snd_soc_codec *codec = wm8994->codec;
	struct wm8994_pdata *pdata = wm8994->pdata;
	struct snd_kcontrol_new controls[] = {
		SOC_ENUM_EXT("AIF1.1 EQ Mode",
			     wm8994->retune_mobile_enum,
			     wm8994_get_retune_mobile_enum,
			     wm8994_put_retune_mobile_enum),
		SOC_ENUM_EXT("AIF1.2 EQ Mode",
			     wm8994->retune_mobile_enum,
			     wm8994_get_retune_mobile_enum,
			     wm8994_put_retune_mobile_enum),
		SOC_ENUM_EXT("AIF2 EQ Mode",
			     wm8994->retune_mobile_enum,
			     wm8994_get_retune_mobile_enum,
			     wm8994_put_retune_mobile_enum),
	};
	int ret, i, j;
	const char **t;

	/* We need an array of texts for the enum API but the number
	 * of texts is likely to be less than the number of
	 * configurations due to the sample rate dependency of the
	 * configurations. */
	wm8994->num_retune_mobile_texts = 0;
	wm8994->retune_mobile_texts = NULL;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		for (j = 0; j < wm8994->num_retune_mobile_texts; j++) {
			if (strcmp(pdata->retune_mobile_cfgs[i].name,
				   wm8994->retune_mobile_texts[j]) == 0)
				break;
		}

		if (j != wm8994->num_retune_mobile_texts)
			continue;

		/* Expand the array... */
		t = krealloc(wm8994->retune_mobile_texts,
			     sizeof(char *) * 
			     (wm8994->num_retune_mobile_texts + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* ...store the new entry... */
		t[wm8994->num_retune_mobile_texts] = 
			pdata->retune_mobile_cfgs[i].name;

		/* ...and remember the new version. */
		wm8994->num_retune_mobile_texts++;
		wm8994->retune_mobile_texts = t;
	}

	dev_dbg(codec->dev, "Allocated %d unique ReTune Mobile names\n",
		wm8994->num_retune_mobile_texts);

	wm8994->retune_mobile_enum.max = wm8994->num_retune_mobile_texts;
	wm8994->retune_mobile_enum.texts = wm8994->retune_mobile_texts;

	ret = snd_soc_add_controls(wm8994->codec, controls,
				   ARRAY_SIZE(controls));
	if (ret != 0)
		dev_err(wm8994->codec->dev,
			"Failed to add ReTune Mobile controls: %d\n", ret);
}

static void wm8994_handle_pdata(struct wm8994_priv *wm8994)
{
	struct snd_soc_codec *codec = wm8994->codec;
	struct wm8994_pdata *pdata = wm8994->pdata;
	int ret, i;

	if (!pdata)
		return;

	wm_hubs_handle_analogue_pdata(codec, pdata->lineout1_diff,
				      pdata->lineout2_diff,
				      pdata->lineout1fb,
				      pdata->lineout2fb,
				      pdata->jd_scthr,
				      pdata->jd_thr,
				      pdata->micbias1_lvl,
				      pdata->micbias2_lvl);

	dev_dbg(codec->dev, "%d DRC configurations\n", pdata->num_drc_cfgs);

	if (pdata->num_drc_cfgs) {
		struct snd_kcontrol_new controls[] = {
			SOC_ENUM_EXT("AIF1DRC1 Mode", wm8994->drc_enum,
				     wm8994_get_drc_enum, wm8994_put_drc_enum),
			SOC_ENUM_EXT("AIF1DRC2 Mode", wm8994->drc_enum,
				     wm8994_get_drc_enum, wm8994_put_drc_enum),
			SOC_ENUM_EXT("AIF2DRC Mode", wm8994->drc_enum,
				     wm8994_get_drc_enum, wm8994_put_drc_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->drc_texts = kmalloc(sizeof(char *)
					    * pdata->num_drc_cfgs, GFP_KERNEL);
		if (!wm8994->drc_texts) {
			dev_err(wm8994->codec->dev,
				"Failed to allocate %d DRC config texts\n",
				pdata->num_drc_cfgs);
			return;
		}

		for (i = 0; i < pdata->num_drc_cfgs; i++)
			wm8994->drc_texts[i] = pdata->drc_cfgs[i].name;

		wm8994->drc_enum.max = pdata->num_drc_cfgs;
		wm8994->drc_enum.texts = wm8994->drc_texts;

		ret = snd_soc_add_controls(wm8994->codec, controls,
					   ARRAY_SIZE(controls));
		if (ret != 0)
			dev_err(wm8994->codec->dev,
				"Failed to add DRC mode controls: %d\n", ret);

		for (i = 0; i < WM8994_NUM_DRC; i++)
			wm8994_set_drc(codec, i);
	}

	dev_dbg(codec->dev, "%d ReTune Mobile configurations\n",
		pdata->num_retune_mobile_cfgs);

	if (pdata->num_mbc_cfgs) {
		struct snd_kcontrol_new control[] = {
			SOC_ENUM_EXT("MBC Mode", wm8994->mbc_enum,
				     wm8958_get_mbc_enum, wm8958_put_mbc_enum),
		};

		/* We need an array of texts for the enum API */
		wm8994->mbc_texts = kmalloc(sizeof(char *)
					    * pdata->num_mbc_cfgs, GFP_KERNEL);
		if (!wm8994->mbc_texts) {
			dev_err(wm8994->codec->dev,
				"Failed to allocate %d MBC config texts\n",
				pdata->num_mbc_cfgs);
			return;
		}

		for (i = 0; i < pdata->num_mbc_cfgs; i++)
			wm8994->mbc_texts[i] = pdata->mbc_cfgs[i].name;

		wm8994->mbc_enum.max = pdata->num_mbc_cfgs;
		wm8994->mbc_enum.texts = wm8994->mbc_texts;

		ret = snd_soc_add_controls(wm8994->codec, control, 1);
		if (ret != 0)
			dev_err(wm8994->codec->dev,
				"Failed to add MBC mode controls: %d\n", ret);
	}

	if (pdata->num_retune_mobile_cfgs)
		wm8994_handle_retune_mobile_pdata(wm8994);
	else
		snd_soc_add_controls(wm8994->codec, wm8994_eq_controls,
				     ARRAY_SIZE(wm8994_eq_controls));
}

/**
 * wm8994_mic_detect - Enable microphone detection via the WM8994 IRQ
 *
 * @codec:   WM8994 codec
 * @jack:    jack to report detection events on
 * @micbias: microphone bias to detect on
 * @det:     value to report for presence detection
 * @shrt:    value to report for short detection
 *
 * Enable microphone detection via IRQ on the WM8994.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8994 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * Configuration of detection levels is available via the micbias1_lvl
 * and micbias2_lvl platform data members.
 */
int wm8994_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      int micbias, int det, int shrt)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_micdet *micdet;
	struct wm8994 *control = codec->control_data;
	int reg;

	if (control->type != WM8994)
		return -EINVAL;

	switch (micbias) {
	case 1:
		micdet = &wm8994->micdet[0];
		break;
	case 2:
		micdet = &wm8994->micdet[1];
		break;
	default:
		return -EINVAL;
	}	

	dev_dbg(codec->dev, "Configuring microphone detection on %d: %x %x\n",
		micbias, det, shrt);

	/* Store the configuration */
	micdet->jack = jack;
	micdet->det = det;
	micdet->shrt = shrt;

	/* If either of the jacks is set up then enable detection */
	if (wm8994->micdet[0].jack || wm8994->micdet[1].jack)
		reg = WM8994_MICD_ENA;
	else 
		reg = 0;

	snd_soc_update_bits(codec, WM8994_MICBIAS, WM8994_MICD_ENA, reg);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8994_mic_detect);

static irqreturn_t wm8994_mic_irq(int irq, void *data)
{
	struct wm8994_priv *priv = data;
	struct snd_soc_codec *codec = priv->codec;
	int reg;
	int report;

	reg = snd_soc_read(codec, WM8994_INTERRUPT_RAW_STATUS_2);
	if (reg < 0) {
		dev_err(codec->dev, "Failed to read microphone status: %d\n",
			reg);
		return IRQ_HANDLED;
	}

	dev_dbg(codec->dev, "Microphone status: %x\n", reg);

	report = 0;
	if (reg & WM8994_MIC1_DET_STS)
		report |= priv->micdet[0].det;
	if (reg & WM8994_MIC1_SHRT_STS)
		report |= priv->micdet[0].shrt;
	snd_soc_jack_report(priv->micdet[0].jack, report,
			    priv->micdet[0].det | priv->micdet[0].shrt);

	report = 0;
	if (reg & WM8994_MIC2_DET_STS)
		report |= priv->micdet[1].det;
	if (reg & WM8994_MIC2_SHRT_STS)
		report |= priv->micdet[1].shrt;
	snd_soc_jack_report(priv->micdet[1].jack, report,
			    priv->micdet[1].det | priv->micdet[1].shrt);

	return IRQ_HANDLED;
}

/* Default microphone detection handler for WM8958 - the user can
 * override this if they wish.
 */
static void wm8958_default_micdet(u16 status, void *data)
{
	struct snd_soc_codec *codec = data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int report = 0;

	/* If nothing present then clear our statuses */
	if (!(status & WM8958_MICD_STS)) {
		wm8994->jack_is_video = false;
		wm8994->jack_is_mic = false;
		goto done;
	}

	/* Assume anything over 475 ohms is a microphone and remember
	 * that we've seen one (since buttons override it) */
	if (status & 0x600)
		wm8994->jack_is_mic = true;
	if (wm8994->jack_is_mic)
		report |= SND_JACK_MICROPHONE;

	/* Video has an impedence of approximately 75 ohms; assume
	 * this isn't used as a button and remember it since buttons
	 * override it. */
	if (status & 0x40)
		wm8994->jack_is_video = true;
	if (wm8994->jack_is_video)
		report |= SND_JACK_VIDEOOUT;

	/* Everything else is buttons; just assign slots */
	if (status & 0x4)
		report |= SND_JACK_BTN_0;
	if (status & 0x8)
		report |= SND_JACK_BTN_1;
	if (status & 0x10)
		report |= SND_JACK_BTN_2;
	if (status & 0x20)
		report |= SND_JACK_BTN_3;
	if (status & 0x80)
		report |= SND_JACK_BTN_4;
	if (status & 0x100)
		report |= SND_JACK_BTN_5;

done:
	snd_soc_jack_report(wm8994->micdet[0].jack,
			    SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2 |
			    SND_JACK_BTN_3 | SND_JACK_BTN_4 | SND_JACK_BTN_5 |
			    SND_JACK_MICROPHONE | SND_JACK_VIDEOOUT,
			    report);
}

/**
 * wm8958_mic_detect - Enable microphone detection via the WM8958 IRQ
 *
 * @codec:   WM8958 codec
 * @jack:    jack to report detection events on
 *
 * Enable microphone detection functionality for the WM8958.  By
 * default simple detection which supports the detection of up to 6
 * buttons plus video and microphone functionality is supported.
 *
 * The WM8958 has an advanced jack detection facility which is able to
 * support complex accessory detection, especially when used in
 * conjunction with external circuitry.  In order to provide maximum
 * flexiblity a callback is provided which allows a completely custom
 * detection algorithm.
 */
int wm8958_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      wm8958_micdet_cb cb, void *cb_data)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994 *control = codec->control_data;

	if (control->type != WM8958)
		return -EINVAL;

	if (jack) {
		if (!cb) {
			dev_dbg(codec->dev, "Using default micdet callback\n");
			cb = wm8958_default_micdet;
			cb_data = codec;
		}

		wm8994->micdet[0].jack = jack;
		wm8994->jack_cb = cb;
		wm8994->jack_cb_data = cb_data;

		snd_soc_update_bits(codec, WM8958_MIC_DETECT_1,
				    WM8958_MICD_ENA, WM8958_MICD_ENA);
	} else {
		snd_soc_update_bits(codec, WM8958_MIC_DETECT_1,
				    WM8958_MICD_ENA, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm8958_mic_detect);

static irqreturn_t wm8958_mic_irq(int irq, void *data)
{
	struct wm8994_priv *wm8994 = data;
	struct snd_soc_codec *codec = wm8994->codec;
	int reg;

	reg = snd_soc_read(codec, WM8958_MIC_DETECT_3);
	if (reg < 0) {
		dev_err(codec->dev, "Failed to read mic detect status: %d\n",
			reg);
		return IRQ_NONE;
	}

	if (!(reg & WM8958_MICD_VALID)) {
		dev_dbg(codec->dev, "Mic detect data not valid\n");
		goto out;
	}

	if (wm8994->jack_cb)
		wm8994->jack_cb(reg, wm8994->jack_cb_data);
	else
		dev_warn(codec->dev, "Accessory detection with no callback\n");

out:
	return IRQ_HANDLED;
}

static int wm8994_codec_probe(struct snd_soc_codec *codec)
{
	struct wm8994 *control;
	struct wm8994_priv *wm8994;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret, i;

	codec->control_data = dev_get_drvdata(codec->dev->parent);
	control = codec->control_data;

	wm8994 = kzalloc(sizeof(struct wm8994_priv), GFP_KERNEL);
	if (wm8994 == NULL)
		return -ENOMEM;
	snd_soc_codec_set_drvdata(codec, wm8994);

	wm8994->pdata = dev_get_platdata(codec->dev->parent);
	wm8994->codec = codec;

	pm_runtime_enable(codec->dev);
	pm_runtime_resume(codec->dev);

	/* Read our current status back from the chip - we don't want to
	 * reset as this may interfere with the GPIO or LDO operation. */
	for (i = 0; i < WM8994_CACHE_SIZE; i++) {
		if (!wm8994_readable(i) || wm8994_volatile(i))
			continue;

		ret = wm8994_reg_read(codec->control_data, i);
		if (ret <= 0)
			continue;

		ret = snd_soc_cache_write(codec, i, ret);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to initialise cache for 0x%x: %d\n",
				i, ret);
			goto err;
		}
	}

	/* Set revision-specific configuration */
	wm8994->revision = snd_soc_read(codec, WM8994_CHIP_REVISION);
	switch (control->type) {
	case WM8994:
		switch (wm8994->revision) {
		case 2:
		case 3:
			wm8994->hubs.dcs_codes = -5;
			wm8994->hubs.hp_startup_mode = 1;
			wm8994->hubs.dcs_readback_mode = 1;
			break;
		default:
			wm8994->hubs.dcs_readback_mode = 1;
			break;
		}

	case WM8958:
		wm8994->hubs.dcs_readback_mode = 1;
		break;

	default:
		break;
	}

	switch (control->type) {
	case WM8994:
		ret = wm8994_request_irq(codec->control_data,
					 WM8994_IRQ_MIC1_DET,
					 wm8994_mic_irq, "Mic 1 detect",
					 wm8994);
		if (ret != 0)
			dev_warn(codec->dev,
				 "Failed to request Mic1 detect IRQ: %d\n",
				 ret);

		ret = wm8994_request_irq(codec->control_data,
					 WM8994_IRQ_MIC1_SHRT,
					 wm8994_mic_irq, "Mic 1 short",
					 wm8994);
		if (ret != 0)
			dev_warn(codec->dev,
				 "Failed to request Mic1 short IRQ: %d\n",
				 ret);

		ret = wm8994_request_irq(codec->control_data,
					 WM8994_IRQ_MIC2_DET,
					 wm8994_mic_irq, "Mic 2 detect",
					 wm8994);
		if (ret != 0)
			dev_warn(codec->dev,
				 "Failed to request Mic2 detect IRQ: %d\n",
				 ret);

		ret = wm8994_request_irq(codec->control_data,
					 WM8994_IRQ_MIC2_SHRT,
					 wm8994_mic_irq, "Mic 2 short",
					 wm8994);
		if (ret != 0)
			dev_warn(codec->dev,
				 "Failed to request Mic2 short IRQ: %d\n",
				 ret);
		break;

	case WM8958:
		ret = wm8994_request_irq(codec->control_data,
					 WM8994_IRQ_MIC1_DET,
					 wm8958_mic_irq, "Mic detect",
					 wm8994);
		if (ret != 0)
			dev_warn(codec->dev,
				 "Failed to request Mic detect IRQ: %d\n",
				 ret);
		break;
	}

	/* Remember if AIFnLRCLK is configured as a GPIO.  This should be
	 * configured on init - if a system wants to do this dynamically
	 * at runtime we can deal with that then.
	 */
	ret = wm8994_reg_read(codec->control_data, WM8994_GPIO_1);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read GPIO1 state: %d\n", ret);
		goto err_irq;
	}
	if ((ret & WM8994_GPN_FN_MASK) != WM8994_GP_FN_PIN_SPECIFIC) {
		wm8994->lrclk_shared[0] = 1;
		wm8994_dai[0].symmetric_rates = 1;
	} else {
		wm8994->lrclk_shared[0] = 0;
	}

	ret = wm8994_reg_read(codec->control_data, WM8994_GPIO_6);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read GPIO6 state: %d\n", ret);
		goto err_irq;
	}
	if ((ret & WM8994_GPN_FN_MASK) != WM8994_GP_FN_PIN_SPECIFIC) {
		wm8994->lrclk_shared[1] = 1;
		wm8994_dai[1].symmetric_rates = 1;
	} else {
		wm8994->lrclk_shared[1] = 0;
	}

	wm8994_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Latch volume updates (right only; we always do left then right). */
	snd_soc_update_bits(codec, WM8994_AIF1_DAC1_RIGHT_VOLUME,
			    WM8994_AIF1DAC1_VU, WM8994_AIF1DAC1_VU);
	snd_soc_update_bits(codec, WM8994_AIF1_DAC2_RIGHT_VOLUME,
			    WM8994_AIF1DAC2_VU, WM8994_AIF1DAC2_VU);
	snd_soc_update_bits(codec, WM8994_AIF2_DAC_RIGHT_VOLUME,
			    WM8994_AIF2DAC_VU, WM8994_AIF2DAC_VU);
	snd_soc_update_bits(codec, WM8994_AIF1_ADC1_RIGHT_VOLUME,
			    WM8994_AIF1ADC1_VU, WM8994_AIF1ADC1_VU);
	snd_soc_update_bits(codec, WM8994_AIF1_ADC2_RIGHT_VOLUME,
			    WM8994_AIF1ADC2_VU, WM8994_AIF1ADC2_VU);
	snd_soc_update_bits(codec, WM8994_AIF2_ADC_RIGHT_VOLUME,
			    WM8994_AIF2ADC_VU, WM8994_AIF1ADC2_VU);
	snd_soc_update_bits(codec, WM8994_DAC1_RIGHT_VOLUME,
			    WM8994_DAC1_VU, WM8994_DAC1_VU);
	snd_soc_update_bits(codec, WM8994_DAC2_RIGHT_VOLUME,
			    WM8994_DAC2_VU, WM8994_DAC2_VU);

	/* Set the low bit of the 3D stereo depth so TLV matches */
	snd_soc_update_bits(codec, WM8994_AIF1_DAC1_FILTERS_2,
			    1 << WM8994_AIF1DAC1_3D_GAIN_SHIFT,
			    1 << WM8994_AIF1DAC1_3D_GAIN_SHIFT);
	snd_soc_update_bits(codec, WM8994_AIF1_DAC2_FILTERS_2,
			    1 << WM8994_AIF1DAC2_3D_GAIN_SHIFT,
			    1 << WM8994_AIF1DAC2_3D_GAIN_SHIFT);
	snd_soc_update_bits(codec, WM8994_AIF2_DAC_FILTERS_2,
			    1 << WM8994_AIF2DAC_3D_GAIN_SHIFT,
			    1 << WM8994_AIF2DAC_3D_GAIN_SHIFT);

	/* Unconditionally enable AIF1 ADC TDM mode; it only affects
	 * behaviour on idle TDM clock cycles. */
	snd_soc_update_bits(codec, WM8994_AIF1_CONTROL_1,
			    WM8994_AIF1ADC_TDM, WM8994_AIF1ADC_TDM);

	wm8994_update_class_w(codec);

	wm8994_handle_pdata(wm8994);

	wm_hubs_add_analogue_controls(codec);
	snd_soc_add_controls(codec, wm8994_snd_controls,
			     ARRAY_SIZE(wm8994_snd_controls));
	snd_soc_dapm_new_controls(dapm, wm8994_dapm_widgets,
				  ARRAY_SIZE(wm8994_dapm_widgets));

	switch (control->type) {
	case WM8994:
		snd_soc_dapm_new_controls(dapm, wm8994_specific_dapm_widgets,
					  ARRAY_SIZE(wm8994_specific_dapm_widgets));
		break;
	case WM8958:
		snd_soc_add_controls(codec, wm8958_snd_controls,
				     ARRAY_SIZE(wm8958_snd_controls));
		snd_soc_dapm_new_controls(dapm, wm8958_dapm_widgets,
					  ARRAY_SIZE(wm8958_dapm_widgets));
		break;
	}
		

	wm_hubs_add_analogue_routes(codec, 0, 0);
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	switch (control->type) {
	case WM8994:
		snd_soc_dapm_add_routes(dapm, wm8994_intercon,
					ARRAY_SIZE(wm8994_intercon));
		break;
	case WM8958:
		snd_soc_dapm_add_routes(dapm, wm8958_intercon,
					ARRAY_SIZE(wm8958_intercon));
		break;
	}

	return 0;

err_irq:
	wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC2_SHRT, wm8994);
	wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC2_DET, wm8994);
	wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC1_SHRT, wm8994);
	wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC1_DET, wm8994);
err:
	kfree(wm8994);
	return ret;
}

static int  wm8994_codec_remove(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994 *control = codec->control_data;

	wm8994_set_bias_level(codec, SND_SOC_BIAS_OFF);

	pm_runtime_disable(codec->dev);

	switch (control->type) {
	case WM8994:
		wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC2_SHRT,
				wm8994);
		wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC2_DET,
				wm8994);
		wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC1_SHRT,
				wm8994);
		wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC1_DET,
				wm8994);
		break;

	case WM8958:
		wm8994_free_irq(codec->control_data, WM8994_IRQ_MIC1_DET,
				wm8994);
		break;
	}
	kfree(wm8994->retune_mobile_texts);
	kfree(wm8994->drc_texts);
	kfree(wm8994);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8994 = {
	.probe =	wm8994_codec_probe,
	.remove =	wm8994_codec_remove,
	.suspend =	wm8994_suspend,
	.resume =	wm8994_resume,
	.read =		wm8994_read,
	.write =	wm8994_write,
	.readable_register = wm8994_readable,
	.volatile_register = wm8994_volatile,
	.set_bias_level = wm8994_set_bias_level,

	.reg_cache_size = WM8994_CACHE_SIZE,
	.reg_cache_default = wm8994_reg_defaults,
	.reg_word_size = 2,
	.compress_type = SND_SOC_RBTREE_COMPRESSION,
};

static int __devinit wm8994_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wm8994,
			wm8994_dai, ARRAY_SIZE(wm8994_dai));
}

static int __devexit wm8994_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver wm8994_codec_driver = {
	.driver = {
		   .name = "wm8994-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = wm8994_probe,
	.remove = __devexit_p(wm8994_remove),
};

static __init int wm8994_init(void)
{
	return platform_driver_register(&wm8994_codec_driver);
}
module_init(wm8994_init);

static __exit void wm8994_exit(void)
{
	platform_driver_unregister(&wm8994_codec_driver);
}
module_exit(wm8994_exit);


MODULE_DESCRIPTION("ASoC WM8994 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8994-codec");
