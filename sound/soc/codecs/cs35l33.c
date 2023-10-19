// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs35l33.c -- CS35L33 ALSA SoC audio driver
 *
 * Copyright 2016 Cirrus Logic, Inc.
 *
 * Author: Paul Handrigan <paul.handrigan@cirrus.com>
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <sound/cs35l33.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include "cs35l33.h"
#include "cirrus_legacy.h"

#define CS35L33_BOOT_DELAY	50

struct cs35l33_private {
	struct snd_soc_component *component;
	struct cs35l33_pdata pdata;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	bool amp_cal;
	int mclk_int;
	struct regulator_bulk_data core_supplies[2];
	int num_core_supplies;
	bool is_tdm_mode;
	bool enable_soft_ramp;
};

static const struct reg_default cs35l33_reg[] = {
	{CS35L33_PWRCTL1, 0x85},
	{CS35L33_PWRCTL2, 0xFE},
	{CS35L33_CLK_CTL, 0x0C},
	{CS35L33_BST_PEAK_CTL, 0x90},
	{CS35L33_PROTECT_CTL, 0x55},
	{CS35L33_BST_CTL1, 0x00},
	{CS35L33_BST_CTL2, 0x01},
	{CS35L33_ADSP_CTL, 0x00},
	{CS35L33_ADC_CTL, 0xC8},
	{CS35L33_DAC_CTL, 0x14},
	{CS35L33_DIG_VOL_CTL, 0x00},
	{CS35L33_CLASSD_CTL, 0x04},
	{CS35L33_AMP_CTL, 0x90},
	{CS35L33_INT_MASK_1, 0xFF},
	{CS35L33_INT_MASK_2, 0xFF},
	{CS35L33_DIAG_LOCK, 0x00},
	{CS35L33_DIAG_CTRL_1, 0x40},
	{CS35L33_DIAG_CTRL_2, 0x00},
	{CS35L33_HG_MEMLDO_CTL, 0x62},
	{CS35L33_HG_REL_RATE, 0x03},
	{CS35L33_LDO_DEL, 0x12},
	{CS35L33_HG_HEAD, 0x0A},
	{CS35L33_HG_EN, 0x05},
	{CS35L33_TX_VMON, 0x00},
	{CS35L33_TX_IMON, 0x03},
	{CS35L33_TX_VPMON, 0x02},
	{CS35L33_TX_VBSTMON, 0x05},
	{CS35L33_TX_FLAG, 0x06},
	{CS35L33_TX_EN1, 0x00},
	{CS35L33_TX_EN2, 0x00},
	{CS35L33_TX_EN3, 0x00},
	{CS35L33_TX_EN4, 0x00},
	{CS35L33_RX_AUD, 0x40},
	{CS35L33_RX_SPLY, 0x03},
	{CS35L33_RX_ALIVE, 0x04},
	{CS35L33_BST_CTL4, 0x63},
};

static const struct reg_sequence cs35l33_patch[] = {
	{ 0x00,  0x99, 0 },
	{ 0x59,  0x02, 0 },
	{ 0x52,  0x30, 0 },
	{ 0x39,  0x45, 0 },
	{ 0x57,  0x30, 0 },
	{ 0x2C,  0x68, 0 },
	{ 0x00,  0x00, 0 },
};

static bool cs35l33_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L33_DEVID_AB:
	case CS35L33_DEVID_CD:
	case CS35L33_DEVID_E:
	case CS35L33_REV_ID:
	case CS35L33_INT_STATUS_1:
	case CS35L33_INT_STATUS_2:
	case CS35L33_HG_STATUS:
		return true;
	default:
		return false;
	}
}

static bool cs35l33_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	/* these are read only registers */
	case CS35L33_DEVID_AB:
	case CS35L33_DEVID_CD:
	case CS35L33_DEVID_E:
	case CS35L33_REV_ID:
	case CS35L33_INT_STATUS_1:
	case CS35L33_INT_STATUS_2:
	case CS35L33_HG_STATUS:
		return false;
	default:
		return true;
	}
}

static bool cs35l33_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L33_DEVID_AB:
	case CS35L33_DEVID_CD:
	case CS35L33_DEVID_E:
	case CS35L33_REV_ID:
	case CS35L33_PWRCTL1:
	case CS35L33_PWRCTL2:
	case CS35L33_CLK_CTL:
	case CS35L33_BST_PEAK_CTL:
	case CS35L33_PROTECT_CTL:
	case CS35L33_BST_CTL1:
	case CS35L33_BST_CTL2:
	case CS35L33_ADSP_CTL:
	case CS35L33_ADC_CTL:
	case CS35L33_DAC_CTL:
	case CS35L33_DIG_VOL_CTL:
	case CS35L33_CLASSD_CTL:
	case CS35L33_AMP_CTL:
	case CS35L33_INT_MASK_1:
	case CS35L33_INT_MASK_2:
	case CS35L33_INT_STATUS_1:
	case CS35L33_INT_STATUS_2:
	case CS35L33_DIAG_LOCK:
	case CS35L33_DIAG_CTRL_1:
	case CS35L33_DIAG_CTRL_2:
	case CS35L33_HG_MEMLDO_CTL:
	case CS35L33_HG_REL_RATE:
	case CS35L33_LDO_DEL:
	case CS35L33_HG_HEAD:
	case CS35L33_HG_EN:
	case CS35L33_TX_VMON:
	case CS35L33_TX_IMON:
	case CS35L33_TX_VPMON:
	case CS35L33_TX_VBSTMON:
	case CS35L33_TX_FLAG:
	case CS35L33_TX_EN1:
	case CS35L33_TX_EN2:
	case CS35L33_TX_EN3:
	case CS35L33_TX_EN4:
	case CS35L33_RX_AUD:
	case CS35L33_RX_SPLY:
	case CS35L33_RX_ALIVE:
	case CS35L33_BST_CTL4:
		return true;
	default:
		return false;
	}
}

