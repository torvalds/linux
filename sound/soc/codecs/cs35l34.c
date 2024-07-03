// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs35l34.c -- CS35l34 ALSA SoC audio driver
 *
 * Copyright 2016 Cirrus Logic, Inc.
 *
 * Author: Paul Handrigan <Paul.Handrigan@cirrus.com>
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
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/gpio/consumer.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/cs35l34.h>

#include "cs35l34.h"
#include "cirrus_legacy.h"

#define PDN_DONE_ATTEMPTS 10
#define CS35L34_START_DELAY 50

struct  cs35l34_private {
	struct snd_soc_component *component;
	struct cs35l34_platform_data pdata;
	struct regmap *regmap;
	struct regulator_bulk_data core_supplies[2];
	int num_core_supplies;
	int mclk_int;
	bool tdm_mode;
	struct gpio_desc *reset_gpio;	/* Active-low reset GPIO */
};

static const struct reg_default cs35l34_reg[] = {
	{CS35L34_PWRCTL1, 0x01},
	{CS35L34_PWRCTL2, 0x19},
	{CS35L34_PWRCTL3, 0x01},
	{CS35L34_ADSP_CLK_CTL, 0x08},
	{CS35L34_MCLK_CTL, 0x11},
	{CS35L34_AMP_INP_DRV_CTL, 0x01},
	{CS35L34_AMP_DIG_VOL_CTL, 0x12},
	{CS35L34_AMP_DIG_VOL, 0x00},
	{CS35L34_AMP_ANLG_GAIN_CTL, 0x0F},
	{CS35L34_PROTECT_CTL, 0x06},
	{CS35L34_AMP_KEEP_ALIVE_CTL, 0x04},
	{CS35L34_BST_CVTR_V_CTL, 0x00},
	{CS35L34_BST_PEAK_I, 0x10},
	{CS35L34_BST_RAMP_CTL, 0x87},
	{CS35L34_BST_CONV_COEF_1, 0x24},
	{CS35L34_BST_CONV_COEF_2, 0x24},
	{CS35L34_BST_CONV_SLOPE_COMP, 0x4E},
	{CS35L34_BST_CONV_SW_FREQ, 0x08},
	{CS35L34_CLASS_H_CTL, 0x0D},
	{CS35L34_CLASS_H_HEADRM_CTL, 0x0D},
	{CS35L34_CLASS_H_RELEASE_RATE, 0x08},
	{CS35L34_CLASS_H_FET_DRIVE_CTL, 0x41},
	{CS35L34_CLASS_H_STATUS, 0x05},
	{CS35L34_VPBR_CTL, 0x0A},
	{CS35L34_VPBR_VOL_CTL, 0x90},
	{CS35L34_VPBR_TIMING_CTL, 0x6A},
	{CS35L34_PRED_MAX_ATTEN_SPK_LOAD, 0x95},
	{CS35L34_PRED_BROWNOUT_THRESH, 0x1C},
	{CS35L34_PRED_BROWNOUT_VOL_CTL, 0x00},
	{CS35L34_PRED_BROWNOUT_RATE_CTL, 0x10},
	{CS35L34_PRED_WAIT_CTL, 0x10},
	{CS35L34_PRED_ZVP_INIT_IMP_CTL, 0x08},
	{CS35L34_PRED_MAN_SAFE_VPI_CTL, 0x80},
	{CS35L34_VPBR_ATTEN_STATUS, 0x00},
	{CS35L34_PRED_BRWNOUT_ATT_STATUS, 0x00},
	{CS35L34_SPKR_MON_CTL, 0xC6},
	{CS35L34_ADSP_I2S_CTL, 0x00},
	{CS35L34_ADSP_TDM_CTL, 0x00},
	{CS35L34_TDM_TX_CTL_1_VMON, 0x00},
	{CS35L34_TDM_TX_CTL_2_IMON, 0x04},
	{CS35L34_TDM_TX_CTL_3_VPMON, 0x03},
	{CS35L34_TDM_TX_CTL_4_VBSTMON, 0x07},
	{CS35L34_TDM_TX_CTL_5_FLAG1, 0x08},
	{CS35L34_TDM_TX_CTL_6_FLAG2, 0x09},
	{CS35L34_TDM_TX_SLOT_EN_1, 0x00},
	{CS35L34_TDM_TX_SLOT_EN_2, 0x00},
	{CS35L34_TDM_TX_SLOT_EN_3, 0x00},
	{CS35L34_TDM_TX_SLOT_EN_4, 0x00},
	{CS35L34_TDM_RX_CTL_1_AUDIN, 0x40},
	{CS35L34_TDM_RX_CTL_3_ALIVE, 0x04},
	{CS35L34_MULT_DEV_SYNCH1, 0x00},
	{CS35L34_MULT_DEV_SYNCH2, 0x80},
	{CS35L34_PROT_RELEASE_CTL, 0x00},
	{CS35L34_DIAG_MODE_REG_LOCK, 0x00},
	{CS35L34_DIAG_MODE_CTL_1, 0x00},
	{CS35L34_DIAG_MODE_CTL_2, 0x00},
	{CS35L34_INT_MASK_1, 0xFF},
	{CS35L34_INT_MASK_2, 0xFF},
	{CS35L34_INT_MASK_3, 0xFF},
	{CS35L34_INT_MASK_4, 0xFF},
	{CS35L34_INT_STATUS_1, 0x30},
	{CS35L34_INT_STATUS_2, 0x05},
	{CS35L34_INT_STATUS_3, 0x00},
	{CS35L34_INT_STATUS_4, 0x00},
	{CS35L34_OTP_TRIM_STATUS, 0x00},
};