static DECLARE_TLV_DB_SCALE(classd_ctl_tlv, 900, 100, 0);
static DECLARE_TLV_DB_SCALE(dac_tlv, -10200, 50, 0);

static const struct snd_kcontrol_new cs35l33_snd_controls[] = {

	SOC_SINGLE_TLV("SPK Amp Volume", CS35L33_AMP_CTL,
		       4, 0x09, 0, classd_ctl_tlv),
	SOC_SINGLE_SX_TLV("DAC Volume", CS35L33_DIG_VOL_CTL,
			0, 0x34, 0xE4, dac_tlv),
};

static int cs35l33_spkrdrv_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!priv->amp_cal) {
			usleep_range(8000, 9000);
			priv->amp_cal = true;
			regmap_update_bits(priv->regmap, CS35L33_CLASSD_CTL,
				    CS35L33_AMP_CAL, 0);
			dev_dbg(component->dev, "Amp calibration done\n");
		}
		dev_dbg(component->dev, "Amp turned on\n");
		break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(component->dev, "Amp turned off\n");
		break;
	default:
		dev_err(component->dev, "Invalid event = 0x%x\n", event);
		break;
	}

	return 0;
}

static int cs35l33_sdin_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);
	unsigned int val;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL1,
				    CS35L33_PDN_BST, 0);
		val = priv->is_tdm_mode ? 0 : CS35L33_PDN_TDM;
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL2,
				    CS35L33_PDN_TDM, val);
		dev_dbg(component->dev, "BST turned on\n");
		break;
	case SND_SOC_DAPM_POST_PMU:
		dev_dbg(component->dev, "SDIN turned on\n");
		if (!priv->amp_cal) {
			regmap_update_bits(priv->regmap, CS35L33_CLASSD_CTL,
				    CS35L33_AMP_CAL, CS35L33_AMP_CAL);
			dev_dbg(component->dev, "Amp calibration started\n");
			usleep_range(10000, 11000);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL2,
				    CS35L33_PDN_TDM, CS35L33_PDN_TDM);
		usleep_range(4000, 4100);
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL1,
				    CS35L33_PDN_BST, CS35L33_PDN_BST);
		dev_dbg(component->dev, "BST and SDIN turned off\n");
		break;
	default:
		dev_err(component->dev, "Invalid event = 0x%x\n", event);

	}

	return 0;
}

static int cs35l33_sdout_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);
	unsigned int mask = CS35L33_SDOUT_3ST_I2S | CS35L33_PDN_TDM;
	unsigned int mask2 = CS35L33_SDOUT_3ST_TDM;
	unsigned int val, val2;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (priv->is_tdm_mode) {
			/* set sdout_3st_i2s and reset pdn_tdm */
			val = CS35L33_SDOUT_3ST_I2S;
			/* reset sdout_3st_tdm */
			val2 = 0;
		} else {
			/* reset sdout_3st_i2s and set pdn_tdm */
			val = CS35L33_PDN_TDM;
			/* set sdout_3st_tdm */
			val2 = CS35L33_SDOUT_3ST_TDM;
		}
		dev_dbg(component->dev, "SDOUT turned on\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = CS35L33_SDOUT_3ST_I2S | CS35L33_PDN_TDM;
		val2 = CS35L33_SDOUT_3ST_TDM;
		dev_dbg(component->dev, "SDOUT turned off\n");
		break;
	default:
		dev_err(component->dev, "Invalid event = 0x%x\n", event);
		return 0;
	}

	regmap_update_bits(priv->regmap, CS35L33_PWRCTL2,
		mask, val);
	regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
		mask2, val2);

	return 0;
}

static const struct snd_soc_dapm_widget cs35l33_dapm_widgets[] = {

	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_OUT_DRV_E("SPKDRV", CS35L33_PWRCTL1, 7, 1, NULL, 0,
		cs35l33_spkrdrv_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_IN_E("SDIN", NULL, 0, CS35L33_PWRCTL2,
		2, 1, cs35l33_sdin_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("MON"),

	SND_SOC_DAPM_ADC("VMON", NULL,
		CS35L33_PWRCTL2, CS35L33_PDN_VMON_SHIFT, 1),
	SND_SOC_DAPM_ADC("IMON", NULL,
		CS35L33_PWRCTL2, CS35L33_PDN_IMON_SHIFT, 1),
	SND_SOC_DAPM_ADC("VPMON", NULL,
		CS35L33_PWRCTL2, CS35L33_PDN_VPMON_SHIFT, 1),
	SND_SOC_DAPM_ADC("VBSTMON", NULL,
		CS35L33_PWRCTL2, CS35L33_PDN_VBSTMON_SHIFT, 1),

	SND_SOC_DAPM_AIF_OUT_E("SDOUT", NULL, 0, SND_SOC_NOPM, 0, 0,
		cs35l33_sdout_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_PRE_PMD),
};

static const struct snd_soc_dapm_route cs35l33_audio_map[] = {
	{"SDIN", NULL, "CS35L33 Playback"},
	{"SPKDRV", NULL, "SDIN"},
	{"SPK", NULL, "SPKDRV"},

	{"VMON", NULL, "MON"},
	{"IMON", NULL, "MON"},

	{"SDOUT", NULL, "VMON"},
	{"SDOUT", NULL, "IMON"},
	{"CS35L33 Capture", NULL, "SDOUT"},
};

static const struct snd_soc_dapm_route cs35l33_vphg_auto_route[] = {
	{"SPKDRV", NULL, "VPMON"},
	{"VPMON", NULL, "CS35L33 Playback"},
};

static const struct snd_soc_dapm_route cs35l33_vp_vbst_mon_route[] = {
	{"SDOUT", NULL, "VPMON"},
	{"VPMON", NULL, "MON"},
	{"SDOUT", NULL, "VBSTMON"},
	{"VBSTMON", NULL, "MON"},
};

static int cs35l33_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	unsigned int val;
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL1,
				    CS35L33_PDN_ALL, 0);
		regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
				    CS35L33_MCLKDIS, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL1,
				    CS35L33_PDN_ALL, CS35L33_PDN_ALL);
		regmap_read(priv->regmap, CS35L33_INT_STATUS_2, &val);
		usleep_range(1000, 1100);
		if (val & CS35L33_PDN_DONE)
			regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
					    CS35L33_MCLKDIS, CS35L33_MCLKDIS);
		break;
	case SND_SOC_BIAS_OFF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct cs35l33_mclk_div {
	int mclk;
	int srate;
	u8 adsp_rate;
	u8 int_fs_ratio;
};

static const struct cs35l33_mclk_div cs35l33_mclk_coeffs[] = {
	/* MCLK, Sample Rate, adsp_rate, int_fs_ratio */
	{5644800, 11025, 0x4, CS35L33_INT_FS_RATE},
	{5644800, 22050, 0x8, CS35L33_INT_FS_RATE},
	{5644800, 44100, 0xC, CS35L33_INT_FS_RATE},

	{6000000,  8000, 0x1, 0},
	{6000000, 11025, 0x2, 0},
	{6000000, 11029, 0x3, 0},
	{6000000, 12000, 0x4, 0},
	{6000000, 16000, 0x5, 0},
	{6000000, 22050, 0x6, 0},
	{6000000, 22059, 0x7, 0},
	{6000000, 24000, 0x8, 0},
	{6000000, 32000, 0x9, 0},
	{6000000, 44100, 0xA, 0},
	{6000000, 44118, 0xB, 0},
	{6000000, 48000, 0xC, 0},

	{6144000,  8000, 0x1, CS35L33_INT_FS_RATE},
	{6144000, 12000, 0x4, CS35L33_INT_FS_RATE},
	{6144000, 16000, 0x5, CS35L33_INT_FS_RATE},
	{6144000, 24000, 0x8, CS35L33_INT_FS_RATE},
	{6144000, 32000, 0x9, CS35L33_INT_FS_RATE},
	{6144000, 48000, 0xC, CS35L33_INT_FS_RATE},
};

static int cs35l33_get_mclk_coeff(int mclk, int srate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l33_mclk_coeffs); i++) {
		if (cs35l33_mclk_coeffs[i].mclk == mclk &&
			cs35l33_mclk_coeffs[i].srate == srate)
			return i;
	}
	return -EINVAL;
}

static int cs35l33_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		regmap_update_bits(priv->regmap, CS35L33_ADSP_CTL,
			CS35L33_MS_MASK, CS35L33_MS_MASK);
		dev_dbg(component->dev, "Audio port in master mode\n");
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		regmap_update_bits(priv->regmap, CS35L33_ADSP_CTL,
			CS35L33_MS_MASK, 0);
		dev_dbg(component->dev, "Audio port in slave mode\n");
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		/*
		 * tdm mode in cs35l33 resembles dsp-a mode very
		 * closely, it is dsp-a with fsync shifted left by half bclk
		 */
		priv->is_tdm_mode = true;
		dev_dbg(component->dev, "Audio port in TDM mode\n");
		break;
	case SND_SOC_DAIFMT_I2S:
		priv->is_tdm_mode = false;
		dev_dbg(component->dev, "Audio port in I2S mode\n");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cs35l33_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);
	int sample_size = params_width(params);
	int coeff = cs35l33_get_mclk_coeff(priv->mclk_int, params_rate(params));

	if (coeff < 0)
		return coeff;

	regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
		CS35L33_ADSP_FS | CS35L33_INT_FS_RATE,
		cs35l33_mclk_coeffs[coeff].int_fs_ratio
		| cs35l33_mclk_coeffs[coeff].adsp_rate);

	if (priv->is_tdm_mode) {
		sample_size = (sample_size / 8) - 1;
		if (sample_size > 2)
			sample_size = 2;
		regmap_update_bits(priv->regmap, CS35L33_RX_AUD,
			CS35L33_AUDIN_RX_DEPTH,
			sample_size << CS35L33_AUDIN_RX_DEPTH_SHIFT);
	}

	dev_dbg(component->dev, "sample rate=%d, bits per sample=%d\n",
		params_rate(params), params_width(params));

	return 0;
}

static const unsigned int cs35l33_src_rates[] = {
	8000, 11025, 11029, 12000, 16000, 22050,
	22059, 24000, 32000, 44100, 44118, 48000
};