static bool cs35l34_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L34_DEVID_AB:
	case CS35L34_DEVID_CD:
	case CS35L34_DEVID_E:
	case CS35L34_FAB_ID:
	case CS35L34_REV_ID:
	case CS35L34_INT_STATUS_1:
	case CS35L34_INT_STATUS_2:
	case CS35L34_INT_STATUS_3:
	case CS35L34_INT_STATUS_4:
	case CS35L34_CLASS_H_STATUS:
	case CS35L34_VPBR_ATTEN_STATUS:
	case CS35L34_OTP_TRIM_STATUS:
		return true;
	default:
		return false;
	}
}

static bool cs35l34_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case	CS35L34_DEVID_AB:
	case	CS35L34_DEVID_CD:
	case	CS35L34_DEVID_E:
	case	CS35L34_FAB_ID:
	case	CS35L34_REV_ID:
	case	CS35L34_PWRCTL1:
	case	CS35L34_PWRCTL2:
	case	CS35L34_PWRCTL3:
	case	CS35L34_ADSP_CLK_CTL:
	case	CS35L34_MCLK_CTL:
	case	CS35L34_AMP_INP_DRV_CTL:
	case	CS35L34_AMP_DIG_VOL_CTL:
	case	CS35L34_AMP_DIG_VOL:
	case	CS35L34_AMP_ANLG_GAIN_CTL:
	case	CS35L34_PROTECT_CTL:
	case	CS35L34_AMP_KEEP_ALIVE_CTL:
	case	CS35L34_BST_CVTR_V_CTL:
	case	CS35L34_BST_PEAK_I:
	case	CS35L34_BST_RAMP_CTL:
	case	CS35L34_BST_CONV_COEF_1:
	case	CS35L34_BST_CONV_COEF_2:
	case	CS35L34_BST_CONV_SLOPE_COMP:
	case	CS35L34_BST_CONV_SW_FREQ:
	case	CS35L34_CLASS_H_CTL:
	case	CS35L34_CLASS_H_HEADRM_CTL:
	case	CS35L34_CLASS_H_RELEASE_RATE:
	case	CS35L34_CLASS_H_FET_DRIVE_CTL:
	case	CS35L34_CLASS_H_STATUS:
	case	CS35L34_VPBR_CTL:
	case	CS35L34_VPBR_VOL_CTL:
	case	CS35L34_VPBR_TIMING_CTL:
	case	CS35L34_PRED_MAX_ATTEN_SPK_LOAD:
	case	CS35L34_PRED_BROWNOUT_THRESH:
	case	CS35L34_PRED_BROWNOUT_VOL_CTL:
	case	CS35L34_PRED_BROWNOUT_RATE_CTL:
	case	CS35L34_PRED_WAIT_CTL:
	case	CS35L34_PRED_ZVP_INIT_IMP_CTL:
	case	CS35L34_PRED_MAN_SAFE_VPI_CTL:
	case	CS35L34_VPBR_ATTEN_STATUS:
	case	CS35L34_PRED_BRWNOUT_ATT_STATUS:
	case	CS35L34_SPKR_MON_CTL:
	case	CS35L34_ADSP_I2S_CTL:
	case	CS35L34_ADSP_TDM_CTL:
	case	CS35L34_TDM_TX_CTL_1_VMON:
	case	CS35L34_TDM_TX_CTL_2_IMON:
	case	CS35L34_TDM_TX_CTL_3_VPMON:
	case	CS35L34_TDM_TX_CTL_4_VBSTMON:
	case	CS35L34_TDM_TX_CTL_5_FLAG1:
	case	CS35L34_TDM_TX_CTL_6_FLAG2:
	case	CS35L34_TDM_TX_SLOT_EN_1:
	case	CS35L34_TDM_TX_SLOT_EN_2:
	case	CS35L34_TDM_TX_SLOT_EN_3:
	case	CS35L34_TDM_TX_SLOT_EN_4:
	case	CS35L34_TDM_RX_CTL_1_AUDIN:
	case	CS35L34_TDM_RX_CTL_3_ALIVE:
	case	CS35L34_MULT_DEV_SYNCH1:
	case	CS35L34_MULT_DEV_SYNCH2:
	case	CS35L34_PROT_RELEASE_CTL:
	case	CS35L34_DIAG_MODE_REG_LOCK:
	case	CS35L34_DIAG_MODE_CTL_1:
	case	CS35L34_DIAG_MODE_CTL_2:
	case	CS35L34_INT_MASK_1:
	case	CS35L34_INT_MASK_2:
	case	CS35L34_INT_MASK_3:
	case	CS35L34_INT_MASK_4:
	case	CS35L34_INT_STATUS_1:
	case	CS35L34_INT_STATUS_2:
	case	CS35L34_INT_STATUS_3:
	case	CS35L34_INT_STATUS_4:
	case	CS35L34_OTP_TRIM_STATUS:
		return true;
	default:
		return false;
	}
}

static bool cs35l34_precious_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L34_INT_STATUS_1:
	case CS35L34_INT_STATUS_2:
	case CS35L34_INT_STATUS_3:
	case CS35L34_INT_STATUS_4:
		return true;
	default:
		return false;
	}
}