static const struct snd_pcm_hw_constraint_list cs35l33_constraints = {
	.count  = ARRAY_SIZE(cs35l33_src_rates),
	.list   = cs35l33_src_rates,
};

static int cs35l33_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	snd_pcm_hw_constraint_list(substream->runtime, 0,
					SNDRV_PCM_HW_PARAM_RATE,
					&cs35l33_constraints);
	return 0;
}

static int cs35l33_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);

	if (tristate) {
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL2,
			CS35L33_SDOUT_3ST_I2S, CS35L33_SDOUT_3ST_I2S);
		regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
			CS35L33_SDOUT_3ST_TDM, CS35L33_SDOUT_3ST_TDM);
	} else {
		regmap_update_bits(priv->regmap, CS35L33_PWRCTL2,
			CS35L33_SDOUT_3ST_I2S, 0);
		regmap_update_bits(priv->regmap, CS35L33_CLK_CTL,
			CS35L33_SDOUT_3ST_TDM, 0);
	}

	return 0;
}

static int cs35l33_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);
	unsigned int reg, bit_pos, i;
	int slot, slot_num;

	if (slot_width != 8)
		return -EINVAL;

	/* scan rx_mask for aud slot */
	slot = ffs(rx_mask) - 1;
	if (slot >= 0) {
		regmap_update_bits(priv->regmap, CS35L33_RX_AUD,
			CS35L33_X_LOC, slot);
		dev_dbg(component->dev, "Audio starts from slots %d", slot);
	}

	/*
	 * scan tx_mask: vmon(2 slots); imon (2 slots);
	 * vpmon (1 slot) vbstmon (1 slot)
	 */
	slot = ffs(tx_mask) - 1;
	slot_num = 0;

	for (i = 0; i < 2 ; i++) {
		/* disable vpmon/vbstmon: enable later if set in tx_mask */
		regmap_update_bits(priv->regmap, CS35L33_TX_VPMON + i,
			CS35L33_X_STATE | CS35L33_X_LOC, CS35L33_X_STATE
			| CS35L33_X_LOC);
	}

	/* disconnect {vp,vbst}_mon routes: eanble later if set in tx_mask*/
	snd_soc_dapm_del_routes(dapm, cs35l33_vp_vbst_mon_route,
		ARRAY_SIZE(cs35l33_vp_vbst_mon_route));

	while (slot >= 0) {
		/* configure VMON_TX_LOC */
		if (slot_num == 0) {
			regmap_update_bits(priv->regmap, CS35L33_TX_VMON,
				CS35L33_X_STATE | CS35L33_X_LOC, slot);
			dev_dbg(component->dev, "VMON enabled in slots %d-%d",
				slot, slot + 1);
		}

		/* configure IMON_TX_LOC */
		if (slot_num == 3) {
			regmap_update_bits(priv->regmap, CS35L33_TX_IMON,
				CS35L33_X_STATE | CS35L33_X_LOC, slot);
			dev_dbg(component->dev, "IMON enabled in slots %d-%d",
				slot, slot + 1);
		}

		/* configure VPMON_TX_LOC */
		if (slot_num == 4) {
			regmap_update_bits(priv->regmap, CS35L33_TX_VPMON,
				CS35L33_X_STATE | CS35L33_X_LOC, slot);
			snd_soc_dapm_add_routes(dapm,
				&cs35l33_vp_vbst_mon_route[0], 2);
			dev_dbg(component->dev, "VPMON enabled in slots %d", slot);
		}

		/* configure VBSTMON_TX_LOC */
		if (slot_num == 5) {
			regmap_update_bits(priv->regmap, CS35L33_TX_VBSTMON,
				CS35L33_X_STATE | CS35L33_X_LOC, slot);
			snd_soc_dapm_add_routes(dapm,
				&cs35l33_vp_vbst_mon_route[2], 2);
			dev_dbg(component->dev,
				"VBSTMON enabled in slots %d", slot);
		}

		/* Enable the relevant tx slot */
		reg = CS35L33_TX_EN4 - (slot/8);
		bit_pos = slot - ((slot / 8) * (8));
		regmap_update_bits(priv->regmap, reg,
			1 << bit_pos, 1 << bit_pos);

		tx_mask &= ~(1 << slot);
		slot = ffs(tx_mask) - 1;
		slot_num++;
	}

	return 0;
}

static int cs35l33_component_set_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct cs35l33_private *cs35l33 = snd_soc_component_get_drvdata(component);

	switch (freq) {
	case CS35L33_MCLK_5644:
	case CS35L33_MCLK_6:
	case CS35L33_MCLK_6144:
		regmap_update_bits(cs35l33->regmap, CS35L33_CLK_CTL,
			CS35L33_MCLKDIV2, 0);
		cs35l33->mclk_int = freq;
		break;
	case CS35L33_MCLK_11289:
	case CS35L33_MCLK_12:
	case CS35L33_MCLK_12288:
		regmap_update_bits(cs35l33->regmap, CS35L33_CLK_CTL,
			CS35L33_MCLKDIV2, CS35L33_MCLKDIV2);
		cs35l33->mclk_int = freq/2;
		break;
	default:
		cs35l33->mclk_int = 0;
		return -EINVAL;
	}

	dev_dbg(component->dev, "external mclk freq=%d, internal mclk freq=%d\n",
		freq, cs35l33->mclk_int);

	return 0;
}

static const struct snd_soc_dai_ops cs35l33_ops = {
	.startup = cs35l33_pcm_startup,
	.set_tristate = cs35l33_set_tristate,
	.set_fmt = cs35l33_set_dai_fmt,
	.hw_params = cs35l33_pcm_hw_params,
	.set_tdm_slot = cs35l33_set_tdm_slot,
};

static struct snd_soc_dai_driver cs35l33_dai = {
		.name = "cs35l33-dai",
		.id = 0,
		.playback = {
			.stream_name = "CS35L33 Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = CS35L33_RATES,
			.formats = CS35L33_FORMATS,
		},
		.capture = {
			.stream_name = "CS35L33 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = CS35L33_RATES,
			.formats = CS35L33_FORMATS,
		},
		.ops = &cs35l33_ops,
		.symmetric_rate = 1,
};

static int cs35l33_set_hg_data(struct snd_soc_component *component,
			       struct cs35l33_pdata *pdata)
{
	struct cs35l33_hg *hg_config = &pdata->hg_config;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct cs35l33_private *priv = snd_soc_component_get_drvdata(component);

	if (hg_config->enable_hg_algo) {
		regmap_update_bits(priv->regmap, CS35L33_HG_MEMLDO_CTL,
			CS35L33_MEM_DEPTH_MASK,
			hg_config->mem_depth << CS35L33_MEM_DEPTH_SHIFT);
		regmap_write(priv->regmap, CS35L33_HG_REL_RATE,
			hg_config->release_rate);
		regmap_update_bits(priv->regmap, CS35L33_HG_HEAD,
			CS35L33_HD_RM_MASK,
			hg_config->hd_rm << CS35L33_HD_RM_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_HG_MEMLDO_CTL,
			CS35L33_LDO_THLD_MASK,
			hg_config->ldo_thld << CS35L33_LDO_THLD_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_HG_MEMLDO_CTL,
			CS35L33_LDO_DISABLE_MASK,
			hg_config->ldo_path_disable <<
				CS35L33_LDO_DISABLE_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_LDO_DEL,
			CS35L33_LDO_ENTRY_DELAY_MASK,
			hg_config->ldo_entry_delay <<
				CS35L33_LDO_ENTRY_DELAY_SHIFT);
		if (hg_config->vp_hg_auto) {
			regmap_update_bits(priv->regmap, CS35L33_HG_EN,
				CS35L33_VP_HG_AUTO_MASK,
				CS35L33_VP_HG_AUTO_MASK);
			snd_soc_dapm_add_routes(dapm, cs35l33_vphg_auto_route,
				ARRAY_SIZE(cs35l33_vphg_auto_route));
		}
		regmap_update_bits(priv->regmap, CS35L33_HG_EN,
			CS35L33_VP_HG_MASK,
			hg_config->vp_hg << CS35L33_VP_HG_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_LDO_DEL,
			CS35L33_VP_HG_RATE_MASK,
			hg_config->vp_hg_rate << CS35L33_VP_HG_RATE_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_LDO_DEL,
			CS35L33_VP_HG_VA_MASK,
			hg_config->vp_hg_va << CS35L33_VP_HG_VA_SHIFT);
		regmap_update_bits(priv->regmap, CS35L33_HG_EN,
			CS35L33_CLASS_HG_EN_MASK, CS35L33_CLASS_HG_EN_MASK);
	}
	return 0;
}

static int cs35l33_set_bst_ipk(struct snd_soc_component *component, unsigned int bst)
{
	struct cs35l33_private *cs35l33 = snd_soc_component_get_drvdata(component);
	int ret = 0, steps = 0;

	/* Boost current in uA */
	if (bst > 3600000 || bst < 1850000) {
		dev_err(component->dev, "Invalid boost current %d\n", bst);
		ret = -EINVAL;
		goto err;
	}

	if (bst % 15625) {
		dev_err(component->dev, "Current not a multiple of 15625uA (%d)\n",
			bst);
		ret = -EINVAL;
		goto err;
	}

	while (bst > 1850000) {
		bst -= 15625;
		steps++;
	}

	regmap_write(cs35l33->regmap, CS35L33_BST_PEAK_CTL,
		steps+0x70);

err:
	return ret;
}