static int cs35l34_sdin_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (priv->tdm_mode)
			regmap_update_bits(priv->regmap, CS35L34_PWRCTL3,
						CS35L34_PDN_TDM, 0x00);

		ret = regmap_update_bits(priv->regmap, CS35L34_PWRCTL1,
						CS35L34_PDN_ALL, 0);
		if (ret < 0) {
			dev_err(component->dev, "Cannot set Power bits %d\n", ret);
			return ret;
		}
		usleep_range(5000, 5100);
	break;
	case SND_SOC_DAPM_POST_PMD:
		if (priv->tdm_mode) {
			regmap_update_bits(priv->regmap, CS35L34_PWRCTL3,
					CS35L34_PDN_TDM, CS35L34_PDN_TDM);
		}
		ret = regmap_update_bits(priv->regmap, CS35L34_PWRCTL1,
					CS35L34_PDN_ALL, CS35L34_PDN_ALL);
	break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
	}
	return 0;
}

static int cs35l34_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);
	unsigned int reg, bit_pos;
	int slot, slot_num;

	if (slot_width != 8)
		return -EINVAL;

	priv->tdm_mode = true;
	/* scan rx_mask for aud slot */
	slot = ffs(rx_mask) - 1;
	if (slot >= 0)
		snd_soc_component_update_bits(component, CS35L34_TDM_RX_CTL_1_AUDIN,
					CS35L34_X_LOC, slot);

	/* scan tx_mask: vmon(2 slots); imon (2 slots); vpmon (1 slot)
	 * vbstmon (1 slot)
	 */
	slot = ffs(tx_mask) - 1;
	slot_num = 0;

	/* disable vpmon/vbstmon: enable later if set in tx_mask */
	snd_soc_component_update_bits(component, CS35L34_TDM_TX_CTL_3_VPMON,
				CS35L34_X_STATE | CS35L34_X_LOC,
				CS35L34_X_STATE | CS35L34_X_LOC);
	snd_soc_component_update_bits(component, CS35L34_TDM_TX_CTL_4_VBSTMON,
				CS35L34_X_STATE | CS35L34_X_LOC,
				CS35L34_X_STATE | CS35L34_X_LOC);

	/* disconnect {vp,vbst}_mon routes: eanble later if set in tx_mask*/
	while (slot >= 0) {
		/* configure VMON_TX_LOC */
		if (slot_num == 0)
			snd_soc_component_update_bits(component, CS35L34_TDM_TX_CTL_1_VMON,
					CS35L34_X_STATE | CS35L34_X_LOC, slot);

		/* configure IMON_TX_LOC */
		if (slot_num == 4) {
			snd_soc_component_update_bits(component, CS35L34_TDM_TX_CTL_2_IMON,
					CS35L34_X_STATE | CS35L34_X_LOC, slot);
		}
		/* configure VPMON_TX_LOC */
		if (slot_num == 3) {
			snd_soc_component_update_bits(component, CS35L34_TDM_TX_CTL_3_VPMON,
					CS35L34_X_STATE | CS35L34_X_LOC, slot);
		}
		/* configure VBSTMON_TX_LOC */
		if (slot_num == 7) {
			snd_soc_component_update_bits(component,
				CS35L34_TDM_TX_CTL_4_VBSTMON,
				CS35L34_X_STATE | CS35L34_X_LOC, slot);
		}

		/* Enable the relevant tx slot */
		reg = CS35L34_TDM_TX_SLOT_EN_4 - (slot/8);
		bit_pos = slot - ((slot / 8) * (8));
		snd_soc_component_update_bits(component, reg,
			1 << bit_pos, 1 << bit_pos);

		tx_mask &= ~(1 << slot);
		slot = ffs(tx_mask) - 1;
		slot_num++;
	}

	return 0;
}

static int cs35l34_main_amp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_update_bits(priv->regmap, CS35L34_BST_CVTR_V_CTL,
				CS35L34_BST_CVTL_MASK, priv->pdata.boost_vtge);
		usleep_range(5000, 5100);
		regmap_update_bits(priv->regmap, CS35L34_PROTECT_CTL,
						CS35L34_MUTE, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, CS35L34_BST_CVTR_V_CTL,
			CS35L34_BST_CVTL_MASK, 0);
		regmap_update_bits(priv->regmap, CS35L34_PROTECT_CTL,
			CS35L34_MUTE, CS35L34_MUTE);
		usleep_range(5000, 5100);
		break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
	}
	return 0;
}

static DECLARE_TLV_DB_SCALE(dig_vol_tlv, -10200, 50, 0);

static DECLARE_TLV_DB_SCALE(amp_gain_tlv, 300, 100, 0);


static const struct snd_kcontrol_new cs35l34_snd_controls[] = {
	SOC_SINGLE_SX_TLV("Digital Volume", CS35L34_AMP_DIG_VOL,
		      0, 0x34, 0xE4, dig_vol_tlv),
	SOC_SINGLE_TLV("Amp Gain Volume", CS35L34_AMP_ANLG_GAIN_CTL,
		      0, 0xF, 0, amp_gain_tlv),
};


static int cs35l34_mclk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);
	int ret, i;
	unsigned int reg;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		ret = regmap_read(priv->regmap, CS35L34_AMP_DIG_VOL_CTL,
			&reg);
		if (ret != 0) {
			pr_err("%s regmap read failure %d\n", __func__, ret);
			return ret;
		}
		if (reg & CS35L34_AMP_DIGSFT)
			msleep(40);
		else
			usleep_range(2000, 2100);

		for (i = 0; i < PDN_DONE_ATTEMPTS; i++) {
			ret = regmap_read(priv->regmap, CS35L34_INT_STATUS_2,
				&reg);
			if (ret != 0) {
				pr_err("%s regmap read failure %d\n",
					__func__, ret);
				return ret;
			}
			if (reg & CS35L34_PDN_DONE)
				break;

			usleep_range(5000, 5100);
		}
		if (i == PDN_DONE_ATTEMPTS)
			pr_err("%s Device did not power down properly\n",
				__func__);
		break;
	default:
		pr_err("Invalid event = 0x%x\n", event);
		break;
	}
	return 0;
}

static const struct snd_soc_dapm_widget cs35l34_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("SDIN", NULL, 0, CS35L34_PWRCTL3,
					1, 1, cs35l34_sdin_event,
					SND_SOC_DAPM_PRE_PMU |
					SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("SDOUT", NULL, 0, CS35L34_PWRCTL3, 2, 1),

	SND_SOC_DAPM_SUPPLY("EXTCLK", CS35L34_PWRCTL3, 7, 1,
		cs35l34_mclk_event, SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_OUTPUT("SPK"),

	SND_SOC_DAPM_INPUT("VP"),
	SND_SOC_DAPM_INPUT("VPST"),
	SND_SOC_DAPM_INPUT("ISENSE"),
	SND_SOC_DAPM_INPUT("VSENSE"),

	SND_SOC_DAPM_ADC("VMON ADC", NULL, CS35L34_PWRCTL2, 7, 1),
	SND_SOC_DAPM_ADC("IMON ADC", NULL, CS35L34_PWRCTL2, 6, 1),
	SND_SOC_DAPM_ADC("VPMON ADC", NULL, CS35L34_PWRCTL3, 3, 1),
	SND_SOC_DAPM_ADC("VBSTMON ADC", NULL, CS35L34_PWRCTL3, 4, 1),
	SND_SOC_DAPM_ADC("CLASS H", NULL, CS35L34_PWRCTL2, 5, 1),
	SND_SOC_DAPM_ADC("BOOST", NULL, CS35L34_PWRCTL2, 2, 1),

	SND_SOC_DAPM_OUT_DRV_E("Main AMP", CS35L34_PWRCTL2, 0, 1, NULL, 0,
		cs35l34_main_amp_event, SND_SOC_DAPM_POST_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route cs35l34_audio_map[] = {
	{"SDIN", NULL, "AMP Playback"},
	{"BOOST", NULL, "SDIN"},
	{"CLASS H", NULL, "BOOST"},
	{"Main AMP", NULL, "CLASS H"},
	{"SPK", NULL, "Main AMP"},

	{"VPMON ADC", NULL, "CLASS H"},
	{"VBSTMON ADC", NULL, "CLASS H"},
	{"SPK", NULL, "VPMON ADC"},
	{"SPK", NULL, "VBSTMON ADC"},

	{"IMON ADC", NULL, "ISENSE"},
	{"VMON ADC", NULL, "VSENSE"},
	{"SDOUT", NULL, "IMON ADC"},
	{"SDOUT", NULL, "VMON ADC"},
	{"AMP Capture", NULL, "SDOUT"},

	{"SDIN", NULL, "EXTCLK"},
	{"SDOUT", NULL, "EXTCLK"},
};

struct cs35l34_mclk_div {
	int mclk;
	int srate;
	u8 adsp_rate;
};

static struct cs35l34_mclk_div cs35l34_mclk_coeffs[] = {

	/* MCLK, Sample Rate, adsp_rate */

	{5644800, 11025, 0x1},
	{5644800, 22050, 0x4},
	{5644800, 44100, 0x7},

	{6000000,  8000, 0x0},
	{6000000, 11025, 0x1},
	{6000000, 12000, 0x2},
	{6000000, 16000, 0x3},
	{6000000, 22050, 0x4},
	{6000000, 24000, 0x5},
	{6000000, 32000, 0x6},
	{6000000, 44100, 0x7},
	{6000000, 48000, 0x8},

	{6144000,  8000, 0x0},
	{6144000, 11025, 0x1},
	{6144000, 12000, 0x2},
	{6144000, 16000, 0x3},
	{6144000, 22050, 0x4},
	{6144000, 24000, 0x5},
	{6144000, 32000, 0x6},
	{6144000, 44100, 0x7},
	{6144000, 48000, 0x8},
};

static int cs35l34_get_mclk_coeff(int mclk, int srate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l34_mclk_coeffs); i++) {
		if (cs35l34_mclk_coeffs[i].mclk == mclk &&
			cs35l34_mclk_coeffs[i].srate == srate)
			return i;
	}
	return -EINVAL;
}

static int cs35l34_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		regmap_update_bits(priv->regmap, CS35L34_ADSP_CLK_CTL,
				    0x80, 0x80);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		regmap_update_bits(priv->regmap, CS35L34_ADSP_CLK_CTL,
				    0x80, 0x00);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cs35l34_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l34_private *priv = snd_soc_component_get_drvdata(component);
	int srate = params_rate(params);
	int ret;

	int coeff = cs35l34_get_mclk_coeff(priv->mclk_int, srate);

	if (coeff < 0) {
		dev_err(component->dev, "ERROR: Invalid mclk %d and/or srate %d\n",
			priv->mclk_int, srate);
		return coeff;
	}

	ret = regmap_update_bits(priv->regmap, CS35L34_ADSP_CLK_CTL,
		CS35L34_ADSP_RATE, cs35l34_mclk_coeffs[coeff].adsp_rate);
	if (ret != 0)
		dev_err(component->dev, "Failed to set clock state %d\n", ret);

	return ret;
}

static const unsigned int cs35l34_src_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};


static const struct snd_pcm_hw_constraint_list cs35l34_constraints = {
	.count  = ARRAY_SIZE(cs35l34_src_rates),
	.list   = cs35l34_src_rates,
};

static int cs35l34_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &cs35l34_constraints);
	return 0;
}