static int cs35l33_probe(struct snd_soc_component *component)
{
	struct cs35l33_private *cs35l33 = snd_soc_component_get_drvdata(component);

	cs35l33->component = component;
	pm_runtime_get_sync(component->dev);

	regmap_update_bits(cs35l33->regmap, CS35L33_PROTECT_CTL,
		CS35L33_ALIVE_WD_DIS, 0x8);
	regmap_update_bits(cs35l33->regmap, CS35L33_BST_CTL2,
				CS35L33_ALIVE_WD_DIS2,
				CS35L33_ALIVE_WD_DIS2);

	/* Set Platform Data */
	regmap_update_bits(cs35l33->regmap, CS35L33_BST_CTL1,
		CS35L33_BST_CTL_MASK, cs35l33->pdata.boost_ctl);
	regmap_update_bits(cs35l33->regmap, CS35L33_CLASSD_CTL,
		CS35L33_AMP_DRV_SEL_MASK,
		cs35l33->pdata.amp_drv_sel << CS35L33_AMP_DRV_SEL_SHIFT);

	if (cs35l33->pdata.boost_ipk)
		cs35l33_set_bst_ipk(component, cs35l33->pdata.boost_ipk);

	if (cs35l33->enable_soft_ramp) {
		snd_soc_component_update_bits(component, CS35L33_DAC_CTL,
			CS35L33_DIGSFT, CS35L33_DIGSFT);
		snd_soc_component_update_bits(component, CS35L33_DAC_CTL,
			CS35L33_DSR_RATE, cs35l33->pdata.ramp_rate);
	} else {
		snd_soc_component_update_bits(component, CS35L33_DAC_CTL,
			CS35L33_DIGSFT, 0);
	}

	/* update IMON scaling rate if different from default of 0x8 */
	if (cs35l33->pdata.imon_adc_scale != 0x8)
		snd_soc_component_update_bits(component, CS35L33_ADC_CTL,
			CS35L33_IMON_SCALE, cs35l33->pdata.imon_adc_scale);

	cs35l33_set_hg_data(component, &(cs35l33->pdata));

	/*
	 * unmask important interrupts that causes the chip to enter
	 * speaker safe mode and hence deserves user attention
	 */
	regmap_update_bits(cs35l33->regmap, CS35L33_INT_MASK_1,
		CS35L33_M_OTE | CS35L33_M_OTW | CS35L33_M_AMP_SHORT |
		CS35L33_M_CAL_ERR, 0);

	pm_runtime_put_sync(component->dev);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs35l33 = {
	.probe			= cs35l33_probe,
	.set_bias_level		= cs35l33_set_bias_level,
	.set_sysclk		= cs35l33_component_set_sysclk,
	.controls		= cs35l33_snd_controls,
	.num_controls		= ARRAY_SIZE(cs35l33_snd_controls),
	.dapm_widgets		= cs35l33_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs35l33_dapm_widgets),
	.dapm_routes		= cs35l33_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs35l33_audio_map),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config cs35l33_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS35L33_MAX_REGISTER,
	.reg_defaults = cs35l33_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l33_reg),
	.volatile_reg = cs35l33_volatile_register,
	.readable_reg = cs35l33_readable_register,
	.writeable_reg = cs35l33_writeable_register,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int __maybe_unused cs35l33_runtime_resume(struct device *dev)
{
	struct cs35l33_private *cs35l33 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	gpiod_set_value_cansleep(cs35l33->reset_gpio, 0);

	ret = regulator_bulk_enable(cs35l33->num_core_supplies,
		cs35l33->core_supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	regcache_cache_only(cs35l33->regmap, false);

	gpiod_set_value_cansleep(cs35l33->reset_gpio, 1);

	msleep(CS35L33_BOOT_DELAY);

	ret = regcache_sync(cs35l33->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to restore register cache\n");
		goto err;
	}

	return 0;

err:
	regcache_cache_only(cs35l33->regmap, true);
	regulator_bulk_disable(cs35l33->num_core_supplies,
		cs35l33->core_supplies);

	return ret;
}

static int __maybe_unused cs35l33_runtime_suspend(struct device *dev)
{
	struct cs35l33_private *cs35l33 = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	/* redo the calibration in next power up */
	cs35l33->amp_cal = false;

	regcache_cache_only(cs35l33->regmap, true);
	regcache_mark_dirty(cs35l33->regmap);
	regulator_bulk_disable(cs35l33->num_core_supplies,
		cs35l33->core_supplies);

	return 0;
}

static const struct dev_pm_ops cs35l33_pm_ops = {
	SET_RUNTIME_PM_OPS(cs35l33_runtime_suspend,
			   cs35l33_runtime_resume,
			   NULL)
};

static int cs35l33_get_hg_data(const struct device_node *np,
			       struct cs35l33_pdata *pdata)
{
	struct device_node *hg;
	struct cs35l33_hg *hg_config = &pdata->hg_config;
	u32 val32;

	hg = of_get_child_by_name(np, "cirrus,hg-algo");
	hg_config->enable_hg_algo = hg ? true : false;

	if (hg_config->enable_hg_algo) {
		if (of_property_read_u32(hg, "cirrus,mem-depth", &val32) >= 0)
			hg_config->mem_depth = val32;
		if (of_property_read_u32(hg, "cirrus,release-rate",
				&val32) >= 0)
			hg_config->release_rate = val32;
		if (of_property_read_u32(hg, "cirrus,ldo-thld", &val32) >= 0)
			hg_config->ldo_thld = val32;
		if (of_property_read_u32(hg, "cirrus,ldo-path-disable",
				&val32) >= 0)
			hg_config->ldo_path_disable = val32;
		if (of_property_read_u32(hg, "cirrus,ldo-entry-delay",
				&val32) >= 0)
			hg_config->ldo_entry_delay = val32;

		hg_config->vp_hg_auto = of_property_read_bool(hg,
			"cirrus,vp-hg-auto");

		if (of_property_read_u32(hg, "cirrus,vp-hg", &val32) >= 0)
			hg_config->vp_hg = val32;
		if (of_property_read_u32(hg, "cirrus,vp-hg-rate", &val32) >= 0)
			hg_config->vp_hg_rate = val32;
		if (of_property_read_u32(hg, "cirrus,vp-hg-va", &val32) >= 0)
			hg_config->vp_hg_va = val32;
	}

	of_node_put(hg);

	return 0;
}

static irqreturn_t cs35l33_irq_thread(int irq, void *data)
{
	struct cs35l33_private *cs35l33 = data;
	struct snd_soc_component *component = cs35l33->component;
	unsigned int sticky_val1, sticky_val2, current_val, mask1, mask2;

	regmap_read(cs35l33->regmap, CS35L33_INT_STATUS_2,
		&sticky_val2);
	regmap_read(cs35l33->regmap, CS35L33_INT_STATUS_1,
		&sticky_val1);
	regmap_read(cs35l33->regmap, CS35L33_INT_MASK_2, &mask2);
	regmap_read(cs35l33->regmap, CS35L33_INT_MASK_1, &mask1);

	/* Check to see if the unmasked bits are active,
	 *  if not then exit.
	 */
	if (!(sticky_val1 & ~mask1) && !(sticky_val2 & ~mask2))
		return IRQ_NONE;

	regmap_read(cs35l33->regmap, CS35L33_INT_STATUS_1,
		&current_val);

	/* handle the interrupts */

	if (sticky_val1 & CS35L33_AMP_SHORT) {
		dev_crit(component->dev, "Amp short error\n");
		if (!(current_val & CS35L33_AMP_SHORT)) {
			dev_dbg(component->dev,
				"Amp short error release\n");
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL,
				CS35L33_AMP_SHORT_RLS, 0);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL,
				CS35L33_AMP_SHORT_RLS,
				CS35L33_AMP_SHORT_RLS);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_AMP_SHORT_RLS,
				0);
		}
	}

	if (sticky_val1 & CS35L33_CAL_ERR) {
		dev_err(component->dev, "Cal error\n");

		/* redo the calibration in next power up */
		cs35l33->amp_cal = false;

		if (!(current_val & CS35L33_CAL_ERR)) {
			dev_dbg(component->dev, "Cal error release\n");
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_CAL_ERR_RLS,
				0);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_CAL_ERR_RLS,
				CS35L33_CAL_ERR_RLS);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_CAL_ERR_RLS,
				0);
		}
	}

	if (sticky_val1 & CS35L33_OTE) {
		dev_crit(component->dev, "Over temperature error\n");
		if (!(current_val & CS35L33_OTE)) {
			dev_dbg(component->dev,
				"Over temperature error release\n");
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTE_RLS, 0);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTE_RLS,
				CS35L33_OTE_RLS);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTE_RLS, 0);
		}
	}

	if (sticky_val1 & CS35L33_OTW) {
		dev_err(component->dev, "Over temperature warning\n");
		if (!(current_val & CS35L33_OTW)) {
			dev_dbg(component->dev,
				"Over temperature warning release\n");
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTW_RLS, 0);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTW_RLS,
				CS35L33_OTW_RLS);
			regmap_update_bits(cs35l33->regmap,
				CS35L33_AMP_CTL, CS35L33_OTW_RLS, 0);
		}
	}
	if (CS35L33_ALIVE_ERR & sticky_val1)
		dev_err(component->dev, "ERROR: ADSPCLK Interrupt\n");

	if (CS35L33_MCLK_ERR & sticky_val1)
		dev_err(component->dev, "ERROR: MCLK Interrupt\n");

	if (CS35L33_VMON_OVFL & sticky_val2)
		dev_err(component->dev,
			"ERROR: VMON Overflow Interrupt\n");

	if (CS35L33_IMON_OVFL & sticky_val2)
		dev_err(component->dev,
			"ERROR: IMON Overflow Interrupt\n");

	if (CS35L33_VPMON_OVFL & sticky_val2)
		dev_err(component->dev,
			"ERROR: VPMON Overflow Interrupt\n");

	return IRQ_HANDLED;
}