static int cs35l34_set_tristate(struct snd_soc_dai *dai, int tristate)
{

	struct snd_soc_component *component = dai->component;

	if (tristate)
		snd_soc_component_update_bits(component, CS35L34_PWRCTL3,
					CS35L34_PDN_SDOUT, CS35L34_PDN_SDOUT);
	else
		snd_soc_component_update_bits(component, CS35L34_PWRCTL3,
					CS35L34_PDN_SDOUT, 0);
	return 0;
}

static int cs35l34_dai_set_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct cs35l34_private *cs35l34 = snd_soc_component_get_drvdata(component);
	unsigned int value;

	switch (freq) {
	case CS35L34_MCLK_5644:
		value = CS35L34_MCLK_RATE_5P6448;
		cs35l34->mclk_int = freq;
	break;
	case CS35L34_MCLK_6:
		value = CS35L34_MCLK_RATE_6P0000;
		cs35l34->mclk_int = freq;
	break;
	case CS35L34_MCLK_6144:
		value = CS35L34_MCLK_RATE_6P1440;
		cs35l34->mclk_int = freq;
	break;
	case CS35L34_MCLK_11289:
		value = CS35L34_MCLK_DIV | CS35L34_MCLK_RATE_5P6448;
		cs35l34->mclk_int = freq / 2;
	break;
	case CS35L34_MCLK_12:
		value = CS35L34_MCLK_DIV | CS35L34_MCLK_RATE_6P0000;
		cs35l34->mclk_int = freq / 2;
	break;
	case CS35L34_MCLK_12288:
		value = CS35L34_MCLK_DIV | CS35L34_MCLK_RATE_6P1440;
		cs35l34->mclk_int = freq / 2;
	break;
	default:
		dev_err(component->dev, "ERROR: Invalid Frequency %d\n", freq);
		cs35l34->mclk_int = 0;
		return -EINVAL;
	}
	regmap_update_bits(cs35l34->regmap, CS35L34_MCLK_CTL,
			CS35L34_MCLK_DIV | CS35L34_MCLK_RATE_MASK, value);
	return 0;
}

static const struct snd_soc_dai_ops cs35l34_ops = {
	.startup = cs35l34_pcm_startup,
	.set_tristate = cs35l34_set_tristate,
	.set_fmt = cs35l34_set_dai_fmt,
	.hw_params = cs35l34_pcm_hw_params,
	.set_sysclk = cs35l34_dai_set_sysclk,
	.set_tdm_slot = cs35l34_set_tdm_slot,
};

static struct snd_soc_dai_driver cs35l34_dai = {
		.name = "cs35l34",
		.id = 0,
		.playback = {
			.stream_name = "AMP Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS35L34_RATES,
			.formats = CS35L34_FORMATS,
		},
		.capture = {
			.stream_name = "AMP Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = CS35L34_RATES,
			.formats = CS35L34_FORMATS,
		},
		.ops = &cs35l34_ops,
		.symmetric_rate = 1,
};

static int cs35l34_boost_inductor(struct cs35l34_private *cs35l34,
	unsigned int inductor)
{
	struct snd_soc_component *component = cs35l34->component;

	switch (inductor) {
	case 1000: /* 1 uH */
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_1, 0x24);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_2, 0x24);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SLOPE_COMP,
			0x4E);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SW_FREQ, 0);
		break;
	case 1200: /* 1.2 uH */
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_1, 0x20);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_2, 0x20);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SLOPE_COMP,
			0x47);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SW_FREQ, 1);
		break;
	case 1500: /* 1.5uH */
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_1, 0x20);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_2, 0x20);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SLOPE_COMP,
			0x3C);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SW_FREQ, 2);
		break;
	case 2200: /* 2.2uH */
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_1, 0x19);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_COEF_2, 0x25);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SLOPE_COMP,
			0x23);
		regmap_write(cs35l34->regmap, CS35L34_BST_CONV_SW_FREQ, 3);
		break;
	default:
		dev_err(component->dev, "%s Invalid Inductor Value %d uH\n",
			__func__, inductor);
		return -EINVAL;
	}
	return 0;
}

static int cs35l34_probe(struct snd_soc_component *component)
{
	int ret = 0;
	struct cs35l34_private *cs35l34 = snd_soc_component_get_drvdata(component);

	pm_runtime_get_sync(component->dev);

	/* Set over temperature warning attenuation to 6 dB */
	regmap_update_bits(cs35l34->regmap, CS35L34_PROTECT_CTL,
		 CS35L34_OTW_ATTN_MASK, 0x8);

	/* Set Power control registers 2 and 3 to have everything
	 * powered down at initialization
	 */
	regmap_write(cs35l34->regmap, CS35L34_PWRCTL2, 0xFD);
	regmap_write(cs35l34->regmap, CS35L34_PWRCTL3, 0x1F);

	/* Set mute bit at startup */
	regmap_update_bits(cs35l34->regmap, CS35L34_PROTECT_CTL,
				CS35L34_MUTE, CS35L34_MUTE);

	/* Set Platform Data */
	if (cs35l34->pdata.boost_peak)
		regmap_update_bits(cs35l34->regmap, CS35L34_BST_PEAK_I,
				CS35L34_BST_PEAK_MASK,
				cs35l34->pdata.boost_peak);

	if (cs35l34->pdata.gain_zc_disable)
		regmap_update_bits(cs35l34->regmap, CS35L34_PROTECT_CTL,
			CS35L34_GAIN_ZC_MASK, 0);
	else
		regmap_update_bits(cs35l34->regmap, CS35L34_PROTECT_CTL,
			CS35L34_GAIN_ZC_MASK, CS35L34_GAIN_ZC_MASK);

	if (cs35l34->pdata.aif_half_drv)
		regmap_update_bits(cs35l34->regmap, CS35L34_ADSP_CLK_CTL,
			CS35L34_ADSP_DRIVE, 0);

	if (cs35l34->pdata.digsft_disable)
		regmap_update_bits(cs35l34->regmap, CS35L34_AMP_DIG_VOL_CTL,
			CS35L34_AMP_DIGSFT, 0);

	if (cs35l34->pdata.amp_inv)
		regmap_update_bits(cs35l34->regmap, CS35L34_AMP_DIG_VOL_CTL,
			CS35L34_INV, CS35L34_INV);

	if (cs35l34->pdata.boost_ind)
		ret = cs35l34_boost_inductor(cs35l34, cs35l34->pdata.boost_ind);

	if (cs35l34->pdata.i2s_sdinloc)
		regmap_update_bits(cs35l34->regmap, CS35L34_ADSP_I2S_CTL,
			CS35L34_I2S_LOC_MASK,
			cs35l34->pdata.i2s_sdinloc << CS35L34_I2S_LOC_SHIFT);

	if (cs35l34->pdata.tdm_rising_edge)
		regmap_update_bits(cs35l34->regmap, CS35L34_ADSP_TDM_CTL,
			1, 1);

	pm_runtime_put_sync(component->dev);

	return ret;
}


static const struct snd_soc_component_driver soc_component_dev_cs35l34 = {
	.probe			= cs35l34_probe,
	.dapm_widgets		= cs35l34_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs35l34_dapm_widgets),
	.dapm_routes		= cs35l34_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(cs35l34_audio_map),
	.controls		= cs35l34_snd_controls,
	.num_controls		= ARRAY_SIZE(cs35l34_snd_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config cs35l34_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS35L34_MAX_REGISTER,
	.reg_defaults = cs35l34_reg,
	.num_reg_defaults = ARRAY_SIZE(cs35l34_reg),
	.volatile_reg = cs35l34_volatile_register,
	.readable_reg = cs35l34_readable_register,
	.precious_reg = cs35l34_precious_register,
	.cache_type = REGCACHE_MAPLE,

	.use_single_read = true,
	.use_single_write = true,
};

static int cs35l34_handle_of_data(struct i2c_client *i2c_client,
				struct cs35l34_platform_data *pdata)
{
	struct device_node *np = i2c_client->dev.of_node;
	unsigned int val;

	if (of_property_read_u32(np, "cirrus,boost-vtge-millivolt",
		&val) >= 0) {
		/* Boost Voltage has a maximum of 8V */
		if (val > 8000 || (val < 3300 && val > 0)) {
			dev_err(&i2c_client->dev,
				"Invalid Boost Voltage %d mV\n", val);
			return -EINVAL;
		}
		if (val == 0)
			pdata->boost_vtge = 0; /* Use VP */
		else
			pdata->boost_vtge = ((val - 3300)/100) + 1;
	} else {
		dev_warn(&i2c_client->dev,
			"Boost Voltage not specified. Using VP\n");
	}