static const char * const cs35l33_core_supplies[] = {
	"VA",
	"VP",
};

static int cs35l33_of_get_pdata(struct device *dev,
				struct cs35l33_private *cs35l33)
{
	struct device_node *np = dev->of_node;
	struct cs35l33_pdata *pdata = &cs35l33->pdata;
	u32 val32;

	if (!np)
		return 0;

	if (of_property_read_u32(np, "cirrus,boost-ctl", &val32) >= 0) {
		pdata->boost_ctl = val32;
		pdata->amp_drv_sel = 1;
	}

	if (of_property_read_u32(np, "cirrus,ramp-rate", &val32) >= 0) {
		pdata->ramp_rate = val32;
		cs35l33->enable_soft_ramp = true;
	}

	if (of_property_read_u32(np, "cirrus,boost-ipk", &val32) >= 0)
		pdata->boost_ipk = val32;

	if (of_property_read_u32(np, "cirrus,imon-adc-scale", &val32) >= 0) {
		if ((val32 == 0x0) || (val32 == 0x7) || (val32 == 0x6))
			pdata->imon_adc_scale = val32;
		else
			/* use default value */
			pdata->imon_adc_scale = 0x8;
	} else {
		/* use default value */
		pdata->imon_adc_scale = 0x8;
	}

	cs35l33_get_hg_data(np, pdata);

	return 0;
}

static int cs35l33_i2c_probe(struct i2c_client *i2c_client)
{
	struct cs35l33_private *cs35l33;
	struct cs35l33_pdata *pdata = dev_get_platdata(&i2c_client->dev);
	int ret, devid, i;
	unsigned int reg;

	cs35l33 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs35l33_private),
			       GFP_KERNEL);
	if (!cs35l33)
		return -ENOMEM;

	i2c_set_clientdata(i2c_client, cs35l33);
	cs35l33->regmap = devm_regmap_init_i2c(i2c_client, &cs35l33_regmap);
	if (IS_ERR(cs35l33->regmap)) {
		ret = PTR_ERR(cs35l33->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	regcache_cache_only(cs35l33->regmap, true);

	for (i = 0; i < ARRAY_SIZE(cs35l33_core_supplies); i++)
		cs35l33->core_supplies[i].supply
			= cs35l33_core_supplies[i];
	cs35l33->num_core_supplies = ARRAY_SIZE(cs35l33_core_supplies);

	ret = devm_regulator_bulk_get(&i2c_client->dev,
			cs35l33->num_core_supplies,
			cs35l33->core_supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to request core supplies: %d\n",
			ret);
		return ret;
	}

	if (pdata) {
		cs35l33->pdata = *pdata;
	} else {
		cs35l33_of_get_pdata(&i2c_client->dev, cs35l33);
		pdata = &cs35l33->pdata;
	}

	ret = devm_request_threaded_irq(&i2c_client->dev, i2c_client->irq, NULL,
			cs35l33_irq_thread, IRQF_ONESHOT | IRQF_TRIGGER_LOW,
			"cs35l33", cs35l33);
	if (ret != 0)
		dev_warn(&i2c_client->dev, "Failed to request IRQ: %d\n", ret);

	/* We could issue !RST or skip it based on AMP topology */
	cs35l33->reset_gpio = devm_gpiod_get_optional(&i2c_client->dev,
			"reset-gpios", GPIOD_OUT_HIGH);
	if (IS_ERR(cs35l33->reset_gpio)) {
		dev_err(&i2c_client->dev, "%s ERROR: Can't get reset GPIO\n",
			__func__);
		return PTR_ERR(cs35l33->reset_gpio);
	}

	ret = regulator_bulk_enable(cs35l33->num_core_supplies,
					cs35l33->core_supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to enable core supplies: %d\n",
			ret);
		return ret;
	}

	gpiod_set_value_cansleep(cs35l33->reset_gpio, 1);

	msleep(CS35L33_BOOT_DELAY);
	regcache_cache_only(cs35l33->regmap, false);

	/* initialize codec */
	devid = cirrus_read_device_id(cs35l33->regmap, CS35L33_DEVID_AB);
	if (devid < 0) {
		ret = devid;
		dev_err(&i2c_client->dev, "Failed to read device ID: %d\n", ret);
		goto err_enable;
	}

	if (devid != CS35L33_CHIP_ID) {
		dev_err(&i2c_client->dev,
			"CS35L33 Device ID (%X). Expected ID %X\n",
			devid, CS35L33_CHIP_ID);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = regmap_read(cs35l33->regmap, CS35L33_REV_ID, &reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Get Revision ID failed\n");
		goto err_enable;
	}

	dev_info(&i2c_client->dev,
		 "Cirrus Logic CS35L33, Revision: %02X\n", reg & 0xFF);

	ret = regmap_register_patch(cs35l33->regmap,
			cs35l33_patch, ARRAY_SIZE(cs35l33_patch));
	if (ret < 0) {
		dev_err(&i2c_client->dev,
			"Error in applying regmap patch: %d\n", ret);
		goto err_enable;
	}

	/* disable mclk and tdm */
	regmap_update_bits(cs35l33->regmap, CS35L33_CLK_CTL,
		CS35L33_MCLKDIS | CS35L33_SDOUT_3ST_TDM,
		CS35L33_MCLKDIS | CS35L33_SDOUT_3ST_TDM);

	pm_runtime_set_autosuspend_delay(&i2c_client->dev, 100);
	pm_runtime_use_autosuspend(&i2c_client->dev);
	pm_runtime_set_active(&i2c_client->dev);
	pm_runtime_enable(&i2c_client->dev);

	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_dev_cs35l33, &cs35l33_dai, 1);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "%s: Register component failed\n",
			__func__);
		goto err_enable;
	}

	return 0;

err_enable:
	gpiod_set_value_cansleep(cs35l33->reset_gpio, 0);

	regulator_bulk_disable(cs35l33->num_core_supplies,
			       cs35l33->core_supplies);

	return ret;
}

static void cs35l33_i2c_remove(struct i2c_client *client)
{
	struct cs35l33_private *cs35l33 = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(cs35l33->reset_gpio, 0);

	pm_runtime_disable(&client->dev);
	regulator_bulk_disable(cs35l33->num_core_supplies,
		cs35l33->core_supplies);
}

static const struct of_device_id cs35l33_of_match[] = {
	{ .compatible = "cirrus,cs35l33", },
	{},
};
MODULE_DEVICE_TABLE(of, cs35l33_of_match);

static const struct i2c_device_id cs35l33_id[] = {
	{"cs35l33", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cs35l33_id);

static struct i2c_driver cs35l33_i2c_driver = {
	.driver = {
		.name = "cs35l33",
		.pm = &cs35l33_pm_ops,
		.of_match_table = cs35l33_of_match,

		},
	.id_table = cs35l33_id,
	.probe_new = cs35l33_i2c_probe,
	.remove = cs35l33_i2c_remove,

};
module_i2c_driver(cs35l33_i2c_driver);

MODULE_DESCRIPTION("ASoC CS35L33 driver");
MODULE_AUTHOR("Paul Handrigan, Cirrus Logic Inc, <paul.handrigan@cirrus.com>");
MODULE_LICENSE("GPL");