	if (of_property_read_u32(np, "cirrus,boost-ind-nanohenry", &val) >= 0) {
		pdata->boost_ind = val;
	} else {
		dev_err(&i2c_client->dev, "Inductor not specified.\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "cirrus,boost-peak-milliamp", &val) >= 0) {
		if (val > 3840 || val < 1200) {
			dev_err(&i2c_client->dev,
				"Invalid Boost Peak Current %d mA\n", val);
			return -EINVAL;
		}
		pdata->boost_peak = ((val - 1200)/80) + 1;
	}

	pdata->aif_half_drv = of_property_read_bool(np,
		"cirrus,aif-half-drv");
	pdata->digsft_disable = of_property_read_bool(np,
		"cirrus,digsft-disable");

	pdata->gain_zc_disable = of_property_read_bool(np,
		"cirrus,gain-zc-disable");
	pdata->amp_inv = of_property_read_bool(np, "cirrus,amp-inv");

	if (of_property_read_u32(np, "cirrus,i2s-sdinloc", &val) >= 0)
		pdata->i2s_sdinloc = val;
	if (of_property_read_u32(np, "cirrus,tdm-rising-edge", &val) >= 0)
		pdata->tdm_rising_edge = val;

	return 0;
}

static irqreturn_t cs35l34_irq_thread(int irq, void *data)
{
	struct cs35l34_private *cs35l34 = data;
	struct snd_soc_component *component = cs35l34->component;
	unsigned int sticky1, sticky2, sticky3, sticky4;
	unsigned int mask1, mask2, mask3, mask4, current1;


	/* ack the irq by reading all status registers */
	regmap_read(cs35l34->regmap, CS35L34_INT_STATUS_4, &sticky4);
	regmap_read(cs35l34->regmap, CS35L34_INT_STATUS_3, &sticky3);
	regmap_read(cs35l34->regmap, CS35L34_INT_STATUS_2, &sticky2);
	regmap_read(cs35l34->regmap, CS35L34_INT_STATUS_1, &sticky1);

	regmap_read(cs35l34->regmap, CS35L34_INT_MASK_4, &mask4);
	regmap_read(cs35l34->regmap, CS35L34_INT_MASK_3, &mask3);
	regmap_read(cs35l34->regmap, CS35L34_INT_MASK_2, &mask2);
	regmap_read(cs35l34->regmap, CS35L34_INT_MASK_1, &mask1);

	if (!(sticky1 & ~mask1) && !(sticky2 & ~mask2) && !(sticky3 & ~mask3)
		&& !(sticky4 & ~mask4))
		return IRQ_NONE;

	regmap_read(cs35l34->regmap, CS35L34_INT_STATUS_1, &current1);

	if (sticky1 & CS35L34_CAL_ERR) {
		dev_err(component->dev, "Cal error\n");

		/* error is no longer asserted; safe to reset */
		if (!(current1 & CS35L34_CAL_ERR)) {
			dev_dbg(component->dev, "Cal error release\n");
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_CAL_ERR_RLS, 0);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_CAL_ERR_RLS,
					CS35L34_CAL_ERR_RLS);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_CAL_ERR_RLS, 0);
			/* note: amp will re-calibrate on next resume */
		}
	}

	if (sticky1 & CS35L34_ALIVE_ERR)
		dev_err(component->dev, "Alive error\n");

	if (sticky1 & CS35L34_AMP_SHORT) {
		dev_crit(component->dev, "Amp short error\n");

		/* error is no longer asserted; safe to reset */
		if (!(current1 & CS35L34_AMP_SHORT)) {
			dev_dbg(component->dev,
				"Amp short error release\n");
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_SHORT_RLS, 0);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_SHORT_RLS,
					CS35L34_SHORT_RLS);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_SHORT_RLS, 0);
		}
	}

	if (sticky1 & CS35L34_OTW) {
		dev_crit(component->dev, "Over temperature warning\n");

		/* error is no longer asserted; safe to reset */
		if (!(current1 & CS35L34_OTW)) {
			dev_dbg(component->dev,
				"Over temperature warning release\n");
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTW_RLS, 0);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTW_RLS,
					CS35L34_OTW_RLS);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTW_RLS, 0);
		}
	}

	if (sticky1 & CS35L34_OTE) {
		dev_crit(component->dev, "Over temperature error\n");

		/* error is no longer asserted; safe to reset */
		if (!(current1 & CS35L34_OTE)) {
			dev_dbg(component->dev,
				"Over temperature error release\n");
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTE_RLS, 0);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTE_RLS,
					CS35L34_OTE_RLS);
			regmap_update_bits(cs35l34->regmap,
					CS35L34_PROT_RELEASE_CTL,
					CS35L34_OTE_RLS, 0);
		}
	}

	if (sticky3 & CS35L34_BST_HIGH) {
		dev_crit(component->dev, "VBST too high error; powering off!\n");
		regmap_update_bits(cs35l34->regmap, CS35L34_PWRCTL2,
				CS35L34_PDN_AMP, CS35L34_PDN_AMP);
		regmap_update_bits(cs35l34->regmap, CS35L34_PWRCTL1,
				CS35L34_PDN_ALL, CS35L34_PDN_ALL);
	}

	if (sticky3 & CS35L34_LBST_SHORT) {
		dev_crit(component->dev, "LBST short error; powering off!\n");
		regmap_update_bits(cs35l34->regmap, CS35L34_PWRCTL2,
				CS35L34_PDN_AMP, CS35L34_PDN_AMP);
		regmap_update_bits(cs35l34->regmap, CS35L34_PWRCTL1,
				CS35L34_PDN_ALL, CS35L34_PDN_ALL);
	}

	return IRQ_HANDLED;
}

static const char * const cs35l34_core_supplies[] = {
	"VA",
	"VP",
};

static int cs35l34_i2c_probe(struct i2c_client *i2c_client)
{
	struct cs35l34_private *cs35l34;
	struct cs35l34_platform_data *pdata =
		dev_get_platdata(&i2c_client->dev);
	int i, devid;
	int ret;
	unsigned int reg;

	cs35l34 = devm_kzalloc(&i2c_client->dev, sizeof(*cs35l34), GFP_KERNEL);
	if (!cs35l34)
		return -ENOMEM;

	i2c_set_clientdata(i2c_client, cs35l34);
	cs35l34->regmap = devm_regmap_init_i2c(i2c_client, &cs35l34_regmap);
	if (IS_ERR(cs35l34->regmap)) {
		ret = PTR_ERR(cs35l34->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	cs35l34->num_core_supplies = ARRAY_SIZE(cs35l34_core_supplies);
	for (i = 0; i < ARRAY_SIZE(cs35l34_core_supplies); i++)
		cs35l34->core_supplies[i].supply = cs35l34_core_supplies[i];

	ret = devm_regulator_bulk_get(&i2c_client->dev,
		cs35l34->num_core_supplies,
		cs35l34->core_supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to request core supplies %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(cs35l34->num_core_supplies,
					cs35l34->core_supplies);
	if (ret != 0) {
		dev_err(&i2c_client->dev,
			"Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	if (pdata) {
		cs35l34->pdata = *pdata;
	} else {
		pdata = devm_kzalloc(&i2c_client->dev, sizeof(*pdata),
				     GFP_KERNEL);
		if (!pdata) {
			ret = -ENOMEM;
			goto err_regulator;
		}

		if (i2c_client->dev.of_node) {
			ret = cs35l34_handle_of_data(i2c_client, pdata);
			if (ret != 0)
				goto err_regulator;

		}
		cs35l34->pdata = *pdata;
	}

	ret = devm_request_threaded_irq(&i2c_client->dev, i2c_client->irq, NULL,
			cs35l34_irq_thread, IRQF_ONESHOT | IRQF_TRIGGER_LOW,
			"cs35l34", cs35l34);
	if (ret != 0)
		dev_err(&i2c_client->dev, "Failed to request IRQ: %d\n", ret);

	cs35l34->reset_gpio = devm_gpiod_get_optional(&i2c_client->dev,
				"reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs35l34->reset_gpio)) {
		ret = PTR_ERR(cs35l34->reset_gpio);
		goto err_regulator;
	}

	gpiod_set_value_cansleep(cs35l34->reset_gpio, 1);

	msleep(CS35L34_START_DELAY);

	devid = cirrus_read_device_id(cs35l34->regmap, CS35L34_DEVID_AB);
	if (devid < 0) {
		ret = devid;
		dev_err(&i2c_client->dev, "Failed to read device ID: %d\n", ret);
		goto err_reset;
	}

	if (devid != CS35L34_CHIP_ID) {
		dev_err(&i2c_client->dev,
			"CS35l34 Device ID (%X). Expected ID %X\n",
			devid, CS35L34_CHIP_ID);
		ret = -ENODEV;
		goto err_reset;
	}

	ret = regmap_read(cs35l34->regmap, CS35L34_REV_ID, &reg);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Get Revision ID failed\n");
		goto err_reset;
	}

	dev_info(&i2c_client->dev,
		 "Cirrus Logic CS35l34 (%x), Revision: %02X\n", devid,
		reg & 0xFF);

	/* Unmask critical interrupts */
	regmap_update_bits(cs35l34->regmap, CS35L34_INT_MASK_1,
				CS35L34_M_CAL_ERR | CS35L34_M_ALIVE_ERR |
				CS35L34_M_AMP_SHORT | CS35L34_M_OTW |
				CS35L34_M_OTE, 0);
	regmap_update_bits(cs35l34->regmap, CS35L34_INT_MASK_3,
				CS35L34_M_BST_HIGH | CS35L34_M_LBST_SHORT, 0);

	pm_runtime_set_autosuspend_delay(&i2c_client->dev, 100);
	pm_runtime_use_autosuspend(&i2c_client->dev);
	pm_runtime_set_active(&i2c_client->dev);
	pm_runtime_enable(&i2c_client->dev);

	ret = devm_snd_soc_register_component(&i2c_client->dev,
			&soc_component_dev_cs35l34, &cs35l34_dai, 1);
	if (ret < 0) {
		dev_err(&i2c_client->dev,
			"%s: Register component failed\n", __func__);
		goto err_reset;
	}

	return 0;

err_reset:
	gpiod_set_value_cansleep(cs35l34->reset_gpio, 0);
err_regulator:
	regulator_bulk_disable(cs35l34->num_core_supplies,
		cs35l34->core_supplies);

	return ret;
}

static void cs35l34_i2c_remove(struct i2c_client *client)
{
	struct cs35l34_private *cs35l34 = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(cs35l34->reset_gpio, 0);

	pm_runtime_disable(&client->dev);
	regulator_bulk_disable(cs35l34->num_core_supplies,
		cs35l34->core_supplies);
}

static int __maybe_unused cs35l34_runtime_resume(struct device *dev)
{
	struct cs35l34_private *cs35l34 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(cs35l34->num_core_supplies,
		cs35l34->core_supplies);

	if (ret != 0) {
		dev_err(dev, "Failed to enable core supplies: %d\n",
			ret);
		return ret;
	}

	regcache_cache_only(cs35l34->regmap, false);

	gpiod_set_value_cansleep(cs35l34->reset_gpio, 1);
	msleep(CS35L34_START_DELAY);

	ret = regcache_sync(cs35l34->regmap);
	if (ret != 0) {
		dev_err(dev, "Failed to restore register cache\n");
		goto err;
	}
	return 0;
err:
	regcache_cache_only(cs35l34->regmap, true);
	regulator_bulk_disable(cs35l34->num_core_supplies,
		cs35l34->core_supplies);

	return ret;
}

static int __maybe_unused cs35l34_runtime_suspend(struct device *dev)
{
	struct cs35l34_private *cs35l34 = dev_get_drvdata(dev);

	regcache_cache_only(cs35l34->regmap, true);
	regcache_mark_dirty(cs35l34->regmap);

	gpiod_set_value_cansleep(cs35l34->reset_gpio, 0);

	regulator_bulk_disable(cs35l34->num_core_supplies,
			cs35l34->core_supplies);

	return 0;
}

static const struct dev_pm_ops cs35l34_pm_ops = {
	SET_RUNTIME_PM_OPS(cs35l34_runtime_suspend,
			   cs35l34_runtime_resume,
			   NULL)
};

static const struct of_device_id cs35l34_of_match[] = {
	{.compatible = "cirrus,cs35l34"},
	{},
};
MODULE_DEVICE_TABLE(of, cs35l34_of_match);

static const struct i2c_device_id cs35l34_id[] = {
	{"cs35l34"},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs35l34_id);

static struct i2c_driver cs35l34_i2c_driver = {
	.driver = {
		.name = "cs35l34",
		.pm = &cs35l34_pm_ops,
		.of_match_table = cs35l34_of_match,

		},
	.id_table = cs35l34_id,
	.probe = cs35l34_i2c_probe,
	.remove = cs35l34_i2c_remove,

};

static int __init cs35l34_modinit(void)
{
	int ret;

	ret = i2c_add_driver(&cs35l34_i2c_driver);
	if (ret != 0) {
		pr_err("Failed to register CS35l34 I2C driver: %d\n", ret);
		return ret;
	}
	return 0;
}
module_init(cs35l34_modinit);

static void __exit cs35l34_exit(void)
{
	i2c_del_driver(&cs35l34_i2c_driver);
}
module_exit(cs35l34_exit);

MODULE_DESCRIPTION("ASoC CS35l34 driver");
MODULE_AUTHOR("Paul Handrigan, Cirrus Logic Inc, <Paul.Handrigan@cirrus.com>");
MODULE_LICENSE("GPL");
